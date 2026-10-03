#include "SceneSerializer.hpp"

#include "../assets/MeshImport.hpp"
#include "../core/Log.hpp"
#include "../core/Str.hpp"
#include "../platform/Filesystem.hpp"

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

// --- writing ----------------------------------------------------------------

// snprintf is used rather than a hand-rolled dtoa so the digits are exactly what
// `std::ostream << float` produced with its default precision of 6. Formatting
// into a scratch buffer and appending avoids ~6000 num_put locale round trips
// per 200 bodies; the plain integer formatter avoids the other 1200.
void appendFloat(std::string& out, float value) {
    char buffer[32];
    const int written = std::snprintf(buffer, sizeof(buffer), "%g", (double)value);
    if (written <= 0) return;
    // %g at precision 6 never comes close to the buffer, but never trust that.
    const size_t length = (size_t)written < sizeof(buffer) ? (size_t)written : sizeof(buffer) - 1;
    out.append(buffer, length);
}

void appendInt(std::string& out, int value) {
    char buffer[12];
    char* const end = buffer + sizeof(buffer);
    char* p = end;
    unsigned rest = (unsigned)value;
    do {
        *--p = (char)('0' + rest % 10u);
        rest /= 10u;
    } while (rest);
    if (value < 0) *--p = '-';
    out.append(p, (size_t)(end - p));
}

// One body per line, space separated, terminated by '\n' - the layout the
// reader below and any human reading the file expect.
struct Row {
    std::string& text;

    // Every field but the first is preceded by a space, so a line never starts
    // with a separator.
    void leading(int v) { appendInt(text, v); }
    void num(float v) {
        text += ' ';
        appendFloat(text, v);
    }
    void integer(int v) {
        text += ' ';
        appendInt(text, v);
    }
    void token(const char* s) {
        text += ' ';
        text += s;
    }
};

// --- reading ----------------------------------------------------------------

// The exact set of characters `operator>>` skips in the classic locale.
inline bool isFieldSpace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

inline const char* skipFieldSpace(const char* p, const char* end) {
    while (p < end && isFieldSpace(*p)) ++p;
    return p;
}

// Field parsers. `end` is the end of the current line, so a field can never run
// past a newline - the same boundary the old per-line std::istringstream drew.
// Each returns false without touching `out` when the field is missing or
// malformed, which is what put the old stream into a failed state.
inline bool nextLong(const char*& p, const char* end, long& out) {
    p = skipFieldSpace(p, end);
    if (p >= end) return false;
    char* stop = nullptr;
    const long value = std::strtol(p, &stop, 10);
    if (stop == p) return false;
    p = stop;
    out = value;
    return true;
}

inline bool nextDouble(const char*& p, const char* end, double& out) {
    p = skipFieldSpace(p, end);
    if (p >= end) return false;
    char* stop = nullptr;
    const double value = std::strtod(p, &stop);
    if (stop == p) return false;
    p = stop;
    out = value;
    return true;
}

inline bool nextToken(const char*& p, const char* end, std::string_view& out) {
    p = skipFieldSpace(p, end);
    if (p >= end) return false;
    const char* begin = p;
    while (p < end && !isFieldSpace(*p)) ++p;
    out = std::string_view(begin, (size_t)(p - begin));
    return true;
}

// Reads the whole file in one call. `text` ends up NUL terminated because
// strtod/strtol need a terminator; `limit` is the last valid byte.
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

