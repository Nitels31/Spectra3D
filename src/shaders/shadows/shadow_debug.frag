#version 330 core
// Licensed under the Blue Oak Model License 1.0.0.

// The shadow map on the screen: what the light sees.
//
// The map holds distances, and a distance is a number nobody can read. The
// picture here is the distance drawn as a shade of grey: the near things are
// black, the far things are white, and the middle is the middle.
//
// A second picture is on top of it: the line where the far plane of the
// light is. Everything past that line is past the light, and the map says
// nothing about it.

uniform sampler2DArray uShadowMap;
uniform int uLayer;
// The far plane of the light, in the map's units.
uniform float uFarPlane;
// One over the size of the map, in texels.
uniform float uTexelSize;

in vec2 vUv;
out vec4 oColour;

void main() {
    // The map is a square and the screen is not, so the map is put in the
    // middle of the screen and the rest is the colour of the background.
    vec2 centred = vUv - 0.5;
    if (abs(centred.x) > 0.5 || abs(centred.y) > 0.5) {
        oColour = vec4(0.08, 0.09, 0.11, 1.0);
        return;
    }

    float distance = texture(uShadowMap, vec3(centred + 0.5, float(uLayer))).r;
    float shade = clamp(distance, 0.0, 1.0);

    // The grey of the distance, and the line of the far plane over it.
    vec3 colour = vec3(shade);
    float line = 1.0 - smoothstep(0.0, 1.5 * uTexelSize, abs(distance - uFarPlane));
    colour = mix(colour, vec3(1.0, 0.35, 0.25), line);

    oColour = vec4(colour, 1.0);
}
