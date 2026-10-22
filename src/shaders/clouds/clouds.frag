#version 330 core
// Licensed under the Blue Oak Model License 1.0.0.

// THE THREE UNIFORMS THE FOG NEEDS.
//
// They are written out here and not further down, because the file of the
// fog is put into this one above the rest of it, and the fog uses these
// three in its own code. A thing has to be declared before it is used.
//
// The fog does not declare them itself - every shader that has the fog in
// it has them already, and a second declaration of the same name is an
// error and not a warning.

uniform vec3 uCameraPosition;
uniform vec3 uSunDirection;   // towards the sun
uniform vec3 uSunColour;

// The air in front of the cloud is the same air as the air in front of
// everything else, and it is in a file of its own: fog/fog.glsl. The loader
// puts the file where this line is.
#include "../fog/fog.glsl"

// The noise the cloud is made of: the weather of it, the detail of it, and
// the hash they are both read out of. The loader puts the file where this
// line is.
#include "cloud_noise.glsl"

// The volume: the box the cloud is in, and how much of it there is at any
// place in the box. The program that draws the cloud's shadow wants it too.
#include "cloud_volume.glsl"

// THE CLOUD'S FRAGMENT STAGE.
//
// The cloud is a volume, and this stage walks a ray through it.
//
// What the eye sees of a cloud is a lot of very small pieces of water, and
// every one of them throws the light of the sun about. So a cloud is drawn
// the way it is made:
//
//   - the ray goes through the box the cloud is in, and the box is found by
//     the slab test - the same question in the same form as everywhere else
//     in the engine;
//   - the ray is walked in small steps, and at every step the sky is asked
//     how much cloud is at that place;
//   - the light of the sun is asked how much of it can get through the
//     cloud to that place, and the answer is how bright that piece of the
//     cloud is;
//   - the pieces are put on top of one another, each one taking away some
//     of the light of the ones behind it.
//
// The last of those is the Beer-Lambert law: the light of the sun is taken
// out of the picture by what it goes through, and what is left of it is
// what comes out the other side. The same law that makes the air eat the
// colour of the far things.
//
// THE THREE THINGS THAT MAKE IT LOOK LIKE A CLOUD
//
//   THE LIGHT THROUGH IT  - a cloud is not lit from the front, it is lit
//     from the side, and the part of it the sun cannot reach is grey. The
//     light is found by walking a few steps towards the sun and adding up
//     what is in the way.
//
//   THE BOUNCE  - the light that goes into a cloud does not all of it come
//     straight back out: most of it bounces inside the cloud first, and
//     that is why the middle of a thick cloud glows instead of being black.
//     Three octaves of the bounce are worked out, and the sum of them is
//     the soft light in the middle of the cloud.
//
//   THE PHASE  - how much of the light of the sun comes back at the eye
//     depends on the angle. Looking at a cloud with the sun behind it, the
//     light comes straight back at the eye and the edge of the cloud is
//     white: the silver lining.
//
// And the powder: the thin edges of the cloud are darker than the thick
// middle of it, and a cloud lit from behind has a white rim with a darker
// one inside it.
//
// THE TWO COLOURS OF THE CLOUD
//
// The cloud has two colours and the light picks between them: the colour
// where the sun reaches it, and the colour where it does not. The sun's
// light is the first of them and the light of the sky is the second, and a
// cloud at sunset is warm where the sun is on it and cold where it is not.
//
// The grey of the bottom of a cloud is the second colour and not the first:
// the sun does not reach the bottom of it.
//
// WHAT THE C++ SIDE HAS TO GIVE THE SHADER
//
// The box of the cloud is uBoxMin and uBoxMax in the cloud's own numbers,
// and the body's matrix puts that box in the world.
//
// The mesh that is drawn is that box: the front faces of it are culled, so
// that the back faces are the ones on the screen, and the back faces are
// there even when the eye is inside the cloud.
//
// The depth that is written is the depth of the first piece of the cloud
// that the ray found, and not the depth of the back of the box: the back of
// the box is under the ground that the cloud is above, and the ground would
// hide the cloud.
//
// The blend is premultiplied, the same as the water's: glBlendFunc(GL_ONE,
// GL_ONE_MINUS_SRC_ALPHA). The colour is written ready to be added to the
// picture, and the bottom comes through what is left of the transparency.

precision highp float;

in vec3 vWorldPosition;