bool save(const PhysicsWorld& world, const MeshRegistry& meshes, const std::string& path) {
    // Text mode on purpose: it is what gives the file its CRLF endings on
    // Windows, and the reader below treats '\r' as whitespace.
    std::ofstream file(path);
    if (!file) {
        Log::error("Scene: cannot write %s", path.c_str());
        return false;
    }

    // Format the whole scene into one buffer and hand it over in a single
    // write. Same tokens, same field order, same separator as before.
    std::string text;
    text.reserve(world.bodies.size() * 320);

    for (const RigidBody& body : world.bodies) {
        // No copy: nameOf() already returns a reference.
        const char* meshName = body.hasMesh() ? meshes.nameOf(body.mesh).c_str() : kNoMesh;

        Row row{text};
        row.leading((int)body.shape);
        row.num(body.position.x);
        row.num(body.position.y);
        row.num(body.position.z);
        row.num(body.velocity.x);
        row.num(body.velocity.y);
        row.num(body.velocity.z);
        row.num(body.orientation.w);
        row.num(body.orientation.x);
        row.num(body.orientation.y);
        row.num(body.orientation.z);
        row.num(body.halfExtents.x);
        row.num(body.halfExtents.y);
        row.num(body.halfExtents.z);
        row.num(body.mass);
        row.num(body.material.friction);
        row.num(body.material.restitution);
        row.num(body.material.density);
        row.num(body.angularVelocity.x);
        row.num(body.angularVelocity.y);
        row.num(body.angularVelocity.z);
        row.num(body.meshScale);
        row.token(meshName);
        row.integer(body.material.isStatic ? 1 : 0);
        row.integer(body.visible ? 1 : 0);
        row.integer(body.locked ? 1 : 0);
        row.integer(body.anchorPosition ? 1 : 0);
        row.num(body.anchorPoint.x);
        row.num(body.anchorPoint.y);
        row.num(body.anchorPoint.z);
        row.integer(body.anchorRotation ? 1 : 0);
        row.num(body.anchorOrientation.w);
        row.num(body.anchorOrientation.x);
        row.num(body.anchorOrientation.y);
        row.num(body.anchorOrientation.z);
        row.num(body.anchorSpring);
        text += '\n';
    }

    file.write(text.data(), (std::streamsize)text.size());
    return (bool)file;
}

int resolveMesh(MeshRegistry& meshes, const std::string& name) {
    const int cached = meshes.find(name);
    if (cached >= 0) return cached;

    // The asset root only changes while the working directory is being adopted,
    // so the models directory is resolved once and reused for every body.
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
        // Probing several extensions is expected to miss, so the attempts stay
        // quiet and the caller reports one warning if every candidate failed.
        const int index = meshes.importFile(bundledPath, /*quiet=*/true);
        if (index >= 0) return index;

        localPath.clear();
        localPath += name;
        localPath += ext;
        const int local = meshes.importFile(localPath, /*quiet=*/true);
        if (local >= 0) return local;
    }
    return -1;
}

