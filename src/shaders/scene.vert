#version 330 core
// Licensed under the Blue Oak Model License 1.0.0.

// The scene's vertex stage.
//
// It does three things and nothing else: it puts the vertex in the world, it
// turns the normal with it, and it works out where the vertex is in the light's
// shadow map. Everything the fragment stage needs comes out of here, so the
// fragment stage never touches a matrix.

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

// One matrix per cascade: the light's view and projection, fitted to the
// stretch of the scene that the cascade covers.
uniform mat4 uLightViewProjection[4];
// Where the stretches end, in the distance from the eye. The cascade a
// fragment is in is the one whose stretch it is in.
uniform float uCascadeSplits[4];
uniform int uCascadeCount;

out vec3 vWorldPosition;
out vec3 vNormal;
out vec2 vTexCoord;
out vec3 vColor;
// The place of the fragment in the light's map, in the light's own clip space.
// The fragment stage divides it by w and compares the result with the map.
out vec4 vShadowCoord;
// Which stretch of the scene the fragment is in. Flat: every fragment of a
// triangle is in the same one, and the fragment stage would rather know that
// than interpolate it.
flat out int vCascade;

void main() {
    const vec4 world = uModel * vec4(aPosition, 1.0);
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

    // The cascade: the stretch of the scene the fragment is in, measured from
    // the eye. The loop is over three and not over uCascadeCount: a loop with
    // a dynamic bound is the one thing a shader cannot unroll, and the number
    // of cascades is the same for every vertex of the frame.
    const float viewDepth = -(uView * world).z;
    int cascade = 0;
    for (int i = 0; i < 3; ++i) {
        if (i < uCascadeCount - 1 && viewDepth > uCascadeSplits[i]) cascade = i + 1;
    }
    vCascade = cascade;

    vShadowCoord = uLightViewProjection[cascade] * world;

    gl_Position = uProjection * uView * world;
}
