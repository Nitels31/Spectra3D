#version 330 core
// Licensed under the Blue Oak Model License 1.0.0.

// THE WATER'S FRAGMENT STAGE.
//
// What the eye sees of water is three things mixed together:
//
//  - the light that comes back out of the water: what is under it, tinted by
//    the water on the way;
//  - the light that comes off the water: the sky, and the sun in the sky;
//  - the shine of the sun on the surface, which is the one thing that says
//    "this is water" and not "this is a blue floor".
//
// How much of each depends on the angle. Looking straight down, the water is
// mostly what is under it; looking along it, the water is mostly the sky.
// The rule for that is Fresnel: the angle decides, and the angle of the eye
// decides with it.
//
// THE SKY IN THE WATER
//
// The water reflects the sky, and the sky it reflects is a picture the shader
// makes: a gradient from the horizon to the zenith and the sun in it.
//
// It is a picture and not the engine's skybox, and the reason is that the
// skybox is six ordinary textures and the water would have to look up all
// six of them to find the one texel it wants - and the sky in the water is
// blurred by the waves anyway, so a texel of it is worth very little. What
// the water needs from the sky is its *colour*: the light of the horizon and
// the light of the zenith, and where the sun is.
//
// THE BOTTOM, BENT
//
// The bottom of the water is seen through the surface, and the surface is a
// sheet of moving water, and a sheet of moving water bends what is under it.
// The picture under the water is therefore a picture of the world taken
// through a lens that ripples - and the lens is the one the shader has just
// drawn the normal of.

in vec3 vWorldPosition;
in vec3 vNormal;
// How high this point is above the still water, in metres.
in float vHeight;
// How much the wave is folding over itself here.
in float vFold;
// How far this point is from the edge of the sheet: 0 at the edge, 1 in the
// middle of it.
in float vEdge;

uniform vec3 uCameraPosition;
uniform vec3 uSunDirection;   // towards the sun
uniform vec3 uSunColour;
uniform vec3 uSkyColour;      // the light that comes from the sky
uniform vec3 uSkyHorizon;     // the sky the water reflects, at the horizon
uniform vec3 uSkyZenith;      // and at the zenith

// The eye's own frame of the world, for the bend of the light: the x and the
// y of a direction in it are the x and the y of the screen.
uniform mat4 uView;

// THE COLOUR OF THE WATER
//
// Two colours and not one: the water is bright where it is thin and dark
// where it is deep, and how fast it goes from the one to the other is how
// fast the light is swallowed on the way down.
uniform vec3 uShallowColour;
uniform vec3 uDeepColour;
uniform float uDepthFade;
uniform int uDepthColour;     // 0 for one colour everywhere
uniform float uWaterDepth;    // how deep the water is, in metres
uniform float uClarity;       // how fast the water swallows the light, 1/m
uniform float uOpacity;       // how much of the water's own colour there is
uniform float uShoreFade;     // how much the water thins at its own edge

// THE LIGHT ON THE WATER
uniform float uReflectivity;  // how much of the sky the water gives back
uniform float uFresnelPower;  // how sharp the edge of the reflection is
uniform float uFresnelIntensity;
uniform float uSpecular;
uniform float uShininess;

// THE FOAM
uniform float uFoam;
uniform vec3 uFoamColour;

// THE SMALL RIPPLES
uniform float uDetailStrength;
uniform float uDetailScale;   // the length of the longest ripple, in metres
uniform float uDetailSpeed;
// How high the waves are: the fragment stage needs it to know how high the
// crest of a wave is and how deep the trough of it is.
uniform float uWaveHeight;
// The time of the water and the way the wind blows: the ripples are a
// function of the two of them, as the waves are.
uniform float uTime;
uniform vec2 uWindDirection;

// THE BEND OF THE LIGHT
uniform int uRefraction;      // 0 for no bend at all
uniform float uRefractionStrength;
// The picture of the world, taken before the water was drawn over it.
uniform sampler2D uSceneColour;
// Where the picture of the scene is in the window, and how big it is: the
// bottom left corner of it and its width and its height in pixels.
uniform vec4 uViewport;

// How much the low places of the water are darkened.
uniform float uDarkening;

