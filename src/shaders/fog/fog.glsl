// THE FOG
//
// The air between the eye and the thing.
//
// This file is the fog and nothing else. The world, the particles and the
// water all have air in front of them, and it has to be the same air in all
// of them: a fog that is written out three times is a fog that is three
// different fogs the first time one of them is changed.
//
// It is put into a shader with a line of its own:
//
//     #include "fog/fog.glsl"
//
// and the loader puts this file where the line was. A shader in a folder of
// its own asks for it as "../fog/fog.glsl".
//
// A shader that has the fog in it needs three uniforms of its own as well -
// uCameraPosition, uSunDirection and uSunColour. The fog is measured from
// the eye and it is lit by the sun.
//
// THE NUMBERS.
//
//  - how thick the air is where it is thickest, in 1/m;
//  - how fast it thins as it goes up, in 1/m;
//  - the height at which it is thickest, in metres;
//  - the colour of the air;
//  - how much of the sun's light it carries;
//  - how forward it throws that light.

uniform vec3 uFogColour;    // the colour of the air
uniform float uFogDensity;  // how thick it is where it is thickest, 1/m
uniform float uFogFalloff;  // how fast it thins as it goes up, 1/m
uniform float uFogHeight;   // the height at which it is thickest, m
uniform float uFogScatter;  // how much of the sun's light it carries, 0..1
uniform float uFogForward;  // how forward it throws that light, 0..0.95

// How much of the fog is between the eye and a fragment: the optical depth,
// which is the integral of the density along the line between them.
//
// THE DENSITY.
//
// The fog is thickest at its floor and thins as it goes up, so the density at
// a height h is
//
//     d(h) = density * exp(-falloff * max(h - fogHeight, 0))
//
// and the amount of it on the way is that density integrated along the line
// between the eye and the thing.
//
// The integral is done exactly, and it is done in two pieces because the fog
// has a floor. Below the floor the density is the same everywhere - the air
// of a valley is as thick at the bottom of it as it is at the top, and it
// does not get thicker and thicker as the ground goes down - and above the
// floor it falls away with the height.
//
// The line crosses the floor at one place at most, and the two pieces are the
// two halves of the line.
//
// THE PIECE BELOW THE FLOOR.
//
// The density there is the density of the floor itself, and the integral of a
// constant is the constant times the length.
//
// THE PIECE ABOVE THE FLOOR.
//
// The density at the floor times the falloff of the exponential, and the
// integral of an exponential is the exponential divided by the rate it falls
// at. The rate can be zero - a level ray - and then the fraction is one, so
// the form is written so that it is also right in that case.
//
// The result is the optical depth: how many times the light has to cross the
// air on its way, and the colour of the thing behind it is what is left of
// it after e^(-depth) of it.
float fogOpticalDepth(vec3 origin, vec3 direction, float distance) {
    float density = uFogDensity;
    if (density <= 0.0) return 0.0;

    float falloff = uFogFalloff;
    // The height of the eye over the floor of the fog, and how fast the line
    // climbs.
    float over = origin.y - uFogHeight;
    float climb = direction.y;

    // The density where the line starts, and how much of the line is in the
    // thick air below the floor.
    float start = density * exp(-falloff * max(over, 0.0));
    float length = distance;
    float below = 0.0;

    if (climb > 1e-6) {
        // The line goes up. It starts below the floor - or it does not, and
        // then the whole of it is in the thin air above.
        if (over < 0.0) {
            float cross = -over / climb;
            if (cross < distance) {
                // It leaves the thick air at the floor.
                below = cross;
                length = distance - cross;
                start = density;
            } else {
                // It never reaches the floor: all of it is in the thick air.
                below = distance;
                length = 0.0;
            }
        }
    } else if (climb < -1e-6) {
        // The line goes down. It starts above the floor - and then it reaches
        // the floor and stays in the thick air.
        if (over > 0.0) {
            float cross = over / -climb;
            if (cross < distance) {
                length = cross;
                below = distance - cross;
            }
        } else {
            below = distance;
            length = 0.0;
            start = density;
        }
    } else if (over < 0.0) {
        // A level line, below the floor: all of it is in the thick air.
        below = distance;
        length = 0.0;
        start = density;
    }

    // The piece in the thick air: the density of the floor times its length.
    float optical = density * below;

    // The piece in the air above the floor: the density where it starts, and
    // the falloff along it.
    if (length > 0.0) {
        float x = falloff * climb * length;
        float scale = (abs(x) < 1e-5) ? 1.0 : (1.0 - exp(-x)) / x;
        optical += start * length * scale;
    }

    return optical;
}