uniform vec3 uSkyColour;      // the light that comes from the sky
uniform vec3 uGroundColour;   // the light that comes off the ground
uniform float uAmbient;       // how much of the sky's light there is at all

// THE TWO COLOURS OF THE CLOUD
uniform vec3 uCloudColour;    // the colour of it where the sun reaches it
uniform vec3 uCloudShade;     // and the colour of it where it does not

// THE QUALITY
//
// How many steps along the ray, and how many steps towards the sun.
//
// The number of steps is the same for every fragment of the frame, so the
// walk is the same length for all of them and the branches in it do not
// pull the fragments of a warp apart.
uniform int uSteps;
uniform int uLightSteps;
// The step towards the sun, as a part of the cloud's own size.
uniform float uLightStep;
// How much the cloud throws the light of the sun about.
uniform float uScatter;

uniform mat4 uView;
uniform mat4 uProjection;

// THE SHADOW
//
// The same map the world is drawn with: the distance from the sun to the
// thing the sun hit first. A cloud that is in the shadow of a mountain is
// lit by the sky and by nothing else.
//
// One tap, and the reason is in the function that does it.
uniform sampler2DArray uShadowMap;
// The sun's matrix, one per stretch of the scene, and what is known about
// every stretch: how far it reaches from the middle of the scene, how deep
// it is in metres, and how big one of its texels is.
uniform mat4 uLightViewProjection[4];
uniform vec4 uCascadeParams[4];
// The middle of the scene, and how many of the stretches there are.
uniform vec3 uSceneCentre;
uniform int uCascadeCount;
// 0 for no shadow at all.
uniform int uShadowEnabled;
// The bias of the comparison, in metres.
uniform float uShadowBias;

out vec4 oColour;

// The shadow of the sun's map on the cloud, in 0..1: one is in the light,
// zero is in the dark.
//
// ONE TAP, and this is the whole optimisation of the shadow on the cloud.
//
// The world asks the map the same question sixteen times when the shadows
// are the soft ones, and the cloud is made of the same little pieces of
// water as the world is. One read is enough to know whether the sun is
// there at all, and what the cloud does with the sun's light afterwards is
// the cloud's own business.
//
// And the read is skipped where the sun's light cannot reach the cloud at
// all: the light of the sun is the whole of the light of a cloud, so there
// is nothing to be gained by asking the map where there is no cloud.
float shadowFactor(vec3 worldPosition) {
    if (uShadowEnabled == 0) return 1.0;

    // The stretch of the scene the point is in: the smallest one that
    // reaches it.
    //
    // The same question the world's shader asks, and it is asked here and
    // not in the vertex stage: a stretch is a sphere around the middle of
    // the scene, and the distance to the middle is not a straight line - it
    // cannot be interpolated across a triangle.
    //
    // The loop is over four and not over uCascadeCount: a loop with a
    // dynamic bound is the one thing a shader cannot unroll.
    //
    // And the distance is squared: the question is "is this inside the
    // sphere", and the square root of the answer is of no use to anybody.
    vec3 fromCentre = worldPosition - uSceneCentre;
    float fromCentreSquared = dot(fromCentre, fromCentre);
    int cascade = max(uCascadeCount - 1, 0);
    for (int i = 0; i < 4; ++i) {
        if (i < uCascadeCount) {
            float reach = uCascadeParams[i].x;
            if (fromCentreSquared <= reach * reach) {
                cascade = i;
                break;
            }
        }
    }

    // The matrix of the light for that stretch, and what is known about it.
    // They are picked out of the four with a chain of tests and not with the
    // number as an index: an array indexed by a number that is different for
    // every fragment is a thing a driver has to copy into a place of its
    // own.
    mat4 light = uLightViewProjection[0];
    vec4 params = uCascadeParams[0];
    if (cascade == 1) {
        light = uLightViewProjection[1];
        params = uCascadeParams[1];
    } else if (cascade == 2) {
        light = uLightViewProjection[2];
        params = uCascadeParams[2];
    } else if (cascade == 3) {
        light = uLightViewProjection[3];
        params = uCascadeParams[3];
    }

    // The place of the point in the map, and the distance of it from the
    // light, both in the light's own units.
    vec4 clip = light * vec4(worldPosition, 1.0);
    vec3 coord = clip.xyz / clip.w * 0.5 + 0.5;
    float distance = coord.z;

    // Outside the box of the stretch: the map says nothing about that place,
    // and a point that reads the edge of the map reads the picture of
    // something else. It is in the light.
    if (coord.z > 1.0 || coord.z < 0.0 || coord.x < 0.0 || coord.x > 1.0 ||
        coord.y < 0.0 || coord.y > 1.0) {
        return 1.0;
    }

    // The bias of the comparison, in the map's own units.
    float bias = uShadowBias / max(params.y, 1e-4);

    float stored = texture(uShadowMap, vec3(coord.xy, float(cascade))).r;
    return distance <= stored + bias ? 1.0 : 0.0;
}

