#pragma once
#include <array>
#include <vector>

#include <GL/gl.h>

#include "assets/MeshRegistry.hpp"
#include "assets/TextureRegistry.hpp"
#include "core/Math.hpp"
#include "physics/PhysicsWorld.hpp"
#include "render/Camera.hpp"
#include "render/Color.hpp"
#include "render/PrimitiveCache.hpp"
#include "render/ShaderProgram.hpp"
#include "render/ShadowMap.hpp"
#include "render/Skybox.hpp"

namespace s3d {

// The planes the camera's frustum is cut at. Public because the frustum is
// checkable: a test has to be able to ask what the renderer projects with.
inline constexpr double kNearPlane = 0.1;
inline constexpr double kFarPlane = 900.0;

// The frustum the camera's field of view makes at the near plane, over a
// viewport of this size. The renderer projects with it and the editor picks
// with the same numbers, and the two have to be the same numbers: a frustum
// that is wider than the camera's own would make the pointer and the picture
// disagree about where a thing is, and the horizon would sit at the wrong
// height.
void cameraFrustum(const Camera& camera, int width, int height, double& halfWidth,
                    double& halfHeight);

// Viewport rect handed down from the editor layout. `glY` is the GL-convention
// bottom edge, which is what glViewport expects.
struct Viewport {
    int glX = 0;
    int glY = 0;
    int width = 0;
    int height = 0;
};

// 3D scene renderer: the arena, the bodies, the shadow of the sun, frustum
// culling and the selection/gizmo overlays. UI drawing lives in UiRenderer
// instead.
class SceneRenderer {
public:
    // Enables fixed-function lighting and builds the primitive cache.
    void initialize(const Vec3& arenaMin, const Vec3& arenaMax);

    // The shadows, as the user sets them. A change of the size or of the
    // number of the stretches of the scene makes the map again.
    void setShadowSettings(const ShadowSettings& settings);
    const ShadowSettings& shadowSettings() const { return shadowMap_.settings(); }
    // True when the shadows are drawn at all.
    bool shadowsEnabled() const { return shadowMap_.settings().quality != ShadowQuality::Off; }
    // The map, for the debug view.
    const ShadowMap& shadowMap() const { return shadowMap_; }

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

    // The map on the screen: what the sun sees. Drawn over the scene, in the
    // corner of the viewport.
    void renderShadowDebug(const Camera& camera, const Viewport& viewport);
    // Whether it is drawn at all.
    void setShowShadowDebug(bool show) { showShadowDebug_ = show; }
    bool showShadowDebug() const { return showShadowDebug_; }

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

    // The shadow pass: the map of what the sun hit first.
    //
    // It is a pass of its own and not a part of the scene pass, because it
    // happens before it: the shadow of a thing is in the picture of the
    // ground it falls on, and the ground cannot be drawn before the map is
    // full.
    void renderShadowMap(const PhysicsWorld& world, const MeshRegistry& meshes,
                         const TextureRegistry& textures, const Camera& camera, float alpha,
                         int hiddenBody);

    // The uniforms the scene pass shares: the sun, the sky, the shadow map.
    void beginScenePass();
    // Draws one mesh with the scene's program.
    bool drawMeshWithShader(const MeshAsset& mesh, const TextureRegistry& textures,
                            const Mat4& model, const Color& tint);

    void extractFrustum(const float* projection, const float* view);
    bool sphereVisible(const Vec3& center, float radius) const;
    void drawCameraEntity(const RigidBody& body, const Mat4& model);

    // The shadow map is drawn again only when the world or the eye has moved.
    //
    // The map is a picture of the world as the sun sees it, and the sun does
    // not move and the world does not move while the editor sits still: the
    // map would be the same picture a thousand times a second. So the state
    // the map depends on is remembered, and the pass is skipped when it has
    // not changed.
    struct ShadowState {
        // Every body that casts: where it is and how big it is.
        std::vector<std::array<float, 7>> casters;
        // The eye, and the way it looks.
        Vec3 eye;
        float yaw = 0.0f, pitch = 0.0f, fov = 70.0f;
        // The settings, for the map's own size.
        int resolution = 0, cascades = 0;
        bool ready = false;
    };
    // True when the map has to be drawn again.
    bool shadowMapIsStale(const PhysicsWorld& world, const Camera& camera) const;
    void rememberShadowState(const PhysicsWorld& world, const Camera& camera);

    PrimitiveCache primitives_;
    Skybox sky_;
    float projection_[16] = {};
    float view_[16] = {};
    float frustum_[6][4] = {};

    // The programs. The scene's own, the one that fills the shadow map, and
    // the one that shows it.
    ShaderProgram sceneProgram_;
    ShaderProgram shadowProgram_;
    ShaderProgram shadowDebugProgram_;
    ShadowMap shadowMap_;

    // The matrices the shaders are given, in the same numbers the fixed
    // pipeline is given: the picture and the pointer have to agree about
    // where a thing is, and so do the two ways of drawing it.
    Mat4 projectionMatrix_;
    Mat4 viewMatrix_;

    // The viewport of the scene, kept for the shadow pass: the map is a
    // picture of its own, and the scene's viewport would cut it to a corner.
    int viewportX_ = 0, viewportY_ = 0, viewportWidth_ = 0, viewportHeight_ = 0;

    // A white texel, for the things that have no picture: the primitives, and
    // a mesh that was not given one.
    GLuint whiteTexture_ = 0;

    // Whether the map of the shadows is drawn over the scene.
    bool showShadowDebug_ = false;

    // The state the shadow map depends on, so that the pass is skipped when
    // nothing has moved.
    ShadowState shadowState_;

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