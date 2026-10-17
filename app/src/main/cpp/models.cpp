#define CGLTF_IMPLEMENTATION
#include "models.h"

#include <cgltf.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include <stb_image.h>

#include <sstream>

#include "log.h"

int ControllerModel::find(const std::string &name) const {
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].name == name) return (int)i;
    return -1;
}

std::string component_part(const std::string &id) {
    if (id == "xr-standard-trigger") return "trigger";
    if (id == "xr-standard-squeeze") return "squeeze";
    if (id == "xr-standard-thumbstick") return "thumbstick";
    if (id == "xr-standard-touchpad") return "trackpad";
    const std::string suffix = "-button";
    if (id.size() > suffix.size() && id.compare(id.size() - suffix.size(), suffix.size(), suffix) == 0)
        return id.substr(0, id.size() - suffix.size());  // x-button -> x
    return id;  // thumbrest, menu
}

static GLuint upload_texture(const cgltf_image *image, std::map<const cgltf_image *, GLuint> &cache) {
    auto it = cache.find(image);
    if (it != cache.end()) return it->second;
    GLuint tex = 0;
    if (image->buffer_view && image->buffer_view->buffer->data) {
        const uint8_t *bytes = (const uint8_t *)image->buffer_view->buffer->data + image->buffer_view->offset;
        int w = 0, h = 0, n = 0;
        unsigned char *px = stbi_load_from_memory(bytes, (int)image->buffer_view->size, &w, &h, &n, 4);
        if (px) {
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            stbi_image_free(px);
        } else {
            LOGE("texture %s: %s", image->name ? image->name : "?", stbi_failure_reason());
        }
    }
    cache[image] = tex;
    return tex;
}

static bool load_primitive(const cgltf_primitive &prim, Primitive &out,
                           std::map<const cgltf_image *, GLuint> &textures) {
    if (prim.type != cgltf_primitive_type_triangles) return false;
    const cgltf_accessor *pos = nullptr, *nrm = nullptr, *uv = nullptr;
    for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
        const cgltf_attribute &att = prim.attributes[a];
        if (att.type == cgltf_attribute_type_position) pos = att.data;
        if (att.type == cgltf_attribute_type_normal) nrm = att.data;
        if (att.type == cgltf_attribute_type_texcoord && att.index == 0) uv = att.data;
    }
    if (!pos) return false;
    std::vector<float> v(pos->count * 8, 0.0f);
    for (cgltf_size i = 0; i < pos->count; ++i) {
        cgltf_accessor_read_float(pos, i, &v[i * 8], 3);
        if (nrm) cgltf_accessor_read_float(nrm, i, &v[i * 8 + 3], 3);
        else v[i * 8 + 4] = 1;
        if (uv) cgltf_accessor_read_float(uv, i, &v[i * 8 + 6], 2);
    }
    std::vector<uint32_t> idx;
    if (prim.indices) {
        idx.resize(prim.indices->count);
        for (cgltf_size i = 0; i < idx.size(); ++i) idx[i] = (uint32_t)cgltf_accessor_read_index(prim.indices, i);
    } else {
        for (cgltf_size i = 0; i < pos->count; ++i) idx.push_back((uint32_t)i);
    }
    out.mesh.upload(v, idx);
    if (prim.material && prim.material->has_pbr_metallic_roughness) {
        const auto &pbr = prim.material->pbr_metallic_roughness;
        out.color = Vec3(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]);
        if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image)
            out.texture = upload_texture(pbr.base_color_texture.texture->image, textures);
    }
    return true;
}

static bool load_gltf(const std::vector<uint8_t> &bytes, const std::string &file, ControllerModel &model) {
    cgltf_options options{};
    cgltf_data *data = nullptr;
    if (cgltf_parse(&options, bytes.data(), bytes.size(), &data) != cgltf_result_success ||
        cgltf_load_buffers(&options, data, nullptr) != cgltf_result_success) {
        LOGE("model %s: cannot parse", file.c_str());
        if (data) cgltf_free(data);
        return false;
    }
    std::map<const cgltf_image *, GLuint> textures;
    model.nodes.resize(data->nodes_count);
    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node &src = data->nodes[n];
        ModelNode &node = model.nodes[n];
        node.name = src.name ? src.name : "";
        if (src.has_matrix) {  // decompose a plain TRS matrix (no shear in these assets)
            const float *m = src.matrix;
            node.t = Vec3(m[12], m[13], m[14]);
            Vec3 cx(m[0], m[1], m[2]), cy(m[4], m[5], m[6]), cz(m[8], m[9], m[10]);
            node.s = Vec3(length(cx), length(cy), length(cz));
            node.r = Quat::from_basis(cx * (1 / node.s.x), cy * (1 / node.s.y), cz * (1 / node.s.z));
        }
        if (src.has_rotation) node.r = Quat(src.rotation[0], src.rotation[1], src.rotation[2], src.rotation[3]);
        if (src.has_translation) node.t = Vec3(src.translation[0], src.translation[1], src.translation[2]);
        if (src.has_scale) node.s = Vec3(src.scale[0], src.scale[1], src.scale[2]);
        node.parent = src.parent ? (int)(src.parent - data->nodes) : -1;
        for (cgltf_size c = 0; c < src.children_count; ++c) node.children.push_back((int)(src.children[c] - data->nodes));
        if (src.mesh)
            for (cgltf_size p = 0; p < src.mesh->primitives_count; ++p) {
                Primitive prim;
                if (load_primitive(src.mesh->primitives[p], prim, textures)) node.prims.push_back(std::move(prim));
            }
    }
    for (size_t n = 0; n < model.nodes.size(); ++n)
        if (model.nodes[n].parent < 0) model.roots.push_back((int)n);
    cgltf_free(data);
    return true;
}