// THE SHADOW
//
// The same map the world is drawn with: the distance from the sun to the
// thing the sun hit first. The water takes the sun's light away where
// something stands in front of it, and a shadow on the water is a shadow like
// any other.
//
// One tap, and the reason is in the function that does it.
uniform sampler2DArray uShadowMap;
// The sun's matrix, one per stretch of the scene, and what is known about
// every stretch: how far it reaches from the middle of the scene, how deep it
// is in metres, and how big one of its texels is.
uniform mat4 uLightViewProjection[4];
uniform vec4 uCascadeParams[4];
// The middle of the scene, and how many of the stretches there are.
uniform vec3 uSceneCentre;
uniform int uCascadeCount;
// 0 for no shadow at all.
uniform int uShadowEnabled;
uniform float uShadowBias;        // the bias of the comparison, in metres
uniform float uShadowNormalTexels;  // the offset along the normal, in texels

// The air in front of the water is the same air as the air in front of
// everything else, and it is in a file of its own: fog/fog.glsl. The loader
// puts the file where this line is.
//
// It is here and not at the top of the file because the fog is written in
// terms of the sun and of the eye, and the fog needs the two of them to be
// known before it is read.
#include "../fog/fog.glsl"

out vec4 oColour;

// The sky, in a direction.
//
// The gradient from the horizon to the zenith, and the glow of the sun in it.
//
// Three things are done here the cheap way, and the reason is the same for
// all three: this is called for every piece of water in the picture, and the
// picture is mostly water.
//
//  - the gradient is a square root and not pow(up, 0.55). The root is a
//    quarter of the price of the power, and the gradient is the same to the
//    eye;
//  - the glow of the sun is the twelfth power written out as four
//    multiplications. A pow on a driver is a logarithm and an exponential,
//    and the twelfth power is not;
//  - the disc of the sun is not in here at all. The water is a million
//    small mirrors and the disc of the sun in it is the shine of the sun
//    below and not the sky.
vec3 skyOf(vec3 direction) {
    float up = clamp(direction.y, 0.0, 1.0);
    vec3 colour = mix(uSkyHorizon, uSkyZenith, sqrt(up));
    float toSun = max(dot(direction, uSunDirection), 0.0);
    float x2 = toSun * toSun;
    float x4 = x2 * x2;
    float x8 = x4 * x4;
    return colour + uSunColour * (x8 * x4) * 0.15;
}

