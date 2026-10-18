#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "assets/MeshAsset.hpp"

namespace s3d {

class TextureRegistry;

// Owns every imported model for one running editor session. Bodies reference a
// model by the stable index returned here; entries are never removed, so those
// indices stay valid for the lifetime of the registry.
class MeshRegistry {
public:
    // Textures a part may reference, or nullptr when this registry never loads
    // textures - which is the case for anything headless. Optional rather than
    // required because importing geometry must not start needing a GL context.
    void setTextureRegistry(TextureRegistry* textures) { textures_ = textures; }

    // Imports `path` unless a model with the same file stem is already loaded.
    // Returns the model index, or -1 on failure. `quiet` suppresses the failure
    // log, which scene loading wants while it probes several extensions.
    int importFile(const std::string& path, bool quiet = false);
    int importMesh(MeshAsset asset);

    int find(const std::string& name) const;
    const MeshAsset* at(int index) const;
    const std::string& nameOf(int index) const;
    int size() const { return (int)assets_.size(); }

private:
    std::vector<MeshAsset> assets_;
    std::vector<std::string> names_;
    std::unordered_map<std::string, int> byName_;
    TextureRegistry* textures_ = nullptr;
};

} // namespace s3d