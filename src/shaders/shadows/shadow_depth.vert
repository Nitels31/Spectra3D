#version 330 core
// Licensed under the Blue Oak Model License 1.0.0.

// The shadow map's own pass: the place in the map where the light hits
// first.
//
// The map holds a distance and not a depth of the window. A directional light
// has an orthographic projection, so the distance in the map is the distance
// in the world: a shadow is then one comparison of two numbers that mean the
// same thing, and the number that is compared is the same for every light
// and every map.

layout(location = 0) in vec3 aPosition;

uniform mat4 uModel;
// The light's view and projection, fitted to the stretch of the scene that
// this cascade covers.
uniform mat4 uLightViewProjection;

out float vDistance;

void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vec4 light = uLightViewProjection * world;

    // The distance from the light, in the map's own units: 0 at the near
    // plane of the light and 1 at the far one. The light is orthographic, so
    // this is a straight line and not a curve, and the map can be compared
    // with a distance measured the same way.
    vDistance = light.z * 0.5 + 0.5;

    gl_Position = light;
}
