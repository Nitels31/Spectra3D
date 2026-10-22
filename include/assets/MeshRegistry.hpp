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

    // How many threads the loader may use. One is the same as no threads at
    // all; the user picks the number in the settings.
    void setLoaderThreads(int threads);
    int loaderThreads() const { return loaderThreads_; }

    // Imports several files at once, and does the slow part in parallel.
    //
    // Parsing a model is the slow part of opening a project and it touches no
    // GL, so it runs on as many threads as there are files to read; the
    // textures are uploaded afterwards, on this thread, because that is where
    // the context is. `threads` is how many the user asked for.
    void importFiles(const std::vector<std::string>& paths, int threads);

    int importMesh(MeshAsset asset);

    // Puts a new mesh in the place of an old one, under the old name.
    //
    // A ground that has been painted is the same mesh as it was, only
    // different, and the body that stands on it keeps pointing at it: the
    // index of a mesh is a thing other things hold, and it must not change
    // under them.
    bool update(int index, MeshAsset asset);

    int find(const std::string& name) const;
    const MeshAsset* at(int index) const;

    // The image files a model refers to, so a build that stages the model can
    // stage its pictures with it: a model without its texture still draws, in
    // its flat colour, and that is not what anybody meant.
    const std::vector<std::string>& textureFilesOf(int index) const;

    // A picture, loaded on demand. The registry knows where the textures live,
    // so a caller that wants grass on the ground does not have to know where
    // the grass is.
    //
    // `path` is relative to the assets: "image/textures/grass.jpg". A repeated
    // picture is one that tiles.
    int loadTexture(const std::string& path, bool repeat = false);
    const std::string& nameOf(int index) const;
    int size() const { return (int)assets_.size(); }

private:
    // The GL half of an import: the textures become real indices and the asset
    // joins the registry. This thread's job.
    int finishImport(MeshAsset asset, bool quiet);

    std::vector<MeshAsset> assets_;
    std::vector<std::string> noTextures_;
    std::vector<std::string> names_;
    std::unordered_map<std::string, int> byName_;
    TextureRegistry* textures_ = nullptr;
    int loaderThreads_ = 1;
};

} // namespace s3d