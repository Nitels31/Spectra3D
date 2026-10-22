#version 330 core
// Licensed under the Blue Oak Model License 1.0.0.

// The particles' vertex stage.
//
// A particle is a quad that faces the camera: four corners around the place
// the particle is. The corners are the same four numbers for every particle
// and are written into their own buffer once - and the centre, the size and
// the colour of the particle are the same for all four of its corners, so
// they are read once per particle and not once per corner. That is what the
// divisor on the attributes is for.

layout(location = 0) in vec3 aCentre;    // where the particle is
layout(location = 1) in vec2 aSizeAlpha;  // how big it is, how see-through
layout(location = 2) in vec3 aColour;     // what colour it is
layout(location = 3) in vec2 aCorner;     // which corner of the quad

uniform mat4 uView;
uniform mat4 uProjection;

// The camera's own right and up, in the world.
//
// The quad is turned to face the eye, and it is turned with the camera's
// axes and not with the world's: a particle that always faces the world's
// axes is a card standing in the wind, and a puff of smoke seen edge-on is a
// line.
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;

out vec2 vCorner;
out vec3 vColour;
out float vAlpha;
out vec3 vWorldPosition;

void main() {
    // The size is the width of the whole quad, and the corner is a half
    // either side of the middle: a particle of size one is a metre across.
    float size = aSizeAlpha.x;
    vec3 world = aCentre + (uCameraRight * aCorner.x + uCameraUp * aCorner.y) * size;

    gl_Position = uProjection * uView * vec4(world, 1.0);

    vCorner = aCorner;
    vColour = aColour;
    vAlpha = aSizeAlpha.y;
    // Where this corner of the particle is in the world: the fog is measured
    // from the eye to the thing, and the thing is a corner of a quad and not
    // a point in the middle of it.
    vWorldPosition = world;
}
