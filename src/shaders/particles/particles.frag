#version 330 core
// Licensed under the Blue Oak Model License 1.0.0.

// The particles' fragment stage.
//
// A particle is a soft round blob. The picture of a puff of smoke or of a
// spark is a circle that fades away at its edge, and a square with hard
// edges is a square with hard edges - which is what a particle without this
// stage would be.
//
// The particles are not lit. A fire is its own light, and a shadow map of a
// puff of smoke is a shadow of a puff of smoke.

in vec2 vCorner;
in vec3 vColour;
in float vAlpha;
in vec3 vWorldPosition;

uniform vec3 uCameraPosition;
// The sun. The fog is lit by it, and the particles stand in it.
uniform vec3 uSunDirection;
uniform vec3 uSunColour;

// 0 for a particle that is painted over the picture, 1 for one that is added
// to it.
uniform int uAdditive;

// The air in front of a particle is the same air as the air in front of
// everything else in the picture, and it is in a file of its own: fog/
// fog.glsl. The loader puts the file where this line is.
//
// It is put in here and not at the top of the file because GLSL wants a thing
// to be declared before it is used, and the fog is lit by the sun: the sun is
// declared above.
#include "../fog/fog.glsl"

out vec4 fragColour;

void main() {
    // How far from the middle of the blob this fragment is: 0 in the middle
    // and 1 at the edge of the quad.
    float blob = length(vCorner) * 2.0;

    // The soft edge. The blob is solid in the middle and gone at the edge,
    // and the falloff is smooth because a hard edge is the one thing a
    // particle must not have.
    float soft = 1.0 - smoothstep(0.25, 1.0, blob);
    float alpha = vAlpha * soft;
    if (alpha <= 0.002) discard;

    vec3 colour = vColour;

    // The air in front of the particle.
    //
    // What the fog does to a particle depends on what the particle is: a puff
    // of smoke is painted over the air and takes the colour of the air, and a
    // spark is light that the air scatters away - so a spark in a thick mist
    // is a dimmer spark and not a brighter one.
    if (uFogDensity > 0.0) {
        vec3 ray = vWorldPosition - uCameraPosition;
        float rayLength = length(ray);
        vec3 direction = (rayLength > 1e-4) ? ray / rayLength : vec3(0.0, 0.0, 1.0);
        float amount = clamp(fogAmount(uCameraPosition, direction, rayLength), 0.0, 1.0);
        if (uAdditive != 0) {
            // Light that the air scatters away. It does not become the
            // colour of the air: a spark in a mist is a dimmer spark.
            colour *= (1.0 - amount);
        } else {
            // A thing that is painted over the picture takes the colour of
            // the air in front of it - and the air in front of it is brighter
            // where the sun is behind it, so a puff of smoke on the side of
            // the sun is a puff of smoke with the sun in it.
            colour = mix(colour, fogColour(direction), amount);
        }
    }

    // The colour is written as it is and the alpha beside it, and the blend
    // function does the multiplying: the two kinds of particle are blended
    // differently and the difference is in the state, not in the picture.
    fragColour = vec4(colour, alpha);
}
