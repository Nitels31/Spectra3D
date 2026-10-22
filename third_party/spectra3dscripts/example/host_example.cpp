// Spectra3D Script - the API of the engine
//
// A complete example of the API: the classes of the engine (Entity, Scene,
// Camera, Light, Rigidbody, Collider, Trigger, AudioSource, Label, Button,
// Vector3, Color), the functions (the input, the time, the sound, the
// movement, the resources) and the hooks the engine calls in the script.

#include "sps/sps.h"
#include "engine.hpp"

#include <cstdio>
#include <cstring>
#include <string>

// ------------------------------------------------------------------
// the helpers
// ------------------------------------------------------------------

static engine::Engine g_engine;
static SpsContext*    g_context = nullptr;

static SpsHostClass* g_entity_class = nullptr;
static SpsHostClass* g_scene_class = nullptr;
static SpsHostClass* g_camera_class = nullptr;
static SpsHostClass* g_light_class = nullptr;
static SpsHostClass* g_rigidbody_class = nullptr;
static SpsHostClass* g_collider_class = nullptr;
static SpsHostClass* g_trigger_class = nullptr;
static SpsHostClass* g_audio_source_class = nullptr;
static SpsHostClass* g_label_class = nullptr;
static SpsHostClass* g_button_class = nullptr;
static SpsHostClass* g_vec3_class = nullptr;
static SpsHostClass* g_color_class = nullptr;


static engine::Entity* to_entity(SpsValue value) {
    return (engine::Entity*)sps_value_to_userdata(value);
}

static engine::Vec3 to_vec3(SpsValue value) {
    engine::Vec3* vec = (engine::Vec3*)sps_value_to_userdata(value);
    return vec ? *vec : engine::Vec3();
}

static engine::Color to_color(SpsValue value) {
    engine::Color* color = (engine::Color*)sps_value_to_userdata(value);
    return color ? *color : engine::Color();
}

static SpsValue make_vec3(const engine::Vec3& value) {
    engine::Vec3* copy = new engine::Vec3(value);
    return sps_host_class_new_object(g_vec3_class, copy);
}

static SpsValue make_color(const engine::Color& value) {
    engine::Color* copy = new engine::Color(value);
    return sps_host_class_new_object(g_color_class, copy);
}

// The text of a value, copied into a static buffer (the rules of the API:
// the text lives until the next call).
static std::string text_of(SpsValue value) {
    return std::string(sps_value_to_text(g_context, value));
}

// ------------------------------------------------------------------
// the classes of the engine
// ------------------------------------------------------------------

// ------------------------------------------------------------------
// the Scene, the Camera and the Light
// ------------------------------------------------------------------

static SpsValue scene_get_name(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    std::string* name = (std::string*)sps_value_to_userdata(object);
    return sps_string(name ? name->c_str() : "");
}

static void scene_set_name(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    std::string* name = (std::string*)sps_value_to_userdata(object);
    if (name) *name = text_of(value);
}

static SpsValue scene_get_gravity(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    return sps_float(g_engine.gravity_);
}

static void scene_set_gravity(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    (void)object;
    g_engine.gravity_ = (float)sps_value_to_float(value);
}

static SpsValue camera_constructor(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)args;
    (void)user;
    g_engine.cameras_.push_back(engine::Camera());
    return sps_host_class_new_object(g_camera_class, &g_engine.cameras_.back());
}

static SpsValue camera_get_fov(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Camera* camera = (engine::Camera*)sps_value_to_userdata(object);
    return sps_float(camera ? camera->fov : 0.0);
}

static void camera_set_fov(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Camera* camera = (engine::Camera*)sps_value_to_userdata(object);
    if (camera) camera->fov = (float)sps_value_to_float(value);
}

static SpsValue camera_get_far(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Camera* camera = (engine::Camera*)sps_value_to_userdata(object);
    return sps_float(camera ? camera->far : 0.0);
}

static void camera_set_far(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Camera* camera = (engine::Camera*)sps_value_to_userdata(object);
    if (camera) camera->far = (float)sps_value_to_float(value);
}

static SpsValue camera_look_at(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    engine::Camera* camera = (engine::Camera*)sps_value_to_userdata(args[0]);
    if (!camera || argc < 2) return sps_nil();
    if (camera->entity) {
        engine::Vec3 target = to_vec3(args[1]);
        engine::Vec3 direction = (target - camera->entity->position).normalized();
        camera->entity->rotation = engine::Vec3(0.0f, std::atan2(direction.x, direction.z) * 57.2958f, 0.0f);
    }
    return sps_nil();
}

static SpsValue light_constructor(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)args;
    (void)user;
    g_engine.lights_.push_back(engine::Light());
    return sps_host_class_new_object(g_light_class, &g_engine.lights_.back());
}

static SpsValue light_get_type(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Light* light = (engine::Light*)sps_value_to_userdata(object);
    return sps_string(light ? light->type.c_str() : "");
}

static void light_set_type(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Light* light = (engine::Light*)sps_value_to_userdata(object);
    if (light) light->type = text_of(value);
}

static SpsValue light_get_intensity(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Light* light = (engine::Light*)sps_value_to_userdata(object);
    return sps_float(light ? light->intensity : 0.0);
}

static void light_set_intensity(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Light* light = (engine::Light*)sps_value_to_userdata(object);
    if (light) light->intensity = (float)sps_value_to_float(value);
}

static SpsValue light_get_range(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Light* light = (engine::Light*)sps_value_to_userdata(object);
    return sps_float(light ? light->range : 0.0);
}

static void light_set_range(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Light* light = (engine::Light*)sps_value_to_userdata(object);
    if (light) light->range = (float)sps_value_to_float(value);
}

// ------------------------------------------------------------------
// the Entity class
// ------------------------------------------------------------------

static SpsValue entity_constructor(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)user;
    const std::string name = argc > 0 ? text_of(args[0]) : "entity";
    return sps_host_class_new_object(g_entity_class, g_engine.create_entity(name));
}

static SpsValue entity_get_name(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Entity* entity = to_entity(object);
    return sps_string(entity ? entity->name.c_str() : "");
}

static void entity_set_name(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Entity* entity = to_entity(object);
    if (entity) entity->name = text_of(value);
}

static SpsValue entity_get_position(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Entity* entity = to_entity(object);
    return make_vec3(entity ? entity->position : engine::Vec3());
}

static void entity_set_position(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Entity* entity = to_entity(object);
    if (entity) entity->position = to_vec3(value);
}

static SpsValue entity_get_rotation(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Entity* entity = to_entity(object);
    return make_vec3(entity ? entity->rotation : engine::Vec3());
}

static void entity_set_rotation(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Entity* entity = to_entity(object);
    if (entity) entity->rotation = to_vec3(value);
}

static SpsValue entity_get_size(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Entity* entity = to_entity(object);
    return make_vec3(entity ? entity->size : engine::Vec3());
}

static void entity_set_size(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Entity* entity = to_entity(object);
    if (entity) entity->size = to_vec3(value);
}

static SpsValue entity_get_color(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Entity* entity = to_entity(object);
    return make_color(entity ? entity->color : engine::Color());
}

static void entity_set_color(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Entity* entity = to_entity(object);
    if (entity) entity->color = to_color(value);
}

static SpsValue entity_get_material(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Entity* entity = to_entity(object);
    return sps_string(entity ? entity->material.c_str() : "");
}

static void entity_set_material(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Entity* entity = to_entity(object);
    if (entity) entity->material = text_of(value);
}

static SpsValue entity_get_texture(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Entity* entity = to_entity(object);
    return sps_string(entity ? entity->texture.c_str() : "");
}

static void entity_set_texture(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Entity* entity = to_entity(object);
    if (entity) entity->texture = text_of(value);
}

