#version 330 core
// Licensed under the Blue Oak Model License 1.0.0.

// The shadow map's own pass, on the far side: the distance is written.
//
// One channel, and the red one: the map is a picture of distances, and the
// other three channels are wasted space. The engine sets the write mask to
// the red one, so the shader writes one channel and the driver writes one
// channel.

precision highp float;

in float vDistance;

layout(location = 0) out vec4 oDistance;

void main() {
    // The distance, in the map's units. The map is read with the filter on
    // in the soft modes, and a filtered distance is a distance that is
    // between two - which is exactly what a soft edge is.
    oDistance = vec4(vDistance, 0.0, 0.0, 1.0);
}
