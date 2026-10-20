#include "scene/SceneSerializer.hpp"

#include "assets/MeshImport.hpp"
#include "core/Log.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"

#include <json/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string_view>
#include <vector>

namespace s3d {
namespace scenes {
namespace {

constexpr const char* kNoMesh = "-";

// Restores the per-shape dimension fields that are only stored as half extents.
void syncShapeDimensions(RigidBody& body) {
    switch (body.shape) {
        case Shape::Cylinder:
            body.cylinderRadius = body.halfExtents.x;
            break;
        case Shape::Capsule:
            body.capsuleRadius = body.halfExtents.x;
            body.capsuleHeight = body.halfExtents.y;
            break;
        case Shape::Cone:
            body.coneRadius = body.halfExtents.x;
            body.coneHeight = body.halfExtents.y * 2.0f;
            break;
        case Shape::Pyramid:
            body.pyramidBaseRadius = body.halfExtents.x;
            body.pyramidHeight = body.halfExtents.y * 2.0f;
            break;
        case Shape::Prism:
            body.prismRadius = body.halfExtents.x;
            body.prismHeight = body.halfExtents.y * 2.0f;
            break;
        default: break;
    }
}

// --- reading ----------------------------------------------------------------

// Reads the whole file in one call.
bool readWholeFile(const std::string& path, std::string& text, size_t& size) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.seekg(0, std::ios::end);
    const std::streamoff end = file.tellg();
    if (end <= 0) {
        size = 0;
        text.assign(1, '\0');
        return true;
    }
    file.seekg(0, std::ios::beg);
    text.resize((size_t)end);
    file.read(&text[0], end);
    if (file.gcount() < end) text.resize((size_t)file.gcount());
    size = text.size();
    text.resize(size + 1);
    text[size] = '\0';
    return true;
}

// Mirrors str::join() into an existing buffer so resolveMesh() stops allocating
// for every candidate path it probes.
void appendJoined(std::string& out, const std::string& dir, const std::string& name) {
    out.clear();
    if (dir.empty()) {
        out += name;
        return;
    }
    if (name.empty()) {
        out += dir;
        return;
    }
    out += dir;
    const char last = dir[dir.size() - 1];
    if (last != '/' && last != '\\') out += '/';
    out += name;
}

} // namespace

// The file a model name is in. The asset root only changes while the working
// directory is being adopted, so the models directory is resolved once and
// reused for every body.
std::string findMeshFile(const std::string& name) {
    static std::string cachedRoot;
    static std::string modelsDir;
    const std::string& root = fs::assetRoot();
    if (cachedRoot != root) {
        cachedRoot = root;
        modelsDir = str::join(root, "models");
    }

    std::string bundledPath;
    std::string localPath;
    bundledPath.reserve(modelsDir.size() + name.size() + 8);
    localPath.reserve(name.size() + 8);
    for (const std::string& ext : meshExtensions()) {
        appendJoined(bundledPath, modelsDir, name);
        bundledPath += ext;
        if (fs::exists(bundledPath)) return bundledPath;

        localPath.clear();
        localPath += name;
        localPath += ext;
        if (fs::exists(localPath)) return localPath;
    }
    return std::string();
}

int resolveMesh(MeshRegistry& meshes, const std::string& name) {
    const int cached = meshes.find(name);
    if (cached >= 0) return cached;

    // Probing several extensions is expected to miss, so the attempts stay
    // quiet and the caller reports one warning if every candidate failed.
    const std::string path = findMeshFile(name);
    if (path.empty()) return -1;
    return meshes.importFile(path, /*quiet=*/true);
}

void preloadMeshes(MeshRegistry& meshes, const std::vector<std::string>& names, int threads) {
    if (names.empty()) return;

    // The names become files first: the probing is a listing of the disk, and
    // it is cheap next to the parse.
    std::vector<std::string> paths;
    paths.reserve(names.size());
    for (const std::string& name : names) {
        if (meshes.find(name) >= 0) continue;
        const std::string path = findMeshFile(name);
        if (path.empty()) continue;
        paths.push_back(path);
    }

    // And the files are parsed in parallel: that is the slow part of opening a
    // project and it touches no GL.
    meshes.importFiles(paths, threads > 0 ? threads : meshes.loaderThreads());
}

// --- JSON --------------------------------------------------------------------
//
// The same scene, said in a shape a person can read. Every field is named, the
// shapes are named rather than numbered, and the numbers are in the places a
// person expects them: a position is three numbers in an array, a quaternion
// is four.
//
// The text format stays the default: it is smaller, it diffs cleanly, and it
// needs no dependency to read. The JSON is for a scene that is meant to be
// read, written by hand, or read by something that is not this engine.

namespace {

// A shape as the JSON names it. Lower case, because that is how a person would
// write it, and because it is the shape of the identifier rather than of a
// class.
const char* jsonShapeName(Shape shape) {
    switch (shape) {
        case Shape::Box: return "box";
        case Shape::Sphere: return "sphere";
        case Shape::Cylinder: return "cylinder";
        case Shape::Capsule: return "capsule";
        case Shape::Cone: return "cone";
        case Shape::Pyramid: return "pyramid";
        case Shape::Prism: return "prism";
        case Shape::Mesh: return "mesh";
        case Shape::Camera: return "camera";
    }
    return "box";
}

// The other direction. A scene can name a shape this build does not have - a
// file written by a newer engine, or a typo - and the body is skipped rather
// than guessed at.
bool jsonShapeFromName(const std::string& name, Shape& out) {
    struct Entry {
        const char* name;
        Shape shape;
    };
    static const Entry kEntries[] = {
        {"box", Shape::Box},         {"sphere", Shape::Sphere},
        {"cylinder", Shape::Cylinder}, {"capsule", Shape::Capsule},
        {"cone", Shape::Cone},       {"pyramid", Shape::Pyramid},
        {"prism", Shape::Prism},     {"mesh", Shape::Mesh},
        {"camera", Shape::Camera},
    };
    for (const Entry& entry : kEntries)
        if (str::equalsIgnoreAscii(entry.name, name)) {
            out = entry.shape;
            return true;
        }
    return false;
}

// The gravity axis as the JSON names it.
const char* jsonGravityName(GravityAxis axis) {
    switch (axis) {
        case GravityAxis::NegX: return "negX";
        case GravityAxis::PosX: return "posX";
        case GravityAxis::NegY: return "negY";
        case GravityAxis::PosY: return "posY";
        case GravityAxis::NegZ: return "negZ";
        case GravityAxis::PosZ: return "posZ";
    }
    return "negY";
}

bool jsonGravityFromName(const std::string& name, GravityAxis& out) {
    struct Entry {
        const char* name;
        GravityAxis axis;
    };
    static const Entry kEntries[] = {
        {"negX", GravityAxis::NegX}, {"posX", GravityAxis::PosX},
        {"negY", GravityAxis::NegY}, {"posY", GravityAxis::PosY},
        {"negZ", GravityAxis::NegZ}, {"posZ", GravityAxis::PosZ},
    };
    for (const Entry& entry : kEntries)
        if (str::equalsIgnoreAscii(entry.name, name)) {
            out = entry.axis;
            return true;
        }
    return false;
}

// Reads a three-number array into a vector. A missing or short array leaves the
// vector alone: a scene that does not say where a body is has it at the
// origin, which is the same answer a body that was never placed has.
template <typename Json>
void readVec3(const Json& node, Vec3& out) {
    if (!node.is_array() || node.size() < 3) return;
    out.x = node[0].template get<float>();
    out.y = node[1].template get<float>();
    out.z = node[2].template get<float>();
}

template <typename Json>
void writeVec3(Json& node, const Vec3& v) {
    node = Json::array({v.x, v.y, v.z});
}

// Writes a number the way the text format writes it: %g, six significant
// digits.
//
// A float 0.3f is 0.30000001192092896 as a double, and the JSON library says
// so - which is true and unreadable. The text format has always written 0.3,
// and the two formats are supposed to say the same thing, so the same six
// digits it is.
void writeNumber(std::string& out, const nlohmann::json& node) {
    if (!node.is_number_float()) {
        out += node.dump();
        return;
    }
    char buffer[32];
    const int written = std::snprintf(buffer, sizeof(buffer), "%g", node.get<double>());
    if (written > 0) out.append(buffer, (size_t)written < sizeof(buffer) ? (size_t)written
                                                                       : sizeof(buffer) - 1);
}

// Writes a JSON tree in a shape a person can read: an object gets one field per
// line, and an array of numbers stays on one line.
//
// The library's own dumper puts every element of an array on its own line, and
// a scene is mostly three-number arrays - a position is one line, not four. The
// tree is still the library's, so the quoting and the escaping of a string are
// the library's too; only the layout is ours.
void writeJson(std::string& out, const nlohmann::ordered_json& node, int depth) {
    const std::string indent((size_t)depth * 4, ' ');
    const std::string child((size_t)(depth + 1) * 4, ' ');

    if (node.is_object()) {
        if (node.empty()) {
            out += "{}";
            return;
        }
        out += "{\n";
        bool first = true;
        for (auto it = node.begin(); it != node.end(); ++it) {
            if (!first) out += ",\n";
            first = false;
            out += child;
            // The key goes through the library as well, so a key with a quote
            // in it is escaped rather than breaking the file.
            out += nlohmann::ordered_json(it.key()).dump();
            out += ": ";
            writeJson(out, it.value(), depth + 1);
        }
        out += "\n";
        out += indent;
        out += "}";
        return;
    }

    if (node.is_array()) {
        // An array of plain numbers stays on one line: that is what a position
        // or a quaternion is.
        bool numbers = true;
        for (const auto& item : node)
            if (!item.is_number()) {
                numbers = false;
                break;
            }
        if (numbers) {
            out += "[";
            for (size_t i = 0; i < node.size(); ++i) {
                if (i) out += ", ";
                writeNumber(out, node[i]);
            }
            out += "]";
            return;
        }
        if (node.empty()) {
            out += "[]";
            return;
        }
        out += "[\n";
        for (size_t i = 0; i < node.size(); ++i) {
            if (i) out += ",\n";
            out += child;
            writeJson(out, node[i], depth + 1);
        }
        out += "\n";
        out += indent;
        out += "]";
        return;
    }

    if (node.is_number()) {
        writeNumber(out, node);
        return;
    }

    out += node.dump();
}

} // namespace

bool saveJson(const PhysicsWorld& world, const MeshRegistry& meshes, const std::string& path) {
    // ordered_json keeps the fields in the order they were written, which is the
    // order a person reads them in: what it is, where it is, what it is made
    // of. The plain json sorts its keys alphabetically, and "angularVelocity"
    // first is not a shape anyone imagines a scene in.
    using nlohmann::ordered_json;

    ordered_json root;
    // A format marker and a version, so a file written by a newer engine is
    // recognisable as such instead of being half-understood.
    root["format"] = "spectra3d-scene";
    root["version"] = 1;
    root["gravity"] = jsonGravityName(world.gravityAxis);

    ordered_json bodies = ordered_json::array();
    for (const RigidBody& body : world.bodies) {
        ordered_json node;
        node["shape"] = jsonShapeName(body.shape);
        writeVec3(node["position"], body.position);
        writeVec3(node["velocity"], body.velocity);
        writeVec3(node["angularVelocity"], body.angularVelocity);
        // A quaternion is four numbers, w first - the order it is written in
        // everywhere else in the engine.
        node["orientation"] = ordered_json::array({body.orientation.w, body.orientation.x,
                                                   body.orientation.y, body.orientation.z});
        writeVec3(node["halfExtents"], body.halfExtents);
        node["mass"] = body.mass;

        ordered_json material;
        material["friction"] = body.material.friction;
        material["restitution"] = body.material.restitution;
        material["density"] = body.material.density;
        material["isStatic"] = body.material.isStatic;
        node["material"] = material;

        if (body.hasMesh()) {
            ordered_json mesh;
            mesh["name"] = meshes.nameOf(body.mesh);
            mesh["scale"] = body.meshScale;
            node["mesh"] = mesh;
        }

        ordered_json flags;
        flags["visible"] = body.visible;
        flags["locked"] = body.locked;
        flags["favorite"] = body.favorite;
        // Whether the body casts a shadow. The ones that are not written are
        // the ones made before the shadows existed, and they all cast.
        flags["castsShadow"] = body.castsShadow;
        node["flags"] = flags;

        if (body.isCamera()) {
            ordered_json camera;
            camera["fov"] = body.cameraFov;
            camera["active"] = body.activeCamera;
            node["camera"] = camera;
        }

        // The sound the body makes when it hits something. Only written when
        // there is one: a body without a sound is the common case and an empty
        // object for every body in the scene is noise in the file.
        if (!body.sound.empty()) {
            ordered_json sound;
            sound["name"] = body.sound;
            sound["volume"] = body.soundVolume;
            node["sound"] = sound;
        }

        if (body.anchorPosition || body.anchorRotation) {
            ordered_json anchor;
            anchor["position"] = body.anchorPosition;
            anchor["rotation"] = body.anchorRotation;
            writeVec3(anchor["point"], body.anchorPoint);
            anchor["orientation"] =
                ordered_json::array({body.anchorOrientation.w, body.anchorOrientation.x,
                                     body.anchorOrientation.y, body.anchorOrientation.z});
            anchor["spring"] = body.anchorSpring;
            node["anchor"] = anchor;
        }

        bodies.push_back(std::move(node));
    }
    root["bodies"] = std::move(bodies);

    std::ofstream file(path);
    if (!file) {
        Log::error("Scene: cannot write %s", path.c_str());
        return false;
    }
    // Laid out by hand rather than by the library's dumper, so a position is
    // one line instead of four. A trailing newline so the file is a well-behaved
    // text file.
    std::string text;
    writeJson(text, root, 0);
    text += "\n";
    file << text;
    return (bool)file;
}

bool loadJson(PhysicsWorld& world, MeshRegistry& meshes, const std::string& path, int threads) {
    using nlohmann::json;

    std::string text;
    size_t size = 0;
    if (!readWholeFile(path, text, size)) {
        Log::error("Scene: cannot read %s", path.c_str());
        return false;
    }

    json root;
    try {
        root = json::parse(text);
    } catch (const std::exception& e) {
        Log::error("Scene: %s is not valid JSON: %s", path.c_str(), e.what());
        return false;
    }
    if (!root.is_object()) {
        Log::error("Scene: %s is not a scene", path.c_str());
        return false;
    }

    // The gravity is optional: a scene that does not say which way is down
    // keeps the world's. The text format leaves it to the project manifest,
    // and a scene that carries it is carrying more than the other one.
    if (root.contains("gravity") && root["gravity"].is_string()) {
        GravityAxis axis = world.gravityAxis;
        if (jsonGravityFromName(root["gravity"].get<std::string>(), axis))
            world.setGravity(axis);
        else
            Log::warn("Scene: unknown gravity '%s'", root["gravity"].get<std::string>().c_str());
    }

    std::vector<RigidBody> loaded;
    const bool hasBodies = root.contains("bodies") && root["bodies"].is_array();
    if (hasBodies) loaded.reserve(root["bodies"].size());
    else
        Log::warn("Scene: %s has no bodies in it, loading it as an empty scene",
                  path.c_str());

    // The models the scene names, read before the bodies are built: parsing a
    // model is the slow part of opening a project and it touches no GL, so it
    // runs on as many threads as the user asked for. The bodies then find the
    // models already in the registry and the import costs nothing.
    if (hasBodies) {
        std::vector<std::string> names;
        for (const json& node : root["bodies"]) {
            if (!node.is_object() || !node.contains("mesh")) continue;
            const json& mesh = node["mesh"];
            if (!mesh.is_object() || !mesh.contains("name") || !mesh["name"].is_string()) continue;
            names.push_back(mesh["name"].get<std::string>());
        }
        preloadMeshes(meshes, names, threads);
    }

    int skipped = 0;
    if (hasBodies)
        for (const json& node : root["bodies"]) {
            if (!node.is_object()) {
                ++skipped;
                continue;
            }

            RigidBody body;
            if (!node.contains("shape") || !node["shape"].is_string() ||
                !jsonShapeFromName(node["shape"].get<std::string>(), body.shape)) {
                ++skipped;
                continue;
            }

            if (node.contains("position")) readVec3(node["position"], body.position);
            if (node.contains("velocity")) readVec3(node["velocity"], body.velocity);
            if (node.contains("angularVelocity")) readVec3(node["angularVelocity"], body.angularVelocity);
            if (node.contains("halfExtents")) readVec3(node["halfExtents"], body.halfExtents);
            if (node.contains("orientation") && node["orientation"].is_array() &&
                node["orientation"].size() >= 4) {
                body.orientation = Quat{node["orientation"][0].get<float>(),
                                       node["orientation"][1].get<float>(),
                                       node["orientation"][2].get<float>(),
                                       node["orientation"][3].get<float>()}
                                       .normalized();
            }
            if (node.contains("mass")) body.mass = node["mass"].get<float>();

            if (node.contains("material") && node["material"].is_object()) {
                const json& material = node["material"];
                if (material.contains("friction"))
                    body.material.friction = material["friction"].get<float>();
                if (material.contains("restitution"))
                    body.material.restitution = material["restitution"].get<float>();
                if (material.contains("density")) body.material.density = material["density"].get<float>();
                if (material.contains("isStatic"))
                    body.material.isStatic = material["isStatic"].get<bool>();
            }

            if (node.contains("flags") && node["flags"].is_object()) {
                const json& flags = node["flags"];
                if (flags.contains("visible")) body.visible = flags["visible"].get<bool>();
                if (flags.contains("locked")) body.locked = flags["locked"].get<bool>();
                if (flags.contains("favorite")) body.favorite = flags["favorite"].get<bool>();
                // A scene made before the shadows existed does not say, and
                // every body in it casts one.
                if (flags.contains("castsShadow"))
                    body.castsShadow = flags["castsShadow"].get<bool>();
            }

            if (node.contains("camera") && node["camera"].is_object()) {
                const json& camera = node["camera"];
                if (camera.contains("fov"))
                    body.cameraFov = clampf(camera["fov"].get<float>(), 10.0f, 170.0f);
                // The active flag only means something on a camera, exactly as
                // in the text format.
                if (camera.contains("active") && body.isCamera())
                    body.activeCamera = camera["active"].get<bool>();
            }

            if (node.contains("anchor") && node["anchor"].is_object()) {
                const json& anchor = node["anchor"];
                if (anchor.contains("position")) body.anchorPosition = anchor["position"].get<bool>();
                if (anchor.contains("rotation")) body.anchorRotation = anchor["rotation"].get<bool>();
                if (anchor.contains("point")) readVec3(anchor["point"], body.anchorPoint);
                if (anchor.contains("orientation") && anchor["orientation"].is_array() &&
                    anchor["orientation"].size() >= 4) {
                    body.anchorOrientation =
                        Quat{anchor["orientation"][0].get<float>(),
                             anchor["orientation"][1].get<float>(),
                             anchor["orientation"][2].get<float>(),
                             anchor["orientation"][3].get<float>()}
                            .normalized();
                }
                if (anchor.contains("spring")) body.anchorSpring = anchor["spring"].get<float>();
            }

            if (node.contains("sound") && node["sound"].is_object()) {
                const json& sound = node["sound"];
                if (sound.contains("name") && sound["name"].is_string())
                    body.sound = sound["name"].get<std::string>();
                if (sound.contains("volume"))
                    body.soundVolume = clampf(sound["volume"].get<float>(), 0.0f, 1.0f);
            }

            if (node.contains("mesh") && node["mesh"].is_object() && node["mesh"].is_object()) {
                const json& mesh = node["mesh"];
                if (mesh.contains("name") && mesh["name"].is_string()) {
                    const std::string name = mesh["name"].get<std::string>();
                    const int meshIndex = resolveMesh(meshes, name);
                    if (meshIndex >= 0) {
                        body.mesh = meshIndex;
                        body.shape = Shape::Mesh;
                        if (mesh.contains("scale")) body.meshScale = mesh["scale"].get<float>();
                    } else {
                        Log::warn("Scene: mesh '%s' not found, using primitive", name.c_str());
                    }
                }
            }

            syncShapeDimensions(body);
            body.configure(body.mass);
            body.syncPrevTransform();
            loaded.push_back(std::move(body));
        }

    if (skipped > 0 && loaded.empty()) {
        // Every body in the file was rejected. That is a file that could not be
        // read, and saying so is the only useful thing to do with it.
        Log::error("Scene: none of the %d body/bodies in %s could be read", skipped,
                   path.c_str());
        return false;
    }
    if (skipped > 0)
        Log::warn("Scene: %d body/bodies in %s could not be read", skipped, path.c_str());

    // A scene with no bodies in it is an empty scene, not a broken file. The
    // first thing anybody does in an editor is have an empty scene, and a scene
    // that says so and is then refused is the editor refusing to open.
    world.bodies = std::move(loaded);
    world.wakeAll();
    return true;
}

bool save(const PhysicsWorld& world, const MeshRegistry& meshes, const std::string& path) {
    // One format, always JSON. The name of the file does not decide anything.
    return saveJson(world, meshes, path);
}

std::vector<std::string> sceneMeshNames(const std::string& path) {
    using nlohmann::json;
    std::vector<std::string> names;

    std::string text;
    size_t size = 0;
    if (!readWholeFile(path, text, size)) return names;
    json root;
    try {
        root = json::parse(text);
    } catch (const std::exception&) {
        return names;
    }
    if (!root.is_object() || !root.contains("bodies") || !root["bodies"].is_array()) return names;

    for (const json& node : root["bodies"]) {
        if (!node.is_object()) continue;
        if (!node.contains("mesh")) continue;
        const json& mesh = node["mesh"];
        if (!mesh.is_object() || !mesh.contains("name") || !mesh["name"].is_string()) continue;
        names.push_back(mesh["name"].get<std::string>());
    }
    return names;
}

bool load(PhysicsWorld& world, MeshRegistry& meshes, const std::string& path, int threads) {
    return loadJson(world, meshes, path, threads);
}

} // namespace scenes
} // namespace s3d