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
// texels spread out by the filter, the second with the points of the texels
// where they are.
//
// The filter belongs to the sampler and not to the texture, so the two are two
// sampler objects over one picture: a texture bound to two units is filtered
// the same in both, and the search for the blockers wants the raw depth of a
// texel and not the average of it with its neighbours.
uniform sampler2DArray uShadowMap;
uniform sampler2DArray uShadowMapPoint;

uniform vec3 uCameraPosition;
uniform vec3 uSunDirection;   // towards the sun
uniform vec3 uSunColour;
uniform vec3 uSkyColour;     // the light that comes from the sky
uniform vec3 uGroundColour;  // the light that comes off the ground
uniform float uAmbient;
uniform float uSpecular;
uniform float uShininess;

// The colour of the thing being drawn, when it is not in its vertices.
//
// The primitives have no colours of their own - the colour of a body is one
// number the editor picked - and the meshes that have them are multiplied by
// it as well, so that a model can be tinted without a new picture.
uniform vec3 uTint;
// Whether the colours of the vertices are to be used at all. A mesh without
// them has nothing to say, and the colour of the picture is the colour.
uniform int uUseVertexColour;

uniform vec3 uFogColour;
uniform float uFogDensity;

uniform int uShadowMode;         // 0 off, 1 hard, 2 filtered, 3 soft
uniform float uShadowTexelSize;  // one over the size of the map
uniform float uShadowBias;       // the bias, in metres
uniform float uShadowNormalTexels;  // the offset along the normal, in texels
uniform float uShadowRadius;     // the radius of the penumbra, in texels

// The stretches of the scene. One matrix each: the light's view and
// projection, fitted to the stretch.
uniform mat4 uLightViewProjection[4];
// What is known about each stretch: how far it reaches from the middle of the
// scene, how deep it is in metres, and how big one of its texels is.
//
// The bias is a distance in metres and the map holds a distance as a fraction
// of one, so the second and the third are what turn the bias into the number
// the map is read with - and they are different in every stretch.
uniform vec4 uCascadeParams[4];
// The middle of the scene, and how many of the stretches there are.
uniform vec3 uSceneCentre;
uniform int uCascadeCount;
// What is being drawn: 0 a surface that is lit, 1 a sheet that is not drawn at
// all and only takes the shadow.
//
// The sheet is the ground of the editor. The ground of the editor is a grid
// of lines, and a shadow on a line is a shadow nobody can see - so the model
// stands on nothing and looks like it is flying. The sheet is a plain quad
// that takes the shadow and nothing else, and the grid is drawn over it.
uniform int uSurfaceMode;
uniform float uShadowStrength;
// Which picture the scene is drawn with when the shadows are being looked at.
// Zero is the picture itself.
//
// The modes are what the shadow code sees, one thing at a time: 1 the shadow
// itself, 2 whether the fragment is inside the box of the map, 3 where it is
// in the map, 4 which stretch of the scene it is in, 5 how far it is from the
// light, 6 what the map holds where the fragment is, 7 the difference between
// the two - white where the fragment is behind what the map holds, which is
// where the shadow is.
//
// A shadow that is not what it should be is a question about these numbers and
// not about the picture, and the picture cannot answer it.
uniform int uShadowDebug;

in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vTexCoord;
in vec3 vColor;

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

// How far the disk of taps is turned, for this fragment.
//
// Sixteen taps in the same sixteen places draw the same sixteen rings, and
// the eye finds them: the edge of a shadow drawn that way is a set of circles
// and not a soft line. Turning the disk a little for every fragment spreads
// the taps over the whole circle instead, and what is left is a grain nobody
// can point at.
//
// The hash is the interleaved gradient noise: a line of pixels, turned into a
// number between zero and one, with no two neighbouring fragments landing on
// the same one.
float diskRotation() {
    vec2 p = gl_FragCoord.xy;
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))) * 6.2831853;
}

// The stretch of the scene the fragment is in: the smallest one that reaches
// it.
//
// The choice is made HERE and not in the vertex stage, and that is the whole
// point. A stretch is a sphere around the middle of the scene, and the
// distance to the middle is not a straight line - it cannot be interpolated
// across a triangle. A ground is two triangles forty metres across, and the
// distance to the middle of the scene is the same at all four of its corners,
// so a vertex stage that chose the stretch would put the whole ground in the
// outermost one - and the shadow of a box in the middle of the scene would
// then fall on a stretch the box was never drawn into.
//
// The loop is over four and not over uCascadeCount: a loop with a dynamic
// bound is the one thing a shader cannot unroll, and the number of stretches
// is the same for every fragment of the frame.
//
// And the fallback is the BIGGEST stretch and not the first one. A fragment
// further from the middle of the scene than every stretch reaches belongs to
// the last one - the first one is a box in the middle of the scene, and
// reading the map there gives the picture of something else entirely.
int cascadeOf(float fromCentre) {
    int cascade = max(uCascadeCount - 1, 0);
    for (int i = 0; i < 4; ++i) {
        if (i < uCascadeCount && fromCentre <= uCascadeParams[i].x) {
            cascade = i;
            break;
        }
    }
    return cascade;
}

