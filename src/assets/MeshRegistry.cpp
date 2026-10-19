#include "assets/MeshRegistry.hpp"

#include "assets/TextureRegistry.hpp"
#include "core/Log.hpp"
#include "core/Str.hpp"
#include "assets/MeshImport.hpp"

namespace s3d {
namespace {

const std::string kNoName = "-";

} // namespace

int MeshRegistry::importFile(const std::string& path, bool quiet) {
    // One allocation instead of two: fileStem() is now a single copy of a view
    // rather than a fileName() temporary plus a substring of it.
    const std::string name = str::fileStem(path);
    const int existing = find(name);
    if (existing >= 0) return existing;

    MeshAsset asset;
    if (!loadMeshFile(path, asset)) {
        if (!quiet) Log::error("Mesh import failed: %s", path.c_str());
        return -1;
    }
    if (asset.parts.empty() || asset.radius < 1e-4f) {
        if (!quiet) Log::error("Mesh has no usable geometry: %s", path.c_str());
        return -1;
    }

    asset.name = name;
    Log::info("Imported mesh '%s': %d parts, %d triangles%s", name.c_str(),
              (int)asset.parts.size(), (int)asset.triangleCount(),
              asset.scale != 1.0f ? " (auto-fitted)" : "");

    // Now that a GL context exists, turn what the loader could only record - a
    // path, or the bytes of an image the model file carried itself - into real
    // indices. Quiet: a missing map leaves the part untextured and the model still
    // draws, which the loader's own log has already explained.
    //
    // One registry entry per image, not per part. The falcon's nineteen parts
    // share two images between them, and a per-part loop uploaded the same
    // megabyte of PNG seventeen times over. The asset's slot number is the image's
    // identity, so the slot is resolved once and every part that points at it
    // takes the answer.
    if (textures_) {
        std::vector<int> slots(asset.texturePaths.size(), -1);
        for (size_t slot = 0; slot < asset.texturePaths.size(); ++slot) {
            // A slot with no path is an image the model file carried itself. The
            // bounds check on textureBytes is the loader's side of the contract:
            // only the glTF loader writes embedded images, and the others never
            // leave a path empty.
            const bool embedded = asset.texturePaths[slot].empty() &&
                                  slot < asset.textureBytes.size() &&
                                  !asset.textureBytes[slot].empty();
            slots[slot] = embedded ? textures_->loadMemory(asset.textureBytes[slot], true)
                                   : textures_->loadFile(asset.texturePaths[slot], true);
        }
        for (MeshPart& part : asset.parts)
            if (part.texture >= 0 && part.texture < (int)slots.size())
                part.texture = slots[(size_t)part.texture];
    }

    return importMesh(std::move(asset));
}

int MeshRegistry::importMesh(MeshAsset asset) {
    const std::string name = asset.name.empty() ? kNoName : asset.name;
    const int existing = find(name);
    if (existing >= 0) return existing;

    const int index = (int)assets_.size();
    assets_.push_back(std::move(asset));
    names_.push_back(name);
    byName_[names_.back()] = index;
    return index;
}

int MeshRegistry::find(const std::string& name) const {
    const auto it = byName_.find(name);
    return it == byName_.end() ? -1 : it->second;
}

const MeshAsset* MeshRegistry::at(int index) const {
    if (index < 0 || index >= (int)assets_.size()) return nullptr;
    return &assets_[index];
}

const std::string& MeshRegistry::nameOf(int index) const {
    if (index < 0 || index >= (int)names_.size()) return kNoName;
    return names_[index];
}

} // namespace s3d