bool load(PhysicsWorld& world, MeshRegistry& meshes, const std::string& path) {
    std::string text;
    size_t size = 0;
    if (!readWholeFile(path, text, size)) {
        Log::error("Scene: cannot read %s", path.c_str());
        return false;
    }

    const char* const limit = text.data() + size;

    std::vector<RigidBody> loaded;
    loaded.reserve(std::count(text.data(), text.data() + size, '\n') + 1);
    std::string meshName;

    const char* p = text.data();
    while (p < limit) {
        // One line at a time: a field may not run past the newline, which is the
        // boundary the previous getline + istringstream reader drew.
        const char* lineStart = p;
        const void* newline = std::memchr(p, '\n', (size_t)(limit - p));
        const char* lineEnd = newline ? (const char*)newline : limit;
        p = newline ? (const char*)newline + 1 : limit;

        long shapeId = 0;
        long isStatic = 0, visible = 1, locked = 0, anchorPos = 0, anchorRot = 0;
        double px = 0, py = 0, pz = 0, vx = 0, vy = 0, vz = 0;
        double qw = 1, qx = 0, qy = 0, qz = 0;
        double hx = 0, hy = 0, hz = 0, mass = 1.0;
        double friction = 0, restitution = 0, density = 1;
        double wx = 0, wy = 0, wz = 0, meshScale = 1.0;
        double ax = 0, ay = 0, az = 0, aqw = 1, aqx = 0, aqy = 0, aqz = 0, anchorSpring = 50.0;
        std::string_view name;

        const char* field = lineStart;
        if (!(nextLong(field, lineEnd, shapeId) &&
              nextDouble(field, lineEnd, px) && nextDouble(field, lineEnd, py) &&
              nextDouble(field, lineEnd, pz) && nextDouble(field, lineEnd, vx) &&
              nextDouble(field, lineEnd, vy) && nextDouble(field, lineEnd, vz) &&
              nextDouble(field, lineEnd, qw) && nextDouble(field, lineEnd, qx) &&
              nextDouble(field, lineEnd, qy) && nextDouble(field, lineEnd, qz) &&
              nextDouble(field, lineEnd, hx) && nextDouble(field, lineEnd, hy) &&
              nextDouble(field, lineEnd, hz) && nextDouble(field, lineEnd, mass) &&
              nextDouble(field, lineEnd, friction) && nextDouble(field, lineEnd, restitution) &&
              nextDouble(field, lineEnd, density) && nextDouble(field, lineEnd, wx) &&
              nextDouble(field, lineEnd, wy) && nextDouble(field, lineEnd, wz) &&
              nextDouble(field, lineEnd, meshScale) && nextToken(field, lineEnd, name)))
            continue;

        // Everything past the mesh name is optional, so older files still load.
        // && short-circuits, which leaves this field and every later one at its
        // default - the same thing a stream that had already failed used to do.
        (void)(nextLong(field, lineEnd, isStatic) && nextLong(field, lineEnd, visible) &&
               nextLong(field, lineEnd, locked) && nextLong(field, lineEnd, anchorPos) &&
               nextDouble(field, lineEnd, ax) && nextDouble(field, lineEnd, ay) &&
               nextDouble(field, lineEnd, az) && nextLong(field, lineEnd, anchorRot) &&
               nextDouble(field, lineEnd, aqw) && nextDouble(field, lineEnd, aqx) &&
               nextDouble(field, lineEnd, aqy) && nextDouble(field, lineEnd, aqz) &&
               nextDouble(field, lineEnd, anchorSpring));

        // Filled in place instead of building a local and copying it in.
        // emplace_back() value-initialises, which also gives force/torque a
        // defined zero rather than the previous body's leftovers.
        loaded.emplace_back();
        RigidBody& body = loaded.back();
        body.shape = (Shape)shapeId;
        body.position = {(float)px, (float)py, (float)pz};
        body.velocity = {(float)vx, (float)vy, (float)vz};
        body.orientation = Quat{(float)qw, (float)qx, (float)qy, (float)qz}.normalized();
        body.halfExtents = {(float)hx, (float)hy, (float)hz};
        body.angularVelocity = {(float)wx, (float)wy, (float)wz};
        body.material.friction = (float)friction;
        body.material.restitution = (float)restitution;
        body.material.density = (float)density;
        syncShapeDimensions(body);
        body.configure((float)mass);

        body.material.isStatic = isStatic != 0;
        body.visible = visible != 0;
        body.locked = locked != 0;
        body.anchorPosition = anchorPos != 0;
        body.anchorPoint = {(float)ax, (float)ay, (float)az};
        body.anchorRotation = anchorRot != 0;
        body.anchorOrientation =
            Quat{(float)aqw, (float)aqx, (float)aqy, (float)aqz}.normalized();
        body.anchorSpring = (float)anchorSpring;

        if (name != kNoMesh) {
            meshName.assign(name.data(), name.size());
            const int meshIndex = resolveMesh(meshes, meshName);
            if (meshIndex >= 0) {
                body.mesh = meshIndex;
                body.meshScale = (float)meshScale;
            } else {
                Log::warn("Scene: mesh '%s' not found, using primitive", meshName.c_str());
            }
        }
    }

    if (loaded.empty()) {
        Log::warn("Scene: no valid bodies in %s", path.c_str());
        return false;
    }

    world.bodies = std::move(loaded);
    world.wakeAll();
    return true;
}

} // namespace scenes
} // namespace s3d
