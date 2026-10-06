#include "script/Bindings.hpp"

#include "core/Log.hpp"
#include "physics/PhysicsWorld.hpp"
#include "scene/BodyFactory.hpp"
#include "script/BindString.hpp"

#if SPECTRA_SCRIPTING

namespace s3d {
namespace script {
namespace {

// Density reaches BodyFactory, which turns it into a mass; a non-positive value
// would collapse to the 1e-6 clamp, so it is treated as the default instead.
float sanitizeDensity(const char* fn, float density) {
    if (density > 0.0f) return density;
    Log::warn("engine::%s: density %g is not positive, using 1.0", fn, (double)density);
    return 1.0f;
}

// --- engine:: logging -------------------------------------------------------
//
// Text arrives as a reference to the registered script string, never as
// std::string - see BindString.hpp for why the two are not interchangeable here.

void log(const ScriptString& text) { Log::info("[script] %s", text.c_str()); }
void logWarning(const ScriptString& text) { Log::warn("[script] %s", text.c_str()); }
void logError(const ScriptString& text) { Log::error("[script] %s", text.c_str()); }

// --- engine:: spawning ------------------------------------------------------
//
// BodyFactory::spawn takes half extents and derives each shape's own fields from
// them, so the size arguments below are full dimensions divided by two here and
// everything downstream stays consistent with the UI and the keyboard shortcuts.

int spawnBox(float x, float y, float z, float sx, float sy, float sz, float density) {
    ScriptState* state = activeState();
    if (!state || !state->world) return -1;

    return BodyFactory::spawn(*state->world, Shape::Box, Vec3{x, y, z}, Vec3{},
                              Vec3{sx * 0.5f, sy * 0.5f, sz * 0.5f},
                              sanitizeDensity("spawnBox", density), Vec3{}, Quat{});
}

int spawnSphere(float x, float y, float z, float radius, float density) {
    ScriptState* state = activeState();
    if (!state || !state->world) return -1;

    return BodyFactory::spawn(*state->world, Shape::Sphere, Vec3{x, y, z}, Vec3{},
                              Vec3{radius, radius, radius},
                              sanitizeDensity("spawnSphere", density), Vec3{}, Quat{});
}

int spawnShape(int shapeValue, float x, float y, float z, float sx, float sy, float sz,
               float density) {
    ScriptState* state = activeState();
    if (!state || !state->world) return -1;

    if (shapeValue < 0 || shapeValue > (int)Shape::Mesh) {
        Log::error("engine::spawnShape: %d is not a Shape value", shapeValue);
        return -1;
    }
    const Shape shape = static_cast<Shape>(shapeValue);
    if (shape == Shape::Mesh) {
        // A mesh body needs a MeshRegistry handle that only importModel can get.
        Log::error("engine::spawnShape: use engine::importModel for ShapeMesh");
        return -1;
    }

    return BodyFactory::spawn(*state->world, shape, Vec3{x, y, z}, Vec3{},
                              Vec3{sx * 0.5f, sy * 0.5f, sz * 0.5f},
                              sanitizeDensity("spawnShape", density), Vec3{}, Quat{});
}

// --- engine:: gravity -------------------------------------------------------

void setGravity(int axisValue) {
    ScriptState* state = activeState();
    if (!state || !state->world) return;

    if (axisValue < (int)GravityAxis::NegX || axisValue > (int)GravityAxis::PosZ) {
        Log::error("engine::setGravity: %d is not a Gravity value (0..5)", axisValue);
        return;
    }
    state->world->setGravity(static_cast<GravityAxis>(axisValue));
}

int getGravity() {
    ScriptState* state = activeState();
    if (!state || !state->world) return -1;
    return (int)state->world->gravityAxis;
}

// --- engine:: per body ------------------------------------------------------

float getBodyX(int index) {
    const RigidBody* body = resolveBody("getBodyX", index);
    return body ? body->position.x : 0.0f;
}

float getBodyY(int index) {
    const RigidBody* body = resolveBody("getBodyY", index);
    return body ? body->position.y : 0.0f;
}

float getBodyZ(int index) {
    const RigidBody* body = resolveBody("getBodyZ", index);
    return body ? body->position.z : 0.0f;
}

void setBodyPosition(int index, float x, float y, float z) {
    RigidBody* body = resolveBody("setBodyPosition", index);
    if (!body) return;
    body->position = Vec3{x, y, z};
    body->wake();
}

void setBodyVelocity(int index, float x, float y, float z) {
    RigidBody* body = resolveBody("setBodyVelocity", index);
    if (!body) return;
    body->velocity = Vec3{x, y, z};
    body->wake();
}

float getBodyMass(int index) {
    const RigidBody* body = resolveBody("getBodyMass", index);
    return body ? body->mass : 0.0f;
}

void setBodyMass(int index, float mass) {
    RigidBody* body = resolveBody("setBodyMass", index);
    if (!body) return;
    // configure() owns the mass/inertia clamp, so hand it straight over.
    body->configure(mass);
    body->wake();
}

float getBodySpeed(int index) {
    const RigidBody* body = resolveBody("getBodySpeed", index);
    return body ? body->velocity.length() : 0.0f;
}

int getBodyShape(int index) {
    const RigidBody* body = resolveBody("getBodyShape", index);
    return body ? (int)body->shape : -1;
}

void wakeBody(int index) {
    RigidBody* body = resolveBody("wakeBody", index);
    if (!body) return;
    body->wake();
}

// --- declarations -----------------------------------------------------------
//
// None of these mention a namespace: the host points the engine's default
// namespace at `engine` before registration, so every entry lands there and
// scripts spell them engine::log, engine::ShapeBox and so on.

const Binding kBindings[] = {
    {"void log(const string &in)", asFUNCTION(log)},
    {"void logWarning(const string &in)", asFUNCTION(logWarning)},
    {"void logError(const string &in)", asFUNCTION(logError)},

    {"int spawnBox(float x, float y, float z, float sx, float sy, float sz, "
     "float density = 1.0)", asFUNCTION(spawnBox)},
    {"int spawnSphere(float x, float y, float z, float radius, float density = 1.0)",
     asFUNCTION(spawnSphere)},
    {"int spawnShape(int shape, float x, float y, float z, float sx, float sy, float sz, "
     "float density = 1.0)", asFUNCTION(spawnShape)},

    {"void setGravity(int axis)", asFUNCTION(setGravity)},
    {"int getGravity()", asFUNCTION(getGravity)},

    {"float getBodyX(int index)", asFUNCTION(getBodyX)},
    {"float getBodyY(int index)", asFUNCTION(getBodyY)},
    {"float getBodyZ(int index)", asFUNCTION(getBodyZ)},
    {"void setBodyPosition(int index, float x, float y, float z)",
     asFUNCTION(setBodyPosition)},
    {"void setBodyVelocity(int index, float x, float y, float z)",
     asFUNCTION(setBodyVelocity)},
    {"float getBodyMass(int index)", asFUNCTION(getBodyMass)},
    {"void setBodyMass(int index, float m)", asFUNCTION(setBodyMass)},
    {"float getBodySpeed(int index)", asFUNCTION(getBodySpeed)},
    {"int getBodyShape(int index)", asFUNCTION(getBodyShape)},
    {"void wakeBody(int index)", asFUNCTION(wakeBody)},
};

// Enum values are the underlying ordinals of s3d::Shape, so a script can pass
// engine::ShapeCone straight into spawnShape. The names land in `engine` too -
// same default namespace as the functions - which is what makes the scope
// mandatory in the script.
struct EnumValue {
    const char* name;
    int value;
};

const char* const kShapeEnum = "Shape";
const EnumValue kShapeValues[] = {
    {"ShapeBox", (int)Shape::Box},
    {"ShapeSphere", (int)Shape::Sphere},
    {"ShapeCylinder", (int)Shape::Cylinder},
    {"ShapeCapsule", (int)Shape::Capsule},
    {"ShapeCone", (int)Shape::Cone},
    {"ShapePyramid", (int)Shape::Pyramid},
    {"ShapePrism", (int)Shape::Prism},
    {"ShapeMesh", (int)Shape::Mesh},
};

const char* const kGravityEnum = "Gravity";
const EnumValue kGravityValues[] = {
    {"GravityNegX", (int)GravityAxis::NegX},
    {"GravityPosX", (int)GravityAxis::PosX},
    {"GravityNegY", (int)GravityAxis::NegY},
    {"GravityPosY", (int)GravityAxis::PosY},
    {"GravityNegZ", (int)GravityAxis::NegZ},
    {"GravityPosZ", (int)GravityAxis::PosZ},
};

int registerEnum(asIScriptEngine* engine, const char* typeName, const EnumValue* values,
                 size_t count) {
    if (engine->RegisterEnum(typeName) < 0) return asNO_MODULE;
    for (size_t i = 0; i < count; ++i)
        if (engine->RegisterEnumValue(typeName, values[i].name, values[i].value) < 0)
            return asNO_MODULE;
    return 0;
}

} // namespace

int registerEngineBindings(asIScriptEngine* engine) {
    if (registerBindings(engine, kBindings, sizeof(kBindings) / sizeof(kBindings[0])) < 0)
        return asNO_MODULE;
    if (registerEnum(engine, kShapeEnum, kShapeValues,
                     sizeof(kShapeValues) / sizeof(kShapeValues[0])) < 0)
        return asNO_MODULE;
    if (registerEnum(engine, kGravityEnum, kGravityValues,
                     sizeof(kGravityValues) / sizeof(kGravityValues[0])) < 0)
        return asNO_MODULE;
    return 0;
}

} // namespace script
} // namespace s3d

#endif // SPECTRA_SCRIPTING