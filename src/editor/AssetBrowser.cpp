#include "editor/AssetBrowser.hpp"

#include "assets/MeshImport.hpp"
#include "core/Log.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"

#include <algorithm>

namespace s3d {
namespace {

// Imported models get a box hull with a plausible mass instead of an exact
// inertia tensor - good enough to stack and throw them around.
float massFromHull(const Vec3& half) { return 200.0f * 8.0f * half.x * half.y * half.z; }

} // namespace

int AssetBrowser::refresh() {
    files_.clear();
    const std::string modelsDir = str::join(fs::assetRoot(), "models");
    files_ = fs::listFilesByExtension(modelsDir, meshExtensions());

    // Fall back to the working directory so a hand-dropped model still shows up.
    if (files_.empty()) files_ = fs::listFilesByExtension(".", meshExtensions());

    std::sort(files_.begin(), files_.end());
    files_.erase(std::unique(files_.begin(), files_.end()), files_.end());
    scanned_ = true;
    return (int)files_.size();
}

int AssetBrowser::import(const std::string& path, PhysicsWorld& world, Camera& camera,
                         MeshRegistry& registry, int& selectionOut) {
    const int meshIndex = registry.importFile(path);
    if (meshIndex < 0) return -1;

    const MeshAsset* mesh = registry.at(meshIndex);
    if (!mesh) return -1;

    RigidBody body;
    body.shape = Shape::Mesh;
    body.mesh = meshIndex;
    body.meshScale = 1.0f;
    body.halfExtents = mesh->half;
    body.position = camera.position + camera.forward().normalized() * (mesh->radius + 3.0f);
    body.configure(massFromHull(mesh->half));
    body.syncPrevTransform();

    world.bodies.push_back(body);
    const int index = (int)world.bodies.size() - 1;
    selectionOut = index;
    return index;
}

} // namespace s3d