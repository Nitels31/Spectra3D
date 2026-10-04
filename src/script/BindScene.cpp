#include "Bindings.hpp"

#include "../assets/MeshRegistry.hpp"
#include "../core/Log.hpp"
#include "../physics/PhysicsWorld.hpp"
#include "BindString.hpp"

#if SPECTRA_SCRIPTING

#include <algorithm>

namespace s3d {
namespace script {
namespace {

// Imported models get a box hull with a plausible mass instead of an exact
// inertia tensor - good enough to stack and throw them around. Same rule as the
// asset browser, kept here because that one is a private helper of another layer.
float massFromHull(const Vec3& half) { return 200.0f * 8.0f * half.x * half.y * half.z; }

int getBodyCount() {
    ScriptState* state = activeState();
    if (!state || !state->world) return 0;
    return (int)state->world->bodies.size();
}

void clearScene() {
    ScriptState* state = activeState();
    if (!state || !state->world) return;

    state->world->clear();
    // Every stored body index just died with them.
    state->groups.clear();
    state->groupNames.clear();
}

int importModel(const ScriptString& path, float x, float y, float z) {
    ScriptState* state = activeState();
    if (!state || !state->world || !state->meshes) return -1;

    const int meshIndex = state->meshes->importFile(path.str());
    if (meshIndex < 0) return -1;

    const MeshAsset* mesh = state->meshes->at(meshIndex);
    if (!mesh) {
        Log::error("engine::importModel: registry has no mesh %d for '%s'", meshIndex,
                   path.c_str());
        return -1;
    }

    RigidBody body;
    body.shape = Shape::Mesh;
    body.mesh = meshIndex;
    body.meshScale = 1.0f;
    body.halfExtents = mesh->half;
    body.position = Vec3{x, y, z};
    body.configure(massFromHull(mesh->half));

    state->world->bodies.push_back(body);
    return (int)state->world->bodies.size() - 1;
}

// PhysicsWorld::remove erases, so every index above `removed` shifts down by
// one. Groups are the only place the engine stores indices the script handed us
// earlier, so they are the only place that has to be told.
void dropBodyFromGroups(ScriptState* state, int removed) {
    for (std::vector<int>& members : state->groups) {
        for (int& index : members)
            if (index > removed) --index;
        members.erase(std::remove(members.begin(), members.end(), removed), members.end());
    }
}

int removeBody(int index) {
    ScriptState* state = activeState();
    if (!state || !state->world) return -1;

    if (!state->world->find(index)) {
        Log::error("engine::removeBody: body %d is out of range (%d bodies)", index,
                   (int)state->world->bodies.size());
        return -1;
    }

    dropBodyFromGroups(state, index);
    state->world->remove(index);
    return index;
}

// --- groups -----------------------------------------------------------------
//
// A group is a bag of body indices, which is the smallest thing that makes
// "one script makes a forest" work: fill the group while spawning, then move the
// whole lot without tracking the individual handles.

int createGroup(const ScriptString& name) {
    ScriptState* state = activeState();
    if (!state) return -1;

    const int group = state->findOrCreateGroup(name.str());
    Log::info("engine::createGroup('%s') -> %d (%zu members)", name.c_str(), group,
              state->groups[group].size());
    return group;
}

void addToGroup(int group, int body) {
    ScriptState* state = activeState();
    if (!state || !state->world) return;

    if (group < 0 || group >= (int)state->groups.size()) {
        Log::error("engine::addToGroup: group %d does not exist (%zu groups)", group,
                   state->groups.size());
        return;
    }
    if (!state->world->find(body)) {
        Log::error("engine::addToGroup: body %d is out of range (%d bodies)", body,
                   (int)state->world->bodies.size());
        return;
    }
    state->groups[group].push_back(body);
}

int getGroupSize(int group) {
    ScriptState* state = activeState();
    if (!state) return -1;

    if (group < 0 || group >= (int)state->groups.size()) {
        Log::error("engine::getGroupSize: group %d does not exist (%zu groups)", group,
                   state->groups.size());
        return -1;
    }
    return (int)state->groups[group].size();
}

// Moves the group so its centroid lands on (x,y,z). "Set", not "translate by":
// calling it twice with the same point is a no-op, which is what a batch script
// expects from an idempotent authoring command.
void setGroupPosition(int group, float x, float y, float z) {
    ScriptState* state = activeState();
    if (!state || !state->world) return;

    if (group < 0 || group >= (int)state->groups.size()) {
        Log::error("engine::setGroupPosition: group %d does not exist (%zu groups)", group,
                   state->groups.size());
        return;
    }
    const std::vector<int>& members = state->groups[group];
    if (members.empty()) return;

    // Bodies can be removed behind a group's back - by removeBody, or by
    // anything else that touches the world - so the average is taken over the
    // members that still resolve and the move skips the ones that do not.
    Vec3 centroid{};
    int live = 0;
    for (int index : members) {
        const RigidBody* body = state->world->find(index);
        if (!body) continue;
        centroid += body->position;
        ++live;
    }
    if (live == 0) {
        Log::warn("engine::setGroupPosition: group %d has no live bodies", group);
        return;
    }

    centroid = centroid / (float)live;
    const Vec3 delta{x - centroid.x, y - centroid.y, z - centroid.z};
    if (delta.lengthSq() < 1e-12f) return;

    for (int index : members) {
        RigidBody* body = state->world->find(index);
        if (!body) continue;
        body->position += delta;
        body->wake();
    }
}

// --- declarations -----------------------------------------------------------
//
// No namespace in the strings: the host points the engine's default namespace at
// `engine` first, so these land there and scripts spell them engine::getBodyCount
// and so on.

const Binding kBindings[] = {
    {"int getBodyCount()", asFUNCTION(getBodyCount)},
    {"void clearScene()", asFUNCTION(clearScene)},
    {"int importModel(const string &in path, float x, float y, float z)",
     asFUNCTION(importModel)},
    {"int removeBody(int index)", asFUNCTION(removeBody)},

    {"int createGroup(const string &in name)", asFUNCTION(createGroup)},
    {"void addToGroup(int group, int body)", asFUNCTION(addToGroup)},
    {"int getGroupSize(int group)", asFUNCTION(getGroupSize)},
    {"void setGroupPosition(int group, float x, float y, float z)",
     asFUNCTION(setGroupPosition)},
};

} // namespace

int registerSceneBindings(asIScriptEngine* engine) {
    return registerBindings(engine, kBindings, sizeof(kBindings) / sizeof(kBindings[0]));
}

} // namespace script
} // namespace s3d

#endif // SPECTRA_SCRIPTING