// The shadow of the fragment, in 0..1: one is in the light, zero is in the
// dark.
//
// `uv` is where the fragment is in the map and `distance` is how far it is
// from the light, in the map's own units. Both are measured once, in main(),
// and passed in: the map is read many times and the place in it is the
// same every time. `bias` is how much of the distance the comparison
// forgives - the stripes a surface draws on itself, and the shadow must not
// be there.
float shadowFactor(vec2 uv, float distance, float bias, int cascade) {
    if (uShadowMode == 1) {
        // One tap. The map is read where it is, and the comparison is the
        // whole test.
        float stored = texture(uShadowMap, vec3(uv, float(cascade))).r;
        return distance <= stored + bias ? 1.0 : 0.0;
    }

    // The disk, turned for this fragment.
    float angle = diskRotation();
    float cs = cos(angle), sn = sin(angle);
    mat2 turn = mat2(cs, -sn, sn, cs);

    if (uShadowMode == 2) {
        // A few taps in a disk. The map is read with the filter on, so each
        // tap is already the average of the four texels around it.
        float sum = 0.0;
        for (int i = 0; i < 16; ++i) {
            vec2 offset = turn * kDisk[i] * uShadowTexelSize;
            float stored = texture(uShadowMap, vec3(uv + offset, float(cascade))).r;
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
        //
        // The blockers are read with the points of the texels where they are:
        // the depth of a blocker must not be averaged with the depth of the
        // ground next to it, or the shadow of a thin thing comes out as wide
        // as the ground it stands on.
        float blockerSum = 0.0;
        float blockerCount = 0.0;
        float search = uShadowRadius;
        for (int i = 0; i < 16; ++i) {
            vec2 offset = turn * kDisk[i] * uShadowTexelSize * search;
            float stored = texture(uShadowMapPoint, vec3(uv + offset, float(cascade))).r;
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
        float average = blockerSum / blockerCount;
        float penumbra = (distance - average) / max(average, 1e-4);
        float radius = clamp(penumbra, 0.6, uShadowRadius);

        // And the filter over that radius.
        float sum = 0.0;
        for (int i = 0; i < 16; ++i) {
            vec2 offset = turn * kDisk[i] * uShadowTexelSize * radius;
            float stored = texture(uShadowMap, vec3(uv + offset, float(cascade))).r;
            sum += distance <= stored + bias ? 1.0 : 0.0;
        }
        return sum * (1.0 / 16.0);
    }

    // The shadows are off.
    return 1.0;
}

void main() {
    vec3 normal = normalize(vNormal);
    vec3 toLight = normalize(uSunDirection);
    vec3 toEye = normalize(uCameraPosition - vWorldPosition);

    // The stretch of the scene this fragment is in, and the light's own matrix
    // for it.
    //
    // The matrix is picked out of the four with a chain of tests and not with
    // the number as an index: an array indexed by a number that is different
    // for every fragment is a thing a driver has to copy into a place of its
    // own, and this way the four matrices are four constants and the tests
    // are four branches that most of the time all take the same way.
    float fromCentre = distance(vWorldPosition, uSceneCentre);
    int cascade = cascadeOf(fromCentre);
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

    // The map's distance and the fragment's own, both measured from the
    // light and both in the light's units. The clip space of the light, in
    // the range 0..1 with the depth in the same place.
    vec4 clip = light * vec4(vWorldPosition, 1.0);
    vec3 coord = clip.xyz / clip.w * 0.5 + 0.5;
    float distance = coord.z;

    // How much the surface faces the sun. It is the light itself, and it is
    // needed by the shadow as well: the bias of the comparison grows with the
    // angle between them.
    float nDotL = max(dot(normal, toLight), 0.0);

    // The shadow, and what it is when the map says nothing: the fragment is
    // in the light.
    float shadow = 1.0;

    // Outside the box of the stretch: the map says nothing about that place,
    // and a fragment that reads the edge of the map reads the picture of
    // something else. It is in the light.
    bool inside = coord.z <= 1.0 && coord.z >= 0.0 && coord.x >= 0.0 && coord.x <= 1.0 &&
                  coord.y >= 0.0 && coord.y <= 1.0;

    if (inside) {
        // The bias, in the map's own units.
        //
        // The bias is a distance in metres and the map holds a distance as a
        // fraction of how deep the box of this stretch is, so the same bias
        // is a different number in every stretch - and the stretch that
        // covers the whole scene is the deepest one and needs the smallest
        // number.
        float depthRange = max(params.y, 1e-4);
        float texel = params.z;

        // Two parts, and they do different jobs.
        //
        // The first pushes the surface along its own normal. The shadow of a
        // thing stays touching the thing: a surface that is moved along the
        // direction it faces still meets the edge of the shadow where it
        // should, and only the stripes a surface draws on itself are gone.
        //
        // The second pushes it along the light, and grows with the slope of
        // the surface: the steeper the surface, the more the distance to the
        // light changes over one texel of the map, and the more the
        // comparison has to be pushed to keep a surface out of its own
        // shadow - the stripes that shadow acne is.
        float slope = sqrt(max(0.0, 1.0 - nDotL * nDotL)) / max(nDotL, 0.15);
        float offset = uShadowNormalTexels * texel * nDotL;
        float biasWorld = uShadowBias + offset + texel * slope * 0.5;
        float bias = biasWorld / depthRange;

        shadow = shadowFactor(coord.xy, distance, bias, cascade);
    }

    if (uShadowDebug == 1) { oColour = vec4(vec3(shadow), 1.0); return; }
    if (uShadowDebug == 2) { oColour = vec4(vec3(inside ? 1.0 : 0.0), 1.0); return; }
    if (uShadowDebug == 3) { oColour = vec4(coord, 1.0); return; }
    if (uShadowDebug == 4) { oColour = vec4(vec3(float(cascade) / 3.0), 1.0); return; }
    if (uShadowDebug == 5) { oColour = vec4(vec3(distance), 1.0); return; }
    if (uShadowDebug == 6) {
        oColour = vec4(vec3(texture(uShadowMap, vec3(coord.xy, float(cascade))).r), 1.0);
        return;
    }
    if (uShadowDebug == 7) {
        float s = texture(uShadowMap, vec3(coord.xy, float(cascade))).r;
        oColour = vec4(vec3(clamp((distance - s) * 40.0 + 0.5, 0.0, 1.0)), 1.0);
        return;
    }
    // A sheet that is not drawn and only takes the shadow. Nothing of it is
    // seen: what is seen is the shadow on it.
    if (uSurfaceMode == 1) {
        float shade = clamp((1.0 - shadow) * uShadowStrength, 0.0, 1.0);
        oColour = vec4(0.0, 0.0, 0.0, shade);
        return;
    }

    // The sun. The shadow is the part of the light that the ground takes
    // away, and it takes it away from the direct light only: the sky's light
    // reaches everywhere, under a roof as well as in the open.
    vec3 direct = uSunColour * (nDotL * shadow);
    // The sky: what comes down from above, and what comes up off the ground.
    // The two are mixed by the way the surface faces, so a slope that looks
    // at the sky is lit like the sky and a slope that looks at the ground is
    // lit like the ground.
    float up = normal.y * 0.5 + 0.5;
    vec3 ambient = mix(uGroundColour, uSkyColour, up) * uAmbient;

    // The shine: a highlight where the surface is half way between the light
    // and the eye. It is the one thing that says "this is a surface" and not
    // a colour, and it is worth having even when it is faint.
    vec3 halfway = normalize(toLight + toEye);
    float nDotH = max(dot(normal, halfway), 0.0);
    float highlight = pow(nDotH, max(uShininess, 1.0)) * uSpecular * shadow;

    // The colour of the surface: the picture, the colour of the vertex and
    // the colour of the thing, in that order.
    vec3 vertexColour = mix(vec3(1.0), vColor, float(uUseVertexColour));
    vec3 albedo = texture(uAlbedoMap, vTexCoord).rgb * vertexColour * uTint;
    vec3 colour = albedo * (ambient + direct) + uSunColour * highlight;

    // The haze: the distance eats the colour of the far things, and it eats
    // it towards the colour of the sky.
    float distanceToEye = length(uCameraPosition - vWorldPosition);
    float fog = 1.0 - exp(-distanceToEye * uFogDensity);
    colour = mix(colour, uFogColour, clamp(fog, 0.0, 1.0));

    oColour = vec4(colour, 1.0);
}
