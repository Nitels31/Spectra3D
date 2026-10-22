#include "script/SpsBindings.hpp"

#if SPECTRA_SPSCRIPT

#include "assets/MeshRegistry.hpp"
#include "core/Log.hpp"
#include "core/Str.hpp"
#include "gui/GuiCanvas.hpp"
#include "physics/PhysicsWorld.hpp"
#include "render/ShadowMap.hpp"
#include "scene/BodyFactory.hpp"

#include <cmath>

namespace s3d {
namespace sps {
namespace {

// The script that a binding belongs to. The runtime hands the user pointer
// back with every call, and the script is what carries the world and the
// classes of that call.
Script& scriptOf(void* user) { return *static_cast<Script*>(user); }

// The body an entity points at, or null.
RigidBody* bodyOf(SpsValue object, void* user) {
    Script& script = scriptOf(user);
    return bodyOf(*script.state, entityIndexOf(object));
}

// --- the turns ---------------------------------------------------------------

// The rotation of a body, in degrees: the yaw around the vertical axis, the
// pitch around the side one and the roll around the one that is in front.
//
// The three are in the order the engine's camera uses them, so a script that
// turns a body and a script that turns the camera mean the same thing.
Quat quatFromEuler(const Vec3& euler) {
    const float k = kPi / 180.0f;
    const Quat yaw = Quat::axisAngle(Vec3{0.0f, 1.0f, 0.0f}, euler.x * k);
    const Quat pitch = Quat::axisAngle(Vec3{1.0f, 0.0f, 0.0f}, euler.y * k);
    const Quat roll = Quat::axisAngle(Vec3{0.0f, 0.0f, 1.0f}, euler.z * k);
    return (yaw * pitch * roll).normalized();
}

Vec3 eulerFromQuat(const Quat& q) {
    const Mat3 r = Mat3::fromQuat(q.normalized());

    // The middle angle comes out of the matrix alone; the other two need it,
    // because the matrix is not a set of three independent turns.
    const float pitch = std::asin(clampf(-r.m[5], -1.0f, 1.0f));
    const float cosPitch = std::cos(pitch);

    Vec3 euler;
    euler.y = pitch * 180.0f / kPi;
    if (std::fabs(cosPitch) > 1e-4f) {
        euler.x = std::atan2(r.m[2], r.m[8]) * 180.0f / kPi;
        euler.z = std::atan2(r.m[3], r.m[4]) * 180.0f / kPi;
    } else {
        // Straight up or straight down: the yaw and the roll are the same
        // turn, and only one of them can be said.
        euler.x = std::atan2(-r.m[1], r.m[0]) * 180.0f / kPi;
        euler.z = 0.0f;
    }
    return euler;
}

// The three numbers of a call that takes either a vector or three numbers.
Vec3 stepOf(int argc, const SpsValue* args, int first) {
    if (argc > first && (args[first].type == SPS_OBJECT || args[first].type == SPS_USERDATA))
        return vectorOf(args[first]);

    Vec3 step;
    if (argc > first) step.x = (float)sps_value_to_float(args[first]);
    if (argc > first + 1) step.y = (float)sps_value_to_float(args[first + 1]);
    if (argc > first + 2) step.z = (float)sps_value_to_float(args[first + 2]);
    return step;
}

// The pointer a property is written through.
Vec3* vectorPointer(SpsValue object) {
    if (sps_value_type(object) != SPS_OBJECT && sps_value_type(object) != SPS_USERDATA)
        return nullptr;
    return static_cast<Vec3*>(sps_value_to_userdata(object));
}

Color* colorPointer(SpsValue object) {
    if (sps_value_type(object) != SPS_OBJECT && sps_value_type(object) != SPS_USERDATA)
        return nullptr;
    return static_cast<Color*>(sps_value_to_userdata(object));
}

// --- the Vector3 -------------------------------------------------------------

SpsValue vectorGetX(SpsContext*, SpsValue object, void*) { return sps_float(vectorOf(object).x); }
SpsValue vectorGetY(SpsContext*, SpsValue object, void*) { return sps_float(vectorOf(object).y); }
SpsValue vectorGetZ(SpsContext*, SpsValue object, void*) { return sps_float(vectorOf(object).z); }

void vectorSetX(SpsContext*, SpsValue object, SpsValue value, void*) {
    Vec3* vector = vectorPointer(object);
    if (vector) vector->x = (float)sps_value_to_float(value);
}

void vectorSetY(SpsContext*, SpsValue object, SpsValue value, void*) {
    Vec3* vector = vectorPointer(object);
    if (vector) vector->y = (float)sps_value_to_float(value);
}

void vectorSetZ(SpsContext*, SpsValue object, SpsValue value, void*) {
    Vec3* vector = vectorPointer(object);
    if (vector) vector->z = (float)sps_value_to_float(value);
}

SpsValue vectorLength(SpsContext*, int argc, const SpsValue* args, void*) {
    if (argc < 1) return sps_float(0.0);
    return sps_float(vectorOf(args[0]).length());
}

SpsValue vectorNormalized(SpsContext*, int argc, const SpsValue* args, void* user) {
    if (argc < 1) return sps_nil();
    Script& script = scriptOf(user);
    return makeVector(script, vectorOf(args[0]).normalized());
}

SpsValue vectorDot(SpsContext*, int argc, const SpsValue* args, void*) {
    if (argc < 2) return sps_float(0.0);
    return sps_float(dot(vectorOf(args[0]), vectorOf(args[1])));
}

SpsValue vectorCross(SpsContext*, int argc, const SpsValue* args, void* user) {
    if (argc < 2) return sps_nil();
    Script& script = scriptOf(user);
    return makeVector(script, cross(vectorOf(args[0]), vectorOf(args[1])));
}

// --- the Color ---------------------------------------------------------------

SpsValue colorGetR(SpsContext*, SpsValue object, void*) { return sps_float(colorOf(object).r); }
SpsValue colorGetG(SpsContext*, SpsValue object, void*) { return sps_float(colorOf(object).g); }
SpsValue colorGetB(SpsContext*, SpsValue object, void*) { return sps_float(colorOf(object).b); }
SpsValue colorGetA(SpsContext*, SpsValue object, void*) { return sps_float(colorOf(object).a); }

void colorSetR(SpsContext*, SpsValue object, SpsValue value, void*) {
    Color* color = colorPointer(object);
    if (color) color->r = (float)sps_value_to_float(value);
}

void colorSetG(SpsContext*, SpsValue object, SpsValue value, void*) {
    Color* color = colorPointer(object);
    if (color) color->g = (float)sps_value_to_float(value);
}

void colorSetB(SpsContext*, SpsValue object, SpsValue value, void*) {
    Color* color = colorPointer(object);
    if (color) color->b = (float)sps_value_to_float(value);
}

void colorSetA(SpsContext*, SpsValue object, SpsValue value, void*) {
    Color* color = colorPointer(object);
    if (color) color->a = (float)sps_value_to_float(value);
}

// --- the Entity --------------------------------------------------------------

SpsValue entityConstructor(SpsContext* context, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    SpsState& state = *script.state;
    if (!state.world) return sps_nil();

    const std::string name = argc > 0 ? textOf(context, args[0]) : std::string("entity");

    const int body = BodyFactory::spawn(*state.world, Shape::Box, Vec3{}, Vec3{},
                                         Vec3{0.5f, 0.5f, 0.5f}, 1.0f, Vec3{}, Quat{});
    if (body < 0) return sps_nil();

    ensureBodyTables(state);
    state.bodyNames[(size_t)body] = name;
    state.created.push_back(body);
    return makeEntity(script, body);
}

SpsValue entityGetName(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    SpsState& state = *script.state;
    ensureBodyTables(state);
    const int index = entityIndexOf(object);
    if (index < 0 || index >= (int)state.bodyNames.size()) return sps_string("");
    return sps_string(state.bodyNames[(size_t)index].c_str());
}

void entitySetName(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    Script& script = scriptOf(user);
    SpsState& state = *script.state;
    ensureBodyTables(state);
    const int index = entityIndexOf(object);
    if (index < 0 || index >= (int)state.bodyNames.size()) return;
    state.bodyNames[(size_t)index] = textOf(context, value);
}

SpsValue entityGetTag(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    SpsState& state = *script.state;
    ensureBodyTables(state);
    const int index = entityIndexOf(object);
    if (index < 0 || index >= (int)state.bodyTags.size()) return sps_string("");
    return sps_string(state.bodyTags[(size_t)index].c_str());
}

void entitySetTag(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    Script& script = scriptOf(user);
    SpsState& state = *script.state;
    ensureBodyTables(state);
    const int index = entityIndexOf(object);
    if (index < 0 || index >= (int)state.bodyTags.size()) return;
    state.bodyTags[(size_t)index] = textOf(context, value);
}

SpsValue entityGetPosition(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    RigidBody* body = bodyOf(object, user);
    return makeVector(script, body ? body->position : Vec3());
}

void entitySetPosition(SpsContext*, SpsValue object, SpsValue value, void* user) {
    RigidBody* body = bodyOf(object, user);
    if (!body) return;
    body->position = vectorOf(value);
    // The body is drawn where it is and not where it was a frame ago.
    body->syncPrevTransform();
    body->wake();
}

SpsValue entityGetRotation(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    RigidBody* body = bodyOf(object, user);
    return makeVector(script, body ? eulerFromQuat(body->orientation) : Vec3());
}

void entitySetRotation(SpsContext*, SpsValue object, SpsValue value, void* user) {
    RigidBody* body = bodyOf(object, user);
    if (!body) return;
    body->orientation = quatFromEuler(vectorOf(value));
    body->syncPrevTransform();
    body->wake();
}

SpsValue entityGetSize(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    RigidBody* body = bodyOf(object, user);
    return makeVector(script, body ? body->halfExtents * 2.0f : Vec3());
}

void entitySetSize(SpsContext*, SpsValue object, SpsValue value, void* user) {
    RigidBody* body = bodyOf(object, user);
    if (!body) return;
    body->halfExtents = vectorOf(value) * 0.5f;
    // configure() owns the numbers that follow from the size: the mass, the
    // inertia and the fields of the shape.
    body->configure(body->mass);
    body->syncPrevTransform();
    body->wake();
}

SpsValue entityGetVelocity(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    RigidBody* body = bodyOf(object, user);
    return makeVector(script, body ? body->velocity : Vec3());
}

void entitySetVelocity(SpsContext*, SpsValue object, SpsValue value, void* user) {
    RigidBody* body = bodyOf(object, user);
    if (!body) return;
    body->velocity = vectorOf(value);
    body->wake();
}

SpsValue entityGetShape(SpsContext*, SpsValue object, void* user) {
    RigidBody* body = bodyOf(object, user);
    return sps_string(body ? shapeName(body->shape) : "");
}

void entitySetShape(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    RigidBody* body = bodyOf(object, user);
    if (!body) return;

    // The names of the shapes are the ones the engine writes into a scene, so
    // a script and a scene file say the same thing about a body.
    static const struct {
        const char* name;
        Shape shape;
    } kShapes[] = {
        {"box", Shape::Box},         {"sphere", Shape::Sphere},   {"cylinder", Shape::Cylinder},
        {"capsule", Shape::Capsule}, {"cone", Shape::Cone},       {"pyramid", Shape::Pyramid},
        {"prism", Shape::Prism},     {"mesh", Shape::Mesh},       {"camera", Shape::Camera},
        {"quad", Shape::Quad},       {"water", Shape::Water},
    };

    const std::string wanted = textOf(context, value);
    for (const auto& entry : kShapes) {
        if (!str::equalsIgnoreAscii(entry.name, wanted)) continue;
        body->shape = entry.shape;
        body->configure(body->mass);
        body->syncPrevTransform();
        return;
    }
    Log::warn("sps: '%s' is not a shape", wanted.c_str());
}

SpsValue entityGetMesh(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    RigidBody* body = bodyOf(object, user);
    if (!body || !script.state->meshes || body->mesh < 0) return sps_string("");
    return sps_string(script.state->meshes->nameOf(body->mesh).c_str());
}

SpsValue entityGetActive(SpsContext*, SpsValue object, void* user) {
    RigidBody* body = bodyOf(object, user);
    return sps_bool(body ? body->visible : false);
}

void entitySetActive(SpsContext*, SpsValue object, SpsValue value, void* user) {
    RigidBody* body = bodyOf(object, user);
    if (!body) return;
    body->visible = sps_value_to_bool(value);
    body->wake();
}

SpsValue entityGetMass(SpsContext*, SpsValue object, void* user) {
    RigidBody* body = bodyOf(object, user);
    return sps_float(body ? body->mass : 0.0f);
}

void entitySetMass(SpsContext*, SpsValue object, SpsValue value, void* user) {
    RigidBody* body = bodyOf(object, user);
    if (!body) return;
    body->configure((float)sps_value_to_float(value));
    body->wake();
}

SpsValue entityMove(SpsContext*, int argc, const SpsValue* args, void* user) {
    RigidBody* body = bodyOf(args[0], user);
    if (!body) return sps_nil();

    // A move takes either three numbers or a vector, the way a script writes
    // it: player.move(0, 1, 0) and player.move(vec3(0, 1, 0)) are the same
    // move.
    body->position += stepOf(argc, args, 1);
    body->syncPrevTransform();
    body->wake();
    return sps_nil();
}

SpsValue entityRotate(SpsContext*, int argc, const SpsValue* args, void* user) {
    RigidBody* body = bodyOf(args[0], user);
    if (!body) return sps_nil();

    const Vec3 step = stepOf(argc, args, 1);
    body->orientation = quatFromEuler(eulerFromQuat(body->orientation) + step);
    body->syncPrevTransform();
    return sps_nil();
}

// Turns a body towards a point. The body is turned around its vertical axis
// only: a body that is looked at from the side does not tip over.
SpsValue entityLookAt(SpsContext*, int argc, const SpsValue* args, void* user) {
    RigidBody* body = bodyOf(args[0], user);
    if (!body || argc < 2) return sps_nil();

    const Vec3 direction = (vectorOf(args[1]) - body->position).normalized();
    if (direction.lengthSq() < 1e-8f) return sps_nil();

    const float yaw = std::atan2(direction.x, direction.z) * 180.0f / kPi;
    body->orientation = quatFromEuler(Vec3{yaw, 0.0f, 0.0f});
    body->syncPrevTransform();
    return sps_nil();
}

SpsValue entityDestroy(SpsContext*, int argc, const SpsValue* args, void* user) {
    (void)argc;
    Script& script = scriptOf(user);
    const int index = entityIndexOf(args[0]);
    if (index < 0) return sps_nil();
    removeBody(*script.state, index);
    return sps_nil();
}

SpsValue entityWake(SpsContext*, int argc, const SpsValue* args, void* user) {
    (void)argc;
    RigidBody* body = bodyOf(args[0], user);
    if (body) body->wake();
    return sps_nil();
}

// --- the Scene ---------------------------------------------------------------

SpsValue sceneConstructor(SpsContext*, int argc, const SpsValue* args, void* user) {
    (void)argc;
    (void)args;
    Script& script = scriptOf(user);
    return sps_host_object(script.sceneClass, script.state);
}

SpsValue sceneGetName(SpsContext*, SpsValue object, void*) {
    SpsState* state = static_cast<SpsState*>(sps_value_to_userdata(object));
    return sps_string(state ? "scene" : "");
}

SpsValue sceneGetGravity(SpsContext*, SpsValue object, void*) {
    SpsState* state = static_cast<SpsState*>(sps_value_to_userdata(object));
    if (!state || !state->world) return sps_float(0.0);
    return sps_float(state->world->gravityMagnitude);
}

void sceneSetGravity(SpsContext*, SpsValue object, SpsValue value, void*) {
    SpsState* state = static_cast<SpsState*>(sps_value_to_userdata(object));
    if (!state || !state->world) return;
    state->world->gravityMagnitude = (float)sps_value_to_float(value);
}

SpsValue sceneGetGravityAxis(SpsContext*, SpsValue object, void*) {
    SpsState* state = static_cast<SpsState*>(sps_value_to_userdata(object));
    if (!state || !state->world) return sps_string("");
    return sps_string(gravityName(state->world->gravityAxis));
}

SpsValue sceneFind(SpsContext* context, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    SpsState& state = *script.state;
    if (argc < 1) return sps_nil();

    ensureBodyTables(state);
    const std::string wanted = textOf(context, args[0]);
    for (size_t i = 0; i < state.bodyNames.size(); ++i) {
        if (state.bodyNames[i] != wanted) continue;
        if (!state.world || (int)i >= (int)state.world->bodies.size()) break;
        return makeEntity(script, (int)i);
    }
    return sps_nil();
}

SpsValue sceneCount(SpsContext*, int argc, const SpsValue* args, void* user) {
    (void)argc;
    (void)args;
    Script& script = scriptOf(user);
    if (!script.state->world) return sps_int(0);
    return sps_int((int64_t)script.state->world->bodies.size());
}

SpsValue sceneClear(SpsContext*, int argc, const SpsValue* args, void* user) {
    (void)argc;
    (void)args;
    Script& script = scriptOf(user);
    clearScene(*script.state);
    return sps_nil();
}

// --- the Rigidbody -----------------------------------------------------------

SpsValue rigidbodyConstructor(SpsContext*, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    const int body = argc > 0 ? entityIndexOf(args[0]) : -1;
    if (body < 0) return sps_nil();
    return makeEntityOf(script, script.rigidbodyClass, body);
}

SpsValue rigidbodyGetMass(SpsContext*, SpsValue object, void* user) {
    RigidBody* body = bodyOf(object, user);
    return sps_float(body ? body->mass : 0.0f);
}

void rigidbodySetMass(SpsContext*, SpsValue object, SpsValue value, void* user) {
    RigidBody* body = bodyOf(object, user);
    if (!body) return;
    body->configure((float)sps_value_to_float(value));
    body->wake();
}

SpsValue rigidbodyGetVelocity(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    RigidBody* body = bodyOf(object, user);
    return makeVector(script, body ? body->velocity : Vec3());
}

void rigidbodySetVelocity(SpsContext*, SpsValue object, SpsValue value, void* user) {
    RigidBody* body = bodyOf(object, user);
    if (!body) return;
    body->velocity = vectorOf(value);
    body->wake();
}

SpsValue rigidbodyGetForce(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    RigidBody* body = bodyOf(object, user);
    return makeVector(script, body ? body->force : Vec3());
}

void rigidbodySetForce(SpsContext*, SpsValue object, SpsValue value, void* user) {
    RigidBody* body = bodyOf(object, user);
    if (!body) return;
    body->force = vectorOf(value);
}

SpsValue rigidbodyGetGravity(SpsContext*, SpsValue object, void*) {
    SpsState* state = static_cast<SpsState*>(sps_value_to_userdata(object));
    if (!state || !state->world) return sps_float(0.0);
    return sps_float(state->world->gravityMagnitude);
}

SpsValue rigidbodyGetSleeping(SpsContext*, SpsValue object, void* user) {
    RigidBody* body = bodyOf(object, user);
    return sps_bool(body ? body->asleep : false);
}

SpsValue rigidbodyApplyImpulse(SpsContext*, int argc, const SpsValue* args, void* user) {
    RigidBody* body = bodyOf(args[0], user);
    if (!body) return sps_nil();
    body->applyImpulse(stepOf(argc, args, 1), Vec3{});
    body->wake();
    return sps_nil();
}

SpsValue rigidbodyWake(SpsContext*, int argc, const SpsValue* args, void* user) {
    (void)argc;
    RigidBody* body = bodyOf(args[0], user);
    if (body) body->wake();
    return sps_nil();
}

// --- the Collider ------------------------------------------------------------

SpsValue colliderConstructor(SpsContext*, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    const int body = argc > 0 ? entityIndexOf(args[0]) : -1;
    if (body < 0) return sps_nil();
    return makeEntityOf(script, script.colliderClass, body);
}

SpsValue colliderGetShape(SpsContext*, SpsValue object, void* user) {
    RigidBody* body = bodyOf(object, user);
    return sps_string(body ? shapeName(body->shape) : "");
}

SpsValue colliderGetSize(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    RigidBody* body = bodyOf(object, user);
    return makeVector(script, body ? body->halfExtents * 2.0f : Vec3());
}

void colliderSetSize(SpsContext*, SpsValue object, SpsValue value, void* user) {
    RigidBody* body = bodyOf(object, user);
    if (!body) return;
    body->halfExtents = vectorOf(value) * 0.5f;
    body->configure(body->mass);
    body->wake();
}

SpsValue colliderGetRadius(SpsContext*, SpsValue object, void* user) {
    RigidBody* body = bodyOf(object, user);
    return sps_float(body ? body->halfExtents.x : 0.0f);
}

void colliderSetRadius(SpsContext*, SpsValue object, SpsValue value, void* user) {
    RigidBody* body = bodyOf(object, user);
    if (!body) return;
    const float radius = (float)sps_value_to_float(value);
    body->halfExtents = Vec3{radius, body->halfExtents.y, body->halfExtents.z};
    body->configure(body->mass);
    body->wake();
}

// Whether the body is a trigger: a volume that something comes into and goes
// out of, and that says so.
SpsValue colliderGetIsTrigger(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    const int index = entityIndexOf(object);
    for (const SpsState::Trigger& trigger : script.state->triggers)
        if (trigger.body == index) return sps_bool(trigger.enabled);
    return sps_bool(false);
}

void colliderSetIsTrigger(SpsContext*, SpsValue object, SpsValue value, void* user) {
    Script& script = scriptOf(user);
    SpsState& state = *script.state;
    const int index = entityIndexOf(object);
    if (index < 0) return;

    const bool wanted = sps_value_to_bool(value);
    for (SpsState::Trigger& trigger : state.triggers) {
        if (trigger.body != index) continue;
        trigger.enabled = wanted;
        return;
    }
    if (wanted) {
        SpsState::Trigger trigger;
        trigger.body = index;
        state.triggers.push_back(trigger);
    }
}

// --- the Camera --------------------------------------------------------------

SpsValue cameraConstructor(SpsContext*, int argc, const SpsValue* args, void* user) {
    (void)argc;
    (void)args;
    Script& script = scriptOf(user);
    SpsState& state = *script.state;
    if (!state.world) return sps_nil();

    const int body = BodyFactory::spawn(*state.world, Shape::Camera, Vec3{}, Vec3{}, Vec3{},
                                         1.0f, Vec3{}, Quat{});
    if (body < 0) return sps_nil();
    ensureBodyTables(state);
    state.created.push_back(body);
    return makeEntityOf(script, script.cameraClass, body);
}

SpsValue cameraGetFov(SpsContext*, SpsValue object, void* user) {
    RigidBody* body = bodyOf(object, user);
    return sps_float(body ? body->cameraFov : 0.0f);
}

void cameraSetFov(SpsContext*, SpsValue object, SpsValue value, void* user) {
    RigidBody* body = bodyOf(object, user);
    if (!body) return;
    // The same range the editor's own field uses, so a script cannot open a
    // lens the editor would not let a person open.
    body->cameraFov = clampf((float)sps_value_to_float(value), 10.0f, 170.0f);
}

SpsValue cameraGetActive(SpsContext*, SpsValue object, void* user) {
    RigidBody* body = bodyOf(object, user);
    return sps_bool(body ? body->activeCamera : false);
}

void cameraSetActive(SpsContext*, SpsValue object, SpsValue value, void* user) {
    Script& script = scriptOf(user);
    RigidBody* body = bodyOf(object, user);
    if (!body || !script.state->world) return;
    const int index = entityIndexOf(object);
    if (sps_value_to_bool(value))
        script.state->world->setActiveCamera(index);
    else if (script.state->world->findActiveCamera() == index)
        script.state->world->setActiveCamera(-1);
}

SpsValue cameraLookAt(SpsContext*, int argc, const SpsValue* args, void* user) {
    RigidBody* body = bodyOf(args[0], user);
    if (!body || argc < 2) return sps_nil();
    const Vec3 direction = (vectorOf(args[1]) - body->position).normalized();
    if (direction.lengthSq() < 1e-8f) return sps_nil();
    const float yaw = std::atan2(direction.x, direction.z) * 180.0f / kPi;
    body->orientation = quatFromEuler(Vec3{yaw, 0.0f, 0.0f});
    body->syncPrevTransform();
    return sps_nil();
}

// --- the Light ---------------------------------------------------------------
//
// The engine has one sun and not a lamp on every object, so the Light of the
// scripts is that one sun: it is where the light comes from and how bright
// it is.

SpsValue lightConstructor(SpsContext*, int argc, const SpsValue* args, void* user) {
    (void)argc;
    (void)args;
    Script& script = scriptOf(user);
    return sps_host_object(script.lightClass, script.state);
}

SpsValue lightGetAzimuth(SpsContext*, SpsValue object, void*) {
    SpsState* state = static_cast<SpsState*>(sps_value_to_userdata(object));
    if (!state || !state->shadows) return sps_float(0.0);
    return sps_float(state->shadows->sunAzimuth);
}

void lightSetAzimuth(SpsContext*, SpsValue object, SpsValue value, void*) {
    SpsState* state = static_cast<SpsState*>(sps_value_to_userdata(object));
    if (!state || !state->shadows) return;
    state->shadows->sunAzimuth = (float)sps_value_to_float(value);
}

SpsValue lightGetElevation(SpsContext*, SpsValue object, void*) {
    SpsState* state = static_cast<SpsState*>(sps_value_to_userdata(object));
    if (!state || !state->shadows) return sps_float(0.0);
    return sps_float(state->shadows->sunElevation);
}

void lightSetElevation(SpsContext*, SpsValue object, SpsValue value, void*) {
    SpsState* state = static_cast<SpsState*>(sps_value_to_userdata(object));
    if (!state || !state->shadows) return;
    state->shadows->sunElevation = (float)sps_value_to_float(value);
}

SpsValue lightGetColor(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    SpsState* state = static_cast<SpsState*>(sps_value_to_userdata(object));
    if (!state || !state->shadows) return makeColor(script, Color());
    const Vec3& colour = state->shadows->sunColour;
    return makeColor(script, Color(colour.x, colour.y, colour.z, 1.0f));
}

void lightSetColor(SpsContext*, SpsValue object, SpsValue value, void*) {
    SpsState* state = static_cast<SpsState*>(sps_value_to_userdata(object));
    if (!state || !state->shadows) return;
    const Color colour = colorOf(value);
    state->shadows->sunColour = Vec3{colour.r, colour.g, colour.b};
}

SpsValue lightGetAmbient(SpsContext*, SpsValue object, void*) {
    SpsState* state = static_cast<SpsState*>(sps_value_to_userdata(object));
    if (!state || !state->shadows) return sps_float(0.0);
    return sps_float(state->shadows->ambient);
}

void lightSetAmbient(SpsContext*, SpsValue object, SpsValue value, void*) {
    SpsState* state = static_cast<SpsState*>(sps_value_to_userdata(object));
    if (!state || !state->shadows) return;
    state->shadows->ambient = (float)sps_value_to_float(value);
}

// --- the Material ------------------------------------------------------------
//
// The material of a body is the one the physics uses: how much it slides and
// how much it bounces.

SpsValue materialConstructor(SpsContext*, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    const int body = argc > 0 ? entityIndexOf(args[0]) : -1;
    if (body < 0) return sps_nil();
    return makeEntityOf(script, script.materialClass, body);
}

SpsValue materialGetFriction(SpsContext*, SpsValue object, void* user) {
    RigidBody* body = bodyOf(object, user);
    return sps_float(body ? body->material.friction : 0.0f);
}

void materialSetFriction(SpsContext*, SpsValue object, SpsValue value, void* user) {
    RigidBody* body = bodyOf(object, user);
    if (body) body->material.friction = (float)sps_value_to_float(value);
}

SpsValue materialGetRestitution(SpsContext*, SpsValue object, void* user) {
    RigidBody* body = bodyOf(object, user);
    return sps_float(body ? body->material.restitution : 0.0f);
}

void materialSetRestitution(SpsContext*, SpsValue object, SpsValue value, void* user) {
    RigidBody* body = bodyOf(object, user);
    if (body) body->material.restitution = (float)sps_value_to_float(value);
}

SpsValue materialGetDensity(SpsContext*, SpsValue object, void* user) {
    RigidBody* body = bodyOf(object, user);
    return sps_float(body ? body->material.density : 0.0f);
}

void materialSetDensity(SpsContext*, SpsValue object, SpsValue value, void* user) {
    RigidBody* body = bodyOf(object, user);
    if (body) body->material.density = (float)sps_value_to_float(value);
}

SpsValue materialGetIsStatic(SpsContext*, SpsValue object, void* user) {
    RigidBody* body = bodyOf(object, user);
    return sps_bool(body ? body->material.isStatic : false);
}

void materialSetIsStatic(SpsContext*, SpsValue object, SpsValue value, void* user) {
    RigidBody* body = bodyOf(object, user);
    if (!body) return;
    body->material.isStatic = sps_value_to_bool(value);
    body->configure(body->mass);
    body->wake();
}

// --- the Trigger -------------------------------------------------------------

SpsValue triggerConstructor(SpsContext*, int argc, const SpsValue* args, void* user) {
    Script& script = scriptOf(user);
    SpsState& state = *script.state;
    if (!state.world) return sps_nil();

    int body = argc > 0 ? entityIndexOf(args[0]) : -1;
    if (body < 0) {
        body = BodyFactory::spawn(*state.world, Shape::Box, Vec3{}, Vec3{},
                                   Vec3{0.5f, 0.5f, 0.5f}, 1.0f, Vec3{}, Quat{});
        if (body < 0) return sps_nil();
        ensureBodyTables(state);
        state.created.push_back(body);
    }

    // A body has one trigger, and asking for it twice does not make two.
    for (const SpsState::Trigger& trigger : state.triggers)
        if (trigger.body == body) return makeEntityOf(script, script.triggerClass, body);

    SpsState::Trigger trigger;
    trigger.body = body;
    state.triggers.push_back(trigger);
    return makeEntityOf(script, script.triggerClass, body);
}

SpsState::Trigger* triggerOf(SpsState& state, int body) {
    for (SpsState::Trigger& trigger : state.triggers)
        if (trigger.body == body) return &trigger;
    return nullptr;
}

SpsValue triggerGetSize(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    RigidBody* body = bodyOf(object, user);
    return makeVector(script, body ? body->halfExtents * 2.0f : Vec3());
}

void triggerSetSize(SpsContext*, SpsValue object, SpsValue value, void* user) {
    RigidBody* body = bodyOf(object, user);
    if (!body) return;
    body->halfExtents = vectorOf(value) * 0.5f;
    body->configure(body->mass);
    body->wake();
}

SpsValue triggerGetOnEnter(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    SpsState::Trigger* trigger = triggerOf(*script.state, entityIndexOf(object));
    return sps_string(trigger ? trigger->onEnter.c_str() : "");
}

void triggerSetOnEnter(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    Script& script = scriptOf(user);
    SpsState::Trigger* trigger = triggerOf(*script.state, entityIndexOf(object));
    if (trigger) trigger->onEnter = textOf(context, value);
}

SpsValue triggerGetOnExit(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    SpsState::Trigger* trigger = triggerOf(*script.state, entityIndexOf(object));
    return sps_string(trigger ? trigger->onExit.c_str() : "");
}

void triggerSetOnExit(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    Script& script = scriptOf(user);
    SpsState::Trigger* trigger = triggerOf(*script.state, entityIndexOf(object));
    if (trigger) trigger->onExit = textOf(context, value);
}

SpsValue triggerGetEnabled(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    SpsState::Trigger* trigger = triggerOf(*script.state, entityIndexOf(object));
    return sps_bool(trigger ? trigger->enabled : false);
}

void triggerSetEnabled(SpsContext*, SpsValue object, SpsValue value, void* user) {
    Script& script = scriptOf(user);
    SpsState::Trigger* trigger = triggerOf(*script.state, entityIndexOf(object));
    if (trigger) trigger->enabled = sps_value_to_bool(value);
}

// --- the AudioSource ---------------------------------------------------------

SpsValue audioConstructor(SpsContext*, int argc, const SpsValue* args, void* user) {
    (void)argc;
    (void)args;
    Script& script = scriptOf(user);
    script.state->audio.push_back(SpsState::AudioValue());
    return sps_host_object(script.audioClass, &script.state->audio.back());
}

SpsState::AudioValue* audioOf(SpsValue object) {
    if (sps_value_type(object) != SPS_OBJECT && sps_value_type(object) != SPS_USERDATA)
        return nullptr;
    return static_cast<SpsState::AudioValue*>(sps_value_to_userdata(object));
}

SpsValue audioGetSound(SpsContext*, SpsValue object, void*) {
    SpsState::AudioValue* audio = audioOf(object);
    return sps_string(audio ? audio->sound.c_str() : "");
}

void audioSetSound(SpsContext* context, SpsValue object, SpsValue value, void*) {
    SpsState::AudioValue* audio = audioOf(object);
    if (audio) audio->sound = textOf(context, value);
}

SpsValue audioGetVolume(SpsContext*, SpsValue object, void*) {
    SpsState::AudioValue* audio = audioOf(object);
    return sps_float(audio ? audio->volume : 0.0f);
}

void audioSetVolume(SpsContext*, SpsValue object, SpsValue value, void*) {
    SpsState::AudioValue* audio = audioOf(object);
    if (audio) audio->volume = (float)sps_value_to_float(value);
}

SpsValue audioGetPitch(SpsContext*, SpsValue object, void*) {
    SpsState::AudioValue* audio = audioOf(object);
    return sps_float(audio ? audio->pitch : 0.0f);
}

void audioSetPitch(SpsContext*, SpsValue object, SpsValue value, void*) {
    SpsState::AudioValue* audio = audioOf(object);
    if (audio) audio->pitch = (float)sps_value_to_float(value);
}

SpsValue audioGetLoop(SpsContext*, SpsValue object, void*) {
    SpsState::AudioValue* audio = audioOf(object);
    return sps_bool(audio ? audio->loop : false);
}

void audioSetLoop(SpsContext*, SpsValue object, SpsValue value, void*) {
    SpsState::AudioValue* audio = audioOf(object);
    if (audio) audio->loop = sps_value_to_bool(value);
}

SpsValue audioPlay(SpsContext*, int argc, const SpsValue* args, void* user) {
    (void)argc;
    Script& script = scriptOf(user);
    SpsState::AudioValue* audio = audioOf(args[0]);
    if (!audio || !script.state->sound || audio->sound.empty()) return sps_nil();
    script.state->sound->play(audio->sound, audio->volume, audio->pitch, audio->loop);
    return sps_nil();
}

SpsValue audioStop(SpsContext*, int argc, const SpsValue* args, void* user) {
    (void)argc;
    Script& script = scriptOf(user);
    SpsState::AudioValue* audio = audioOf(args[0]);
    if (!audio || !script.state->sound || audio->sound.empty()) return sps_nil();
    script.state->sound->stop(audio->sound);
    return sps_nil();
}

SpsValue audioIsPlaying(SpsContext*, int argc, const SpsValue* args, void* user) {
    (void)argc;
    Script& script = scriptOf(user);
    SpsState::AudioValue* audio = audioOf(args[0]);
    if (!audio || !script.state->sound) return sps_bool(false);
    return sps_bool(script.state->sound->isPlaying(audio->sound));
}

// --- the pieces of the interface --------------------------------------------

SpsValue labelConstructor(SpsContext*, int argc, const SpsValue* args, void* user) {
    (void)argc;
    (void)args;
    Script& script = scriptOf(user);
    SpsState& state = *script.state;
    if (!state.canvas) return sps_nil();

    gui::Element element;
    element.kind = gui::ElementKind::Text;
    element.name = "label";
    element.width = 240.0f;
    element.height = 32.0f;
    element.fontSize = 20.0f;
    const int index = state.canvas->add(element);
    if (index < 0) return sps_nil();

    state.elements.push_back(index);
    return sps_host_object(script.labelClass, &state.elements.back());
}

SpsValue buttonConstructor(SpsContext*, int argc, const SpsValue* args, void* user) {
    (void)argc;
    (void)args;
    Script& script = scriptOf(user);
    SpsState& state = *script.state;
    if (!state.canvas) return sps_nil();

    gui::Element element;
    element.kind = gui::ElementKind::Button;
    element.name = "button";
    element.width = 160.0f;
    element.height = 40.0f;
    element.fontSize = 20.0f;
    const int index = state.canvas->add(element);
    if (index < 0) return sps_nil();

    state.elements.push_back(index);
    return sps_host_object(script.buttonClass, &state.elements.back());
}

// The index of the element a piece of the interface points at, or -1.
int elementIndexOf(SpsState& state, SpsValue object) {
    if (!state.canvas) return -1;
    if (sps_value_type(object) != SPS_OBJECT && sps_value_type(object) != SPS_USERDATA)
        return -1;
    const int* index = static_cast<const int*>(sps_value_to_userdata(object));
    if (!index || *index < 0 || *index >= (int)state.canvas->elements.size()) return -1;
    return *index;
}

gui::Element* elementOf(SpsState& state, SpsValue object) {
    const int index = elementIndexOf(state, object);
    if (index < 0) return nullptr;
    return &state.canvas->elements[(size_t)index];
}

SpsValue elementGetText(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    gui::Element* element = elementOf(*script.state, object);
    return sps_string(element ? element->text.c_str() : "");
}

void elementSetText(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    Script& script = scriptOf(user);
    gui::Element* element = elementOf(*script.state, object);
    if (element) element->text = textOf(context, value);
}

SpsValue elementGetPosition(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    gui::Element* element = elementOf(*script.state, object);
    if (!element) return makeVector(script, Vec3());
    return makeVector(script, Vec3{element->x, element->y, 0.0f});
}

void elementSetPosition(SpsContext*, SpsValue object, SpsValue value, void* user) {
    Script& script = scriptOf(user);
    gui::Element* element = elementOf(*script.state, object);
    if (!element) return;
    const Vec3 position = vectorOf(value);
    element->x = position.x;
    element->y = position.y;
}

SpsValue elementGetColor(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    gui::Element* element = elementOf(*script.state, object);
    if (!element) return makeColor(script, Color());
    return makeColor(script, element->color);
}

void elementSetColor(SpsContext*, SpsValue object, SpsValue value, void* user) {
    Script& script = scriptOf(user);
    gui::Element* element = elementOf(*script.state, object);
    if (element) element->color = colorOf(value);
}

SpsValue elementGetSize(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    gui::Element* element = elementOf(*script.state, object);
    if (!element) return makeVector(script, Vec3());
    return makeVector(script, Vec3{element->width, element->height, 0.0f});
}

void elementSetSize(SpsContext*, SpsValue object, SpsValue value, void* user) {
    Script& script = scriptOf(user);
    gui::Element* element = elementOf(*script.state, object);
    if (!element) return;
    const Vec3 size = vectorOf(value);
    element->width = size.x;
    element->height = size.y;
}

SpsValue elementGetVisible(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    gui::Element* element = elementOf(*script.state, object);
    return sps_bool(element ? element->visible : false);
}

void elementSetVisible(SpsContext*, SpsValue object, SpsValue value, void* user) {
    Script& script = scriptOf(user);
    gui::Element* element = elementOf(*script.state, object);
    if (element) element->visible = sps_value_to_bool(value);
}

SpsValue buttonGetOnClick(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    gui::Element* element = elementOf(*script.state, object);
    if (!element) return sps_string("");
    // The name of the element is where the name of the function is kept: the
    // interface of the engine has one name for a piece, and a button is a
    // piece with a function behind it.
    return sps_string(element->name.c_str());
}

void buttonSetOnClick(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    Script& script = scriptOf(user);
    gui::Element* element = elementOf(*script.state, object);
    if (element) element->name = textOf(context, value);
}

SpsValue buttonGetEnabled(SpsContext*, SpsValue object, void* user) {
    Script& script = scriptOf(user);
    gui::Element* element = elementOf(*script.state, object);
    return sps_bool(element ? element->interactable : false);
}

void buttonSetEnabled(SpsContext*, SpsValue object, SpsValue value, void* user) {
    Script& script = scriptOf(user);
    gui::Element* element = elementOf(*script.state, object);
    if (element) element->interactable = sps_value_to_bool(value);
}

} // namespace

// --- the registration --------------------------------------------------------

// The classes of the engine, as the scripts see them.
//
// Every class is made in the context of its own script, because the runtime
// keeps its classes per context - and the values the classes hand out are
// the host's, because the world is the host's.
bool registerClasses(Script& script) {
    SpsContext* context = script.context;
    void* user = &script;

    script.vectorClass = sps_host_class_new(context, "Vector3");
    if (!script.vectorClass) return false;
    sps_host_class_property(script.vectorClass, "x", vectorGetX, vectorSetX, user);
    sps_host_class_property(script.vectorClass, "y", vectorGetY, vectorSetY, user);
    sps_host_class_property(script.vectorClass, "z", vectorGetZ, vectorSetZ, user);
    sps_host_class_method(script.vectorClass, "length", vectorLength, user);
    sps_host_class_method(script.vectorClass, "normalized", vectorNormalized, user);
    sps_host_class_method(script.vectorClass, "dot", vectorDot, user);
    sps_host_class_method(script.vectorClass, "cross", vectorCross, user);

    script.colorClass = sps_host_class_new(context, "Color");
    if (!script.colorClass) return false;
    sps_host_class_property(script.colorClass, "r", colorGetR, colorSetR, user);
    sps_host_class_property(script.colorClass, "g", colorGetG, colorSetG, user);
    sps_host_class_property(script.colorClass, "b", colorGetB, colorSetB, user);
    sps_host_class_property(script.colorClass, "a", colorGetA, colorSetA, user);

    script.entityClass = sps_host_class_new(context, "Entity");
    if (!script.entityClass) return false;
    sps_host_class_constructor(script.entityClass, entityConstructor, user);
    sps_host_class_property(script.entityClass, "name", entityGetName, entitySetName, user);
    sps_host_class_property(script.entityClass, "tag", entityGetTag, entitySetTag, user);
    sps_host_class_property(script.entityClass, "position", entityGetPosition, entitySetPosition,
                            user);
    sps_host_class_property(script.entityClass, "rotation", entityGetRotation, entitySetRotation,
                            user);
    sps_host_class_property(script.entityClass, "size", entityGetSize, entitySetSize, user);
    sps_host_class_property(script.entityClass, "velocity", entityGetVelocity, entitySetVelocity,
                            user);
    sps_host_class_property(script.entityClass, "shape", entityGetShape, entitySetShape, user);
    sps_host_class_property(script.entityClass, "mesh", entityGetMesh, nullptr, user);
    sps_host_class_property(script.entityClass, "active", entityGetActive, entitySetActive, user);
    sps_host_class_property(script.entityClass, "mass", entityGetMass, entitySetMass, user);
    sps_host_class_method(script.entityClass, "move", entityMove, user);
    sps_host_class_method(script.entityClass, "rotate", entityRotate, user);
    sps_host_class_method(script.entityClass, "lookAt", entityLookAt, user);
    sps_host_class_method(script.entityClass, "destroy", entityDestroy, user);
    sps_host_class_method(script.entityClass, "wake", entityWake, user);

    script.sceneClass = sps_host_class_new(context, "Scene");
    if (!script.sceneClass) return false;
    sps_host_class_constructor(script.sceneClass, sceneConstructor, user);
    sps_host_class_property(script.sceneClass, "name", sceneGetName, nullptr, user);
    sps_host_class_property(script.sceneClass, "gravity", sceneGetGravity, sceneSetGravity, user);
    sps_host_class_property(script.sceneClass, "gravityAxis", sceneGetGravityAxis, nullptr, user);
    sps_host_class_method(script.sceneClass, "find", sceneFind, user);
    sps_host_class_method(script.sceneClass, "count", sceneCount, user);
    sps_host_class_method(script.sceneClass, "clear", sceneClear, user);

    script.rigidbodyClass = sps_host_class_new(context, "Rigidbody");
    if (!script.rigidbodyClass) return false;
    sps_host_class_constructor(script.rigidbodyClass, rigidbodyConstructor, user);
    sps_host_class_property(script.rigidbodyClass, "mass", rigidbodyGetMass, rigidbodySetMass,
                            user);
    sps_host_class_property(script.rigidbodyClass, "velocity", rigidbodyGetVelocity,
                            rigidbodySetVelocity, user);
    sps_host_class_property(script.rigidbodyClass, "force", rigidbodyGetForce, rigidbodySetForce,
                            user);
    sps_host_class_property(script.rigidbodyClass, "gravity", rigidbodyGetGravity, nullptr, user);
    sps_host_class_property(script.rigidbodyClass, "sleeping", rigidbodyGetSleeping, nullptr, user);
    sps_host_class_method(script.rigidbodyClass, "applyImpulse", rigidbodyApplyImpulse, user);
    sps_host_class_method(script.rigidbodyClass, "wake", rigidbodyWake, user);

    script.colliderClass = sps_host_class_new(context, "Collider");
    if (!script.colliderClass) return false;
    sps_host_class_constructor(script.colliderClass, colliderConstructor, user);
    sps_host_class_property(script.colliderClass, "shape", colliderGetShape, nullptr, user);
    sps_host_class_property(script.colliderClass, "size", colliderGetSize, colliderSetSize, user);
    sps_host_class_property(script.colliderClass, "radius", colliderGetRadius, colliderSetRadius,
                            user);
    sps_host_class_property(script.colliderClass, "isTrigger", colliderGetIsTrigger,
                            colliderSetIsTrigger, user);

    script.cameraClass = sps_host_class_new(context, "Camera");
    if (!script.cameraClass) return false;
    sps_host_class_constructor(script.cameraClass, cameraConstructor, user);
    sps_host_class_property(script.cameraClass, "fov", cameraGetFov, cameraSetFov, user);
    sps_host_class_property(script.cameraClass, "active", cameraGetActive, cameraSetActive, user);
    sps_host_class_method(script.cameraClass, "lookAt", cameraLookAt, user);

    script.lightClass = sps_host_class_new(context, "Light");
    if (!script.lightClass) return false;
    sps_host_class_constructor(script.lightClass, lightConstructor, user);
    sps_host_class_property(script.lightClass, "azimuth", lightGetAzimuth, lightSetAzimuth, user);
    sps_host_class_property(script.lightClass, "elevation", lightGetElevation, lightSetElevation,
                            user);
    sps_host_class_property(script.lightClass, "color", lightGetColor, lightSetColor, user);
    sps_host_class_property(script.lightClass, "ambient", lightGetAmbient, lightSetAmbient, user);

    script.materialClass = sps_host_class_new(context, "Material");
    if (!script.materialClass) return false;
    sps_host_class_constructor(script.materialClass, materialConstructor, user);
    sps_host_class_property(script.materialClass, "friction", materialGetFriction,
                            materialSetFriction, user);
    sps_host_class_property(script.materialClass, "restitution", materialGetRestitution,
                            materialSetRestitution, user);
    sps_host_class_property(script.materialClass, "density", materialGetDensity,
                            materialSetDensity, user);
    sps_host_class_property(script.materialClass, "isStatic", materialGetIsStatic,
                            materialSetIsStatic, user);

    script.triggerClass = sps_host_class_new(context, "Trigger");
    if (!script.triggerClass) return false;
    sps_host_class_constructor(script.triggerClass, triggerConstructor, user);
    sps_host_class_property(script.triggerClass, "size", triggerGetSize, triggerSetSize, user);
    sps_host_class_property(script.triggerClass, "onEnter", triggerGetOnEnter, triggerSetOnEnter,
                            user);
    sps_host_class_property(script.triggerClass, "onExit", triggerGetOnExit, triggerSetOnExit,
                            user);
    sps_host_class_property(script.triggerClass, "enabled", triggerGetEnabled, triggerSetEnabled,
                            user);

    script.audioClass = sps_host_class_new(context, "AudioSource");
    if (!script.audioClass) return false;
    sps_host_class_constructor(script.audioClass, audioConstructor, user);
    sps_host_class_property(script.audioClass, "sound", audioGetSound, audioSetSound, user);
    sps_host_class_property(script.audioClass, "volume", audioGetVolume, audioSetVolume, user);
    sps_host_class_property(script.audioClass, "pitch", audioGetPitch, audioSetPitch, user);
    sps_host_class_property(script.audioClass, "loop", audioGetLoop, audioSetLoop, user);
    sps_host_class_method(script.audioClass, "play", audioPlay, user);
    sps_host_class_method(script.audioClass, "stop", audioStop, user);
    sps_host_class_method(script.audioClass, "isPlaying", audioIsPlaying, user);

    script.labelClass = sps_host_class_new(context, "Label");
    if (!script.labelClass) return false;
    sps_host_class_constructor(script.labelClass, labelConstructor, user);
    sps_host_class_property(script.labelClass, "text", elementGetText, elementSetText, user);
    sps_host_class_property(script.labelClass, "position", elementGetPosition, elementSetPosition,
                            user);
    sps_host_class_property(script.labelClass, "color", elementGetColor, elementSetColor, user);
    sps_host_class_property(script.labelClass, "size", elementGetSize, elementSetSize, user);
    sps_host_class_property(script.labelClass, "visible", elementGetVisible, elementSetVisible, user);

    script.buttonClass = sps_host_class_new(context, "Button");
    if (!script.buttonClass) return false;
    sps_host_class_constructor(script.buttonClass, buttonConstructor, user);
    sps_host_class_property(script.buttonClass, "text", elementGetText, elementSetText, user);
    sps_host_class_property(script.buttonClass, "position", elementGetPosition, elementSetPosition,
                            user);
    sps_host_class_property(script.buttonClass, "color", elementGetColor, elementSetColor, user);
    sps_host_class_property(script.buttonClass, "size", elementGetSize, elementSetSize, user);
    sps_host_class_property(script.buttonClass, "visible", elementGetVisible, elementSetVisible, user);
    sps_host_class_property(script.buttonClass, "onClick", buttonGetOnClick, buttonSetOnClick, user);
    sps_host_class_property(script.buttonClass, "enabled", buttonGetEnabled, buttonSetEnabled, user);

    return true;
}

} // namespace sps
} // namespace s3d

#endif // SPECTRA_SPSCRIPT