static SpsValue entity_get_tag(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Entity* entity = to_entity(object);
    return sps_string(entity ? entity->tag.c_str() : "");
}

static void entity_set_tag(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Entity* entity = to_entity(object);
    if (entity) entity->tag = text_of(value);
}

static SpsValue entity_get_active(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Entity* entity = to_entity(object);
    return sps_bool(entity ? entity->active : false);
}

static void entity_set_active(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Entity* entity = to_entity(object);
    if (entity) entity->active = sps_value_to_bool(value);
}

static SpsValue entity_move(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    engine::Entity* entity = to_entity(args[0]);
    if (!entity) return sps_nil();
    if (argc > 1) entity->position.x += (float)sps_value_to_float(args[1]);
    if (argc > 2) entity->position.y += (float)sps_value_to_float(args[2]);
    if (argc > 3) entity->position.z += (float)sps_value_to_float(args[3]);
    return sps_nil();
}

static SpsValue entity_rotate(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    engine::Entity* entity = to_entity(args[0]);
    if (!entity) return sps_nil();
    if (argc > 1) entity->rotation.x += (float)sps_value_to_float(args[1]);
    if (argc > 2) entity->rotation.y += (float)sps_value_to_float(args[2]);
    if (argc > 3) entity->rotation.z += (float)sps_value_to_float(args[3]);
    return sps_nil();
}

static SpsValue entity_scale(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    engine::Entity* entity = to_entity(args[0]);
    if (!entity) return sps_nil();
    if (argc > 1) entity->size.x *= (float)sps_value_to_float(args[1]);
    if (argc > 2) entity->size.y *= (float)sps_value_to_float(args[2]);
    if (argc > 3) entity->size.z *= (float)sps_value_to_float(args[3]);
    return sps_nil();
}

static SpsValue entity_look_at(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    engine::Entity* entity = to_entity(args[0]);
    if (!entity) return sps_nil();
    engine::Vec3 target;
    if (argc > 1 && sps_value_to_host_class(args[1]) == g_vec3_class) {
        target = to_vec3(args[1]);
    } else {
        target = engine::Vec3((float)sps_value_to_float(args[1]), (float)sps_value_to_float(args[2]),
                              (float)sps_value_to_float(args[3]));
    }
    engine::Vec3 direction = (target - entity->position).normalized();
    entity->rotation = engine::Vec3(0.0f, std::atan2(direction.x, direction.z) * 57.2958f, 0.0f);
    return sps_nil();
}

static SpsValue entity_destroy(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)user;
    engine::Entity* entity = to_entity(args[0]);
    if (entity) entity->active = false;
    return sps_nil();
}

// ------------------------------------------------------------------
// the Scene class
// ------------------------------------------------------------------

static SpsValue scene_constructor(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)args;
    (void)user;
    return sps_host_class_new_object(g_scene_class, new std::string("scene"));
}

static SpsValue scene_find(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 1) return sps_nil();
    engine::Entity* entity = g_engine.find_entity(text_of(args[0]));
    if (!entity) return sps_nil();
    return sps_host_class_new_object(g_entity_class, entity);
}

static SpsValue scene_count(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)args;
    (void)user;
    return sps_int((int64_t)g_engine.entities_.size());
}

// ------------------------------------------------------------------
// the Rigidbody class
// ------------------------------------------------------------------

static SpsValue rigidbody_constructor(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    engine::Rigidbody body;
    if (argc > 0) body.entity = to_entity(args[0]);
    g_engine.rigidbodies_.push_back(body);
    return sps_host_class_new_object(g_rigidbody_class, &g_engine.rigidbodies_.back());
}

static SpsValue rigidbody_get_velocity(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Rigidbody* body = (engine::Rigidbody*)sps_value_to_userdata(object);
    return make_vec3(body ? body->velocity : engine::Vec3());
}

static void rigidbody_set_velocity(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Rigidbody* body = (engine::Rigidbody*)sps_value_to_userdata(object);
    if (body) body->velocity = to_vec3(value);
}

static SpsValue rigidbody_get_mass(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Rigidbody* body = (engine::Rigidbody*)sps_value_to_userdata(object);
    return sps_float(body ? body->mass : 0.0);
}

static void rigidbody_set_mass(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Rigidbody* body = (engine::Rigidbody*)sps_value_to_userdata(object);
    if (body) body->mass = (float)sps_value_to_float(value);
}

static SpsValue rigidbody_get_gravity(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Rigidbody* body = (engine::Rigidbody*)sps_value_to_userdata(object);
    return sps_float(body ? body->gravity : 0.0);
}

static void rigidbody_set_gravity(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Rigidbody* body = (engine::Rigidbody*)sps_value_to_userdata(object);
    if (body) body->gravity = (float)sps_value_to_float(value);
}

static SpsValue rigidbody_get_kinematic(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Rigidbody* body = (engine::Rigidbody*)sps_value_to_userdata(object);
    return sps_bool(body ? body->kinematic : false);
}

static void rigidbody_set_kinematic(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Rigidbody* body = (engine::Rigidbody*)sps_value_to_userdata(object);
    if (body) body->kinematic = sps_value_to_bool(value);
}

static SpsValue rigidbody_apply_force(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    engine::Rigidbody* body = (engine::Rigidbody*)sps_value_to_userdata(args[0]);
    if (!body) return sps_nil();
    if (argc > 1) body->force.x += (float)sps_value_to_float(args[1]);
    if (argc > 2) body->force.y += (float)sps_value_to_float(args[2]);
    if (argc > 3) body->force.z += (float)sps_value_to_float(args[3]);
    return sps_nil();
}

static SpsValue rigidbody_set_velocity(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    engine::Rigidbody* body = (engine::Rigidbody*)sps_value_to_userdata(args[0]);
    if (body) body->velocity = to_vec3(args[1]);
    return sps_nil();
}

// ------------------------------------------------------------------
// the Collider class
// ------------------------------------------------------------------

static SpsValue collider_constructor(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    engine::Collider collider;
    if (argc > 0) collider.entity = to_entity(args[0]);
    g_engine.colliders_.push_back(collider);
    return sps_host_class_new_object(g_collider_class, &g_engine.colliders_.back());
}

static SpsValue collider_get_shape(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Collider* collider = (engine::Collider*)sps_value_to_userdata(object);
    return sps_string(collider ? collider->shape.c_str() : "");
}

static void collider_set_shape(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Collider* collider = (engine::Collider*)sps_value_to_userdata(object);
    if (collider) collider->shape = text_of(value);
}

static SpsValue collider_get_size(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Collider* collider = (engine::Collider*)sps_value_to_userdata(object);
    return make_vec3(collider ? collider->size : engine::Vec3());
}

static void collider_set_size(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Collider* collider = (engine::Collider*)sps_value_to_userdata(object);
    if (collider) collider->size = to_vec3(value);
}

static SpsValue collider_get_radius(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Collider* collider = (engine::Collider*)sps_value_to_userdata(object);
    return sps_float(collider ? collider->radius : 0.0);
}

static void collider_set_radius(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Collider* collider = (engine::Collider*)sps_value_to_userdata(object);
    if (collider) collider->radius = (float)sps_value_to_float(value);
}

static SpsValue collider_get_is_trigger(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Collider* collider = (engine::Collider*)sps_value_to_userdata(object);
    return sps_bool(collider ? collider->isTrigger : false);
}

static void collider_set_is_trigger(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Collider* collider = (engine::Collider*)sps_value_to_userdata(object);
    if (collider) collider->isTrigger = sps_value_to_bool(value);
}

// ------------------------------------------------------------------
// the Trigger class
// ------------------------------------------------------------------

static SpsValue trigger_constructor(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    engine::Trigger trigger;
    if (argc > 0) trigger.entity = to_entity(args[0]);
    g_engine.triggers_.push_back(trigger);
    return sps_host_class_new_object(g_trigger_class, &g_engine.triggers_.back());
}

