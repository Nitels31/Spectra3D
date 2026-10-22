#version 330 core
// Licensed under the Blue Oak Model License 1.0.0.

// THE WATER'S VERTEX STAGE.
//
// A water surface is a sheet of points that move. This stage does three
// things: it puts the point in the world, it moves it with the waves, and it
// works out which way the water faces at that point - which is not the
// direction of the mesh, because the waves are what turn the water over.
//
// THE WAVES
//
// Four of them, each a wave that travels in its own direction with its own
// length and its own height, crossing each other. Four waves that cross at
// different angles are what water looks like, and four *fixed* waves are
// what a picture of water has to be: the same surface every time, and a
// surface that moves.
//
// The shape of a wave is not a sine. A Gerstner wave has a sharp crest and a
// flat trough, because the water at the top of the wave is thrown forward.
// The cost of that is that the normal is not the slope of the height any
// more - the wave moves the points sideways as well - so the normal is
// worked out of the two directions the surface runs in, which is what the
// derivative of the surface is.
//
// The speed of a wave follows from its length: a long wave travels faster
// than a short one, and the speed of the real thing is sqrt(g * k) for the
// wave number k. It is the one number in here that comes out of the
// physics and not out of a picture.
//
// THE SHORE
//
// The sheet knows where its own edge is, because the edge is where the
// grid ends. The water is shallow there and white where it breaks, and the
// fragment stage needs to know how far from the edge each point is: it is
// the only thing that says where the shore is without a picture of the
// ground under the water.

layout(location = 0) in vec3 aPosition;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;

// The time of the water, in seconds. The waves are a function of it and of
// nothing else.
uniform float uTime;
// How high the waves are, in metres.
uniform float uWaveHeight;
// How long they are, in metres: the distance from one crest to the next.
uniform float uWaveLength;
// How fast they travel. One is the speed of the real thing for this length.
uniform float uWaveSpeed;
// How sharp the crests are, 0..1. Zero is a flat sea, and one is a wave
// that folds over itself.
uniform float uWaveSteepness;
// The way the wind blows. The waves travel with it, and the direction is in
// the plane of the water.
uniform vec2 uWindDirection;

out vec3 vWorldPosition;
out vec3 vNormal;
// How high this point is above the still water, in metres.
out float vHeight;
// How much the wave is folding over itself here.
out float vFold;
// How far this point is from the edge of the sheet, 0 at the edge and 1 in
// the middle of it.
out float vEdge;

// What the waves do to a point of the surface.
struct Surface {
    // Where the wave moves the point to.
    vec3 offset;
    // Which way the water faces.
    vec3 normal;
    // How much the wave is folding over itself: the area of the piece of
    // water that was flat is now smaller than it was, and the water has to
    // go somewhere. It goes up as white water.
    float fold;
};

// The four waves: the directions they travel in, and how long and how high
// each of them is. The lengths and the heights are fractions of the ones the
// user asked for, so the water is the same shape whatever the size of it.
const vec2 kDirection[4] = vec2[4](
    vec2( 0.94,  0.34),
    vec2( 0.62, -0.78),
    vec2(-0.31,  0.95),
    vec2( 0.16, -0.99)
);
const float kLengthScale[4] = float[4](1.0, 0.62, 0.37, 0.21);
const float kHeightScale[4] = float[4](1.0, 0.55, 0.32, 0.18);

// The gravity of the world, in metres per second squared. It is the number
// that decides how fast a wave of a given length travels.
const float kGravity = 9.81;

