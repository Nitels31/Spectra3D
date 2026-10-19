#pragma once
#include <vector>

#include <GL/gl.h>

#include "assets/MeshRegistry.hpp"
#include "assets/TextureRegistry.hpp"
#include "physics/PhysicsWorld.hpp"
#include "render/Camera.hpp"
#include "render/Color.hpp"
#include "render/PrimitiveCache.hpp"

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

    // `alpha` is the fixed-step accumulator's position into the next physics step,
    // 0..1, from FixedClock::alpha(). A body is drawn at
    // lerp(prevPosition, position, alpha) so the 60 Hz physics reads as smooth
    // motion at any refresh rate. 0 reproduces the old behaviour exactly.
    // `hiddenBody` is a body index the caller does not want drawn - the camera
    // the viewport is looking through, which sits at the eye and would fill the
    // screen with its own icon. -1 for none.
    void renderWorld(const PhysicsWorld& world, const MeshRegistry& meshes,
                     const TextureRegistry& textures, const Camera& camera,
                     const Viewport& viewport, bool showGrid, bool showVectors,
                     float alpha, int hiddenBody = -1);

    void renderSelection(const RigidBody& body);

    // Which handles the gizmo draws. One enum instead of a bool because the tool
    // now has three shapes, and a bool that encodes two of them is how the rotate
    // tool ends up drawn as an arrow.
    enum class GizmoShape { Arrows, Rings, Cubes };

    void renderGizmo(const Vec3& position, const Vec3* axes, float length, float ringRadius,
                     const Vec3* ringU, const Vec3* ringV, int activeAxis, GizmoShape shape);

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
    void drawBodies(const PhysicsWorld& world, const MeshRegistry& meshes,
                     const TextureRegistry& textures, float alpha, int hiddenBody);
    void drawDebugVectors(const PhysicsWorld& world);

    void extractFrustum(const float* projection, const float* view);
    bool sphereVisible(const Vec3& center, float radius) const;
    bool drawImportedMesh(const RigidBody& body, const MeshRegistry& meshes,
                          const TextureRegistry& textures, const Quat& orientation);
    void drawCameraEntity(const RigidBody& body, const Quat& orientation);

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