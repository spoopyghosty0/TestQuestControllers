// Controller models: the WebXR Input Profiles assets (assets/webxr/<profile id>/), glTF in the grip pose frame,
// with the profile's visual responses (which node moves between which two poses for which input).
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "gfx.h"

struct Primitive {
    Mesh mesh;
    GLuint texture = 0;  // base colour texture, 0 = none
    Vec3 color{1, 1, 1};  // base colour factor
};

struct ModelNode {
    std::string name;
    Quat r;
    Vec3 t, s{1, 1, 1};
    int parent = -1;
    std::vector<int> children;
    std::vector<Primitive> prims;
    int component = -1;  // the component this node belongs to (its nearest component root), -1 = body
};

struct ModelComponent {
    std::string id;    // WebXR component id, e.g. "x-button"
    std::string part;  // the input part it shows (see part_for() in profiles.h), e.g. "x"
    int root = -1, touch_point = -1;
};

struct ModelResponse {
    enum Property { kButton, kXAxis, kYAxis, kState } property = kButton;
    bool visibility = false;  // else: transform between min and max
    int component = -1, value = -1, min = -1, max = -1;
    unsigned states = 0;  // bit 0 default, 1 touched, 2 pressed
};

struct ControllerModel {
    std::vector<ModelNode> nodes;
    std::vector<int> roots;
    std::vector<ModelComponent> components;
    std::vector<ModelResponse> responses;
    int find(const std::string &name) const;
};

// The input part a WebXR component shows ("xr-standard-trigger" -> "trigger", "x-button" -> "x", ...).
std::string component_part(const std::string &component_id);

class ModelCache {
public:
    // Reads a file from the app's assets (e.g. "webxr/oculus-touch-v3/left.glb") into `out`.
    using Reader = std::function<bool(const std::string &path, std::vector<uint8_t> &out)>;
    void set_reader(Reader reader) { reader_ = std::move(reader); }
    // profile as in Profile::family (a WebXR profile id), hand 0 = left, 1 = right. nullptr if missing or broken.
    const ControllerModel *get(const std::string &profile, int hand);

private:
    Reader reader_;
    std::map<std::string, ControllerModel> loaded_;
    std::map<std::string, bool> failed_;
};