// How much of the fog is between the eye and a fragment, 0..1.
//
// One is fog you cannot see through and zero is no fog at all, and it is
// what is left of the thing behind it: the light that comes out of the other
// side of the air is e^(-depth) of the light that went in.
float fogAmount(vec3 origin, vec3 direction, float distance) {
    float depth = fogOpticalDepth(origin, direction, distance);
    if (depth <= 0.0) return 0.0;
    return 1.0 - exp(-depth);
}

// The phase function of the fog: how much of the light of the sun comes to
// the eye from the direction the eye is looking.
//
// The fog is made of small drops of water, and a drop throws the light of the
// sun forward. That is why the mist around the sun is bright and the mist on
// the other side of the sky is not, and it is why the sun in a fog is a lamp
// and not a hole in the sky.
//
// There are two of these lobes and not one.
//
// The forward lobe is the light that comes straight back at the eye - the
// aureole around the sun. `g` is how forward it is: zero is a fog that
// scatters equally in every direction, and 0.8 is a fog that throws the
// sun's light straight back at whoever looks at it.
//
// The backward lobe is the light that comes back from behind the drops, and
// it is what keeps the fog from going black on the side of the sun that is
// away from it.
//
// The two are mixed with weights that add up to one, and each of them is one
// on the average over the whole of the sky. So the fog is as bright on
// average as it would be with no scattering in it at all: the glow around
// the sun is paid for out of the air around it and not out of the light of
// the sun.
//
// The power is not pow(d, 1.5): the root is a quarter of the price of the
// power, and the number is the same.
float fogPhase(float cosTheta) {
    float g = clamp(uFogForward, 0.0, 0.95);
    float g2 = g * g;
    float forward = 1.0 + g2 - 2.0 * g * cosTheta;
    float forwardRoot = sqrt(max(forward, 1e-4));
    float ahead = (1.0 - g2) / (forwardRoot * forwardRoot * forwardRoot);

    // The backward lobe: the light that comes back from behind the drops.
    float b = 0.15;
    float backward = 1.0 + b * b + 2.0 * b * cosTheta;
    float backwardRoot = sqrt(max(backward, 1e-4));
    float behind = (1.0 - b * b) / (backwardRoot * backwardRoot * backwardRoot);

    return 0.8 * ahead + 0.2 * behind;
}

// The colour of the air in the direction the eye is looking.
//
// It is the light that is in the air: the light of the sky, which is the same
// everywhere, and the light of the sun, which the drops of water throw
// forward.
//
// The light of the sun is very bright close to the sun and the picture has
// no room for it - a glow that is cut off is a flat white disc, and a glow
// that is rolled off is a glow. So it is rolled off and not cut, and what is
// left of it is a colour and not a hole in the picture.
vec3 fogColour(vec3 direction) {
    float phase = fogPhase(dot(direction, uSunDirection));
    vec3 sun = uSunColour * (phase * uFogScatter);
    sun = sun / (1.0 + sun * 0.35);
    return uFogColour + sun;
}

// The grain.
//
// The picture is eight bits and the fog is a very slow change of the colour,
// and the eye finds the steps: a sky with a fog in it is a set of bands, and
// the bands are the one thing in the picture that is not in the world.
//
// A little noise, different for every fragment and the same for every frame,
// turns the bands into a grain - and a grain is what the sky really has.
//
// It is the interleaved gradient noise: a line of pixels turned into a
// number between zero and one, with no two neighbouring fragments landing on
// the same one.
float fogGrain() {
    vec2 p = gl_FragCoord.xy;
    return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

// The colour of a thing, with the air in front of it mixed in.
//
// The fog is the last thing that happens to a fragment: the sun has already
// lit it, the shadow has already fallen on it, and what the air does to it on
// the way to the eye is the last word.
//
// The mix is not a straight line between two colours: the thing behind the
// air is multiplied by what is left of it, and the light of the air is added
// on top. Those are the same thing written out, and writing them out is
// what lets the grain be added to the right place.
vec3 applyFog(vec3 colour, vec3 worldPosition) {
    // No fog at all: the picture is the picture. This is a test and not a
    // branch on a number that changes from fragment to fragment, so the whole
    // picture takes it the same way and the work is not done at all.
    if (uFogDensity <= 0.0) return colour;

    vec3 ray = worldPosition - uCameraPosition;
    float distance = length(ray);
    vec3 direction = (distance > 1e-4) ? ray / distance : vec3(0.0, 0.0, 1.0);

    float amount = clamp(fogAmount(uCameraPosition, direction, distance), 0.0, 1.0);
    vec3 air = fogColour(direction);

    // What is left of the thing behind the air, and the light of the air on
    // top of it.
    vec3 result = colour * (1.0 - amount) + air * amount;

    // The grain, and only where the air is: the bands are in the air, and the
    // grain is put where the bands are.
    result += (fogGrain() - 0.5) * amount * (1.0 / 255.0);

    return result;
}
