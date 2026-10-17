#define CGLTF_IMPLEMENTATION
#include "models.h"

#include <cgltf.h>

#include "log.h"

const ModelNode *ControllerModel::find(const std::string &name) const {
    for (const auto &n : nodes)
        if (n.name == name) return &n;
    return nullptr;
}

static bool read_floats(const cgltf_accessor *acc, std::vector<float> &out, int width) {
    out.resize(acc->count * width);
    for (cgltf_size i = 0; i < acc->count; ++i)
        if (!cgltf_accessor_read_float(acc, i, &out[i * width], width)) return false;
    return true;
}

static bool load(const ModelCache::Reader &reader, const std::string &file, ControllerModel &model) {
    std::vector<uint8_t> bytes;
    if (!reader(file, bytes)) {
        LOGE("model %s missing", file.c_str());
        return false;
    }

    cgltf_options options{};
    cgltf_data *data = nullptr;
    if (cgltf_parse(&options, bytes.data(), bytes.size(), &data) != cgltf_result_success ||
        cgltf_load_buffers(&options, data, nullptr) != cgltf_result_success) {
        LOGE("model %s: cannot parse", file.c_str());
        if (data) cgltf_free(data);
        return false;
    }
    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node &node = data->nodes[n];
        if (!node.mesh || node.mesh->primitives_count == 0) continue;
        const cgltf_primitive &prim = node.mesh->primitives[0];
        const cgltf_accessor *pos = nullptr, *nrm = nullptr;
        for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
            if (prim.attributes[a].type == cgltf_attribute_type_position) pos = prim.attributes[a].data;
            if (prim.attributes[a].type == cgltf_attribute_type_normal) nrm = prim.attributes[a].data;
        }
        std::vector<float> p, nv;
        if (!pos || !nrm || !prim.indices || !read_floats(pos, p, 3) || !read_floats(nrm, nv, 3)) continue;
        std::vector<float> interleaved(pos->count * 6);
        for (cgltf_size i = 0; i < pos->count; ++i) {
            for (int k = 0; k < 3; ++k) {
                interleaved[i * 6 + k] = p[i * 3 + k];
                interleaved[i * 6 + 3 + k] = nv[i * 3 + k];
            }
        }
        std::vector<uint32_t> indices(prim.indices->count);
        for (cgltf_size i = 0; i < indices.size(); ++i)
            indices[i] = (uint32_t)cgltf_accessor_read_index(prim.indices, i);

        ModelNode out;
        out.name = node.name ? node.name : "";
        if (node.has_rotation)
            out.rest.q = Quat(node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]);
        if (node.has_translation) out.rest.p = Vec3(node.translation[0], node.translation[1], node.translation[2]);
        out.mesh.upload(interleaved, indices);
        model.nodes.push_back(std::move(out));
    }
    cgltf_free(data);
    LOGI("model %s: %zu parts", file.c_str(), model.nodes.size());
    return !model.nodes.empty();
}

const ControllerModel *ModelCache::get(const std::string &family, int hand) {
    std::string file = "models/" + family + (hand == 0 ? "_left.glb" : "_right.glb");
    auto it = loaded_.find(file);
    if (it != loaded_.end()) return &it->second;
    if (failed_.count(file) || !reader_) return nullptr;
    ControllerModel model;
    if (!load(reader_, file, model)) {
        failed_[file] = true;
        return nullptr;
    }
    return &(loaded_[file] = std::move(model));
}
