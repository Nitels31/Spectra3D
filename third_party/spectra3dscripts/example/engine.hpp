// Spectra3D Script - the state of the example engine
//
// A tiny engine that shows how the API of the language is used from C++.
// Everything here is the "engine side" of the API: the entities, the input,
// the sounds, the triggers, the events, the timers.

#ifndef SPS_EXAMPLE_ENGINE_H
#define SPS_EXAMPLE_ENGINE_H

#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace engine {

// ------------------------------------------------------------------
// the math
// ------------------------------------------------------------------

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vec3() = default;
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3& other) const { return Vec3(x + other.x, y + other.y, z + other.z); }
    Vec3 operator-(const Vec3& other) const { return Vec3(x - other.x, y - other.y, z - other.z); }
    Vec3 operator*(const Vec3& other) const { return Vec3(x * other.x, y * other.y, z * other.z); }
    Vec3 operator*(float scale) const { return Vec3(x * scale, y * scale, z * scale); }

    float length() const { return std::sqrt(x * x + y * y + z * z); }

    Vec3 normalized() const {
        float size = length();
        if (size < 0.0001f) return Vec3();
        return Vec3(x / size, y / size, z / size);
    }

    static float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

    static Vec3 cross(const Vec3& a, const Vec3& b) {
        return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
    }

    static Vec3 lerp(const Vec3& a, const Vec3& b, float t) {
        return Vec3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
    }
};

struct Color {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    Color() = default;
    Color(float r, float g, float b, float a) : r(r), g(g), b(b), a(a) {}
};

struct Quaternion {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};

// ------------------------------------------------------------------
// the objects of the engine
// ------------------------------------------------------------------

struct Entity {
    std::string name;
    Vec3        position;
    Vec3        rotation;
    Vec3        size{1.0f, 1.0f, 1.0f};
    Color       color;
    std::string material;
    std::string texture;
    std::string tag;
    bool        active = true;
    Entity*     parent = nullptr;
    std::vector<Entity*> children;
};

struct Rigidbody {
    Entity*     entity = nullptr;
    float       mass = 1.0f;
    float       gravity = 9.81f;
    bool        kinematic = false;
    Vec3        velocity;
    Vec3        force;
};

struct Collider {
    Entity*     entity = nullptr;
    std::string shape = "box";   // box, sphere, capsule
    Vec3        size{1.0f, 1.0f, 1.0f};
    float       radius = 0.5f;
    float       height = 1.0f;
    bool        isTrigger = false;
};

struct Trigger {
    Entity*     entity = nullptr;
    Vec3        size{1.0f, 1.0f, 1.0f};
    bool        isTrigger = true;
    std::string onEnter;   // the name of the function of the script
    std::string onExit;
    std::string onOverlap;
};

struct AudioSource {
    std::string sound;
    float       volume = 1.0f;
    float       pitch = 1.0f;
    bool        loop = false;
    bool        playing = false;
    Vec3        position;
};

struct Camera {
    Entity*     entity = nullptr;
    float       fov = 60.0f;
    float       near = 0.1f;
    float       far = 1000.0f;
    bool        orthographic = false;
};

struct Light {
    Entity*     entity = nullptr;
    std::string type = "directional";  // directional, point, spot
    Color       color;
    float       intensity = 1.0f;
    float       range = 10.0f;
    float       angle = 45.0f;
};

struct Label {
    std::string text;
    Vec3        position;
    Color       color;
    float       size = 16.0f;
};

struct Button {
    std::string text;
    Vec3        position;
    Color       color;
    std::string onClick;
    bool        enabled = true;
};

struct Sound {
    float volume = 1.0f;
    float pitch = 1.0f;
    bool  playing = false;
};

struct Animation {
    Entity*     entity = nullptr;
    std::string name;
    bool        playing = false;
    bool        loop = false;
    float       speed = 1.0f;
};

// The shader and its uniforms.
struct Shader {
    std::string name;
    std::map<std::string, float>       floats;
    std::map<std::string, Vec3>        vectors;
    std::map<std::string, int>         integers;
    std::map<std::string, std::string> textures;
};

// The material of an object.
struct Material {
    std::string name;
    Color       color;
    float       shininess = 0.0f;
    Shader*     shader = nullptr;
    std::map<std::string, float> uniforms;   // the uniforms of the material
};

struct RaycastHit {
    Entity*     entity = nullptr;
    Vec3        point;
    Vec3        normal;
    float       distance = 0.0f;
};

// The timer of the script: "after" and "every".
struct Timer {
    int         id = 0;
    std::string function;
    double      remaining = 0.0;
    double      interval = 0.0;
    bool        repeat = false;
};

// ------------------------------------------------------------------
// the engine
// ------------------------------------------------------------------

