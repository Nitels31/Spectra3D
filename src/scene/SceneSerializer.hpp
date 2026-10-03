#pragma once
#include <string>

#include "../assets/MeshRegistry.hpp"
#include "../physics/PhysicsWorld.hpp"

namespace s3d {

// Plain-text scene serialisation. One line per body, whitespace separated, in a
// fixed field order - readable, diffable and cheap to parse without a
// dependency. Mesh references are stored by name so scenes survive restarts.
namespace scenes {

bool save(const PhysicsWorld& world, const MeshRegistry& meshes, const std::string& path);

// Replaces the world contents on success. Mesh names that cannot be resolved are
// dropped (the body keeps its primitive shape).
bool load(PhysicsWorld& world, MeshRegistry& meshes, const std::string& path);

// Looks for `name` next to the bundle's `models` folder, then in the working
// directory, so a scene saved from an old layout still resolves.
int resolveMesh(MeshRegistry& meshes, const std::string& name);

} // namespace scenes
} // namespace s3d