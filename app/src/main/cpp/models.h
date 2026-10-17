// Controller models (glb files in the APK's assets/models).
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "gfx.h"

struct ModelNode {
    std::string name;  // part name (x, trigger, thumbstick, ...) or body/ring
    Pose rest;         // node pose in the grip frame
    Mesh mesh;
};

struct ControllerModel {
    std::vector<ModelNode> nodes;
    const ModelNode *find(const std::string &name) const;
};

class ModelCache {
public:
    // Reads a file from the app's assets (models/<family>_<hand>.glb) into `out`.
    using Reader = std::function<bool(const std::string &path, std::vector<uint8_t> &out)>;
    void set_reader(Reader reader) { reader_ = std::move(reader); }
    // family as in Profile::family, hand 0 = left, 1 = right. nullptr if the file is missing or broken.
    const ControllerModel *get(const std::string &family, int hand);

private:
    Reader reader_;
    std::map<std::string, ControllerModel> loaded_;
    std::map<std::string, bool> failed_;
};
