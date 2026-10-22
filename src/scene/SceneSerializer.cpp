#include "scene/SceneSerializer.hpp"

#include "assets/MeshImport.hpp"
#include "core/Log.hpp"
#include "core/Project.hpp"
#include "core/Str.hpp"
#include "platform/Filesystem.hpp"
#include "render/ParticleSystem.hpp"
#include "scene/Cloud.hpp"
#include "scene/Landform.hpp"

#include <json/json.hpp>

#include <algorithm>
#include <cmath>
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

// The models of the project that is open. Empty when there is no project.
std::string projectModelsDir() {
    static std::string cachedRoot;
    static std::string dir;
    const std::string& root = fs::projectAssetRoot();
    if (cachedRoot != root) {
        cachedRoot = root;
        dir = fs::assetFolder(root, assetdirs::kModels,
                              sizeof(assetdirs::kModels) / sizeof(assetdirs::kModels[0]));
    }
    return dir;
}

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
//
// The engine's own file reader, not a stream: a path the operating system
// cannot spell is a scene that is not there, and a project in a folder with a
// Cyrillic name is exactly that.
bool readWholeFile(const std::string& path, std::string& text, size_t& size) {
    std::string bytes;
    if (!fs::readFile(path, bytes)) return false;
    size = bytes.size();
    text = std::move(bytes);
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

// The file a model name is in.
//
// The project's own models first, the engine's after them, and the working
// directory last. A model that a project brings is found in the project's
// `assets/models`, and the engine's models are still there for the ones the
// project did not bring.
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
        // The project's own models.
        const std::string projectModels = projectModelsDir();
        if (!projectModels.empty()) {
            bundledPath = str::join(projectModels, name);
            bundledPath += ext;
            if (fs::exists(bundledPath)) return bundledPath;
        }

        // The engine's models.
        appendJoined(bundledPath, modelsDir, name);
        bundledPath += ext;
        if (fs::exists(bundledPath)) return bundledPath;

        // And the working directory, for a model that was dropped there.
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
        case Shape::Quad: return "quad";
        case Shape::Water: return "water";
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
        {"camera", Shape::Camera},   {"quad", Shape::Quad},
        {"water", Shape::Water},
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

// --- the ground -------------------------------------------------------------
//
// A ground is a mesh the engine made instead of read from a file, and it says
// in the scene how it was made: the shape it takes, the numbers it is made of
// and the seed of the noise. The noise is the same on every machine, so the
// settings are all that is needed to make the ground again - and a ground
// that is not made again is a box the size of the ground.

// The shape of a ground, as the JSON names it.
const char* jsonLandformName(LandformShape shape) {
    switch (shape) {
        case LandformShape::Hills: return "hills";
        case LandformShape::Mountains: return "mountains";
        case LandformShape::Island: return "island";
        case LandformShape::Plateau: return "plateau";
        case LandformShape::Canyon: return "canyon";
        case LandformShape::Dunes: return "dunes";
    }
    return "hills";
}

// The other direction. A scene can name a shape this build does not have - a
// file written by a newer engine, or a typo - and the ground is then made
// with the plainest shape there is.
bool jsonLandformFromName(const std::string& name, LandformShape& out) {
    struct Entry {
        const char* name;
        LandformShape shape;
    };
    static const Entry kEntries[] = {
        {"hills", LandformShape::Hills},         {"mountains", LandformShape::Mountains},
        {"island", LandformShape::Island},       {"plateau", LandformShape::Plateau},
        {"canyon", LandformShape::Canyon},       {"dunes", LandformShape::Dunes},
    };
    for (const Entry& entry : kEntries)
        if (str::equalsIgnoreAscii(entry.name, name)) {
            out = entry.shape;
            return true;
        }
    return false;
}

// The biome of a ground, as the JSON names it.
const char* jsonLandformBiomeName(LandformBiome biome) {
    switch (biome) {
    case LandformBiome::Grassland: return "grassland";
    case LandformBiome::Desert: return "desert";
    case LandformBiome::Snow: return "snow";
    case LandformBiome::Volcanic: return "volcanic";
    case LandformBiome::Jungle: return "jungle";
    case LandformBiome::Tundra: return "tundra";
    case LandformBiome::Savanna: return "savanna";
    case LandformBiome::Alpine: return "alpine";
    }
    return "grassland";
}

// The other direction. A biome this build does not have is a biome that is
// made with the plainest one there is.
bool jsonLandformBiomeFromName(const std::string& name, LandformBiome& out) {
    struct Entry {
        const char* name;
        LandformBiome biome;
    };
    static const Entry kEntries[] = {
        {"grassland", LandformBiome::Grassland}, {"desert", LandformBiome::Desert},
        {"snow", LandformBiome::Snow},           {"volcanic", LandformBiome::Volcanic},
        {"jungle", LandformBiome::Jungle},       {"tundra", LandformBiome::Tundra},
        {"savanna", LandformBiome::Savanna},     {"alpine", LandformBiome::Alpine},
    };
    for (const Entry& entry : kEntries)
        if (str::equalsIgnoreAscii(entry.name, name)) {
            out = entry.biome;
            return true;
        }
    return false;
}

// The settings of a ground, as the file says them.
//
// Every number is written, not only the ones of the shape that is chosen: a
// file that is read by hand should say what the ground is, and a number that
// is missing is a number that is not the one that was used.
void writeLandform(nlohmann::ordered_json& node, const MeshAsset& asset) {
    const LandformSettings& settings = *asset.landform;
    node["shape"] = jsonLandformName(settings.shape);
    node["biome"] = jsonLandformBiomeName(settings.biome);
    node["seed"] = settings.seed;
    node["cells"] = settings.cells;
    node["width"] = settings.width;
    node["depth"] = settings.depth;
    node["minHeight"] = settings.minHeight;
    node["maxHeight"] = settings.maxHeight;
    node["levels"] = settings.levels;
    node["roundness"] = settings.roundness;
    node["ground"] = settings.ground;
    node["cut"] = settings.cut;
    node["river"] = settings.river;
    node["meander"] = settings.meander;
    node["shore"] = settings.shore;
    node["lowland"] = settings.lowland;
    node["stretch"] = settings.stretch;
    node["variation"] = settings.variation;
    node["frequency"] = settings.frequency;
    node["octaves"] = settings.octaves;
    node["lacunarity"] = settings.lacunarity;
    node["gain"] = settings.gain;
    node["warp"] = settings.warp;
    node["ridge"] = settings.ridge;
    node["erosion"] = settings.erosion;
    node["smoothing"] = settings.smoothing;
    node["detail"] = settings.detail;
    node["shading"] = settings.shading;
    node["color"] = nlohmann::ordered_json::array(
        {settings.color[0], settings.color[1], settings.color[2]});
    node["texture"] = settings.texture;
    node["biomes"] = settings.biomes;

    // The heights of a ground that has been painted. The settings are the
    // recipe of the ground, and a ground that has been painted is not the
    // ground the recipe makes - so the heights go into the file as well, as
    // one line of letters: a heightfield is thousands of numbers, and a file
    // a person reads cannot be thousands of lines of numbers.
    if (!asset.landformHeights.empty()) {
        std::vector<unsigned char> bytes;
        bytes.reserve(asset.landformHeights.size() * 2);
        for (float h : asset.landformHeights) {
            // A height in thousandths of the range of the ground: the range
            // is -1..2, so the number is in -10000..20000 and fits in two
            // bytes with its sign.
            const int value = (int)std::lround(clampf(h, -1.0f, 2.0f) * 10000.0f);
            bytes.push_back((unsigned char)((unsigned)value & 0xFFu));
            bytes.push_back((unsigned char)(((unsigned)value >> 8) & 0xFFu));
        }
        node["heights"] = str::toBase64(bytes.data(), bytes.size());
    }
}

// The heights of a ground that was painted, as the file says them.
//
// The other direction of the line of letters in writeLandform(): a height in
// thousandths of the range of the ground, two bytes each.
bool readLandformHeights(const std::string& text, std::vector<float>& out) {
    std::vector<unsigned char> bytes;
    if (!str::fromBase64(text, bytes)) return false;
    if (bytes.size() % 2 != 0) return false;

    out.clear();
    out.reserve(bytes.size() / 2);
    for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
        const unsigned short raw =
            (unsigned short)((unsigned)bytes[i] | ((unsigned)bytes[i + 1] << 8));
        const short value = (short)raw;
        out.push_back((float)value / 10000.0f);
    }
    return true;
}

