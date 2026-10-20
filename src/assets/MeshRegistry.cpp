#include "assets/MeshRegistry.hpp"

#include "assets/TextureRegistry.hpp"
#include "core/Log.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"
#include "assets/MeshImport.hpp"

#include <algorithm>
#include <atomic>
#include <thread>

namespace s3d {
namespace {

const std::string kNoName = "-";

} // namespace

void MeshRegistry::setLoaderThreads(int threads) {
    loaderThreads_ = std::clamp(threads, 1, 6);
}

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
    return finishImport(std::move(asset), quiet);
}

// The GL half of an import: the textures become real indices and the asset
// joins the registry. This thread's job - the context is here and nowhere else.
int MeshRegistry::finishImport(MeshAsset asset, bool quiet) {
    Log::info("Imported mesh '%s': %d parts, %d triangles%s", asset.name.c_str(),
              (int)asset.parts.size(), asset.triangleCount(),
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

int MeshRegistry::loadTexture(const std::string& path, bool repeat) {
    if (!textures_) return -1;
    const std::string& root = fs::assetRoot();
    if (root.empty()) return -1;
    return textures_->loadFile(str::join(root, path), /*quiet=*/true, repeat);
}

const std::vector<std::string>& MeshRegistry::textureFilesOf(int index) const {
    if (index < 0 || index >= (int)assets_.size()) return noTextures_;
    return assets_[(size_t)index].texturePaths;
}

void MeshRegistry::importFiles(const std::vector<std::string>& paths, int threads) {
    const size_t count = paths.size();
    if (count == 0) return;
    const int workers = std::clamp(threads, 1, 6);

    // The parse is the slow part of opening a project and it touches no GL, so
    // it runs on as many threads as the user asked for. Each file is its own
    // asset and the workers only fill their own slot, so nothing is shared.
    std::vector<MeshAsset> parsed(count);
    std::vector<char> ok(count, 0);
    if (workers <= 1 || count == 1) {
        for (size_t i = 0; i < count; ++i) ok[i] = loadMeshFile(paths[i], parsed[i]) ? 1 : 0;
    } else {
        std::atomic<size_t> next{0};
        std::vector<std::thread> pool;
        pool.reserve((size_t)workers);
        for (int w = 0; w < workers; ++w) {
            pool.emplace_back([&] {
                for (size_t i = next.fetch_add(1); i < count; i = next.fetch_add(1))
                    ok[i] = loadMeshFile(paths[i], parsed[i]) ? 1 : 0;
            });
        }
        for (std::thread& t : pool) t.join();
    }

    // The textures and the registry are this thread's business: the GL context
    // is here and nowhere else.
    for (size_t i = 0; i < count; ++i) {
        if (!ok[i]) {
            Log::error("Mesh import failed: %s", paths[i].c_str());
            continue;
        }
        const std::string name = str::fileStem(paths[i]);
        if (find(name) >= 0) continue;
        parsed[i].name = name;
        finishImport(std::move(parsed[i]), false);
    }
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