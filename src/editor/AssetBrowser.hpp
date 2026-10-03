#pragma once
#include <string>
#include <vector>

#include "../assets/MeshRegistry.hpp"
#include "../physics/PhysicsWorld.hpp"
#include "../render/Camera.hpp"

namespace s3d {

// Lists importable models from the bundled `models` folder and creates bodies
// for them. Kept out of the editor panels so both the asset dock and any future
// headless importer share one code path.
class AssetBrowser {
public:
    // Rescans the asset root. Returns the number of files found.
    int refresh();

    const std::vector<std::string>& files() const { return files_; }
    bool scanned() const { return scanned_; }
    void invalidate() { scanned_ = false; }

    // Imports `path` and returns the body index, or -1 on failure. The body is
    // placed in front of the camera and `selectionOut` receives its index.
    int import(const std::string& path, PhysicsWorld& world, Camera& camera,
               MeshRegistry& registry, int& selectionOut);

private:
    std::vector<std::string> files_;
    bool scanned_ = false;
};

} // namespace s3d