// One number of the settings. A field that is not there keeps the value the
// settings already have: a scene that says half of a ground is a ground with
// the other half as it was written down.
template <typename Json>
void readLandformNumber(const Json& node, const char* key, float& out) {
    if (!node.is_object() || !node.contains(key)) return;
    const Json& value = node[key];
    if (!value.is_number()) return;
    out = value.template get<float>();
}

// One whole number of the settings.
template <typename Json>
void readLandformCount(const Json& node, const char* key, int& out) {
    if (!node.is_object() || !node.contains(key)) return;
    const Json& value = node[key];
    if (!value.is_number()) return;
    out = value.template get<int>();
}

// The settings of a ground, as the file says them.
template <typename Json>
void readLandform(const Json& node, LandformSettings& out) {
    if (!node.is_object()) return;

    if (node.contains("shape") && node["shape"].is_string())
        jsonLandformFromName(node["shape"].template get<std::string>(), out.shape);
    if (node.contains("biome") && node["biome"].is_string())
        jsonLandformBiomeFromName(node["biome"].template get<std::string>(), out.biome);
    if (node.contains("seed") && node["seed"].is_number())
        out.seed = (unsigned)node["seed"].template get<long long>();

    readLandformCount(node, "cells", out.cells);
    readLandformCount(node, "levels", out.levels);
    readLandformCount(node, "octaves", out.octaves);
    readLandformCount(node, "erosion", out.erosion);

    readLandformNumber(node, "width", out.width);
    readLandformNumber(node, "depth", out.depth);
    readLandformNumber(node, "minHeight", out.minHeight);
    readLandformNumber(node, "maxHeight", out.maxHeight);
    readLandformNumber(node, "roundness", out.roundness);
    readLandformNumber(node, "ground", out.ground);
    readLandformNumber(node, "cut", out.cut);
    readLandformNumber(node, "river", out.river);
    readLandformNumber(node, "meander", out.meander);
    readLandformNumber(node, "shore", out.shore);
    readLandformNumber(node, "lowland", out.lowland);
    readLandformNumber(node, "stretch", out.stretch);
    readLandformNumber(node, "variation", out.variation);
    readLandformNumber(node, "frequency", out.frequency);
    readLandformNumber(node, "lacunarity", out.lacunarity);
    readLandformNumber(node, "gain", out.gain);
    readLandformNumber(node, "warp", out.warp);
    readLandformNumber(node, "ridge", out.ridge);
    readLandformNumber(node, "smoothing", out.smoothing);
    readLandformNumber(node, "detail", out.detail);
    readLandformNumber(node, "shading", out.shading);

    if (node.contains("color") && node["color"].is_array() && node["color"].size() >= 3)
        for (int i = 0; i < 3; ++i)
            out.color[i] = node["color"][i].template get<float>();
    if (node.contains("texture") && node["texture"].is_string())
        out.texture = node["texture"].template get<std::string>();
    if (node.contains("biomes") && node["biomes"].is_boolean())
        out.biomes = node["biomes"].template get<bool>();
}