// The waves at a point of the surface.
//
// The point comes in as the two numbers of the world it is at, and what comes
// out is where the wave moves it, which way the water faces there, and how
// much the wave is folding.
Surface waveSurface(vec2 xz) {
    // The wind turns the whole set of waves: the water does not care which
    // way the first wave went, only which way the wind blows.
    float windAngle = atan(uWindDirection.y, uWindDirection.x);
    float cs = cos(windAngle);
    float sn = sin(windAngle);

    vec3 offset = vec3(0.0);
    // The two directions the surface runs in. They start as the directions
    // of the flat water and every wave bends them.
    vec3 tangentX = vec3(1.0, 0.0, 0.0);
    vec3 tangentZ = vec3(0.0, 0.0, 1.0);

    for (int i = 0; i < 4; ++i) {
        // The direction of this wave, turned by the wind.
        vec2 direction = vec2(kDirection[i].x * cs - kDirection[i].y * sn,
                              kDirection[i].x * sn + kDirection[i].y * cs);

        float length = max(uWaveLength * kLengthScale[i], 0.01);
        float amplitude = uWaveHeight * kHeightScale[i];
        float k = 6.2831853 / length;
        float omega = sqrt(kGravity * k) * uWaveSpeed;

        // The steepness: how much of the wave the horizontal movement takes.
        //
        // It is divided by the number of waves and by the wave number, so
        // that the sharpest wave does not fold the surface on its own - and
        // it is capped at one, because a wave steeper than that is not a
        // wave any more.
        float q = 0.0;
        if (k * amplitude > 1e-5) q = min(uWaveSteepness / (k * amplitude * 4.0), 1.0);

        float phase = k * dot(direction, xz) - omega * uTime;
        float s = sin(phase);
        float c = cos(phase);

        // The point is thrown forward by the wave and lifted by it.
        offset.x += q * amplitude * direction.x * c;
        offset.z += q * amplitude * direction.y * c;
        offset.y += amplitude * s;

        // And the two directions of the surface are bent by the same wave:
        // the horizontal part of the wave shortens the surface where the
        // wave is steep, and that is where the water piles up.
        float qa = q * k * amplitude;
        tangentX.x -= qa * direction.x * direction.x * s;
        tangentX.y += direction.x * k * amplitude * c;
        tangentX.z -= qa * direction.x * direction.y * s;

        tangentZ.x -= qa * direction.x * direction.y * s;
        tangentZ.y += direction.y * k * amplitude * c;
        tangentZ.z -= qa * direction.y * direction.y * s;
    }

    Surface result;
    result.offset = offset;
    // The normal of the surface: the cross product of the two directions it
    // runs in. The order matters - the other way round, the water faces
    // down.
    result.normal = normalize(cross(tangentZ, tangentX));
    // How much of the surface is folded: the area of the piece of water that
    // was flat is now smaller than it was.
    //
    // The water is white where it folds, and it folds only where the wave is
    // steep enough to throw itself over - so the first tenth of the area
    // that is gone is not foam, it is a wave that is a little steeper than it
    // was.
    float area = tangentX.x * tangentZ.z - tangentX.z * tangentZ.x;
    result.fold = clamp((1.0 - area - 0.10) * 2.5, 0.0, 1.0);
    return result;
}

void main() {
    // The point in the world, before the waves. The mesh is put in the world
    // first, and the waves are the world's: two sheets of water in the same
    // place have the same waves in them, and a wave that belonged to the
    // mesh would be a different wave in every body.
    vec3 world = (uModel * vec4(aPosition, 1.0)).xyz;

    Surface surface = waveSurface(world.xz);
    world += surface.offset;

    vWorldPosition = world;
    vNormal = surface.normal;
    vHeight = surface.offset.y;
    vFold = surface.fold;

    // How far this point is from the edge of the sheet.
    //
    // The sheet is a grid that runs from -1 to 1 in both directions, and the
    // edge is where the grid ends: the distance to the nearest of the two
    // edges, in the sheet's own numbers. It is the shore - the water is thin
    // there and it breaks - and it is the only thing that says where the
    // shore is without a picture of the ground under the water.
    float toX = 1.0 - abs(aPosition.x);
    float toZ = 1.0 - abs(aPosition.z);
    vEdge = clamp(min(toX, toZ), 0.0, 1.0);

    gl_Position = uProjection * uView * vec4(world, 1.0);
}
