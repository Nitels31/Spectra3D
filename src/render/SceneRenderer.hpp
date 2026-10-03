#pragma once
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

    void renderWorld(const PhysicsWorld& world, const MeshRegistry& meshes,
                     const Camera& camera, const Viewport& viewport, bool showGrid,
                     bool showVectors);

    void renderSelection(const RigidBody& body);
    void renderGizmo(const Vec3& position, const Vec3* axes, float length,
                     int activeAxis, bool scaleMode);

    const PrimitiveCache& primitives() const { return primitives_; }

private:
    void setProjection(const Camera& camera, const Viewport& viewport);
    void setViewMatrix(const Camera& camera);
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
};

} // namespace s3d