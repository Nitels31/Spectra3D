#version 330 core
// Licensed under the Blue Oak Model License 1.0.0.

// THE CLOUD'S SHADOW: THE VERTEX STAGE.
//
// The shadow of a cloud on the ground is the same cloud seen from the sun.
//
// The box of the cloud is drawn from the sun's point of view - with the sun's
// own matrix, the one the shadow map is drawn with - and the fragment stage
// walks the ray of the sun through the cloud and writes down how much of the
// sun's light comes out the other side.
//
// The normal of the face comes out as well, because the fragment stage has
// to know which face the sun is looking at: the box is drawn from the other
// side, and the faces that are turned away from the sun are turned towards
// the eye.

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

// The cloud's own matrix.
uniform mat4 uModel;
// The sun's matrix for the stretch of the scene being drawn: the same
// matrix the shadow map is drawn with, so that a texel of the cloud's
// shadow and a texel of the shadow map are the same piece of the world.
uniform mat4 uLightViewProjection;

out vec3 vWorldPosition;
out vec3 vWorldNormal;

void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPosition = world.xyz;
    // The normal is put through the matrix as a direction and not as a
    // point: the translation of the matrix is not part of a direction.
    vWorldNormal = mat3(uModel) * aNormal;

    gl_Position = uLightViewProjection * world;
}
