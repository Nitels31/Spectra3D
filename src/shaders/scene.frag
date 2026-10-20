#version 330 core
// Licensed under the Blue Oak Model License 1.0.0.

// The scene's fragment stage: the sun, the sky, the shadow, and the picture
// of the surface.
//
// The shadow is the part that is worth reading twice. The map holds the
// distance from the light to the thing the light hit first, so the test is a
// single comparison: is this fragment further from the light than the thing
// in the map? The three modes are that one comparison with a different
// neighbourhood around it.
//
//   1  one tap. The edge is a stair, and it costs one read.
//   2  a few taps in a disk. The edge is soft over a texel or two, and the
//      cost is the number of taps.
//   3  the blockers are looked for first, and the radius of the penumbra
//      follows from how far they are: a shadow is sharp at the foot of the
//      thing that casts it and soft a long way from it, the way a shadow
//      cast by the sun is.

precision highp float;

// The picture on the surface. A mesh without one is drawn with a white
// texel, so the shader does not have to know whether there is a picture.
uniform sampler2D uAlbedoMap;

// The shadow map: one layer per cascade, and the distance in the light's own
// units. The two samplers are the same map read two ways: the first with the
// points of the texels where they are, the second with them spread out by the
// filter. The filter belongs to the sampler, so the two can be the same
// picture.
uniform sampler2DArray uShadowMap;
uniform sampler2DArray uShadowMapFiltered;

uniform vec3 uCameraPosition;
uniform vec3 uSunDirection;   // towards the sun
uniform vec3 uSunColour;
uniform vec3 uSkyColour;     // the light that comes from the sky
uniform vec3 uGroundColour;  // the light that comes off the ground
uniform float uAmbient;
uniform float uSpecular;
uniform float uShininess;

uniform vec3 uFogColour;
uniform float uFogDensity;

uniform int uShadowMode;         // 0 off, 1 hard, 2 filtered, 3 soft
uniform float uShadowTexelSize;  // one over the size of the map
uniform float uShadowBias;
uniform float uShadowRadius;     // the radius of the penumbra, in texels

in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vTexCoord;
in vec3 vColor;
in vec4 vShadowCoord;
flat in int vCascade;

out vec4 oColour;

// The taps of the soft filter: a Poisson disk. The points are spread out
// instead of lying in a square, and the noise they make is not a pattern
// you can see.
const vec2 kDisk[16] = vec2[16](
    vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870), vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845), vec2( 0.97484398,  0.15148367),
    vec2( 0.44323325, -0.97511554), vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507), vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367), vec2( 0.14383161, -0.14100790)
);

// The shadow of the fragment, in 0..1: one is in the light, zero is in the
// dark.
//
// `uv` is where the fragment is in the map and `distance` is how far it is
// from the light, in the map's own units. Both are measured once, in main(),
// and passed in: the map is read many times and the place in it is the
// same every time.
float shadowFactor(vec2 uv, float distance, float bias) {
    if (uShadowMode == 1) {
        // One tap. The map is read where it is, and the comparison is the
        // whole test.
        float stored = texture(uShadowMap, vec3(uv, float(vCascade))).r;
        return distance <= stored + bias ? 1.0 : 0.0;
    }

    if (uShadowMode == 2) {
        // A few taps in a disk. The map is read with the filter on, so each
        // tap is already the average of the four texels around it.
        float sum = 0.0;
        for (int i = 0; i < 16; ++i) {
            vec2 offset = kDisk[i] * uShadowTexelSize;
            float stored = texture(uShadowMapFiltered, vec3(uv + offset, float(vCascade))).r;
            sum += distance <= stored + bias ? 1.0 : 0.0;
        }
        return sum * (1.0 / 16.0);
    }

    if (uShadowMode == 3) {
        // The soft one. First the blockers: the things between the fragment
        // and the light, and how far they are from it. The radius of the
        // penumbra follows from them: a blocker that is close to the light
        // casts a shadow that stays sharp, and one that is far casts a
        // shadow that spreads.
        float blockerSum = 0.0;
        float blockerCount = 0.0;
        const float search = uShadowRadius;
        for (int i = 0; i < 16; ++i) {
            vec2 offset = kDisk[i] * uShadowTexelSize * search;
            float stored = texture(uShadowMap, vec3(uv + offset, float(vCascade))).r;
            if (stored < distance - bias) {
                blockerSum += stored;
                blockerCount += 1.0;
            }
        }
        // Nothing between: the fragment is in the light.
        if (blockerCount < 0.5) return 1.0;

        // The penumbra, in texels: the distance from the light to the
        // blocker, over the distance from the light to the fragment. A
        // blocker right against the fragment gives a shadow with no width at
        // all, and one far away gives a shadow as wide as the distance
        // between them.
        const float average = blockerSum / blockerCount;
        float penumbra = (distance - average) / max(average, 1e-4);
        float radius = clamp(penumbra, 0.6, uShadowRadius);

        // And the filter over that radius.
        float sum = 0.0;
        for (int i = 0; i < 16; ++i) {
            vec2 offset = kDisk[i] * uShadowTexelSize * radius;
            float stored = texture(uShadowMapFiltered, vec3(uv + offset, float(vCascade))).r;
            sum += distance <= stored + bias ? 1.0 : 0.0;
        }
        return sum * (1.0 / 16.0);
    }

    // The shadows are off.
    return 1.0;
}