// The ground, made again from the settings that are in the file.
//
// The mesh it becomes is registered under the name the file gives it, so the
// ground in the scene is the ground that was saved - and the settings travel
// with it, so a scene that is saved again says how the ground was made.
//
// The heights are the ones of a ground that was painted: the settings are the
// recipe, and the ground that was painted is not what the recipe makes.
int makeLandformMesh(MeshRegistry& meshes, const LandformSettings& settings,
                     const std::string& name, const std::vector<float>& heights) {
    LandformSettings made = settings;
    // How many threads the making may use is the machine's business and not
    // the ground's: the same ground is the same ground on a machine with one
    // thread as on a machine with six.
    made.threads = meshes.loaderThreads();

    Landform ground;
    ground.build(made);
    if (!heights.empty()) ground.setHeights(heights);

    MeshAsset asset = ground.toMesh();
    asset.name = name;
    // The settings the ground was really made of - the ones the making
    // clamped - and not the ones that were asked for.
    asset.landform = ground.settings();
    // And the heights of a ground that was painted, so that the ground that
    // is opened is the ground that was painted.
    if (ground.painted()) asset.landformHeights = ground.heights();

    // The picture on the ground. The biomes carry their own colours, and a
    // picture on top of them only muddies them.
    if (!made.biomes && !made.texture.empty()) {
        const int texture = meshes.loadTexture(made.texture, /*repeat=*/true);
        if (texture >= 0 && !asset.parts.empty()) asset.parts[0].texture = texture;
    }

    return meshes.importMesh(std::move(asset));
}

// --- the cloud ---------------------------------------------------------------
//
// The sky, made of noise: the same arrangement as the ground. The settings go
// into the file, and the cloud is made again from them when the scene is
// opened.

// The way a cloud is drawn, as the JSON names it.
const char* jsonCloudModeName(CloudMode mode) {
    switch (mode) {
    case CloudMode::Flat: return "flat";
    case CloudMode::Puffy: return "puffy";
    }
    return "puffy";
}

// The other direction. A scene can name a mode this build does not have - a
// file written by a newer engine, or a typo - and the cloud is then made in
// the plainest way there is.
bool jsonCloudModeFromName(const std::string& name, CloudMode& out) {
    if (str::equalsIgnoreAscii("flat", name)) {
        out = CloudMode::Flat;
        return true;
    }
    if (str::equalsIgnoreAscii("puffy", name)) {
        out = CloudMode::Puffy;
        return true;
    }
    return false;
}

// The shape of a cloud, as the JSON names it.
const char* jsonCloudShapeName(CloudShape shape) {
    switch (shape) {
    case CloudShape::Cumulus: return "cumulus";
    case CloudShape::Stratus: return "stratus";
    case CloudShape::Cirrus: return "cirrus";
    case CloudShape::Cumulonimbus: return "cumulonimbus";
    }
    return "cumulus";
}

// The other direction.
bool jsonCloudShapeFromName(const std::string& name, CloudShape& out) {
    struct Entry {
        const char* name;
        CloudShape shape;
    };
    static const Entry kEntries[] = {
        {"cumulus", CloudShape::Cumulus},
        {"stratus", CloudShape::Stratus},
        {"cirrus", CloudShape::Cirrus},
        {"cumulonimbus", CloudShape::Cumulonimbus},
    };
    for (const Entry& entry : kEntries)
        if (str::equalsIgnoreAscii(entry.name, name)) {
            out = entry.shape;
            return true;
        }
    return false;
}

// The settings of a cloud, as the file says them.
//
// Every number is written, not only the ones that are in use: a file that is
// read by hand should say what the cloud is, and a number that is missing is
// a number that is not the one that was used.
void writeCloud(nlohmann::ordered_json& node, const CloudSettings& settings) {
    node["mode"] = jsonCloudModeName(settings.mode);
    node["shape"] = jsonCloudShapeName(settings.shape);
    node["seed"] = settings.seed;
    node["puffs"] = settings.puffs;
    node["puffSize"] = settings.puffSize;
    node["spread"] = settings.spread;
    node["height"] = settings.height;
    node["coverage"] = settings.coverage;
    node["softness"] = settings.softness;
    node["detail"] = settings.detail;
    node["resolution"] = settings.resolution;
    node["color"] = nlohmann::ordered_json::array(
        {settings.color[0], settings.color[1], settings.color[2]});
    node["shade"] = nlohmann::ordered_json::array(
        {settings.shade[0], settings.shade[1], settings.shade[2]});
}