static unsigned parse_states(const std::string &s) {
    unsigned m = 0;
    if (s.find("default") != std::string::npos) m |= 1;
    if (s.find("touched") != std::string::npos) m |= 2;
    if (s.find("pressed") != std::string::npos) m |= 4;
    return m;
}

static void mark_component(ControllerModel &model, int node, int component) {
    model.nodes[node].component = component;
    for (int c : model.nodes[node].children) mark_component(model, c, component);
}

static bool load(const ModelCache::Reader &reader, const std::string &dir, const std::string &hand,
                 ControllerModel &model) {
    std::vector<uint8_t> text;
    if (!reader(dir + "/" + hand + ".responses", text)) {
        LOGE("model %s/%s: no responses file", dir.c_str(), hand.c_str());
        return false;
    }
    std::istringstream in(std::string(text.begin(), text.end()));
    std::string line, asset;
    struct Pending { std::string component, prop, kind, value, min, max, states; };
    std::vector<std::pair<std::string, std::pair<std::string, std::string>>> comps;  // id, (root, touch)
    std::vector<Pending> pending;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (key == "asset") {
            ls >> asset;
        } else if (key == "component") {
            std::string id, root, touch;
            ls >> id >> root >> touch;
            comps.push_back({id, {root, touch}});
        } else if (key == "response") {
            Pending p;
            ls >> p.component >> p.prop >> p.kind >> p.value >> p.min >> p.max >> p.states;
            pending.push_back(p);
        }
    }
    std::vector<uint8_t> glb;
    std::string file = dir + "/" + asset;
    if (asset.empty() || !reader(file, glb) || !load_gltf(glb, file, model)) return false;

    for (const auto &c : comps) {
        ModelComponent mc;
        mc.id = c.first;
        mc.part = component_part(c.first);
        mc.root = model.find(c.second.first);
        mc.touch_point = c.second.second.empty() ? -1 : model.find(c.second.second);
        model.components.push_back(mc);  // the component's root node is often missing: see below
    }
    for (const auto &p : pending) {
        ModelResponse r;
        for (size_t i = 0; i < model.components.size(); ++i)
            if (model.components[i].id == p.component) r.component = (int)i;
        r.property = p.prop == "xAxis" ? ModelResponse::kXAxis
                     : p.prop == "yAxis" ? ModelResponse::kYAxis
                     : p.prop == "state" ? ModelResponse::kState
                                         : ModelResponse::kButton;
        r.visibility = p.kind == "visibility";
        r.value = model.find(p.value);
        r.min = p.min == "-" ? -1 : model.find(p.min);
        r.max = p.max == "-" ? -1 : model.find(p.max);
        r.states = parse_states(p.states);
        if (r.component >= 0 && r.value >= 0 && (r.visibility || (r.min >= 0 && r.max >= 0)))
            model.responses.push_back(r);
    }
    // A component's geometry hangs below the value nodes of its responses (x_button_pressed_value -> x_button);
    // the component root node named in profile.json is missing in most of these assets. Labels go to the
    // component root if it exists, else to its first value node.
    for (size_t i = 0; i < model.components.size(); ++i) {
        ModelComponent &mc = model.components[i];
        if (mc.root >= 0) mark_component(model, mc.root, (int)i);
        for (const ModelResponse &r : model.responses)
            if (r.component == (int)i && !r.visibility) {
                mark_component(model, r.value, (int)i);
                if (mc.root < 0) mc.root = r.value;
            }
    }
    LOGI("model %s: %zu nodes, %zu components, %zu responses", file.c_str(), model.nodes.size(),
         model.components.size(), model.responses.size());
    return true;
}

const ControllerModel *ModelCache::get(const std::string &profile, int hand) {
    std::string dir = "webxr/" + profile, h = hand == 0 ? "left" : "right", key = dir + "/" + h;
    auto it = loaded_.find(key);
    if (it != loaded_.end()) return &it->second;
    if (failed_.count(key) || !reader_) return nullptr;
    ControllerModel model;
    if (!load(reader_, dir, h, model)) {
        failed_[key] = true;
        return nullptr;
    }
    return &(loaded_[key] = std::move(model));
}
