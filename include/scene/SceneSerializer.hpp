#pragma once
#include <string>
#include <vector>

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
//
// `threads` is how many the loader may use for the models the scene names. The
// parse is the slow part of opening a project and it touches no GL, so it runs
// in parallel; one is the same as no threads at all.
bool load(PhysicsWorld& world, MeshRegistry& meshes, const std::string& path, int threads = 1);

// The format, named. `save` and `load` are these; they are here for a caller
// that wants to be explicit.
bool saveJson(const PhysicsWorld& world, const MeshRegistry& meshes, const std::string& path);
bool loadJson(PhysicsWorld& world, MeshRegistry& meshes, const std::string& path,
              int threads = 1);

// Looks for `name` next to the bundle's `models` folder, then in the working
// directory, so a scene saved from an old layout still resolves.
int resolveMesh(MeshRegistry& meshes, const std::string& name);

// The file a model name is in, or an empty string when there is none. The
// probing is a file listing, and it is cheap next to the parse.
std::string findMeshFile(const std::string& name);

// Reads the models a scene names, in parallel. The names are resolved to files
// first and the files are parsed on as many threads as asked for; the textures
// are uploaded afterwards, on the thread that owns the context.
void preloadMeshes(MeshRegistry& meshes, const std::vector<std::string>& names, int threads);

// The models a scene names, without reading any of them. A build that stages
// the models the project uses asks this first.
std::vector<std::string> sceneMeshNames(const std::string& path);

} // namespace scenes
} // namespace s3d