// The settings of a cloud, as the file says them.
template <typename Json>
void readCloud(const Json& node, CloudSettings& out) {
    if (!node.is_object()) return;

    if (node.contains("mode") && node["mode"].is_string())
        jsonCloudModeFromName(node["mode"].template get<std::string>(), out.mode);
    if (node.contains("shape") && node["shape"].is_string())
        jsonCloudShapeFromName(node["shape"].template get<std::string>(), out.shape);
    if (node.contains("seed") && node["seed"].is_number())
        out.seed = (unsigned)node["seed"].template get<long long>();

    if (node.contains("puffs") && node["puffs"].is_number())
        out.puffs = node["puffs"].template get<int>();
    if (node.contains("detail") && node["detail"].is_number())
        out.detail = node["detail"].template get<int>();
    if (node.contains("resolution") && node["resolution"].is_number())
        out.resolution = node["resolution"].template get<int>();

    if (node.contains("puffSize") && node["puffSize"].is_number())
        out.puffSize = node["puffSize"].template get<float>();
    if (node.contains("spread") && node["spread"].is_number())
        out.spread = node["spread"].template get<float>();
    if (node.contains("height") && node["height"].is_number())
        out.height = node["height"].template get<float>();
    if (node.contains("coverage") && node["coverage"].is_number())
        out.coverage = node["coverage"].template get<float>();
    if (node.contains("softness") && node["softness"].is_number())
        out.softness = node["softness"].template get<float>();

    if (node.contains("color") && node["color"].is_array() && node["color"].size() >= 3)
        for (int i = 0; i < 3; ++i)
            out.color[i] = node["color"][i].template get<float>();
    if (node.contains("shade") && node["shade"].is_array() && node["shade"].size() >= 3)
        for (int i = 0; i < 3; ++i)
            out.shade[i] = node["shade"][i].template get<float>();
}

// The cloud, made again from the settings that are in the file.
//
// The mesh it becomes is registered under the name the file gives it, so the
// cloud in the scene is the cloud that was saved - and the settings travel
// with it, so a scene that is saved again says how the cloud was made.
int makeCloudMesh(MeshRegistry& meshes, const CloudSettings& settings,
                  const std::string& name) {
    CloudSettings made = settings;
    // How many threads the making may use is the machine's business and not
    // the cloud's: the same cloud is the same cloud on a machine with one
    // thread as on a machine with six.
    made.threads = meshes.loaderThreads();

    Cloud cloud;
    cloud.build(made);

    MeshAsset asset = cloud.toMesh();
    asset.name = name;
    // The settings the cloud was really made of - the ones the making
    // clamped - and not the ones that were asked for.
    asset.cloud = cloud.settings();

    return meshes.importMesh(std::move(asset));
}

// --- the emitters ------------------------------------------------------------
//
// The places particles come from. They are part of the scene, and the recipe
// of each one is written out in full: a file that is read by hand should say
// what the effect is made of, and a number that is missing is a number that
// is not the one that was used.
//
// The blend is a name and not a number, because a person can read "additive"
// and cannot read 1.

// The blend, as the JSON names it.
const char* jsonBlendName(ParticleBlend blend) {
    return blend == ParticleBlend::Alpha ? "alpha" : "additive";
}

// The other direction. A blend this build does not have is the one that is
// drawn over the picture.
bool jsonBlendFromName(const std::string& name, ParticleBlend& out) {
    if (str::equalsIgnoreAscii(name, "alpha")) {
        out = ParticleBlend::Alpha;
        return true;
    }
    if (str::equalsIgnoreAscii(name, "additive")) {
        out = ParticleBlend::Additive;
        return true;
    }
    return false;
}

void writeParticleSettings(nlohmann::ordered_json& node, const ParticleSettings& settings) {
    node["count"] = settings.count;
    node["lifetime"] = settings.lifetime;
    node["lifetimeJitter"] = settings.lifetimeJitter;
    node["speed"] = settings.speed;
    node["speedJitter"] = settings.speedJitter;
    node["spread"] = settings.spread;
    writeVec3(node["gravity"], settings.gravity);
    node["drag"] = settings.drag;
    node["size"] = settings.size;
    node["sizeEnd"] = settings.sizeEnd;
    node["sizeJitter"] = settings.sizeJitter;
    writeVec3(node["colour"], settings.colour);
    writeVec3(node["colourEnd"], settings.colourEnd);
    node["alpha"] = settings.alpha;
    node["alphaEnd"] = settings.alphaEnd;
    node["blend"] = jsonBlendName(settings.blend);
}

// A number that is in the file is the number that is used, and a number that
// is not in it keeps the one the recipe has.
void readParticleNumber(const nlohmann::json& node, const char* key, float& out) {
    if (node.contains(key) && node[key].is_number())
        out = node[key].get<float>();
}