void main() {
    const vec3 normal = normalize(vNormal);
    const vec3 toLight = normalize(uSunDirection);
    const vec3 toEye = normalize(uCameraPosition - vWorldPosition);

    // The map's distance and the fragment's own, both measured from the
    // light and both in the light's units. The clip space of the light, in
    // the range 0..1 with the depth in the same place.
    const vec3 coord = vShadowCoord.xyz / vShadowCoord.w * 0.5 + 0.5;
    const float distance = coord.z;

    // How much the surface faces the sun. It is the light itself, and it is
    // needed by the shadow as well: the bias of the comparison grows with the
    // angle between them.
    const float nDotL = max(dot(normal, toLight), 0.0);

    // Past the far plane of the light there is nothing to be in the shadow
    // of: the map says nothing about that distance, and the fragment is in
    // the light.
    float shadow = 1.0;
    if (coord.z <= 1.0) {
        // The bias grows with the angle between the surface and the light.
        // The steeper the surface, the more the distance changes over one
        // texel of the map, and the more the comparison has to be pushed to
        // keep a surface out of its own shadow - the stripes that shadow
        // acne is.
        const float slope = sqrt(max(0.0, 1.0 - nDotL * nDotL)) / max(nDotL, 0.15);
        shadow = shadowFactor(coord.xy, distance, uShadowBias * (1.0 + slope));
    }

    // The sun. The shadow is the part of the light that the ground takes
    // away, and it takes it away from the direct light only: the sky's light
    // reaches everywhere, under a roof as well as in the open.
    const vec3 direct = uSunColour * (nDotL * shadow);

    // The sky: what comes down from above, and what comes up off the ground.
    // The two are mixed by the way the surface faces, so a slope that looks
    // at the sky is lit like the sky and a slope that looks at the ground is
    // lit like the ground.
    const float up = normal.y * 0.5 + 0.5;
    const vec3 ambient = mix(uGroundColour, uSkyColour, up) * uAmbient;

    // The shine: a highlight where the surface is half way between the light
    // and the eye. It is the one thing that says "this is a surface" and not
    // a colour, and it is worth having even when it is faint.
    const vec3 halfway = normalize(toLight + toEye);
    const float nDotH = max(dot(normal, halfway), 0.0);
    const float highlight = pow(nDotH, max(uShininess, 1.0)) * uSpecular * shadow;

    // The colour of the surface: the picture, the colour of the vertex and
    // the light, in that order.
    vec3 albedo = texture(uAlbedoMap, vTexCoord).rgb * vColor;
    vec3 colour = albedo * (ambient + direct) + uSunColour * highlight;

    // The haze: the distance eats the colour of the far things, and it eats
    // it towards the colour of the sky.
    const float distanceToEye = length(uCameraPosition - vWorldPosition);
    const float fog = 1.0 - exp(-distanceToEye * uFogDensity);
    colour = mix(colour, uFogColour, clamp(fog, 0.0, 1.0));

    oColour = vec4(colour, 1.0);
}
