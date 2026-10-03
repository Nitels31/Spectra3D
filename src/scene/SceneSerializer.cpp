#include "SceneSerializer.hpp"

#include "../assets/MeshImport.hpp"
#include "../core/Log.hpp"
#include "../core/Str.hpp"
#include "../platform/Filesystem.hpp"

#include <fstream>
#include <sstream>
#include <vector>

namespace s3d {
namespace scenes {
namespace {

constexpr const char* kNoMesh = "-";

// Restores the per-shape dimension fields that are only stored as half extents.
void syncShapeDimensions(RigidBody& body) {
    switch (body.shape) {
        case Shape::Cylinder:
            body.cylinderRadius = body.halfExtents.x;
            break;
        case Shape::Capsule:
            body.capsuleRadius = body.halfExtents.x;
            body.capsuleHeight = body.halfExtents.y;
            break;
        case Shape::Cone:
            body.coneRadius = body.halfExtents.x;
            body.coneHeight = body.halfExtents.y * 2.0f;
            break;
        case Shape::Pyramid:
            body.pyramidBaseRadius = body.halfExtents.x;
            body.pyramidHeight = body.halfExtents.y * 2.0f;
            break;
        case Shape::Prism:
            body.prismRadius = body.halfExtents.x;
            body.prismHeight = body.halfExtents.y * 2.0f;
            break;
        default: break;
    }
}

} // namespace

bool save(const PhysicsWorld& world, const MeshRegistry& meshes, const std::string& path) {
    std::ofstream file(path);
    if (!file) {
        Log::error("Scene: cannot write %s", path.c_str());
        return false;
    }

    for (const RigidBody& body : world.bodies) {
        const std::string meshName =
            body.hasMesh() ? meshes.nameOf(body.mesh) : std::string(kNoMesh);

        file << (int)body.shape << ' '
             << body.position.x << ' ' << body.position.y << ' ' << body.position.z << ' '
             << body.velocity.x << ' ' << body.velocity.y << ' ' << body.velocity.z << ' '
             << body.orientation.w << ' ' << body.orientation.x << ' '
             << body.orientation.y << ' ' << body.orientation.z << ' '
             << body.halfExtents.x << ' ' << body.halfExtents.y << ' '
             << body.halfExtents.z << ' '
             << body.mass << ' '
             << body.material.friction << ' ' << body.material.restitution << ' '
             << body.material.density << ' '
             << body.angularVelocity.x << ' ' << body.angularVelocity.y << ' '
             << body.angularVelocity.z << ' '
             << body.meshScale << ' ' << meshName << ' '
             << (body.material.isStatic ? 1 : 0) << ' '
             << (body.visible ? 1 : 0) << ' '
             << (body.locked ? 1 : 0) << ' '
             << (body.anchorPosition ? 1 : 0) << ' ' << body.anchorPoint.x << ' '
             << body.anchorPoint.y << ' ' << body.anchorPoint.z << ' '
             << (body.anchorRotation ? 1 : 0) << ' ' << body.anchorOrientation.w << ' '
             << body.anchorOrientation.x << ' ' << body.anchorOrientation.y << ' '
             << body.anchorOrientation.z << ' ' << body.anchorSpring << '\n';
    }
    return (bool)file;
}

int resolveMesh(MeshRegistry& meshes, const std::string& name) {
    const int cached = meshes.find(name);
    if (cached >= 0) return cached;

    const std::string root = fs::assetRoot();
    const std::string modelsDir = str::join(root, "models");
    for (const std::string& ext : meshExtensions()) {
        const int index = meshes.importFile(str::join(modelsDir, name + ext));
        if (index >= 0) return index;
        const int local = meshes.importFile(name + ext);
        if (local >= 0) return local;
    }
    return -1;
}

bool load(PhysicsWorld& world, MeshRegistry& meshes, const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        Log::error("Scene: cannot read %s", path.c_str());
        return false;
    }

    std::vector<RigidBody> loaded;
    std::string line;
    while (std::getline(file, line)) {
        if (str::trim(line).empty()) continue;
        std::istringstream row(line);

        int shapeId = 0, isStatic = 0, visible = 1, locked = 0;
        int anchorPos = 0, anchorRot = 0;
        double px = 0, py = 0, pz = 0, vx = 0, vy = 0, vz = 0;
        double qw = 1, qx = 0, qy = 0, qz = 0;
        double hx = 0, hy = 0, hz = 0, mass = 1.0;
        double friction = 0, restitution = 0, density = 1;
        double wx = 0, wy = 0, wz = 0, meshScale = 1.0;
        double ax = 0, ay = 0, az = 0, aqw = 1, aqx = 0, aqy = 0, aqz = 0, anchorSpring = 50.0;
        std::string meshName;

        if (!(row >> shapeId >> px >> py >> pz >> vx >> vy >> vz >> qw >> qx >> qy >> qz >>
              hx >> hy >> hz >> mass >> friction >> restitution >> density >> wx >> wy >> wz >>
              meshScale >> meshName))
            continue;

        RigidBody body;
        body.shape = (Shape)shapeId;
        body.position = {(float)px, (float)py, (float)pz};
        body.velocity = {(float)vx, (float)vy, (float)vz};
        body.orientation = Quat{(float)qw, (float)qx, (float)qy, (float)qz}.normalized();
        body.halfExtents = {(float)hx, (float)hy, (float)hz};
        body.angularVelocity = {(float)wx, (float)wy, (float)wz};
        body.material.friction = (float)friction;
        body.material.restitution = (float)restitution;
        body.material.density = (float)density;
        syncShapeDimensions(body);
        body.configure((float)mass);

        // Everything past the mesh name is optional, so older files still load.
        row >> isStatic >> visible >> locked >> anchorPos >> ax >> ay >> az >> anchorRot >> aqw >>
            aqx >> aqy >> aqz >> anchorSpring;
        body.material.isStatic = isStatic != 0;
        body.visible = visible != 0;
        body.locked = locked != 0;
        body.anchorPosition = anchorPos != 0;
        body.anchorPoint = {(float)ax, (float)ay, (float)az};
        body.anchorRotation = anchorRot != 0;
        body.anchorOrientation =
            Quat{(float)aqw, (float)aqx, (float)aqy, (float)aqz}.normalized();
        body.anchorSpring = (float)anchorSpring;

        if (meshName != kNoMesh) {
            const int meshIndex = resolveMesh(meshes, meshName);
            if (meshIndex >= 0) {
                body.mesh = meshIndex;
                body.meshScale = (float)meshScale;
            } else {
                Log::warn("Scene: mesh '%s' not found, using primitive", meshName.c_str());
            }
        }
        loaded.push_back(body);
    }

    if (loaded.empty()) {
        Log::warn("Scene: no valid bodies in %s", path.c_str());
        return false;
    }

    world.bodies = std::move(loaded);
    world.wakeAll();
    return true;
}

} // namespace scenes
} // namespace s3d