// A number between zero and one, the same for the same place and a
// different one for every other place.
//
// It is the grain of the picture: the water is a smooth thing, and a smooth
// thing in a picture of eight bits shows its steps. The foam is the part
// that needs it the most - a mat of bubbles is patches, and the patches
// have to be patches of something.
float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// The noise between two whole numbers, smooth in between.
float valueNoise(vec2 p) {
    vec2 cell = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(cell);
    float b = hash21(cell + vec2(1.0, 0.0));
    float c = hash21(cell + vec2(0.0, 1.0));
    float d = hash21(cell + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// THE SMALL RIPPLES
//
// The mesh carries the waves, and the mesh is a grid: anything smaller than
// a cell of it has to be made here, per fragment, or the water is a sheet of
// glass with four big waves in it.
//
// Four waves, each shorter than the one before and turned a little further
// from the wind. The slope of a wave is its height times its wave number
// times the cosine of its phase, and the sum of the four slopes is the
// slope of the ripples.
//
// The speed of them is the speed of the real thing, as it is for the big
// waves: a short wave travels slower than a long one.
vec2 detailSlope(vec2 p, float t) {
    vec2 slope = vec2(0.0);

    // The wind turns them all, as it turns the big waves.
    float turn = atan(uWindDirection.y, uWindDirection.x);

    float length = max(uDetailScale, 0.05);
    float amplitude = 0.012 * uDetailStrength;

    for (int i = 0; i < 4; ++i) {
        float angle = turn + float(i) * 0.83;
        vec2 direction = vec2(cos(angle), sin(angle));

        float k = 6.2831853 / length;
        float omega = sqrt(9.81 * k) * uDetailSpeed;
        float phase = dot(direction, p) * k - omega * t;

        slope += direction * (cos(phase) * amplitude * k);

        // Each wave is a quarter of the size of the one before it and a
        // little shorter, so the four of them make one shape and not four.
        amplitude *= 0.45;
        length *= 0.52;
    }
    return slope;
}

// The shadow of a point of the water, in 0..1: one is in the light, zero is
// in the dark.
//
// ONE TAP, and this is the whole optimisation of the shadow on the water.
//
// The world asks the map the same question sixteen times when the shadows
// are the soft ones, and the water covers a good half of the screen - so
// sixteen reads there are sixteen reads over half the picture. What the
// shadow takes away of the water is the shine of the sun, which is a small
// part of the water, and the sun is in the water whether the shadow is soft
// or not. One read is enough to know whether the sun is there.
//
// The wave makes the edge of the shadow noisy: the water is not a plane and
// the shadow of a thing on a wave is a thing on a wave. A soft edge would
// smooth that noise at a price nobody would see, under a shine that is
// already a small part of the picture.
//
// The water does not cast a shadow: a sheet of water in the map is a sheet
// of water that shadows itself.
//
// The normal and the direction of the sun are given to it and not worked out
// again inside: the caller has them already, and a square root that is done
// twice is a square root that is paid for twice.
float shadowFactor(vec3 worldPosition, vec3 normal, vec3 toLight) {
    if (uShadowEnabled == 0) return 1.0;

    // The stretch of the scene the point is in: the smallest one that
    // reaches it.
    //
    // The same question the world's shader asks, and it is asked here and not
    // in the vertex stage: a stretch is a sphere around the middle of the
    // scene, and the distance to the middle is not a straight line - it
    // cannot be interpolated across a triangle. A water sheet is two
    // triangles four hundred metres across, and a vertex stage that chose the
    // stretch would put the whole sheet in the outermost one.
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
    //
    // The bias is a distance in metres and the map holds a distance as a
    // fraction of how deep the box of this stretch is, so the same bias is a
    // different number in every stretch.
    //
    // Two parts, as in the world: the first pushes the surface along its own
    // normal, so that the shadow of a thing stays touching the thing, and
    // the second grows with the slope of the surface, because the steeper the
    // surface the more the distance to the light changes over one texel.
    float nDotL = max(dot(normal, toLight), 0.0);
    float slope = sqrt(max(0.0, 1.0 - nDotL * nDotL)) / max(nDotL, 0.15);
    float texel = params.z;
    float biasWorld = uShadowBias + uShadowNormalTexels * texel * nDotL + texel * slope * 0.5;
    float bias = biasWorld / max(params.y, 1e-4);

    float stored = texture(uShadowMap, vec3(coord.xy, float(cascade))).r;
    return distance <= stored + bias ? 1.0 : 0.0;
}

void main() {
    vec3 toEye = normalize(uCameraPosition - vWorldPosition);
    vec3 toLight = normalize(uSunDirection);
    float distance = length(uCameraPosition - vWorldPosition);

    // THE NORMAL
    //
    // The waves in the mesh give the normal of the big water and the
    // ripples give the small ones, and they are added as slopes: the slope
    // of the surface is what the light sees, and the slope of a big wave and
    // the slope of a ripple are the same kind of number.
    //
    // The ripples fade out with the distance. A ripple that is smaller than
    // a pixel is a shimmer and not a ripple, and the far water is the mirror
    // it should be.
    vec3 waveNormal = normalize(vNormal);
    vec2 slope = vec2(-waveNormal.x, -waveNormal.z) / max(waveNormal.y, 0.08);
    float rippleFade = 1.0 / (1.0 + distance * distance * 0.0006);
    slope += detailSlope(vWorldPosition.xz, uTime) * rippleFade;
    vec3 normal = normalize(vec3(-slope.x, 1.0, -slope.y));

    float cosTheta = clamp(dot(normal, toEye), 0.0, 1.0);

    // THE DEPTH OF THE WATER HERE
    //
    // The water is deep in the middle of the sheet and thin at its own edge,
    // and a wave that has risen makes the water under it shallower while a
    // wave that has sunk makes it deeper. The depth is what the colour of
    // the water is decided by.
    float shore = mix(1.0, vEdge, uShoreFade);
    float localDepth = max(uWaterDepth * shore + vHeight, 0.0);

    // The way through the water: the light goes down to the bottom and back
    // up again, and the way is longer the flatter the eye lies to the
    // surface. It is why a shallow river shows its bottom and a deep one
    // does not.
    float path = localDepth / max(cosTheta, 0.2);

    // THE FRESNEL
    //
    // Water gives back about two hundredths of the light that hits it
    // straight on, and almost all of it when it is seen at a grazing angle -
    // which is why a river seen from above is a river, and a river seen
    // along its length is a mirror.
    //
    // The power is the user's: five is the water of the real thing, and a
    // lower one is a mirror and a higher one is a sheet of plastic.
    float f = 1.0 - cosTheta;
    float fresnel = uFresnelIntensity * uReflectivity *
                    (0.02 + 0.98 * pow(max(f, 1e-4), max(uFresnelPower, 0.5)));
    fresnel = clamp(fresnel, 0.0, 1.0);

    // The sky in the water: the world reflected in the surface. The
    // reflection never points down - there is nothing under the sky to
    // reflect but the ground, and the ground is not what the sky looks like.
    //
    // And the sky is only asked for where it can be seen. A piece of water
    // that faces the eye square on gives back two hundredths of the light,
    // and two hundredths of a sky is less than one quantum of the picture -
    // so the sky is not computed there at all. The water straight below the
    // eye is the piece of water that is looking at the eye, and it is a good
    // half of the water in a picture seen from above.
    vec3 reflection = vec3(0.0);
    if (fresnel > 0.004) {
        vec3 reflected = reflect(-toEye, normal);
        reflected.y = abs(reflected.y);
        reflection = skyOf(reflected);
    }

    // The shine of the sun on the water. It is the one thing that says
    // "this is water": the surface is turned a little by every wave, and
    // the sun's light finds a different wave for every one of them - which
    // is the glitter of a path of light on the water.
    vec3 halfway = normalize(toLight + toEye);
    float highlight = pow(max(dot(normal, halfway), 1e-4), max(uShininess, 1.0)) * uSpecular;

    // The shadow.
    //
    // It is asked for only where the sun's light can reach this piece of
    // water at all. A surface that faces away from the sun is dark whether
    // the sun is behind a mountain or not, and the far side of a wave is
    // most of the water in a picture - so the read of the map is skipped
    // there, and it is the read that costs.
    float nDotL = max(dot(normal, toLight), 0.0);
    float shadow = 1.0;
    if (nDotL > 0.0 || highlight > 0.0)
        shadow = shadowFactor(vWorldPosition, normal, toLight);

    // THE COLOUR OF THE WATER
    //
    // The water is the light that comes back out of it: the bottom seen
    // through it, and the light the water itself scatters. How much of the
    // bottom comes back is the water's own doing - the red is eaten first -
    // and how much of the water is the water is the setting called opacity.
    float depthFactor = 1.0 - exp(-localDepth * uDepthFade);
    vec3 bodyColour = mix(uShallowColour, uDeepColour, depthFactor);
    if (uDepthColour == 0) bodyColour = uShallowColour;

    // The waves shade themselves: the trough of a wave is shut in by the
    // wave around it and sees less of the sky. It is the shading of the
    // waves on their own, and it is what makes a sheet of water look like
    // it has a shape.
    float lift = clamp(vHeight / max(uWaveHeight, 0.01) * 0.5 + 0.5, 0.0, 1.0);
    float shade = mix(1.0 - uDarkening, 1.0, lift);
    vec3 light = uSunColour * nDotL * 0.55 * shadow + uSkyColour * shade;
    vec3 body = bodyColour * light;

    // The light of the bottom that made it back, and the water that is in
    // the way of it.
    vec3 absorption = (1.0 - uDeepColour) * uClarity;
    vec3 through = exp(-absorption * path);
    float clarity = clamp(dot(through, vec3(0.3333333)), 0.0, 1.0);

    // THE BOTTOM, BENT
    //
    // The picture of the world under the water is the picture of the world
    // seen through a sheet of moving glass, and the sheet is what bends it.
    // The bend is the difference between the way the light would have come
    // and the way it does come, and the x and the y of that difference - in
    // the eye's own frame, because the eye's frame is the frame the screen
    // is in.
    vec3 bottom = vec3(0.0);
    if (uRefraction == 1) {
        vec2 screen = (gl_FragCoord.xy - uViewport.xy) / max(uViewport.zw, vec2(1.0));
        vec3 bent = refract(-toEye, normal, 0.752);
        vec3 delta = bent + toEye;
        vec2 offset = (mat3(uView) * delta).xy * (uRefractionStrength * 0.06);
        vec2 uv = clamp(screen + offset, vec2(0.001), vec2(0.999));
        bottom = texture(uSceneColour, uv).rgb;
    }

    // THE FOAM
    //
    // Three kinds of it.
    //
    // The water is white where the wave folds over itself, white where it
    // breaks on the shore, and a little white at the top of a big wave - the
    // last one is the smallest of the three, because a crest is not foam: it
    // is a crest.
    //
    // And it is not a coat: it is patches, and the patches are broken up by
    // a noise that moves with the water.
    float foamMask = vFold;
    foamMask = max(foamMask, (1.0 - shore) * 1.4);
    foamMask = max(foamMask, max(vHeight, 0.0) / max(uWaveHeight, 0.01) * 0.3);

    vec2 foamPoint = vWorldPosition.xz * 1.7 + vec2(uTime * 0.08, uTime * 0.05);
    float patches = valueNoise(foamPoint) * 0.65 +
                    valueNoise(vWorldPosition.xz * 5.3 - vec2(uTime * 0.13, 0.0)) * 0.35;
    float foam = clamp(foamMask * uFoam * (0.45 + 1.1 * patches), 0.0, 1.0);

    // THE WATER, ALL OF IT
    //
    // With the bend on, the water draws the bottom itself and is opaque:
    // the picture under it is already in its own hands, bent by the waves.
    // With it off, the water is a tint over the picture, and the alpha says
    // how much of it there is.
    vec3 colour;
    float alpha;
    if (uRefraction == 1) {
        colour = mix(bottom * through, body, (1.0 - clarity) * uOpacity);
        alpha = 1.0;
    } else {
        colour = body;
        alpha = clamp((1.0 - clarity) * uOpacity, 0.0, 1.0);
    }

    // The foam is on top of all of it: it is a mat of bubbles and it hides
    // what is under it. It is bright, because a mat of bubbles is a white
    // thing and not a blue one.
    vec3 foamLight = uFoamColour * (uSunColour * nDotL * 0.55 * shadow + uSkyColour * 1.1 +
                                     vec3(0.12));
    colour = mix(colour, foamLight, foam);
    alpha = clamp(alpha + foam * (1.0 - alpha), 0.0, 1.0);

    // The sky and the shine are added on top of the water and not mixed into
    // it: they are light that comes off the surface, and the surface does
    // not swallow them.
    //
    // The shine is the one thing the shadow takes away: the sun's light on
    // the water is the sun's light, and where the sun is behind a mountain
    // there is no shine there. The sky is still in the water in the shadow -
    // the shadow of a thing on a lake is a darker patch of lake and not a
    // hole in it.
    //
    // And the colour is written ready for a premultiplied alpha: the blend
    // adds it to the picture and takes the bottom through what is left of
    // the transparency. The reflection is added at its full strength even
    // where the water is shallow, which is the one thing a mixed alpha could
    // not do.
    vec3 premultiplied =
        colour * alpha + reflection * fresnel + uSunColour * highlight * shadow;

    // The air in front of the water, like the air in front of everything.
    //
    // The colour of the air is multiplied by the transparency of the water,
    // because the colour of the air is behind the water and not in front of
    // it: the water is in the way, and what is in the way takes its part of
    // the light with it.
    vec3 ray = vWorldPosition - uCameraPosition;
    if (uFogDensity > 0.0) {
        vec3 direction = (distance > 1e-4) ? ray / distance : vec3(0.0, 0.0, 1.0);
        float amount = clamp(fogAmount(uCameraPosition, direction, distance), 0.0, 1.0);
        premultiplied = mix(premultiplied, fogColour(direction) * alpha, amount);
    }

    oColour = vec4(premultiplied, alpha);
}