static SpsValue trigger_get_size(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Trigger* trigger = (engine::Trigger*)sps_value_to_userdata(object);
    return make_vec3(trigger ? trigger->size : engine::Vec3());
}

static void trigger_set_size(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Trigger* trigger = (engine::Trigger*)sps_value_to_userdata(object);
    if (trigger) trigger->size = to_vec3(value);
}

static SpsValue trigger_get_on_enter(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Trigger* trigger = (engine::Trigger*)sps_value_to_userdata(object);
    return sps_string(trigger ? trigger->onEnter.c_str() : "");
}

static void trigger_set_on_enter(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Trigger* trigger = (engine::Trigger*)sps_value_to_userdata(object);
    if (trigger) trigger->onEnter = text_of(value);
}

static SpsValue trigger_get_on_exit(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Trigger* trigger = (engine::Trigger*)sps_value_to_userdata(object);
    return sps_string(trigger ? trigger->onExit.c_str() : "");
}

static void trigger_set_on_exit(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Trigger* trigger = (engine::Trigger*)sps_value_to_userdata(object);
    if (trigger) trigger->onExit = text_of(value);
}

// ------------------------------------------------------------------
// the AudioSource class
// ------------------------------------------------------------------

static SpsValue audio_source_constructor(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)args;
    (void)user;
    g_engine.sources_.push_back(engine::AudioSource());
    return sps_host_class_new_object(g_audio_source_class, &g_engine.sources_.back());
}

static SpsValue audio_source_get_sound(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::AudioSource* source = (engine::AudioSource*)sps_value_to_userdata(object);
    return sps_string(source ? source->sound.c_str() : "");
}

static void audio_source_set_sound(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::AudioSource* source = (engine::AudioSource*)sps_value_to_userdata(object);
    if (source) source->sound = text_of(value);
}

static SpsValue audio_source_get_volume(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::AudioSource* source = (engine::AudioSource*)sps_value_to_userdata(object);
    return sps_float(source ? source->volume : 0.0);
}

static void audio_source_set_volume(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::AudioSource* source = (engine::AudioSource*)sps_value_to_userdata(object);
    if (source) source->volume = (float)sps_value_to_float(value);
}

static SpsValue audio_source_get_pitch(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::AudioSource* source = (engine::AudioSource*)sps_value_to_userdata(object);
    return sps_float(source ? source->pitch : 0.0);
}

static void audio_source_set_pitch(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::AudioSource* source = (engine::AudioSource*)sps_value_to_userdata(object);
    if (source) source->pitch = (float)sps_value_to_float(value);
}

static SpsValue audio_source_get_loop(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::AudioSource* source = (engine::AudioSource*)sps_value_to_userdata(object);
    return sps_bool(source ? source->loop : false);
}

static void audio_source_set_loop(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::AudioSource* source = (engine::AudioSource*)sps_value_to_userdata(object);
    if (source) source->loop = sps_value_to_bool(value);
}

static SpsValue audio_source_play(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)user;
    engine::AudioSource* source = (engine::AudioSource*)sps_value_to_userdata(args[0]);
    if (!source) return sps_nil();
    source->playing = true;
    g_engine.play_sound(source->sound, source->volume, source->pitch);
    return sps_nil();
}

static SpsValue audio_source_stop(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)user;
    engine::AudioSource* source = (engine::AudioSource*)sps_value_to_userdata(args[0]);
    if (!source) return sps_nil();
    source->playing = false;
    g_engine.stop_sound(source->sound);
    return sps_nil();
}

static SpsValue audio_source_is_playing(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)user;
    engine::AudioSource* source = (engine::AudioSource*)sps_value_to_userdata(args[0]);
    return sps_bool(source ? source->playing : false);
}

// ------------------------------------------------------------------
// the Label and the Button classes
// ------------------------------------------------------------------

static SpsValue label_constructor(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)args;
    (void)user;
    g_engine.labels_.push_back(engine::Label());
    return sps_host_class_new_object(g_label_class, &g_engine.labels_.back());
}

static SpsValue label_get_text(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Label* label = (engine::Label*)sps_value_to_userdata(object);
    return sps_string(label ? label->text.c_str() : "");
}

static void label_set_text(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Label* label = (engine::Label*)sps_value_to_userdata(object);
    if (label) label->text = text_of(value);
}

static SpsValue label_get_position(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Label* label = (engine::Label*)sps_value_to_userdata(object);
    return make_vec3(label ? label->position : engine::Vec3());
}

static void label_set_position(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Label* label = (engine::Label*)sps_value_to_userdata(object);
    if (label) label->position = to_vec3(value);
}

static SpsValue button_constructor(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)args;
    (void)user;
    g_engine.buttons_.push_back(engine::Button());
    return sps_host_class_new_object(g_button_class, &g_engine.buttons_.back());
}

static SpsValue button_get_text(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Button* button = (engine::Button*)sps_value_to_userdata(object);
    return sps_string(button ? button->text.c_str() : "");
}

static void button_set_text(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Button* button = (engine::Button*)sps_value_to_userdata(object);
    if (button) button->text = text_of(value);
}

static SpsValue button_get_on_click(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Button* button = (engine::Button*)sps_value_to_userdata(object);
    return sps_string(button ? button->onClick.c_str() : "");
}

static void button_set_on_click(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Button* button = (engine::Button*)sps_value_to_userdata(object);
    if (button) button->onClick = text_of(value);
}

// ------------------------------------------------------------------
// the Vector3 and the Color classes
// ------------------------------------------------------------------

static SpsValue vec3_constructor(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    engine::Vec3* vec = new engine::Vec3();
    if (argc > 0) vec->x = (float)sps_value_to_float(args[0]);
    if (argc > 1) vec->y = (float)sps_value_to_float(args[1]);
    if (argc > 2) vec->z = (float)sps_value_to_float(args[2]);
    return sps_host_class_new_object(g_vec3_class, vec);
}

static SpsValue vec3_get_x(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Vec3* vec = (engine::Vec3*)sps_value_to_userdata(object);
    return sps_float(vec ? vec->x : 0.0);
}

static void vec3_set_x(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Vec3* vec = (engine::Vec3*)sps_value_to_userdata(object);
    if (vec) vec->x = (float)sps_value_to_float(value);
}

static SpsValue vec3_get_y(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Vec3* vec = (engine::Vec3*)sps_value_to_userdata(object);
    return sps_float(vec ? vec->y : 0.0);
}

static void vec3_set_y(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Vec3* vec = (engine::Vec3*)sps_value_to_userdata(object);
    if (vec) vec->y = (float)sps_value_to_float(value);
}

static SpsValue vec3_get_z(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Vec3* vec = (engine::Vec3*)sps_value_to_userdata(object);
    return sps_float(vec ? vec->z : 0.0);
}

static void vec3_set_z(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Vec3* vec = (engine::Vec3*)sps_value_to_userdata(object);
    if (vec) vec->z = (float)sps_value_to_float(value);
}

static SpsValue color_constructor(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    engine::Color* color = new engine::Color();
    if (argc > 0) color->r = (float)sps_value_to_float(args[0]);
    if (argc > 1) color->g = (float)sps_value_to_float(args[1]);
    if (argc > 2) color->b = (float)sps_value_to_float(args[2]);
    if (argc > 3) color->a = (float)sps_value_to_float(args[3]);
    return sps_host_class_new_object(g_color_class, color);
}

static SpsValue color_get_r(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Color* color = (engine::Color*)sps_value_to_userdata(object);
    return sps_float(color ? color->r : 0.0);
}

static void color_set_r(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Color* color = (engine::Color*)sps_value_to_userdata(object);
    if (color) color->r = (float)sps_value_to_float(value);
}

