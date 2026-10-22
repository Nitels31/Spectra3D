#pragma once
#include <string>
#include <vector>

#include "assets/MeshRegistry.hpp"
#include "gui/GuiCanvas.hpp"
#include "physics/PhysicsWorld.hpp"

namespace s3d {

class ParticleSystem;

// Scene serialisation.
//
// The scene is JSON. There is one format a scene is written in, and it is one a
// person can read: named fields, named shapes, numbers in the places a person
// expects them, and a position on one line rather than four.
//
// Mesh references are stored by name, so scenes survive restarts.
//
// The canvas of the game - the interface, the buttons and the labels - is part
// of the scene and is written into the same file: an interface that is not in
// the scene is an interface that is not in the game.
namespace scenes {

// Writes the scene as JSON.
//
// The particles are the emitters of the world, and they are part of the scene:
// an effect that is in the picture and not in the file is an effect that is
// gone when the scene is opened again. They may be null, and then the scene
// is written without them.
bool save(const PhysicsWorld& world, const MeshRegistry& meshes, const gui::Canvas& canvas,
          const std::string& path, const ParticleSystem* particles = nullptr);

// Replaces the world contents on success. Mesh names that cannot be resolved are
// dropped (the body keeps its primitive shape).
//
// `threads` is how many the loader may use for the models the scene names. The
// parse is the slow part of opening a project and it touches no GL, so it runs
// in parallel; one is the same as no threads at all.
//
// The particles are the emitters of the world, and they are replaced by the
// ones in the file - the emitters of the last scene are not emitters of this
// one. They may be null, and then the scene is read without them.
bool load(PhysicsWorld& world, MeshRegistry& meshes, gui::Canvas& canvas,
          const std::string& path, int threads = 1, ParticleSystem* particles = nullptr);

// The format, named. `save` and `load` are these; they are here for a caller
// that wants to be explicit.
bool saveJson(const PhysicsWorld& world, const MeshRegistry& meshes, const gui::Canvas& canvas,
              const std::string& path, const ParticleSystem* particles = nullptr);
bool loadJson(PhysicsWorld& world, MeshRegistry& meshes, gui::Canvas& canvas,
               const std::string& path, int threads = 1, ParticleSystem* particles = nullptr);

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