// THE LIGHT OF THE SUN THAT COMES OUT OF THE CLOUD AT A POINT.
//
// The sun's light has to come through the cloud to reach a point inside it,
// and how much of it comes through is decided by how much of the cloud is
// between the point and the sun.
//
// The bounce is in here as well: the light that comes out of a cloud has
// usually been inside it first - it has come in through the near side and
// gone round and come out through the near side again, and that is why the
// middle of a thick cloud is a soft grey and not a black hole.
//
// Three octaves of the bounce: the first is the light of the sun itself,
// the second is the light that has bounced once and has had further to
// travel to get here, and the third is the light that has bounced twice.
// Each one is dimmer, and each one reaches further into the cloud - so the
// deep inside of it is lit by the light that has been round the most, and
// not by the light of the sun.
vec3 cloudSunLight(vec3 q, float density, float cosTheta, vec3 sunInBox, float scale) {
    // The sun's light is not asked for where the cloud is so thin that it
    // makes no difference: a wisp is lit by the sky and by nothing else,
    // and the walk towards the sun is the most expensive thing in here.
    if (density * uExtinction <= 0.05) return vec3(0.0);

    // The depth of the cloud between this point and the sun.
    float depth = 0.0;
    float step = scale * uLightStep;
    vec3 p = q;
    for (int i = 0; i < uLightSteps; ++i) {
        p += sunInBox * step;
        depth += cloudThickness(p) * (step / scale);
        // The steps grow: the near part of the shadow matters more than the
        // far part of it, and the far part is further away.
        step *= 1.7;
    }

    float optical = depth * uExtinction;
    float phase = cloudPhase(cosTheta);
    float reach = 1.0;
    float weight = 1.0;
    vec3 light = vec3(0.0);
    for (int i = 0; i < 3; ++i) {
        light += uSunColour * (weight * phase * exp(-optical * reach));
        reach *= 0.4;
        weight *= 0.5;
    }

    // THE POWDER.
    //
    // The thin edges of the cloud are darker than the thick middle of it. A
    // cloud lit from behind has a white rim and a darker one inside the
    // rim, and this is where the darker one comes from.
    float powder = 1.0 - exp(-density * 8.0);
    return light * mix(1.0, powder, 0.6);
}

