#version 330 core
// Licensed under the Blue Oak Model License 1.0.0.

// THE CLOUD'S VERTEX STAGE.
//
// The cloud is a volume, and a volume is not drawn - it is walked through.
//
// What is drawn is the box the cloud is in. The box is drawn with its front
// faces taken away, so that the back faces are the ones on the screen, and
// the back faces are there even when the eye is inside the box. That is the
// whole reason for the culling: a cloud that is walked into has to still be
// a cloud when the eye is in it, and a box whose front faces are the ones
// on the screen is not there at all once the eye is inside it.
//
// The vertex stage therefore does almost nothing: it puts the point of the
// box in the world and lets it go. The ray is worked out in the fragment
// stage, where the distance is the distance and not a thing that was
// interpolated across a triangle - the same rule the world follows when it
// chooses the stretch of the scene a fragment belongs to.
//
// THE BOX AND THE CLOUD ARE THE SAME BOX.
//
// The box on the screen is the mesh of the cloud, and the cloud is walked
// through in the box of the cloud itself - the one uBoxMin and uBoxMax
// describe. They are the same box, and they have to be: a box that is a
// little bigger than the cloud is a box whose edges are in the wrong place,
// and the walk through it would start outside the cloud.

layout(location = 0) in vec3 aPosition;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

out vec3 vWorldPosition;

void main() {
    // The point of the box in the world. The box is put in the world first,
    // and the walk through the cloud is in the world's own numbers - the
    // cloud is a thing of the world and not a thing of the mesh.
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPosition = world.xyz;

    gl_Position = uProjection * uView * world;
}