static SpsValue color_get_g(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Color* color = (engine::Color*)sps_value_to_userdata(object);
    return sps_float(color ? color->g : 0.0);
}

static void color_set_g(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Color* color = (engine::Color*)sps_value_to_userdata(object);
    if (color) color->g = (float)sps_value_to_float(value);
}

static SpsValue color_get_b(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Color* color = (engine::Color*)sps_value_to_userdata(object);
    return sps_float(color ? color->b : 0.0);
}

static void color_set_b(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Color* color = (engine::Color*)sps_value_to_userdata(object);
    if (color) color->b = (float)sps_value_to_float(value);
}

static SpsValue color_get_a(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Color* color = (engine::Color*)sps_value_to_userdata(object);
    return sps_float(color ? color->a : 0.0);
}

static void color_set_a(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Color* color = (engine::Color*)sps_value_to_userdata(object);
    if (color) color->a = (float)sps_value_to_float(value);
}

// ------------------------------------------------------------------
// the functions of the engine
// ------------------------------------------------------------------

static SpsValue native_log(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)user;
    for (int i = 0; i < argc; i++) {
        printf("[engine] %s\n", sps_value_to_text(context, args[i]));
    }
    return sps_nil();
}

static SpsValue native_down_key(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 1) return sps_bool(false);
    return sps_bool(g_engine.down_key(text_of(args[0])));
}

static SpsValue native_just_pressed(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 1) return sps_bool(false);
    return sps_bool(g_engine.was_just_pressed(text_of(args[0])));
}

static SpsValue native_just_released(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 1) return sps_bool(false);
    return sps_bool(g_engine.was_just_released(text_of(args[0])));
}

static SpsValue native_mouse_x(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)args;
    (void)user;
    return sps_float(g_engine.mouse_x());
}

static SpsValue native_mouse_y(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)args;
    (void)user;
    return sps_float(g_engine.mouse_y());
}

static SpsValue native_mouse_down(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    int button = argc > 0 ? (int)sps_value_to_int(args[0]) : 0;
    return sps_bool(g_engine.mouse_down(button));
}

static SpsValue native_gamepad_down(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_bool(false);
    return sps_bool(g_engine.gamepad_down((int)sps_value_to_int(args[0]),
                                         (int)sps_value_to_int(args[1])));
}

static SpsValue native_gamepad_axis(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_float(0.0);
    return sps_float(g_engine.gamepad_axis((int)sps_value_to_int(args[0]),
                                           (int)sps_value_to_int(args[1])));
}

static SpsValue native_delta_time(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)args;
    (void)user;
    return sps_float(g_engine.delta_time());
}

static SpsValue native_time(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)args;
    (void)user;
    return sps_float(g_engine.time());
}

static SpsValue native_play_sound(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 1) return sps_nil();
    float volume = argc > 1 ? (float)sps_value_to_float(args[1]) : 1.0f;
    float pitch = argc > 2 ? (float)sps_value_to_float(args[2]) : 1.0f;
    g_engine.play_sound(text_of(args[0]), volume, pitch);
    return sps_nil();
}

static SpsValue native_stop_sound(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 1) return sps_nil();
    g_engine.stop_sound(text_of(args[0]));
    return sps_nil();
}

static SpsValue native_sound_playing(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 1) return sps_bool(false);
    return sps_bool(g_engine.sound_playing(text_of(args[0])));
}

static SpsValue native_set_volume(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_nil();
    g_engine.set_volume(text_of(args[0]), (float)sps_value_to_float(args[1]));
    return sps_nil();
}

static SpsValue native_vec3(SpsContext* context, int argc, const SpsValue* args, void* user) {
    return vec3_constructor(context, argc, args, user);
}

static SpsValue native_color(SpsContext* context, int argc, const SpsValue* args, void* user) {
    return color_constructor(context, argc, args, user);
}

static SpsValue native_vec3_add(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_nil();
    return make_vec3(to_vec3(args[0]) + to_vec3(args[1]));
}

static SpsValue native_vec3_sub(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_nil();
    return make_vec3(to_vec3(args[0]) - to_vec3(args[1]));
}

static SpsValue native_vec3_mul(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_nil();
    if (sps_value_type(args[1]) == SPS_FLOAT || sps_value_type(args[1]) == SPS_INT) {
        return make_vec3(to_vec3(args[0]) * (float)sps_value_to_float(args[1]));
    }
    return make_vec3(to_vec3(args[0]) * to_vec3(args[1]));
}

static SpsValue native_vec3_dot(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_float(0.0);
    return sps_float(engine::Vec3::dot(to_vec3(args[0]), to_vec3(args[1])));
}

static SpsValue native_vec3_cross(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_nil();
    return make_vec3(engine::Vec3::cross(to_vec3(args[0]), to_vec3(args[1])));
}

static SpsValue native_vec3_length(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 1) return sps_float(0.0);
    return sps_float(to_vec3(args[0]).length());
}

static SpsValue native_vec3_normalize(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 1) return sps_nil();
    return make_vec3(to_vec3(args[0]).normalized());
}

static SpsValue native_vec3_lerp(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 3) return sps_nil();
    return make_vec3(engine::Vec3::lerp(to_vec3(args[0]), to_vec3(args[1]),
                                        (float)sps_value_to_float(args[2])));
}

static SpsValue native_distance(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_float(0.0);
    return sps_float((to_vec3(args[0]) - to_vec3(args[1])).length());
}

static SpsValue native_clamp(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 3) return sps_float(0.0);
    double value = sps_value_to_float(args[0]);
    double low = sps_value_to_float(args[1]);
    double high = sps_value_to_float(args[2]);
    if (value < low) value = low;
    if (value > high) value = high;
    return sps_float(value);
}

static SpsValue native_load_material(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)user;
    if (argc < 1) return sps_string("");
    printf("[assets] the material %s is loaded\n", sps_value_to_text(context, args[0]));
    return sps_string(sps_value_to_text(context, args[0]));
}

static SpsValue native_load_texture(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)user;
    if (argc < 1) return sps_string("");
    printf("[assets] the texture %s is loaded\n", sps_value_to_text(context, args[0]));
    return sps_string(sps_value_to_text(context, args[0]));
}

static SpsValue native_load_model(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)user;
    if (argc < 1) return sps_nil();
    printf("[assets] the model %s is loaded\n", sps_value_to_text(context, args[0]));
    return sps_nil();
}

static SpsValue native_show_message(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)user;
    if (argc < 1) return sps_nil();
    printf("[ui] %s\n", sps_value_to_text(context, args[0]));
    return sps_nil();
}

// ------------------------------------------------------------------
// the shaders and the materials
// ------------------------------------------------------------------

static SpsHostClass* g_shader_class = nullptr;
static SpsHostClass* g_material_class = nullptr;

static engine::Shader* to_shader(SpsValue value) {
    return (engine::Shader*)sps_value_to_userdata(value);
}

static engine::Material* to_material(SpsValue value) {
    return (engine::Material*)sps_value_to_userdata(value);
}

static SpsValue shader_constructor(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    engine::Shader shader;
    if (argc > 0) shader.name = text_of(args[0]);
    g_engine.shaders_.push_back(shader);
    printf("[shader] the shader %s is compiled\n", g_engine.shaders_.back().name.c_str());
    return sps_host_class_new_object(g_shader_class, &g_engine.shaders_.back());
}

static SpsValue shader_get_name(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Shader* shader = to_shader(object);
    return sps_string(shader ? shader->name.c_str() : "");
}

static SpsValue shader_set_float(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_nil();
    engine::Shader* shader = to_shader(args[0]);
    if (shader) shader->floats[text_of(args[1])] = (float)sps_value_to_float(args[2]);
    return sps_nil();
}

