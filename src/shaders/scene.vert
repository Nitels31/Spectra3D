#version 330 core
// Licensed under the Blue Oak Model License 1.0.0.

// The scene's vertex stage.
//
// It does two things and nothing else: it puts the vertex in the world and it
// turns the normal with it. Everything the fragment stage needs comes out of
// here, so the fragment stage never touches a matrix of its own - except the
// one matrix of the light, which is picked per fragment and is therefore not
// picked here.

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
// The colour of the vertex, when the mesh carries colours of its own - the
// ground does, and it is coloured by its height and its steepness. A mesh
// without them is drawn with the white of this default.
layout(location = 3) in vec3 aColor;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vWorldPosition;
out vec3 vNormal;
out vec2 vTexCoord;
out vec3 vColor;

void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPosition = world.xyz;

    // The normal is turned by the model matrix and not by the view matrix: the
    // light is in the world, and a light that moved with the camera would
    // light the scene from wherever the camera is.
    //
    // The full matrix and not its upper left three: the engine scales a mesh
    // evenly and turns it, and a matrix that only turns and scales evenly
    // turns the normals correctly. A scale that is not the same on every axis
    // would need the inverse transpose, and the engine has none.
    vNormal = mat3(uModel) * aNormal;

    vTexCoord = aTexCoord;
    vColor = aColor;

    gl_Position = uProjection * uView * world;
}