void readParticleSettings(const nlohmann::json& node, ParticleSettings& out) {
    if (!node.is_object()) return;
    if (node.contains("count") && node["count"].is_number())
        out.count = node["count"].get<int>();
    readParticleNumber(node, "lifetime", out.lifetime);
    readParticleNumber(node, "lifetimeJitter", out.lifetimeJitter);
    readParticleNumber(node, "speed", out.speed);
    readParticleNumber(node, "speedJitter", out.speedJitter);
    readParticleNumber(node, "spread", out.spread);
    if (node.contains("gravity")) readVec3(node["gravity"], out.gravity);
    readParticleNumber(node, "drag", out.drag);
    readParticleNumber(node, "size", out.size);
    readParticleNumber(node, "sizeEnd", out.sizeEnd);
    readParticleNumber(node, "sizeJitter", out.sizeJitter);
    if (node.contains("colour")) readVec3(node["colour"], out.colour);
    if (node.contains("colourEnd")) readVec3(node["colourEnd"], out.colourEnd);
    readParticleNumber(node, "alpha", out.alpha);
    readParticleNumber(node, "alphaEnd", out.alphaEnd);
    if (node.contains("blend") && node["blend"].is_string())
        jsonBlendFromName(node["blend"].get<std::string>(), out.blend);
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

// --- the canvas -------------------------------------------------------------
//
// The interface of the game: the panels, the pictures, the labels and the
// buttons. It is part of the scene and it is written into the same file, so a
// game that is built carries its interface with it.
//
// The names are the ones a person would write - "button", "text" - and the
// rectangle is four numbers, the way a rectangle is four numbers everywhere
// else in the engine.

void writeColor(nlohmann::ordered_json& node, const Color& color) {
    node = nlohmann::ordered_json::array({color.r, color.g, color.b, color.a});
}

void readColor(const nlohmann::json& node, Color& out) {
    if (!node.is_array() || node.size() < 3) return;
    out.r = node[0].get<float>();
    out.g = node[1].get<float>();
    out.b = node[2].get<float>();
    out.a = node.size() >= 4 ? node[3].get<float>() : 1.0f;
}

void writeCanvas(nlohmann::ordered_json& node, const gui::Canvas& canvas) {
    node["width"] = canvas.width;
    node["height"] = canvas.height;

    nlohmann::ordered_json elements = nlohmann::ordered_json::array();
    for (const gui::Element& element : canvas.elements) {
        nlohmann::ordered_json item;
        item["kind"] = gui::elementKindName(element.kind);
        item["name"] = element.name;
        item["parent"] = element.parent;
        item["rect"] = nlohmann::ordered_json::array(
            {element.x, element.y, element.width, element.height});
        writeColor(item["color"], element.color);
        writeColor(item["textColor"], element.textColor);
        item["text"] = element.text;
        // The picture is only written when there is one: an empty field in
        // every element of every scene is noise in the file.
        if (!element.texture.empty()) item["texture"] = element.texture;
        item["fontSize"] = element.fontSize;
        item["visible"] = element.visible;
        item["interactable"] = element.interactable;
        elements.push_back(std::move(item));
    }
    node["elements"] = std::move(elements);
}

void readCanvas(const nlohmann::json& node, gui::Canvas& canvas) {
    if (!node.is_object()) return;

    if (node.contains("width") && node["width"].is_number())
        canvas.width = node["width"].get<float>();
    if (node.contains("height") && node["height"].is_number())
        canvas.height = node["height"].get<float>();
    if (!node.contains("elements") || !node["elements"].is_array()) return;

    canvas.elements.clear();
    canvas.elements.reserve(node["elements"].size());
    for (const nlohmann::json& item : node["elements"]) {
        if (!item.is_object()) continue;

        gui::Element element;
        if (item.contains("kind") && item["kind"].is_string())
            gui::elementKindFromName(item["kind"].get<std::string>(), element.kind);
        if (item.contains("name") && item["name"].is_string())
            element.name = item["name"].get<std::string>();
        if (item.contains("parent") && item["parent"].is_number())
            element.parent = item["parent"].get<int>();
        if (item.contains("rect") && item["rect"].is_array() && item["rect"].size() >= 4) {
            element.x = item["rect"][0].get<float>();
            element.y = item["rect"][1].get<float>();
            element.width = item["rect"][2].get<float>();
            element.height = item["rect"][3].get<float>();
        }
        if (item.contains("color")) readColor(item["color"], element.color);
        if (item.contains("textColor")) readColor(item["textColor"], element.textColor);
        if (item.contains("text") && item["text"].is_string())
            element.text = item["text"].get<std::string>();
        if (item.contains("texture") && item["texture"].is_string())
            element.texture = item["texture"].get<std::string>();
        if (item.contains("fontSize") && item["fontSize"].is_number())
            element.fontSize = item["fontSize"].get<float>();
        if (item.contains("visible") && item["visible"].is_boolean())
            element.visible = item["visible"].get<bool>();
        if (item.contains("interactable") && item["interactable"].is_boolean())
            element.interactable = item["interactable"].get<bool>();

        // A parent that is not there is no parent: a file written by hand can
        // point at an element that is not in the file, and an element whose
        // parent does not exist would be measured against nothing.
        if (element.parent >= (int)canvas.elements.size()) element.parent = -1;

        canvas.elements.push_back(std::move(element));
    }
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

bool saveJson(const PhysicsWorld& world, const MeshRegistry& meshes, const gui::Canvas& canvas,
              const std::string& path, const ParticleSystem* particles) {
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
        // How much the air takes out of it, and how fast the spin dies.
        material["linearDamping"] = body.material.linearDamping;
        material["angularDamping"] = body.material.angularDamping;
        // The name of the material it came out of the library, when it came
        // out of one. It is the name and not the numbers that are written
        // again on load, so that a scene that says "Concrete" is still
        // concrete when the table of materials changes under it.
        if (!body.material.preset.empty()) material["preset"] = body.material.preset;
        node["material"] = material;

        // The parameters of the thing: the values a person put on it. They are
        // only written when there are any, because most things have none and
        // an empty list in every body of the scene is noise in the file.
        if (!body.params.empty()) {
            ordered_json params = ordered_json::array();
            for (const CustomParam& param : body.params) {
                if (param.name.empty()) continue;
                ordered_json item;
                item["name"] = param.name;
                item["type"] = paramTypeName(param.type);
                switch (param.type) {
                    case ParamType::Number:
                    case ParamType::Integer:
                        item["value"] = param.number;
                        break;
                    case ParamType::Flag:
                        item["value"] = param.flag;
                        break;
                    case ParamType::Text:
                        item["value"] = param.text;
                        break;
                }
                params.push_back(std::move(item));
            }
            if (!params.empty()) node["params"] = std::move(params);
        }

        if (body.hasMesh()) {
            ordered_json mesh;
            mesh["name"] = meshes.nameOf(body.mesh);
            mesh["scale"] = body.meshScale;
            node["mesh"] = mesh;

            // A ground is a mesh the engine made, and it says how it was made:
            // the settings are in the file, and the ground is made again when
            // the scene is opened. A ground without them would come back as a
            // box the size of the ground.
            const MeshAsset* asset = meshes.at(body.mesh);
            if (asset && asset->landform) writeLandform(node["landform"], *asset);
            // And a cloud is the same thing: a mesh the engine made, and the
            // settings say how it was made.
            if (asset && asset->cloud) writeCloud(node["cloud"], *asset->cloud);
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

        // The water of a body that is water. The level it stands at and how
        // big it is are the position and the size of the body, like every
        // other body keeps them; what is written here is what is the water's
        // own: its colour and how much of what is under it shows through.
        if (body.isWater()) {
            ordered_json water;
            water["color"] = ordered_json::array({body.material.color.x, body.material.color.y,
                                                  body.material.color.z});
            water["alpha"] = body.waterAlpha;

            // The rest of the water: the waves, the colours, the light and
            // the foam. It is written in full, because a water that is not
            // the water it was is a different water - and the numbers are
            // the water, all of them.
            const WaterSettings& settings = body.water;
            water["depthColour"] = settings.depthColour;
            writeVec3(water["shallowColour"], settings.shallowColour);
            writeVec3(water["deepColour"], settings.deepColour);
            water["depthFade"] = settings.depthFade;
            water["depth"] = settings.depth;
            water["clarity"] = settings.clarity;
            water["opacity"] = settings.opacity;
            water["shoreFade"] = settings.shoreFade;
            water["waveHeight"] = settings.waveHeight;
            water["waveLength"] = settings.waveLength;
            water["waveSpeed"] = settings.waveSpeed;
            water["waveSteepness"] = settings.waveSteepness;
            water["wind"] = ordered_json::array({settings.windDirection.x,
                                                 settings.windDirection.y});
            water["detailStrength"] = settings.detailStrength;
            water["detailScale"] = settings.detailScale;
            water["detailSpeed"] = settings.detailSpeed;
            water["reflectivity"] = settings.reflectivity;
            water["fresnelPower"] = settings.fresnelPower;
            water["fresnelIntensity"] = settings.fresnelIntensity;
            water["specular"] = settings.specular;
            water["shininess"] = settings.shininess;
            water["foam"] = settings.foam;
            writeVec3(water["foamColour"], settings.foamColour);
            water["refraction"] = settings.refraction;
            water["refractionStrength"] = settings.refractionStrength;
            water["darkening"] = settings.darkening;
            node["water"] = water;
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

    // The interface of the game, in the same file. A scene without a canvas
    // is a scene without an interface, and the file says so rather than
    // leaving it to be guessed at.
    if (!canvas.elements.empty()) {
        ordered_json gui;
        writeCanvas(gui["canvas"], canvas);
        root["gui"] = std::move(gui);
    }

    // The emitters of the world. They are part of the scene: an effect that
    // is in the picture and not in the file is an effect that is gone when
    // the scene is opened again.
    if (particles && !particles->emitters().empty()) {
        ordered_json emitters = ordered_json::array();
        for (const Emitter& emitter : particles->emitters()) {
            ordered_json node;
            writeVec3(node["position"], emitter.position);
            writeVec3(node["direction"], emitter.direction);
            node["body"] = emitter.body;
            node["continuous"] = emitter.continuous;
            node["interval"] = emitter.interval;
            node["enabled"] = emitter.enabled;
            writeParticleSettings(node["settings"], emitter.settings);
            emitters.push_back(std::move(node));
        }
        root["emitters"] = std::move(emitters);
    }

    // Laid out by hand rather than by the library's dumper, so a position is
    // one line instead of four. A trailing newline so the file is a well-behaved
    // text file.
    std::string text;
    writeJson(text, root, 0);
    text += "\n";
    if (!fs::writeFile(path, text)) {
        Log::error("Scene: cannot write %s", path.c_str());
        return false;
    }
    return true;
}

bool loadJson(PhysicsWorld& world, MeshRegistry& meshes, gui::Canvas& canvas,
               const std::string& path, int threads, ParticleSystem* particles) {
    using nlohmann::json;

    std::string text;
    size_t size = 0;
    if (!readWholeFile(path, text, size)) {
        Log::error("Scene: cannot read %s", path.c_str());
        return false;
    }

    // An empty file is a fresh project and not an error: the file is made when
    // the project is, so that the folder says what it is before the first save.
    if (size == 0) return true;

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

    // The interface of the game. It is optional: a scene made before there
    // was a canvas has none, and it opens as an empty one.
    canvas.elements.clear();
    if (root.contains("gui") && root["gui"].is_object() && root["gui"].contains("canvas") &&
        root["gui"]["canvas"].is_object()) {
        readCanvas(root["gui"]["canvas"], canvas);
    }

    // The emitters of the world. They replace the ones that are there: the
    // emitters of the last scene are not the emitters of this one, and a
    // scene that has none has none - not the ones that were left over.
    if (particles) {
        particles->clearEmitters();
        if (root.contains("emitters") && root["emitters"].is_array()) {
            for (const json& node : root["emitters"]) {
                if (!node.is_object()) continue;
                Emitter emitter;
                if (node.contains("position")) readVec3(node["position"], emitter.position);
                if (node.contains("direction")) readVec3(node["direction"], emitter.direction);
                if (node.contains("body") && node["body"].is_number())
                    emitter.body = node["body"].get<int>();
                if (node.contains("continuous") && node["continuous"].is_boolean())
                    emitter.continuous = node["continuous"].get<bool>();
                if (node.contains("interval") && node["interval"].is_number())
                    emitter.interval = node["interval"].get<float>();
                if (node.contains("enabled") && node["enabled"].is_boolean())
                    emitter.enabled = node["enabled"].get<bool>();
                if (node.contains("settings"))
                    readParticleSettings(node["settings"], emitter.settings);
                particles->addEmitter(emitter);
            }
        }
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
                if (material.contains("linearDamping"))
                    body.material.linearDamping = material["linearDamping"].get<float>();
                if (material.contains("angularDamping"))
                    body.material.angularDamping = material["angularDamping"].get<float>();
                // The name of the material. A scene that names a material this
                // engine does not have keeps the numbers it was saved with -
                // the name is a convenience, not the truth.
                if (material.contains("preset") && material["preset"].is_string())
                    body.material.preset = material["preset"].get<std::string>();
            }

            // The parameters of the thing. A scene made before there were
            // parameters has none, and it opens with none.
            if (node.contains("params") && node["params"].is_array()) {
                for (const json& item : node["params"]) {
                    if (!item.is_object()) continue;
                    if (!item.contains("name") || !item["name"].is_string()) continue;
                    CustomParam param;
                    param.name = item["name"].get<std::string>();
                    if (param.name.empty()) continue;
                    if (item.contains("type") && item["type"].is_string())
                        paramTypeFromName(item["type"].get<std::string>().c_str(), param.type);
                    if (item.contains("value")) {
                        const json& value = item["value"];
                        switch (param.type) {
                            case ParamType::Number:
                            case ParamType::Integer:
                                if (value.is_number())
                                    param.number = value.get<float>();
                                break;
                            case ParamType::Flag:
                                if (value.is_boolean()) param.flag = value.get<bool>();
                                break;
                            case ParamType::Text:
                                if (value.is_string()) param.text = value.get<std::string>();
                                break;
                        }
                    }
                    body.params.push_back(std::move(param));
                }
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

            if (node.contains("water") && node["water"].is_object()) {
                const json& water = node["water"];
                if (water.contains("color") && water["color"].is_array() &&
                    water["color"].size() >= 3)
                    body.material.color = Vec3{water["color"][0].get<float>(),
                                               water["color"][1].get<float>(),
                                               water["color"][2].get<float>()};
                if (water.contains("alpha"))
                    body.waterAlpha = clampf(water["alpha"].get<float>(), 0.0f, 1.0f);

                // The rest of the water. A scene written before the water had
                // settings of its own keeps the defaults: the water is still
                // water, and it is the water it always was.
                WaterSettings& settings = body.water;
                if (water.contains("depthColour"))
                    settings.depthColour = water["depthColour"].get<bool>();
                if (water.contains("shallowColour"))
                    readVec3(water["shallowColour"], settings.shallowColour);
                if (water.contains("deepColour"))
                    readVec3(water["deepColour"], settings.deepColour);
                if (water.contains("foamColour"))
                    readVec3(water["foamColour"], settings.foamColour);
                if (water.contains("depthFade"))
                    settings.depthFade = std::fmax(0.0f, water["depthFade"].get<float>());
                if (water.contains("depth"))
                    settings.depth = std::fmax(0.0f, water["depth"].get<float>());
                if (water.contains("clarity"))
                    settings.clarity = std::fmax(0.0f, water["clarity"].get<float>());
                if (water.contains("opacity"))
                    settings.opacity = clampf(water["opacity"].get<float>(), 0.0f, 1.0f);
                if (water.contains("shoreFade"))
                    settings.shoreFade = clampf(water["shoreFade"].get<float>(), 0.0f, 1.0f);
                if (water.contains("waveHeight"))
                    settings.waveHeight = std::fmax(0.0f, water["waveHeight"].get<float>());
                if (water.contains("waveLength"))
                    settings.waveLength = std::fmax(0.05f, water["waveLength"].get<float>());
                if (water.contains("waveSpeed"))
                    settings.waveSpeed = std::fmax(0.0f, water["waveSpeed"].get<float>());
                if (water.contains("waveSteepness"))
                    settings.waveSteepness =
                        clampf(water["waveSteepness"].get<float>(), 0.0f, 1.0f);
                if (water.contains("wind") && water["wind"].is_array() &&
                    water["wind"].size() >= 2)
                    settings.windDirection = Vec2{water["wind"][0].get<float>(),
                                                 water["wind"][1].get<float>()};
                if (water.contains("detailStrength"))
                    settings.detailStrength =
                        std::fmax(0.0f, water["detailStrength"].get<float>());
                if (water.contains("detailScale"))
                    settings.detailScale = std::fmax(0.05f, water["detailScale"].get<float>());
                if (water.contains("detailSpeed"))
                    settings.detailSpeed = std::fmax(0.0f, water["detailSpeed"].get<float>());
                if (water.contains("reflectivity"))
                    settings.reflectivity =
                        clampf(water["reflectivity"].get<float>(), 0.0f, 1.0f);
                if (water.contains("fresnelPower"))
                    settings.fresnelPower =
                        std::fmax(0.5f, water["fresnelPower"].get<float>());
                if (water.contains("fresnelIntensity"))
                    settings.fresnelIntensity =
                        clampf(water["fresnelIntensity"].get<float>(), 0.0f, 1.0f);
                if (water.contains("specular"))
                    settings.specular = std::fmax(0.0f, water["specular"].get<float>());
                if (water.contains("shininess"))
                    settings.shininess = std::fmax(1.0f, water["shininess"].get<float>());
                if (water.contains("foam"))
                    settings.foam = clampf(water["foam"].get<float>(), 0.0f, 1.0f);
                if (water.contains("refraction"))
                    settings.refraction = water["refraction"].get<bool>();
                if (water.contains("refractionStrength"))
                    settings.refractionStrength =
                        std::fmax(0.0f, water["refractionStrength"].get<float>());
                if (water.contains("darkening"))
                    settings.darkening = clampf(water["darkening"].get<float>(), 0.0f, 1.0f);
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

            // A ground: a mesh the engine made, and the settings it was made
            // of are in the file. The ground is made again from them, under
            // the name the file gives it - there is no file to look for, so
            // without this the ground would come back as the box that was
            // standing in for it.
            const bool isLandform = node.contains("landform") && node["landform"].is_object();
            // And a cloud: the same arrangement, and the same reason.
            const bool isCloud = node.contains("cloud") && node["cloud"].is_object();

            if (!isLandform && !isCloud && node.contains("mesh") && node["mesh"].is_object() &&
                node["mesh"].is_object()) {
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

            if (isLandform) {
                LandformSettings settings;
                readLandform(node["landform"], settings);

                std::string name = "landform " + std::to_string(settings.seed);
                if (node.contains("mesh") && node["mesh"].is_object() &&
                    node["mesh"].contains("name") && node["mesh"]["name"].is_string())
                    name = node["mesh"]["name"].get<std::string>();

                // The heights of a ground that was painted.
                std::vector<float> heights;
                if (node["landform"].contains("heights") &&
                    node["landform"]["heights"].is_string())
                    readLandformHeights(node["landform"]["heights"].get<std::string>(), heights);

                const int index = makeLandformMesh(meshes, settings, name, heights);
                if (index >= 0) {
                    body.mesh = index;
                    body.shape = Shape::Mesh;
                    if (node.contains("mesh") && node["mesh"].is_object() &&
                        node["mesh"].contains("scale"))
                        body.meshScale = node["mesh"]["scale"].get<float>();
                } else {
                    Log::warn("Scene: the ground '%s' could not be made", name.c_str());
                }
            }

            if (isCloud) {
                CloudSettings settings;
                readCloud(node["cloud"], settings);

                std::string name = "cloud " + std::to_string(settings.seed);
                if (node.contains("mesh") && node["mesh"].is_object() &&
                    node["mesh"].contains("name") && node["mesh"]["name"].is_string())
                    name = node["mesh"]["name"].get<std::string>();

                const int index = makeCloudMesh(meshes, settings, name);
                if (index >= 0) {
                    body.mesh = index;
                    body.shape = Shape::Mesh;
                    if (node.contains("mesh") && node["mesh"].is_object() &&
                        node["mesh"].contains("scale"))
                        body.meshScale = node["mesh"]["scale"].get<float>();
                } else {
                    Log::warn("Scene: the cloud '%s' could not be made", name.c_str());
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

bool save(const PhysicsWorld& world, const MeshRegistry& meshes, const gui::Canvas& canvas,
          const std::string& path, const ParticleSystem* particles) {
    // One format, always JSON. The name of the file does not decide anything.
    return saveJson(world, meshes, canvas, path, particles);
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

bool load(PhysicsWorld& world, MeshRegistry& meshes, gui::Canvas& canvas, const std::string& path,
          int threads, ParticleSystem* particles) {
    return loadJson(world, meshes, canvas, path, threads, particles);
}

} // namespace scenes
} // namespace s3d