static SpsValue shader_set_vec3(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_nil();
    engine::Shader* shader = to_shader(args[0]);
    if (shader) shader->vectors[text_of(args[1])] = to_vec3(args[2]);
    return sps_nil();
}

static SpsValue shader_set_int(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_nil();
    engine::Shader* shader = to_shader(args[0]);
    if (shader) shader->integers[text_of(args[1])] = (int)sps_value_to_int(args[2]);
    return sps_nil();
}

static SpsValue shader_set_bool(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_nil();
    engine::Shader* shader = to_shader(args[0]);
    if (shader) shader->integers[text_of(args[1])] = sps_value_to_bool(args[2]) ? 1 : 0;
    return sps_nil();
}

static SpsValue shader_set_texture(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_nil();
    engine::Shader* shader = to_shader(args[0]);
    if (shader) shader->textures[text_of(args[1])] = text_of(args[2]);
    return sps_nil();
}

static SpsValue material_constructor(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    engine::Material material;
    if (argc > 0) material.name = text_of(args[0]);
    g_engine.materials_.push_back(material);
    printf("[material] the material %s is loaded\n", g_engine.materials_.back().name.c_str());
    return sps_host_class_new_object(g_material_class, &g_engine.materials_.back());
}

static SpsValue material_get_name(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Material* material = to_material(object);
    return sps_string(material ? material->name.c_str() : "");
}

static SpsValue material_get_color(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Material* material = to_material(object);
    return make_color(material ? material->color : engine::Color());
}

static void material_set_color(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Material* material = to_material(object);
    if (material) material->color = to_color(value);
}

static SpsValue material_get_shininess(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Material* material = to_material(object);
    return sps_float(material ? material->shininess : 0.0);
}

static void material_set_shininess(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Material* material = to_material(object);
    if (material) material->shininess = (float)sps_value_to_float(value);
}

static SpsValue material_get_shader(SpsContext* context, SpsValue object, void* user) {
    (void)context;
    (void)user;
    engine::Material* material = to_material(object);
    if (!material || !material->shader) return sps_nil();
    return sps_host_class_new_object(g_shader_class, material->shader);
}

static void material_set_shader(SpsContext* context, SpsValue object, SpsValue value, void* user) {
    (void)context;
    (void)user;
    engine::Material* material = to_material(object);
    if (material) material->shader = to_shader(value);
}

// The uniforms of the material are sent to the shader.
static SpsValue material_set_float(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_nil();
    engine::Material* material = to_material(args[0]);
    if (material) material->uniforms[text_of(args[1])] = (float)sps_value_to_float(args[2]);
    return sps_nil();
}

static SpsValue material_set_vec3(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_nil();
    engine::Material* material = to_material(args[0]);
    if (material) {
        engine::Shader* shader = material->shader;
        if (shader) shader->vectors[text_of(args[1])] = to_vec3(args[2]);
    }
    return sps_nil();
}

static SpsValue material_set_texture(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_nil();
    engine::Material* material = to_material(args[0]);
    if (material && material->shader) {
        material->shader->textures[text_of(args[1])] = text_of(args[2]);
    }
    return sps_nil();
}

// ------------------------------------------------------------------
// the events, the timers and the physics
// ------------------------------------------------------------------

static SpsValue native_on(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_nil();
    g_engine.on(text_of(args[0]), text_of(args[1]));
    return sps_nil();
}

static SpsValue native_off(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_nil();
    g_engine.off(text_of(args[0]), text_of(args[1]));
    return sps_nil();
}

// emit(event, value) - the engine calls the functions of the script
static SpsValue native_emit(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)user;
    if (argc < 1) return sps_nil();
    const std::string event = text_of(args[0]);
    SpsValue value = argc > 1 ? args[1] : sps_nil();

    for (const std::string& function : g_engine.listeners(event)) {
        SpsValue hook = sps_context_get_global(context, function.c_str());
        if (hook.type != SPS_FUNCTION) continue;
        SpsValue result;
        sps_call(context, hook, 1, &value, &result);
    }
    return sps_nil();
}

static SpsValue native_after(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_int(0);
    return sps_int(g_engine.add_timer(text_of(args[1]), sps_value_to_float(args[0]), false));
}

static SpsValue native_every(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_int(0);
    return sps_int(g_engine.add_timer(text_of(args[1]), sps_value_to_float(args[0]), true));
}

static SpsValue native_cancel_timer(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 1) return sps_nil();
    g_engine.cancel_timer((int)sps_value_to_int(args[0]));
    return sps_nil();
}

// The hit of the raycast is a dictionary: entity, point, normal, distance.
static SpsValue native_raycast(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_nil();

    engine::Vec3 origin;
    engine::Vec3 direction;
    if (sps_value_to_host_class(args[0]) == g_vec3_class) {
        origin = to_vec3(args[0]);
        direction = to_vec3(args[1]);
    } else {
        origin = engine::Vec3((float)sps_value_to_float(args[0]), (float)sps_value_to_float(args[1]),
                              (float)sps_value_to_float(args[2]));
        direction = engine::Vec3((float)sps_value_to_float(args[3]), (float)sps_value_to_float(args[4]),
                                 (float)sps_value_to_float(args[5]));
    }
    float distance = argc > 2 ? (float)sps_value_to_float(args[2]) : 1000.0f;

    engine::RaycastHit hit = g_engine.raycast(origin, direction, distance);
    if (!hit.entity) return sps_nil();

    SpsValue result = sps_value_list(context, 0, nullptr);
    SpsValue entity = sps_host_class_new_object(g_entity_class, hit.entity);
    SpsValue point = make_vec3(hit.point);
    SpsValue normal = make_vec3(hit.normal);
    SpsValue distance_value = sps_float(hit.distance);

    SpsValue item = sps_value_list(context, 0, nullptr);
    sps_value_list_append(context, item, entity);
    sps_value_list_append(context, item, point);
    sps_value_list_append(context, item, normal);
    sps_value_list_append(context, item, distance_value);
    return item;
}

static SpsValue native_apply_impulse(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_nil();
    engine::Entity* entity = to_entity(args[0]);
    if (!entity) return sps_nil();
    engine::Vec3 impulse;
    if (sps_value_to_host_class(args[1]) == g_vec3_class) {
        impulse = to_vec3(args[1]);
    } else {
        impulse = engine::Vec3((float)sps_value_to_float(args[1]), (float)sps_value_to_float(args[2]),
                               (float)sps_value_to_float(args[3]));
    }
    for (engine::Rigidbody& body : g_engine.rigidbodies_) {
        if (body.entity == entity) {
            body.velocity = body.velocity + impulse * (1.0f / (body.mass > 0.0f ? body.mass : 1.0f));
        }
    }
    return sps_nil();
}

static SpsValue native_set_gravity(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 1) return sps_nil();
    g_engine.gravity_ = (float)sps_value_to_float(args[0]);
    return sps_nil();
}

static SpsValue native_get_gravity(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)args;
    (void)user;
    return sps_float(g_engine.gravity_);
}

// ------------------------------------------------------------------
// the entities
// ------------------------------------------------------------------

static SpsValue native_create_entity(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 1) return sps_nil();
    return sps_host_class_new_object(g_entity_class, g_engine.create_entity(text_of(args[0])));
}

static SpsValue native_clone(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 1) return sps_nil();
    engine::Entity* source = to_entity(args[0]);
    if (!source) return sps_nil();
    const std::string name = argc > 1 ? text_of(args[1]) : source->name + "_copy";
    return sps_host_class_new_object(g_entity_class, g_engine.clone_entity(source, name));
}

static SpsValue native_set_parent(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_nil();
    engine::Entity* child = to_entity(args[0]);
    engine::Entity* parent = to_entity(args[1]);
    if (!child || !parent) return sps_nil();
    child->parent = parent;
    parent->children.push_back(child);
    return sps_nil();
}

