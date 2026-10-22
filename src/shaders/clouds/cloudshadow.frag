#version 330 core
// Licensed under the Blue Oak Model License 1.0.0.

// THE CLOUD'S SHADOW: THE FRAGMENT STAGE.
//
// The shadow of a cloud on the ground is one of the biggest things in a
// picture of the weather, and it moves: it is the cloud, seen from the sun.
//
// So this stage does what the cloud's own stage does, with the sun in place
// of the eye:
//
//   - the ray of the sun goes through the box the cloud is in, from the face
//     of the box that the sun is looking at;
//   - the ray is walked in small steps, and at every step the sky is asked
//     how much cloud is at that place;
//   - what is added up is how much of the sun's light the cloud took away,
//     and what is left of it is what reaches the ground.
//
// What is written is the light that comes through: one is a clear sky and
// zero is a cloud that is thick enough to be a night.
//
// The light of the sky is not in here: the shadow of a cloud on the ground
// is the shadow of the sun's light and not of the sky's, and the sky's light
// is not stopped by anything.

precision highp float;

// The noise the cloud is made of.
#include "cloud_noise.glsl"

// The volume: the box the cloud is in, and how much of it there is at any
// place in the box.
#include "cloud_volume.glsl"

in vec3 vWorldPosition;
in vec3 vWorldNormal;

uniform vec3 uSunDirection;   // towards the sun

// How many steps the ray of the sun is walked in.
uniform int uSteps;

out vec4 oColour;

void main() {
    // The face the sun is looking at.
    //
    // The box is drawn from the sun's point of view, so the faces of it are
    // the other way round from the way they are in the picture - and the
    // culling of the fixed pipeline cannot know which way the sun is. So the
    // face itself decides: the one the sun is looking at is the one whose
    // normal points at the sun, and the others are thrown away here.
    //
    // It is thrown away and not covered over: the ray of the sun enters the
    // cloud at one face only, and that is the one the walk starts at.
    if (dot(vWorldNormal, uSunDirection) <= 0.0) discard;

    // The ray of the sun: it comes out of the sun and goes away from it, so
    // it travels in the opposite direction to the one the sun is in.
    vec3 direction = -uSunDirection;

    // The face of the box the ray of the sun comes in at is this fragment:
    // the box is drawn with the face the sun is looking at, and that is
    // where the ray of the sun is.
    vec3 origin = vWorldPosition;

    // Where the ray of the sun comes out of the box.
    vec3 qo = cloudBoxOf(origin);
    vec3 qd = cloudBoxDirection(direction);
    float t0, t1;
    if (!cloudBoxHit(qo, qd, t0, t1)) {
        // The box is not in the way: the sun's light reaches the ground.
        oColour = vec4(1.0);
        return;
    }

    // The walk starts where the ray is and not where the box begins: the
    // fragment is on the face of the box, and the face of the box is where
    // the ray is.
    float step = max(t1, 0.0) / float(max(uSteps, 1));
    float scale = cloudScale();

    // The light that is taken away on the way through.
    float depth = 0.0;
    for (int i = 0; i < uSteps; ++i) {
        float t = step * (float(i) + 0.5);
        if (t > t1) break;
        vec3 q = qo + qd * t;
        depth += cloudThickness(q) * (step / scale);
    }

    // What is left of the light of the sun. The same law the cloud's own
    // stage uses: the light is taken out of the picture by what it goes
    // through.
    float transmittance = exp(-depth * uExtinction);

    oColour = vec4(transmittance, transmittance, transmittance, 1.0);
}
