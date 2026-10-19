#pragma once
#include <string>

#include "assets/MeshRegistry.hpp"
#include "physics/PhysicsWorld.hpp"

namespace s3d {

// Scene serialisation.
//
// The scene is JSON. There is one format a scene is written in, and it is one a
// person can read: named fields, named shapes, numbers in the places a person
// expects them, and a position on one line rather than four.
//
// Mesh references are stored by name, so scenes survive restarts.
namespace scenes {

// Writes the scene as JSON.
bool save(const PhysicsWorld& world, const MeshRegistry& meshes, const std::string& path);

// Replaces the world contents on success. Mesh names that cannot be resolved are
// dropped (the body keeps its primitive shape).
bool load(PhysicsWorld& world, MeshRegistry& meshes, const std::string& path);

// The format, named. `save` and `load` are these; they are here for a caller
// that wants to be explicit.
bool saveJson(const PhysicsWorld& world, const MeshRegistry& meshes, const std::string& path);
bool loadJson(PhysicsWorld& world, MeshRegistry& meshes, const std::string& path);

// Looks for `name` next to the bundle's `models` folder, then in the working
// directory, so a scene saved from an old layout still resolves.
int resolveMesh(MeshRegistry& meshes, const std::string& name);

} // namespace scenes
} // namespace s3d