static SpsValue native_find_by_tag(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 1) return sps_nil();
    std::vector<engine::Entity*> found = g_engine.find_by_tag(text_of(args[0]));
    if (found.empty()) return sps_nil();
    return sps_host_class_new_object(g_entity_class, found.front());
}

static SpsValue native_find_all_by_tag(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)user;
    if (argc < 1) return sps_nil();
    SpsValue result = sps_value_list(context, 0, nullptr);
    for (engine::Entity* entity : g_engine.find_by_tag(text_of(args[0]))) {
        sps_value_list_append(context, result, sps_host_class_new_object(g_entity_class, entity));
    }
    return result;
}

// ------------------------------------------------------------------
// the screen, the animations and the system
// ------------------------------------------------------------------

static SpsValue native_screen_to_world(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_nil();
    return make_vec3(g_engine.screen_to_world((float)sps_value_to_float(args[0]),
                                             (float)sps_value_to_float(args[1])));
}

static SpsValue native_world_to_screen(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 1) return sps_nil();
    return make_vec3(g_engine.world_to_screen(to_vec3(args[0])));
}

static SpsValue native_set_cursor_visible(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 1) return sps_nil();
    g_engine.cursor_visible_ = sps_value_to_bool(args[0]);
    return sps_nil();
}

static SpsValue native_play_animation(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_nil();
    engine::Animation animation;
    animation.entity = to_entity(args[0]);
    animation.name = text_of(args[1]);
    animation.playing = true;
    animation.loop = argc > 2 && sps_value_to_bool(args[2]);
    g_engine.animations_.push_back(animation);
    printf("[anim] play %s\n", animation.name.c_str());
    return sps_nil();
}

static SpsValue native_stop_animation(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_nil();
    for (engine::Animation& animation : g_engine.animations_) {
        if (animation.entity == to_entity(args[0]) && animation.name == text_of(args[1])) {
            animation.playing = false;
        }
    }
    return sps_nil();
}

static SpsValue native_emit_particles(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 1) return sps_nil();
    printf("[fx] the particles %s are emitted\n", sps_value_to_text(context, args[0]));
    return sps_nil();
}

static SpsValue native_get_fps(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)args;
    (void)user;
    return sps_int(g_engine.fps());
}

static SpsValue native_frame(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)args;
    (void)user;
    return sps_int(g_engine.frame());
}

static SpsValue native_set_time_scale(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 1) return sps_nil();
    g_engine.set_time_scale((float)sps_value_to_float(args[0]));
    return sps_nil();
}

static SpsValue native_pause_game(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)args;
    (void)user;
    g_engine.set_paused(true);
    return sps_nil();
}

static SpsValue native_resume_game(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)args;
    (void)user;
    g_engine.set_paused(false);
    return sps_nil();
}

static SpsValue native_platform(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)argc;
    (void)args;
    (void)user;
    return sps_string("windows");
}

static SpsValue native_log_warning(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)user;
    if (argc < 1) return sps_nil();
    printf("[warning] %s\n", sps_value_to_text(context, args[0]));
    return sps_nil();
}

static SpsValue native_log_error(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)user;
    if (argc < 1) return sps_nil();
    fprintf(stderr, "[error] %s\n", sps_value_to_text(context, args[0]));
    return sps_nil();
}

// callFunction(name, ...) - the script calls another function of the script
static SpsValue native_call_function(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)user;
    if (argc < 1) return sps_nil();
    SpsValue function = sps_context_get_global(context, text_of(args[0]).c_str());
    if (function.type != SPS_FUNCTION) return sps_nil();
    SpsValue result;
    if (!sps_call(context, function, argc - 1, args + 1, &result)) return sps_nil();
    return result;
}

// ------------------------------------------------------------------
// the utilities
// ------------------------------------------------------------------

static SpsValue native_lerp(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 3) return sps_float(0.0);
    double a = sps_value_to_float(args[0]);
    double b = sps_value_to_float(args[1]);
    double t = sps_value_to_float(args[2]);
    return sps_float(a + (b - a) * t);
}

static SpsValue native_move_towards(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 3) return sps_float(0.0);
    double value = sps_value_to_float(args[0]);
    double target = sps_value_to_float(args[1]);
    double step = sps_value_to_float(args[2]);
    if (value < target) return sps_float(value + step < target ? value + step : target);
    if (value > target) return sps_float(value - step > target ? value - step : target);
    return sps_float(target);
}

static SpsValue native_wrap(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 3) return sps_float(0.0);
    double value = sps_value_to_float(args[0]);
    double low = sps_value_to_float(args[1]);
    double high = sps_value_to_float(args[2]);
    double range = high - low;
    if (range == 0.0) return sps_float(low);
    return sps_float(value - range * std::floor((value - low) / range));
}

static SpsValue native_sign(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 1) return sps_float(0.0);
    double value = sps_value_to_float(args[0]);
    return sps_float(value > 0.0 ? 1.0 : (value < 0.0 ? -1.0 : 0.0));
}

static SpsValue native_repeat(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    if (argc < 2) return sps_float(0.0);
    double value = sps_value_to_float(args[0]);
    double length = sps_value_to_float(args[1]);
    if (length == 0.0) return sps_float(0.0);
    return sps_float(value - length * std::floor(value / length));
}

static SpsValue native_shuffle(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)user;
    if (argc < 1 || sps_value_type(args[0]) != SPS_LIST) return sps_nil();
    return args[0];
}

static SpsValue native_random_point(SpsContext* context, int argc, const SpsValue* args, void* user) {
    (void)context;
    (void)user;
    float radius = argc > 0 ? (float)sps_value_to_float(args[0]) : 1.0f;
    float angle = (float)rand() / (float)RAND_MAX * 6.28318f;
    float distance = (float)rand() / (float)RAND_MAX * radius;
    return make_vec3(engine::Vec3(std::cos(angle) * distance, 0.0f, std::sin(angle) * distance));
}

// ------------------------------------------------------------------
// the hooks of the script
// ------------------------------------------------------------------

// Calls the function of the script by its name, if the script has it.
static bool call_hook(const char* name, int argc, const SpsValue* args) {
    SpsValue hook = sps_context_get_global(g_context, name);
    if (hook.type != SPS_FUNCTION) return false;
    SpsValue result;
    sps_call(g_context, hook, argc, args, &result);
    return true;
}

// The engine calls these when the scene lives.
static void run_frame() {
    SpsValue delta = sps_float(g_engine.delta_time());
    call_hook("on_update", 1, &delta);
    g_engine.advance(g_engine.delta_time());

    // the timers of the script
    for (auto it = g_engine.timers_.begin(); it != g_engine.timers_.end();) {
        it->remaining -= g_engine.delta_time();
        if (it->remaining > 0.0) {
            ++it;
            continue;
        }
        const std::string function = it->function;
        if (it->repeat) {
            it->remaining = it->interval;
            ++it;
        } else {
            it = g_engine.timers_.erase(it);
        }
        SpsValue hook = sps_context_get_global(g_context, function.c_str());
        if (hook.type == SPS_FUNCTION) {
            SpsValue result;
            sps_call(g_context, hook, 0, nullptr, &result);
        }
    }
}

// The engine checks the triggers and calls the script.
static void check_triggers() {
    for (engine::Entity* entity : g_engine.entities_) {
        if (!entity->active) continue;
        std::vector<engine::Trigger*> triggers = g_engine.overlapping(entity);
        for (engine::Trigger* trigger : triggers) {
            if (trigger->onEnter.empty()) continue;
            SpsValue hook = sps_context_get_global(g_context, trigger->onEnter.c_str());
            if (hook.type != SPS_FUNCTION) continue;
            SpsValue value = sps_host_class_new_object(g_entity_class, entity);
            SpsValue result;
            sps_call(g_context, hook, 1, &value, &result);
        }
    }
}

