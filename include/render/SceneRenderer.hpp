#pragma once
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include <GL/gl.h>

#include "assets/MeshRegistry.hpp"
#include "assets/TextureRegistry.hpp"
#include "core/Math.hpp"
#include "physics/PhysicsWorld.hpp"
#include "render/Camera.hpp"
#include "render/Color.hpp"
#include "render/Fog.hpp"
#include "render/ParticleSystem.hpp"
#include "render/PrimitiveCache.hpp"
#include "render/ShaderProgram.hpp"
#include "render/ShadowMap.hpp"
#include "render/Skybox.hpp"
#include "render/WaterSystem.hpp"

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
    //
    // There is no arena: the world has no box around it, and the primitives are
    // only the shapes the scenes are made of.
    void initialize();

    // The shadows, as the user sets them. A change of the size or of the
    // number of the stretches of the scene makes the map again.
    void setShadowSettings(const ShadowSettings& settings);
    const ShadowSettings& shadowSettings() const { return shadowMap_.settings(); }
    // True when the shadows are drawn at all.
    bool shadowsEnabled() const { return shadowMap_.settings().quality != ShadowQuality::Off; }
    // The map, for the debug view.
    const ShadowMap& shadowMap() const { return shadowMap_; }

    // The fog: the air between the eye and the thing.
    //
    // The settings live here and not with the shadows because the renderer is
    // what draws the fog, and the editor changes them in place - where the fog
    // can be seen, which is the only place a fog is worth setting.
    FogSettings& fogSettings() { return fog_; }
    const FogSettings& fogSettings() const { return fog_; }

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
    //
    // `time` is the time of the world, in seconds: the waves of the water are
    // a function of it and of nothing else.
    void renderWorld(const PhysicsWorld& world, const MeshRegistry& meshes,
                     const TextureRegistry& textures, const Camera& camera,
                     const Viewport& viewport, bool showGrid, bool showVectors,
                     float alpha, float time, int hiddenBody = -1);

    void renderSelection(const RigidBody& body);

    // The map on the screen: what the sun sees. Drawn over the scene, in the
    // corner of the viewport.
    void renderShadowDebug(const Camera& camera, const Viewport& viewport);
    // Whether it is drawn at all.
    void setShowShadowDebug(bool show) { showShadowDebug_ = show; }
    bool showShadowDebug() const { return showShadowDebug_; }

    // A picture of the shadow instead of the picture: 1 the shadow itself, 2
    // whether the fragment is inside the box of the map, 3 where it is in
    // the map, 4 which stretch of the scene it is in, 5 how far it is from
    // the light.
    void setShadowDebugMode(int mode) { shadowDebugMode_ = mode; }

    // Which handles the gizmo draws. One enum instead of a bool because the tool
    // now has three shapes, and a bool that encodes two of them is how the rotate
    // tool ends up drawn as an arrow.
    enum class GizmoShape { Arrows, Rings, Cubes };

    // The axes the gizmo draws, and how many of them.
    //
    // The 3D view draws all three. A flat picture draws the two that are in
    // it, and its rotate ring is the one about the axis the picture looks
    // down. The editor works out which and passes them, so the handles that
    // are drawn and the handles that can be grabbed are one list.
    void renderGizmo(const Vec3& position, const Vec3* axes, float length, float ringRadius,
                     const Vec3* ringU, const Vec3* ringV, int activeAxis, GizmoShape shape,
                     const int* drawAxes, int drawCount);

    // The world in 2D: the grid is in the plane the picture is of and the
    // picture is measured and not seen. It is the renderer's own setting,
    // because the grid and the shadow catcher are the renderer's.
    void setFlatView(const Camera& camera);
    bool mode2D() const { return mode2D_; }

    const PrimitiveCache& primitives() const { return primitives_; }

    // The particles of the world: the emitters, the pool and the drawing.
    //
    // They live here because this is the class that owns the GL objects, and
    // the particles are a program and two buffers. The game and the editor
    // ask for them through here, and the emitters are part of the scene.
    ParticleSystem& particles() { return particles_; }
    const ParticleSystem& particles() const { return particles_; }

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

    // The plane the grid is in.
    //
    // In the 3D view it is the ground: the plane XZ, with the eye above it.
    // In the flat view it is the plane the picture is of, because the eye
    // looks down an axis there and the grid has to be in the plane it looks
    // at - a grid built for one plane and drawn in another is a grid that
    // lies about where the world is.
    //
    // The axes of the plane are the picture's own: its horizontal and its
    // vertical. The world axes are the world's, and they are drawn along the
    // world axes whatever the picture is of.
    struct GridPlane {
        Vec3 u{1.0f, 0.0f, 0.0f};  // the horizontal of the picture
        Vec3 v{0.0f, 0.0f, 1.0f};  // the vertical of the picture
        int uAxis = 0;             // the world axis the horizontal is
        int vAxis = 2;             // the world axis the vertical is
        int normal = 1;            // the world axis that points out of the plane
        bool operator==(const GridPlane& other) const {
            return uAxis == other.uAxis && vAxis == other.vAxis && normal == other.normal &&
                   u.x == other.u.x && u.y == other.u.y && u.z == other.u.z && v.x == other.v.x &&
                   v.y == other.v.y && v.z == other.v.z;
        }
    };
    // The sheet the shadow falls on. The ground of the editor is a grid of
    // lines, and a shadow on a line is a shadow nobody can see - so there is
    // a plain sheet under the grid that takes the shadow and is not drawn.
    void drawShadowCatcher();
    void drawBodies(const PhysicsWorld& world, const MeshRegistry& meshes,
                     const TextureRegistry& textures, const Camera& camera, float alpha,
                     int hiddenBody);
    // The water of the world, over the picture of it: the waves, the colour
    // of the water, the foam and the mirror of the sky.
    void drawWater(const PhysicsWorld& world, const Camera& camera, float time, int hiddenBody);
    // The water in a flat world: a sheet seen from above, drawn with the
    // scene's own program because the water's own is a program of a world
    // with a horizon in it.
    void drawFlatWater(const PhysicsWorld& world, float alpha, int hiddenBody);
    void drawDebugVectors(const PhysicsWorld& world);

    // The shadow pass: the map of what the sun hit first.
    //
    // It is a pass of its own and not a part of the scene pass, because it
    // happens before it: the shadow of a thing is in the picture of the
    // ground it falls on, and the ground cannot be drawn before the map is
    // full.
    void renderShadowMap(const PhysicsWorld& world, const MeshRegistry& meshes, float alpha,
                         int hiddenBody);

    // The uniforms the scene pass shares: the sun, the sky, the shadow map.
    void beginScenePass(const Camera& camera);
    // Draws one mesh with the scene's program.
    //
    // `meshIndex` is the number the mesh has in the registry, and it is what
    // the buffers on the card belong to: -1 for a mesh that is not in the
    // registry and is drawn out of the memory of the program as it always
    // was.
    bool drawMeshWithShader(const MeshAsset& mesh, const TextureRegistry& textures,
                            const Mat4& model, int meshIndex = -1);

    void extractFrustum(const float* projection, const float* view);
    bool sphereVisible(const Vec3& center, float radius) const;
    void drawCameraEntity(const RigidBody& body, const Mat4& model);

    // THE MESHES ON THE CARD
    //
    // Every mesh used to be drawn out of the memory of the program: the
    // driver was given a pointer to the vertices and copied them into the
    // card again, for every frame, for every body. A ground of nine thousand
    // vertices is three hundred kilobytes copied sixty times a second for a
    // picture that did not change, and a cloud of two hundred puffs is worse.
    //
    // The buffers are made once - the first time the mesh is drawn - and the
    // card keeps them. The mesh in the registry never changes under an index
    // (the registry never replaces an entry), so the buffers belong to the
    // index and not to the picture.
    struct MeshBuffers {
        // One pair of buffers per part: the vertices (position, normal and
        // texture coordinate in one array) and the colours, and one buffer
        // of indices.
        std::vector<unsigned int> vertices;
        std::vector<unsigned int> colors;
        std::vector<unsigned int> indices;
        // The name of the mesh the buffers were made of. The mesh in the
        // registry never changes under an index, but a registry that is
        // replaced starts its numbers again - and then the name is what says
        // that the buffers belong to another mesh.
        std::string name;
        // How much of the mesh is in them, so that a mesh that has changed is
        // made again instead of drawn out of the old picture of it.
        size_t vertexCount = 0;
        size_t indexCount = 0;
        bool ready = false;
    };
    std::unordered_map<int, MeshBuffers> meshBuffers_;
    // The buffers of a mesh, made the first time it is drawn. Null when the
    // driver has no buffers at all, and then the mesh is drawn the way it
    // always was.
    MeshBuffers* buffersFor(int mesh, const MeshAsset& asset);

    // The shadow map is drawn again only when the world or the sun has moved.
    //
    // The map is a picture of the world as the sun sees it, and the sun does
    // not move and the world does not move while the editor sits still: the
    // map would be the same picture a thousand times a second. So the state
    // the map depends on is remembered, and the pass is skipped when it has
    // not changed.
    struct ShadowState {
        // Every body that casts: where it is, how big it is and how it is
        // turned. The turn is in there because a thing that spins in place
        // casts a different shadow while standing in the same place.
        std::vector<std::array<float, 11>> casters;
        // The direction the light comes from. The sun is a setting, and a
        // setting that moves turns the map into a different picture.
        Vec3 sun{0.0f, 1.0f, 0.0f};
        // The settings, for the map's own size.
        int resolution = 0, cascades = 0;
        // Which map the picture is in. A map that was made again is a new
        // picture, and a new picture holds nothing.
        unsigned int generation = 0;
        bool ready = false;
    };
    // The state of every body that casts, in the form the map remembers it in.
    void collectCasters(const PhysicsWorld& world,
                        std::vector<std::array<float, 11>>& out) const;
    // True when the map has to be drawn again.
    bool shadowMapIsStale(const PhysicsWorld& world);
    void rememberShadowState(const PhysicsWorld& world);

    PrimitiveCache primitives_;
    Skybox sky_;
    // The particles of the world: the emitters, the pool and the drawing.
    //
    // They are here because this is the class that owns the GL objects, and
    // the particles are a program and two buffers. They are drawn with the
    // scene, after the bodies: a particle is behind a box when it is behind
    // the box.
    ParticleSystem particles_;
    // The water of the world: the program, the grid and the picture of the
    // scene that the water bends.
    //
    // It is here for the same reason the particles are: it is a program and
    // a buffer, and this is the class that owns the GL objects.
    WaterSystem water_;
    // The fog, as the user sets it. It belongs to the renderer and not to the
    // application: the renderer is what draws it, and there is one fog in a
    // frame however many programs are in use.
    FogSettings fog_;
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
    // The world in 2D: the grid is in the plane the picture is of and the sun
    // casts no shadow - a flat world has no sun in it.
    bool mode2D_ = false;
    // The plane the grid is in, and the one it was built for. The grid is made
    // again when the plane changes: the plane is decided when the grid is
    // built, and a grid that is built for one plane and drawn in another is a
    // grid that lies about where the world is.
    GridPlane gridPlane_{};
    // Which picture the scene is drawn with when the shadows are being looked
    // at. Zero is the picture itself.
    int shadowDebugMode_ = 0;

    // The state the shadow map depends on, so that the pass is skipped when
    // nothing has moved.
    ShadowState shadowState_;
    // The same list, built while the map is asked whether it is stale. A
    // member and not a local: the question is asked every frame, and a vector
    // that is built and thrown away every frame is a heap allocation every
    // frame.
    std::vector<std::array<float, 11>> casterScratch_;

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

    // The sheet the shadow falls on: four corners and the normal they face,
    // in the layout the scene's program reads.
    std::vector<float> shadowCatcher_;
    bool shadowCatcherReady_ = false;
};

} // namespace s3d