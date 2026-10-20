#version 330 core
// Licensed under the Blue Oak Model License 1.0.0.

// The shadow map on the screen: what the light sees.
//
// It is a full-screen quad and nothing else. The picture is the map, read
// layer by layer, and the layer is the one the engine is looking at - the
// first one, unless the engine is told to look at another.

layout(location = 0) in vec2 aPosition;

out vec2 vUv;

void main() {
    // The quad covers the screen, and the map is read in the same square:
    // the picture is the map, and the map is a square.
    vUv = aPosition * 0.5 + 0.5;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