// ------------------------------------------------------------------
// the engine
// ------------------------------------------------------------------

static void on_error(SpsContext* context, const char* message, const char* stack, void* user) {
    (void)context;
    (void)stack;
    (void)user;
    fprintf(stderr, "[engine] the script failed: %s\n", message);
}

int main(int argc, char** argv) {
    const char* script = argc > 1 ? argv[1] : "examples/host/game.sps";

    g_context = sps_context_new();
    if (!g_context) {
        fprintf(stderr, "cannot create the context\n");
        return 1;
    }
    sps_context_set_error_handler(g_context, on_error, NULL);

    // --- the classes ---
    g_vec3_class = sps_host_class_new(g_context, "Vector3");
    sps_host_class_property(g_vec3_class, "x", vec3_get_x, vec3_set_x, NULL);
    sps_host_class_property(g_vec3_class, "y", vec3_get_y, vec3_set_y, NULL);
    sps_host_class_property(g_vec3_class, "z", vec3_get_z, vec3_set_z, NULL);

    g_color_class = sps_host_class_new(g_context, "Color");
    sps_host_class_property(g_color_class, "r", color_get_r, color_set_r, NULL);
    sps_host_class_property(g_color_class, "g", color_get_g, color_set_g, NULL);
    sps_host_class_property(g_color_class, "b", color_get_b, color_set_b, NULL);
    sps_host_class_property(g_color_class, "a", color_get_a, color_set_a, NULL);

    g_entity_class = sps_host_class_new(g_context, "Entity");
    sps_host_class_constructor(g_entity_class, entity_constructor, NULL);
    sps_host_class_method(g_entity_class, "move", entity_move, NULL);
    sps_host_class_method(g_entity_class, "rotate", entity_rotate, NULL);
    sps_host_class_method(g_entity_class, "scale", entity_scale, NULL);
    sps_host_class_method(g_entity_class, "lookAt", entity_look_at, NULL);
    sps_host_class_method(g_entity_class, "destroy", entity_destroy, NULL);
    sps_host_class_property(g_entity_class, "name", entity_get_name, entity_set_name, NULL);
    sps_host_class_property(g_entity_class, "position", entity_get_position, entity_set_position, NULL);
    sps_host_class_property(g_entity_class, "rotation", entity_get_rotation, entity_set_rotation, NULL);
    sps_host_class_property(g_entity_class, "size", entity_get_size, entity_set_size, NULL);
    sps_host_class_property(g_entity_class, "color", entity_get_color, entity_set_color, NULL);
    sps_host_class_property(g_entity_class, "material", entity_get_material, entity_set_material, NULL);
    sps_host_class_property(g_entity_class, "texture", entity_get_texture, entity_set_texture, NULL);
    sps_host_class_property(g_entity_class, "tag", entity_get_tag, entity_set_tag, NULL);
    sps_host_class_property(g_entity_class, "active", entity_get_active, entity_set_active, NULL);

    g_scene_class = sps_host_class_new(g_context, "Scene");
    sps_host_class_constructor(g_scene_class, scene_constructor, NULL);
    sps_host_class_method(g_scene_class, "find", scene_find, NULL);
    sps_host_class_method(g_scene_class, "count", scene_count, NULL);
    sps_host_class_property(g_scene_class, "name", scene_get_name, scene_set_name, NULL);
    sps_host_class_property(g_scene_class, "gravity", scene_get_gravity, scene_set_gravity, NULL);

    g_camera_class = sps_host_class_new(g_context, "Camera");
    sps_host_class_constructor(g_camera_class, camera_constructor, NULL);
    sps_host_class_method(g_camera_class, "lookAt", camera_look_at, NULL);
    sps_host_class_property(g_camera_class, "fov", camera_get_fov, camera_set_fov, NULL);
    sps_host_class_property(g_camera_class, "far", camera_get_far, camera_set_far, NULL);

    g_light_class = sps_host_class_new(g_context, "Light");
    sps_host_class_constructor(g_light_class, light_constructor, NULL);
    sps_host_class_property(g_light_class, "type", light_get_type, light_set_type, NULL);
    sps_host_class_property(g_light_class, "intensity", light_get_intensity, light_set_intensity, NULL);
    sps_host_class_property(g_light_class, "range", light_get_range, light_set_range, NULL);

    g_rigidbody_class = sps_host_class_new(g_context, "Rigidbody");
    sps_host_class_constructor(g_rigidbody_class, rigidbody_constructor, NULL);
    sps_host_class_method(g_rigidbody_class, "applyForce", rigidbody_apply_force, NULL);
    sps_host_class_method(g_rigidbody_class, "setVelocity", rigidbody_set_velocity, NULL);
    sps_host_class_property(g_rigidbody_class, "velocity", rigidbody_get_velocity,
                             rigidbody_set_velocity, NULL);
    sps_host_class_property(g_rigidbody_class, "mass", rigidbody_get_mass, rigidbody_set_mass, NULL);
    sps_host_class_property(g_rigidbody_class, "gravity", rigidbody_get_gravity,
                             rigidbody_set_gravity, NULL);
    sps_host_class_property(g_rigidbody_class, "kinematic", rigidbody_get_kinematic,
                             rigidbody_set_kinematic, NULL);

    g_collider_class = sps_host_class_new(g_context, "Collider");
    sps_host_class_constructor(g_collider_class, collider_constructor, NULL);
    sps_host_class_property(g_collider_class, "shape", collider_get_shape, collider_set_shape, NULL);
    sps_host_class_property(g_collider_class, "size", collider_get_size, collider_set_size, NULL);
    sps_host_class_property(g_collider_class, "radius", collider_get_radius,
                             collider_set_radius, NULL);
    sps_host_class_property(g_collider_class, "isTrigger", collider_get_is_trigger,
                             collider_set_is_trigger, NULL);

    g_trigger_class = sps_host_class_new(g_context, "Trigger");
    sps_host_class_constructor(g_trigger_class, trigger_constructor, NULL);
    sps_host_class_property(g_trigger_class, "size", trigger_get_size, trigger_set_size, NULL);
    sps_host_class_property(g_trigger_class, "onEnter", trigger_get_on_enter,
                             trigger_set_on_enter, NULL);
    sps_host_class_property(g_trigger_class, "onExit", trigger_get_on_exit,
                             trigger_set_on_exit, NULL);

    g_audio_source_class = sps_host_class_new(g_context, "AudioSource");
    sps_host_class_constructor(g_audio_source_class, audio_source_constructor, NULL);
    sps_host_class_method(g_audio_source_class, "play", audio_source_play, NULL);
    sps_host_class_method(g_audio_source_class, "stop", audio_source_stop, NULL);
    sps_host_class_method(g_audio_source_class, "isPlaying", audio_source_is_playing, NULL);
    sps_host_class_property(g_audio_source_class, "sound", audio_source_get_sound,
                             audio_source_set_sound, NULL);
    sps_host_class_property(g_audio_source_class, "volume", audio_source_get_volume,
                             audio_source_set_volume, NULL);
    sps_host_class_property(g_audio_source_class, "pitch", audio_source_get_pitch,
                             audio_source_set_pitch, NULL);
    sps_host_class_property(g_audio_source_class, "loop", audio_source_get_loop,
                             audio_source_set_loop, NULL);

    g_shader_class = sps_host_class_new(g_context, "Shader");
    sps_host_class_constructor(g_shader_class, shader_constructor, NULL);
    sps_host_class_method(g_shader_class, "setFloat", shader_set_float, NULL);
    sps_host_class_method(g_shader_class, "setVec3", shader_set_vec3, NULL);
    sps_host_class_method(g_shader_class, "setInt", shader_set_int, NULL);
    sps_host_class_method(g_shader_class, "setBool", shader_set_bool, NULL);
    sps_host_class_method(g_shader_class, "setTexture", shader_set_texture, NULL);
    sps_host_class_property(g_shader_class, "name", shader_get_name, NULL, NULL);

    g_material_class = sps_host_class_new(g_context, "Material");
    sps_host_class_constructor(g_material_class, material_constructor, NULL);
    sps_host_class_method(g_material_class, "setFloat", material_set_float, NULL);
    sps_host_class_method(g_material_class, "setVec3", material_set_vec3, NULL);
    sps_host_class_method(g_material_class, "setTexture", material_set_texture, NULL);
    sps_host_class_property(g_material_class, "name", material_get_name, NULL, NULL);
    sps_host_class_property(g_material_class, "color", material_get_color, material_set_color, NULL);
    sps_host_class_property(g_material_class, "shininess", material_get_shininess,
                             material_set_shininess, NULL);
    sps_host_class_property(g_material_class, "shader", material_get_shader, material_set_shader, NULL);

    g_label_class = sps_host_class_new(g_context, "Label");
    sps_host_class_constructor(g_label_class, label_constructor, NULL);
    sps_host_class_property(g_label_class, "text", label_get_text, label_set_text, NULL);
    sps_host_class_property(g_label_class, "position", label_get_position, label_set_position, NULL);

    g_button_class = sps_host_class_new(g_context, "Button");
    sps_host_class_constructor(g_button_class, button_constructor, NULL);
    sps_host_class_property(g_button_class, "text", button_get_text, button_set_text, NULL);
    sps_host_class_property(g_button_class, "onClick", button_get_on_click, button_set_on_click, NULL);

    // --- the functions ---
    sps_register_function(g_context, "log", native_log, NULL);
    sps_register_function(g_context, "downKey", native_down_key, NULL);
    sps_register_function(g_context, "justPressed", native_just_pressed, NULL);
    sps_register_function(g_context, "justReleased", native_just_released, NULL);
    sps_register_function(g_context, "mouseX", native_mouse_x, NULL);
    sps_register_function(g_context, "mouseY", native_mouse_y, NULL);
    sps_register_function(g_context, "mouseDown", native_mouse_down, NULL);
    sps_register_function(g_context, "gamepadDown", native_gamepad_down, NULL);
    sps_register_function(g_context, "gamepadAxis", native_gamepad_axis, NULL);
    sps_register_function(g_context, "deltaTime", native_delta_time, NULL);
    sps_register_function(g_context, "time", native_time, NULL);
    sps_register_function(g_context, "playSound", native_play_sound, NULL);
    sps_register_function(g_context, "stopSound", native_stop_sound, NULL);
    sps_register_function(g_context, "soundPlaying", native_sound_playing, NULL);
    sps_register_function(g_context, "setVolume", native_set_volume, NULL);
    sps_register_function(g_context, "vec3", native_vec3, NULL);
    sps_register_function(g_context, "color", native_color, NULL);
    sps_register_function(g_context, "vec3.add", native_vec3_add, NULL);
    sps_register_function(g_context, "vec3.sub", native_vec3_sub, NULL);
    sps_register_function(g_context, "vec3.mul", native_vec3_mul, NULL);
    sps_register_function(g_context, "vec3.dot", native_vec3_dot, NULL);
    sps_register_function(g_context, "vec3.cross", native_vec3_cross, NULL);
    sps_register_function(g_context, "vec3.length", native_vec3_length, NULL);
    sps_register_function(g_context, "vec3.normalize", native_vec3_normalize, NULL);
    sps_register_function(g_context, "vec3.lerp", native_vec3_lerp, NULL);
    sps_register_function(g_context, "distance", native_distance, NULL);
    sps_register_function(g_context, "clamp", native_clamp, NULL);
    sps_register_function(g_context, "loadMaterial", native_load_material, NULL);
    sps_register_function(g_context, "loadTexture", native_load_texture, NULL);
    sps_register_function(g_context, "loadModel", native_load_model, NULL);
    sps_register_function(g_context, "showMessage", native_show_message, NULL);

    // the events, the timers and the physics
    sps_register_function(g_context, "on", native_on, NULL);
    sps_register_function(g_context, "off", native_off, NULL);
    sps_register_function(g_context, "emit", native_emit, NULL);
    sps_register_function(g_context, "after", native_after, NULL);
    sps_register_function(g_context, "every", native_every, NULL);
    sps_register_function(g_context, "cancelTimer", native_cancel_timer, NULL);
    sps_register_function(g_context, "raycast", native_raycast, NULL);
    sps_register_function(g_context, "applyImpulse", native_apply_impulse, NULL);
    sps_register_function(g_context, "setGravity", native_set_gravity, NULL);
    sps_register_function(g_context, "getGravity", native_get_gravity, NULL);

    // the entities
    sps_register_function(g_context, "createEntity", native_create_entity, NULL);
    sps_register_function(g_context, "clone", native_clone, NULL);
    sps_register_function(g_context, "setParent", native_set_parent, NULL);
    sps_register_function(g_context, "findByTag", native_find_by_tag, NULL);
    sps_register_function(g_context, "findAllByTag", native_find_all_by_tag, NULL);

    // the screen, the animations and the system
    sps_register_function(g_context, "screenToWorld", native_screen_to_world, NULL);
    sps_register_function(g_context, "worldToScreen", native_world_to_screen, NULL);
    sps_register_function(g_context, "setCursorVisible", native_set_cursor_visible, NULL);
    sps_register_function(g_context, "playAnimation", native_play_animation, NULL);
    sps_register_function(g_context, "stopAnimation", native_stop_animation, NULL);
    sps_register_function(g_context, "emitParticles", native_emit_particles, NULL);
    sps_register_function(g_context, "getFPS", native_get_fps, NULL);
    sps_register_function(g_context, "frame", native_frame, NULL);
    sps_register_function(g_context, "setTimeScale", native_set_time_scale, NULL);
    sps_register_function(g_context, "pauseGame", native_pause_game, NULL);
    sps_register_function(g_context, "resumeGame", native_resume_game, NULL);
    sps_register_function(g_context, "platform", native_platform, NULL);
    sps_register_function(g_context, "logWarning", native_log_warning, NULL);
    sps_register_function(g_context, "logError", native_log_error, NULL);
    sps_register_function(g_context, "callFunction", native_call_function, NULL);

    // the utilities
    sps_register_function(g_context, "lerp", native_lerp, NULL);
    sps_register_function(g_context, "moveTowards", native_move_towards, NULL);
    sps_register_function(g_context, "wrap", native_wrap, NULL);
    sps_register_function(g_context, "sign", native_sign, NULL);
    sps_register_function(g_context, "repeat", native_repeat, NULL);
    sps_register_function(g_context, "shuffle", native_shuffle, NULL);
    sps_register_function(g_context, "randomPoint", native_random_point, NULL);

    // --- the script ---
    bool ok = sps_context_exec_file(g_context, script);
    if (!ok) {
        fprintf(stderr, "[engine] the script was not finished: %s\n",
                sps_context_last_error(g_context));
    }

    // --- the life of the scene ---
    call_hook("on_start", 0, nullptr);
    g_engine.set_mouse(420.0f, 310.0f);
    g_engine.set_mouse_button(0, true);

    for (int frame = 0; frame < 40; frame++) {
        // the space is pressed only in the first frame
        if (frame == 0) g_engine.press("space");
        else g_engine.release("space");
        run_frame();
    }
    check_triggers();

    SpsValue result;
    if (sps_call(g_context, sps_context_get_global(g_context, "start"), 0, nullptr, &result)) {
        printf("[engine] the script returned: %s\n", sps_value_to_text(g_context, result));
    }

    sps_context_free(g_context);
    return ok ? 0 : 1;
}
