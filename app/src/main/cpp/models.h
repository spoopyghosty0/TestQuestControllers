// Controller models (glb files in the APK's assets/models).
#pragma once

#include <android/asset_manager.h>

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
    void set_assets(AAssetManager *assets) { assets_ = assets; }
    // family as in Profile::family, hand 0 = left, 1 = right. nullptr if the file is missing or broken.
    const ControllerModel *get(const std::string &family, int hand);

private:
    AAssetManager *assets_ = nullptr;
    std::map<std::string, ControllerModel> loaded_;
    std::map<std::string, bool> failed_;
};