class Engine {
public:
    Engine() = default;

    // --- the scene -----------------------------------------------------

    Entity* create_entity(const std::string& name) {
        Entity* entity = new Entity();
        entity->name = name;
        entities_.push_back(entity);
        return entity;
    }

    Entity* find_entity(const std::string& name) {
        for (Entity* entity : entities_) {
            if (entity->name == name) return entity;
        }
        return nullptr;
    }

    std::vector<Entity*> find_by_tag(const std::string& tag) {
        std::vector<Entity*> result;
        for (Entity* entity : entities_) {
            if (entity->tag == tag) result.push_back(entity);
        }
        return result;
    }

    Entity* clone_entity(Entity* source, const std::string& name) {
        Entity* copy = new Entity(*source);
        copy->name = name;
        copy->parent = nullptr;
        copy->children.clear();
        entities_.push_back(copy);
        return copy;
    }

    // --- the input -----------------------------------------------------

    void press(const std::string& key) { pressed_keys_.insert(key); }
    void release(const std::string& key) { pressed_keys_.erase(key); }
    bool down_key(const std::string& key) const { return pressed_keys_.count(key) > 0; }

    void just_press(const std::string& key) { just_pressed_.insert(key); }
    void just_release(const std::string& key) { just_released_.insert(key); }
    bool was_just_pressed(const std::string& key) const { return just_pressed_.count(key) > 0; }
    bool was_just_released(const std::string& key) const { return just_released_.count(key) > 0; }

    float mouse_x() const { return mouse_x_; }
    float mouse_y() const { return mouse_y_; }
    void set_mouse(float x, float y) { mouse_x_ = x; mouse_y_ = y; }
    bool mouse_down(int button) const { return mouse_buttons_[button & 1]; }
    void set_mouse_button(int button, bool down) { mouse_buttons_[button & 1] = down; }

    bool gamepad_down(int pad, int button) const {
        return gamepad_buttons_.count(std::make_pair(pad, button)) > 0;
    }
    float gamepad_axis(int pad, int axis) const {
        auto found = gamepad_axes_.find(std::make_pair(pad, axis));
        return found == gamepad_axes_.end() ? 0.0f : found->second;
    }

    // --- the sounds ----------------------------------------------------

    void play_sound(const std::string& name, float volume, float pitch) {
        Sound& sound = sounds_[name];
        sound.volume = volume;
        sound.pitch = pitch;
        sound.playing = true;
        printf("[audio] play %s (volume %.2f, pitch %.2f)\n", name.c_str(), volume, pitch);
    }

    void stop_sound(const std::string& name) {
        auto found = sounds_.find(name);
        if (found != sounds_.end()) found->second.playing = false;
        printf("[audio] stop %s\n", name.c_str());
    }

    bool sound_playing(const std::string& name) const {
        auto found = sounds_.find(name);
        return found != sounds_.end() && found->second.playing;
    }

    void set_volume(const std::string& name, float volume) {
        sounds_[name].volume = volume;
    }

    // --- the events ----------------------------------------------------

    void on(const std::string& event, const std::string& function) {
        handlers_[event].push_back(function);
    }

    void off(const std::string& event, const std::string& function) {
        auto found = handlers_.find(event);
        if (found == handlers_.end()) return;
        std::vector<std::string>& list = found->second;
        for (size_t i = 0; i < list.size(); i++) {
            if (list[i] == function) {
                list.erase(list.begin() + static_cast<long>(i));
                return;
            }
        }
    }

    std::vector<std::string> listeners(const std::string& event) {
        auto found = handlers_.find(event);
        if (found == handlers_.end()) return {};
        return found->second;
    }

    // --- the timers ----------------------------------------------------

    int add_timer(const std::string& function, double seconds, bool repeat) {
        Timer timer;
        timer.id = ++timer_id_;
        timer.function = function;
        timer.remaining = seconds;
        timer.interval = seconds;
        timer.repeat = repeat;
        timers_.push_back(timer);
        return timer.id;
    }

    void cancel_timer(int id) {
        for (size_t i = 0; i < timers_.size(); i++) {
            if (timers_[i].id == id) {
                timers_.erase(timers_.begin() + static_cast<long>(i));
                return;
            }
        }
    }

    // --- the time ------------------------------------------------------

    float delta_time() const { return delta_time_; }
    void set_delta_time(float value) { delta_time_ = value; }
    double time() const { return time_; }
    void advance(float delta) { time_ += delta; frame_++; }
    int frame() const { return frame_; }
    float time_scale() const { return time_scale_; }
    void set_time_scale(float scale) { time_scale_ = scale; }
    bool paused() const { return paused_; }
    void set_paused(bool paused) { paused_ = paused; }
    int fps() const { return fps_; }
    void set_fps(int fps) { fps_ = fps; }

