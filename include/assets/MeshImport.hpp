#pragma once
#include <string>
#include <vector>

#include "assets/MeshAsset.hpp"

namespace s3d {

// Format-specific decoders. Each one owns a third-party parser and produces a
// MeshAsset; none of them touch global state.
namespace loaders {

// Returns false and logs the reason when the file cannot be parsed.
bool loadGltf(const std::string& path, MeshAsset& out);
bool loadObj(const std::string& path, MeshAsset& out);
bool loadPly(const std::string& path, MeshAsset& out);
bool loadFbx(const std::string& path, MeshAsset& out);

} // namespace loaders

// Dispatches on the file extension, falling back to glTF.
bool loadMeshFile(const std::string& path, MeshAsset& out);

// Extensions the editor's asset browser treats as importable.
const std::vector<std::string>& meshExtensions();

} // namespace s3d