#pragma once
#include <vector>

#include <GL/gl.h>

#include "../assets/MeshRegistry.hpp"
#include "../physics/PhysicsWorld.hpp"
#include "Camera.hpp"
#include "Color.hpp"
#include "PrimitiveCache.hpp"

namespace s3d {

// Viewport rect handed down from the editor layout. `glY` is the GL-convention
// bottom edge, which is what glViewport expects.
struct Viewport {
    int glX = 0;
    int glY = 0;
    int width = 0;
    int height = 0;
};

// 3D scene renderer: fixed-function lighting, arena, bodies, frustum culling and
// the selection/gizmo overlays. UI drawing lives in UiRenderer instead.
class SceneRenderer {
public:
    // Enables fixed-function lighting and builds the primitive cache.
    void initialize(const Vec3& arenaMin, const Vec3& arenaMax);

    // Clears the whole framebuffer before the viewport pass. Without this a
    // window that was just resized (or a frame captured mid-resize) can show
    // whatever the back buffer happened to contain - usually black - in the
    // strip the layout has not caught up with yet.
    void beginFrame(int framebufferWidth, int framebufferHeight);

    void renderWorld(const PhysicsWorld& world, const MeshRegistry& meshes,
                     const Camera& camera, const Viewport& viewport, bool showGrid,
                     bool showVectors);

    void renderSelection(const RigidBody& body);
    void renderGizmo(const Vec3& position, const Vec3* axes, float length,
                     int activeAxis, bool scaleMode);

    const PrimitiveCache& primitives() const { return primitives_; }

private:
    // The arena grid geometry is fixed: 201 candidate lines from -100..100 on
    // both X and Z, with v == 0 skipped because the world axes are drawn as
    // their own pair of coloured segments. Each surviving line contributes two
    // segments (4 vertices), and the three world axes add 6 more vertices
    // (X red, Y green, Z blue - the same colours the gizmo and the navigation
    // balls use, so the world axes and the handles agree).
    struct ArenaGrid {
        static constexpr int kSteps = 201;         // -100, -99, ... 100
        static constexpr int kLines = kSteps - 1;  // v == 0 is skipped
        static constexpr int kGridVertices = kLines * 4;
        static constexpr int kAxisVertices = 6;
        static constexpr int kVertexCount = kGridVertices + kAxisVertices;
        static constexpr int kLineStride = 5;      // v, r, g, b, major
    };

    void setProjection(const Camera& camera, const Viewport& viewport);
    void setViewMatrix(const Camera& camera);
    void buildArenaGrid();
    void drawArena(const Camera& camera);
    void drawBodies(const PhysicsWorld& world, const MeshRegistry& meshes);
    void drawDebugVectors(const PhysicsWorld& world);

    void extractFrustum(const float* projection, const float* view);
    bool sphereVisible(const Vec3& center, float radius) const;
    bool drawImportedMesh(const RigidBody& body, const MeshRegistry& meshes);

    PrimitiveCache primitives_;
    float projection_[16] = {};
    float view_[16] = {};
    float frustum_[6][4] = {};

    // Static grid positions plus the per-frame colour stream. Built once, then
    // drawn with a single glDrawArrays instead of ~1000 immediate-mode calls.
    bool arenaGridReady_ = false;
    std::vector<float> arenaPositions_;
    std::vector<float> arenaColors_;
    std::vector<float> arenaLines_;
};

} // namespace s3d