// THE LIGHT OF THE SKY AT A POINT INSIDE THE CLOUD.
//
// The light that comes down from above, and the light that comes up off the
// ground. The two are mixed by the height of the point in the cloud, so the
// bottom of it is grey and the top of it is white.
//
// And the deep inside of a cloud is darker than the outside of it: the
// light of the sky has to come through the cloud to get there.
vec3 cloudSkyLight(vec3 q, float density) {
    float t = clamp(q.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 ambient = mix(uGroundColour, uSkyColour, t) * uAmbient;
    return ambient / (1.0 + density * 2.0);
}

// Where the first step of the walk starts, as a part of a step.
//
// The interleaved gradient noise: a line of pixels turned into a number
// between zero and one, with no two neighbouring fragments landing on the
// same one. The same noise the fog's grain uses.
//
// Without it the picture of a cloud with a few steps in it is a set of
// shells, and the shells are the one thing in the picture that is not in
// the world.
float cloudDither() {
    vec2 p = gl_FragCoord.xy;
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main() {
    // The ray: from the eye, through this piece of the box.
    vec3 origin = uCameraPosition;
    vec3 direction = normalize(vWorldPosition - uCameraPosition);

    // The ray in the cloud's own box.
    vec3 qo = cloudBoxOf(origin);
    vec3 qd = cloudBoxDirection(direction);

    // Where the ray goes through the box.
    float t0, t1;
    if (!cloudBoxHit(qo, qd, t0, t1)) discard;

    // The walk starts where the ray enters the cloud and not before it, so
    // that the eye being inside the cloud is not a special case.
    t0 = max(t0, 0.0);
    if (t1 <= t0) discard;

    // The size of the cloud, and the sun in the box.
    float scale = cloudScale();
    vec3 sunInBox = cloudBoxDirection(uSunDirection);

    // The step: the length of one step of the walk.
    float step = (t1 - t0) / float(max(uSteps, 1));

    // The walk does not start at the edge of the first step but somewhere
    // inside it, and where it lands is different for every pixel.
    float t = t0 + step * cloudDither();

    // The shadow of the sun's map on the cloud: one tap, and it is the same
    // for the whole of it, so it is asked for once and not once per step.
    vec3 entry = origin + direction * t0;
    float shadow = shadowFactor(entry);

    // The picture of the cloud: the light that comes out of it, and how much
    // of it is left to see through.
    vec3 scattered = vec3(0.0);
    float transmittance = 1.0;
    // Where the cloud starts, for the depth and for the fog.
    float front = -1.0;

    for (int i = 0; i < uSteps; ++i) {
        // The end of the cloud.
        if (t > t1) break;
        // The light is gone: what is behind the front of the cloud cannot be
        // seen any more, and the walk stops there. This is the single
        // biggest saving in the whole shader - a thick cloud is opaque long
        // before the ray has come out the other side of it.
        if (transmittance < 0.01) break;

        vec3 q = qo + qd * t;
        float density = cloudDensity(q);
        if (density > 0.0) {
            if (front < 0.0) front = t;

            // The light at this piece of the cloud.
            //
            // The sun's light is the colour where the sun reaches the cloud
            // and the light of the sky is the colour where it does not, and
            // the shadow of the sun's map takes away the first of them and
            // not the second: the sky is not behind the mountain.
            vec3 light = cloudSunLight(q, density, dot(direction, uSunDirection), sunInBox, scale) *
                             uScatter * shadow * uCloudColour +
                         cloudSkyLight(q, density) * uCloudShade;

            // The light that is taken out of the picture by this piece of the
            // cloud, and the light that comes out of it.
            //
            // The step is measured in metres and the cloud is measured in
            // its own size, so that a cloud twice as big is not twice as
            // thick.
            float optical = density * uExtinction * (step / scale);
            float a = 1.0 - exp(-optical);
            scattered += transmittance * light * a;
            transmittance *= 1.0 - a;
        }
        t += step;
    }

    float alpha = 1.0 - transmittance;
    // Nothing there: the fragment is not drawn at all, and it writes no
    // depth.
    if (alpha <= 0.002) discard;

    // THE DEPTH OF THE CLOUD.
    //
    // The depth of the first piece of the cloud the ray found, and not the
    // depth of the back of the box: the back of the box is under the ground
    // that the cloud is above, and the ground would hide the cloud.
    //
    // It is written by hand and not left to the hardware, which is the one
    // thing this stage cannot do cheaply - a fragment that writes its depth
    // is a fragment that is shaded even where something else is in front of
    // it.
    if (front >= 0.0) {
        vec3 hit = origin + direction * front;
        vec4 clip = uProjection * uView * vec4(hit, 1.0);
        gl_FragDepth = clip.z / clip.w * 0.5 + 0.5;
    }

    // THE AIR IN FRONT OF THE CLOUD.
    //
    // The same air as the air in front of everything else, and it is
    // measured to the front of the cloud and not to the middle of it: the
    // air between the eye and the cloud is in front of the cloud, and the
    // air inside it is the cloud.
    vec3 air = vec3(0.0);
    float fog = 0.0;
    if (uFogDensity > 0.0) {
        vec3 ray = origin + direction * max(front, 0.0) - uCameraPosition;
        float distance = length(ray);
        vec3 toAir = (distance > 1e-4) ? ray / distance : vec3(0.0, 0.0, 1.0);
        fog = clamp(fogAmount(uCameraPosition, toAir, distance), 0.0, 1.0);
        air = fogColour(toAir);
    }

    // What is left of the cloud is what came through the air, and the light
    // of the air is added on top of it. The colour is written ready for a
    // premultiplied alpha: the blend adds it to the picture and takes the
    // bottom through what is left of the transparency.
    scattered = mix(scattered, air * alpha, fog);
    scattered += (fogGrain() - 0.5) * fog * (1.0 / 255.0);

    oColour = vec4(scattered, alpha);
}
