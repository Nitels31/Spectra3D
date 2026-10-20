#pragma once
#include <string>
#include <unordered_map>
#include <vector>

namespace s3d {

// Every texture uploaded to GL for the running session, owned here.
//
// Textures are keyed by the path they were loaded from and never removed, so the
// index a MeshPart holds stays valid for the lifetime of the registry - the same
// contract MeshRegistry gives models. Handles are GL object names, which is why
// this cannot outlive the GL context: release() must run before the context goes.
class TextureRegistry {
public:
    ~TextureRegistry();
    TextureRegistry() = default;
    TextureRegistry(const TextureRegistry&) = delete;
    TextureRegistry& operator=(const TextureRegistry&) = delete;

    // Loads `path` unless that path is already loaded. Returns the texture index,
    // or -1 on failure. `quiet` suppresses the log, which scene loading wants while
    // it probes a model next to its map.
    //
    // Failures are not fatal on purpose: a model whose map is missing still draws,
    // in its flat material colour. A broken texture reference should not make the
    // whole model disappear.
    // A repeated picture is one that tiles: the ground is a hundred metres of
    // grass, and one picture of grass stretched over it is not grass.
    int loadFile(const std::string& path, bool quiet = false, bool repeat = false);

    // The same, for an image that was embedded in a model file instead of sitting
    // beside it. A .glb keeps its textures in its own binary chunk, so there is no
    // path to open and nothing to name - the bytes are the only handle there is.
    // Returns the texture index, or -1 if the bytes are not an image.
    int loadMemory(const std::vector<unsigned char>& bytes, bool quiet = false);

    // GL texture name for an index, or 0 when there is none. 0 is GL's own "no
    // texture" value, so a caller can bind it unconditionally.
    unsigned at(int index) const;

    int find(const std::string& path) const;
    const std::string& nameOf(int index) const;
    int size() const { return (int)textures_.size(); }

    void release();

private:
    std::vector<unsigned> textures_; // 0 where a load failed
    std::vector<std::string> names_;
    std::unordered_map<std::string, int> byPath_;
};

} // namespace s3d