    // --- the triggers --------------------------------------------------

    std::vector<Trigger*> overlapping(const Entity* entity) {
        std::vector<Trigger*> result;
        for (Trigger& trigger : triggers_) {
            if (!trigger.isTrigger) continue;
            if (inside(trigger, *entity)) result.push_back(&trigger);
        }
        return result;
    }

    static bool inside(const Trigger& trigger, const Entity& entity) {
        Vec3 delta = entity.position - trigger.entity->position;
        Vec3 half = trigger.size * 0.5f;
        return std::fabs(delta.x) <= half.x && std::fabs(delta.y) <= half.y &&
               std::fabs(delta.z) <= half.z;
    }

    // --- the physics ---------------------------------------------------

    // The ray hits the box of the entity. It is a simple test of the slab
    // method, enough for the example.
    RaycastHit raycast(const Vec3& origin, const Vec3& direction, float max_distance) {
        RaycastHit hit;
        Vec3 dir = direction.normalized();
        if (dir.length() < 0.5f) return hit;

        float best = max_distance;
        for (Entity* entity : entities_) {
            if (!entity->active) continue;
            Vec3 half = entity->size * 0.5f;
            float tmin = 0.0f;
            float tmax = best;
            bool miss = false;

            const float origin_axis[3] = { origin.x, origin.y, origin.z };
            const float dir_axis[3] = { dir.x, dir.y, dir.z };
            const float center_axis[3] = { entity->position.x, entity->position.y, entity->position.z };
            const float half_axis[3] = { half.x, half.y, half.z };

            for (int axis = 0; axis < 3; axis++) {
                if (std::fabs(dir_axis[axis]) < 0.0001f) {
                    if (std::fabs(origin_axis[axis] - center_axis[axis]) > half_axis[axis]) {
                        miss = true;
                        break;
                    }
                    continue;
                }
                float inverse = 1.0f / dir_axis[axis];
                float t1 = (center_axis[axis] - half_axis[axis] - origin_axis[axis]) * inverse;
                float t2 = (center_axis[axis] + half_axis[axis] - origin_axis[axis]) * inverse;
                if (t1 > t2) {
                    float swap = t1;
                    t1 = t2;
                    t2 = swap;
                }
                if (t1 > tmin) tmin = t1;
                if (t2 < tmax) tmax = t2;
                if (tmin > tmax) {
                    miss = true;
                    break;
                }
            }
            if (miss || tmin >= best) continue;
            best = tmin;
            hit.entity = entity;
            hit.distance = tmin;
            hit.point = origin + dir * tmin;
        }
        return hit;
    }

    // --- the screen ----------------------------------------------------

    Vec3 screen_to_world(float x, float y) {
        // the example camera looks at the origin from (0, 5, 10)
        float nx = (x - 400.0f) / 400.0f;
        float ny = (300.0f - y) / 300.0f;
        return Vec3(nx * 10.0f, ny * 10.0f, 0.0f);
    }

    Vec3 world_to_screen(const Vec3& world) {
        return Vec3(world.x * 40.0f + 400.0f, 300.0f - world.y * 30.0f, 0.0f);
    }

    // --- the storage ---------------------------------------------------

    std::vector<Entity*>     entities_;
    std::vector<Rigidbody>   rigidbodies_;
    std::vector<Collider>    colliders_;
    std::vector<Trigger>     triggers_;
    std::vector<AudioSource> sources_;
    std::vector<Camera>      cameras_;
    std::vector<Light>       lights_;
    std::vector<Label>       labels_;
    std::vector<Button>      buttons_;
    std::vector<Animation>   animations_;
    std::vector<Shader>      shaders_;
    std::vector<Material>    materials_;
    std::vector<Timer>       timers_;
    std::map<std::string, Sound> sounds_;
    std::map<std::string, std::vector<std::string>> handlers_;

    std::set<std::string> pressed_keys_;
    std::set<std::string> just_pressed_;
    std::set<std::string> just_released_;
    bool        mouse_buttons_[2] = { false, false };
    float       mouse_x_ = 0.0f;
    float       mouse_y_ = 0.0f;
    std::map<std::pair<int, int>, bool> gamepad_buttons_;
    std::map<std::pair<int, int>, float> gamepad_axes_;
    float       gravity_ = 9.81f;
    float       delta_time_ = 0.016f;
    float       time_scale_ = 1.0f;
    double      time_ = 0.0;
    int         frame_ = 0;
    int         timer_id_ = 0;
    int         fps_ = 60;
    bool        paused_ = false;
    bool        cursor_visible_ = true;
};

}   // namespace engine

#endif  // SPS_EXAMPLE_ENGINE_H
