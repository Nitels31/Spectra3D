#pragma once
#include <vector>

#include <GL/gl.h>

#include "assets/MeshRegistry.hpp"
#include "assets/TextureRegistry.hpp"
#include "physics/PhysicsWorld.hpp"
#include "render/Camera.hpp"
#include "render/Color.hpp"
#include "render/PrimitiveCache.hpp"
#include "render/Skybox.hpp"

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
    //
    // `viewport` is the area the scene is drawn in: the scene column of the
    // editor, between the two docks and above the console. The projection is the
    // camera's own field of view over it, so what is on screen and what the
    // pointer points at are the same picture.
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

    // The sky the world is seen against. It lives here because it is a GL
    // object, and this is the class that owns GL objects.
    Skybox& sky() { return sky_; }
    const Skybox& sky() const { return sky_; }

private:
    // The arena grid geometry is fixed. It is drawn in two parts, because one
    // part cannot be both fine and endless: the minor lines are one unit
    // apart and are only worth drawing near the camera, and the major lines
    // are ten units apart and carry the grid out to the horizon.
    //
    // Every line is cut into short segments, so that each segment can have its
    // own transparency. A line drawn as one long segment can only be faded as
    // a whole, and a line faded as a whole either ends at an edge or is too
    // dark at its far end.
    //
    // The three world axes are drawn on top of the grid at the world origin
    // (X red, Y green, Z blue - the same colours the gizmo and the navigation
    // balls use, so the world axes and the handles agree).
    struct ArenaGrid {
        // Minor lines: one unit apart, a patch of 60 units around the camera,
        // cut into segments of 6 units.
        static constexpr int kMinorSteps = 121;
        static constexpr int kMinorSegments = 20;
        static constexpr int kMinorVertices = kMinorSteps * 2 * kMinorSegments * 2;
        // Major lines: ten units apart, a patch of 400 units around the
        // camera, cut into segments of 40 units.
        static constexpr int kMajorSteps = 81;
        static constexpr int kMajorSegments = 20;
        static constexpr int kMajorVertices = kMajorSteps * 2 * kMajorSegments * 2;
        static constexpr int kAxisVertices = 6;
        static constexpr int kGridVertices = kMinorVertices + kMajorVertices;
        static constexpr int kVertexCount = kGridVertices + kAxisVertices;
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
    Skybox sky_;
    float projection_[16] = {};
    float view_[16] = {};
    float frustum_[6][4] = {};

    // Static grid positions plus the per-frame colour stream. Built once, then
    // drawn with a single glDrawArrays instead of ~1000 immediate-mode calls.
    bool arenaGridReady_ = false;
    // 3 floats per vertex: x, y, z, in the patch's own frame.
    std::vector<float> arenaPositions_;
    // 4 floats per vertex: the colour, with the alpha slot filled in each
    // frame.
    std::vector<float> arenaColors_;
    // 3 floats per vertex: the colour the vertex has when it is at full
    // transparency. Baked once, because it is the same for every frame.
    std::vector<float> arenaVertexColors_;
    // 1 float per vertex: how transparent the vertex is before the distance
    // fade is applied. The minor lines, the major lines and the axes each have
    // their own.
    std::vector<float> arenaVertexAlpha_;
};

} // namespace s3d