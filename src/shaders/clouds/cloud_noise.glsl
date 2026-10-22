// THE NOISE OF THE CLOUD
//
// Three noises, and they are not the same noise.
//
//   THE WEATHER  - where the cloud is thick and where it is thin. It is
//                  large, it is smooth, and it is the same every time.
//   THE DETAIL   - the fine structure at the edges of the cloud: the ragged
//                  fringe, the wisps, the cauliflower. It is small and it is
//                  sharp, and it only has to look like noise.
//   THE GRADIENTS - the twelve edges of a cube, which is where the gradient
//                  noise reads its directions from.
//
// THE WEATHER IS THE ENGINE'S OWN NOISE.
//
// The engine's noise on the CPU is Perlin's gradient noise in three
// dimensions, and the heap of puffs a cloud is made of is placed with it.
// The noise in this file is that noise, written out again for the card: the
// same hash, the same twelve directions, the same octaves, the same curve
// between the lattice points.
//
// It matters that they are the same. A cloud that is a different shape on
// the card than it is in the engine is a cloud that is not the cloud that
// was made, and nobody would know where the difference came from.
//
// The hash is on the INTEGERS of the lattice and not on the coordinates, and
// that is the same rule as on the CPU: a hash of a float is a hash of a
// rounding, and the rounding moves when the cloud is scaled - the same cloud
// would be a different one at another size.
//
// The detail is a different noise on purpose. It is the expensive part of
// the picture and it is the part nobody compares with anything: it is
// worked out for every step of every ray, and it is the first thing that is
// left out where the cloud is not there at all.

// The twelve edges of a cube: the directions of the lattice points.
//
// The eight directions of the flat noise cannot be used here - a noise made
// of them has a grain along the axis that has no direction of its own, and
// the third dimension of a cloud is exactly where the eye looks for the
// grain. The edges of the cube have none: every one of the twelve has a
// part in all three axes.
//
// The order is the order of the CPU's own table, and the two have to be the
// same order: the twelfth edge is the twelfth edge.
const vec3 kCloudGradients[12] = vec3[12](
    vec3( 0.70710678,  0.70710678,  0.00000000),
    vec3(-0.70710678,  0.70710678,  0.00000000),
    vec3( 0.70710678, -0.70710678,  0.00000000),
    vec3(-0.70710678, -0.70710678,  0.00000000),
    vec3( 0.70710678,  0.00000000,  0.70710678),
    vec3(-0.70710678,  0.00000000,  0.70710678),
    vec3( 0.70710678,  0.00000000, -0.70710678),
    vec3(-0.70710678,  0.00000000, -0.70710678),
    vec3( 0.00000000,  0.70710678,  0.70710678),
    vec3( 0.00000000, -0.70710678,  0.70710678),
    vec3( 0.00000000,  0.70710678, -0.70710678),
    vec3( 0.00000000, -0.70710678, -0.70710678)
);

// The seed of one octave.
//
// A stride that is not a small number: two octaves with seeds next to each
// other would share their lattice, and the noise would come out with the
// same shape in both.
const uint kCloudOctaveStride = 1013u;

// The hash of a lattice point.
//
// The same mix as on the CPU: the three coordinates are mixed in one after
// the other and not combined into one number first, because combining them
// would make (1,2,3) and (3,2,1) the same point, and a cloud that is the
// same cloud from two sides is a cloud with a mirror in it.
//
// The multiply-xor-shift chain at the end is what makes it avalanche:
// without it, neighbouring lattice points give neighbouring hashes, and
// neighbouring hashes give a noise that has a grain in it - stripes along
// one axis.
uint cloudHash(ivec3 point, uint seed) {
    uint h = uint(point.x) * 0x8DA6B343u;
    h ^= uint(point.y) * 0xD8163841u;
    h ^= uint(point.z) * 0xCB1AB31Fu;
    h += seed * 0x165667B1u;
    h ^= h >> 15u;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12u;
    h *= 0x297A2D39u;
    h ^= h >> 15u;
    return h;
}

// A number in 0..1 out of the hash.
//
// The twenty-four bits at the bottom are the ones that are used, and they are
// divided by the number that twenty-four bits can hold - the same rule as
// the CPU's own hash, so that the two pictures are the same picture.
float cloudHashUnit(ivec3 point, uint seed) {
    return float(cloudHash(point, seed) & 0x00FFFFFFu) / 16777215.0;
}

// The curve between two lattice points.
//
// The quintic and not the smoothstep: the smoothstep is continuous but its
// slope jumps at the lattice points, and a jump in the slope is a crease -
// a line you can see across a hill, or across a cloud.
vec3 cloudFade(vec3 t) {
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

// Perlin's gradient noise in three dimensions: -1..1.
//
// The same rule as on the CPU: zero at the lattice points, and it fades to
// zero there, so a cloud has no seams in it where the samples happen to
// land.
float cloudPerlin3(vec3 p, uint seed) {
    ivec3 cell = ivec3(floor(p));
    vec3 f = p - vec3(cell);
    vec3 u = cloudFade(f);

    // The eight corners of the cube the point is in, and what the gradient
    // at each of them says about the way to it.
    vec3 g0 = kCloudGradients[cloudHash(cell + ivec3(0, 0, 0), seed) % 12u];
    vec3 g1 = kCloudGradients[cloudHash(cell + ivec3(1, 0, 0), seed) % 12u];
    vec3 g2 = kCloudGradients[cloudHash(cell + ivec3(0, 1, 0), seed) % 12u];
    vec3 g3 = kCloudGradients[cloudHash(cell + ivec3(1, 1, 0), seed) % 12u];
    vec3 g4 = kCloudGradients[cloudHash(cell + ivec3(0, 0, 1), seed) % 12u];
    vec3 g5 = kCloudGradients[cloudHash(cell + ivec3(1, 0, 1), seed) % 12u];
    vec3 g6 = kCloudGradients[cloudHash(cell + ivec3(0, 1, 1), seed) % 12u];
    vec3 g7 = kCloudGradients[cloudHash(cell + ivec3(1, 1, 1), seed) % 12u];

    float c0 = dot(g0, f - vec3(0.0, 0.0, 0.0));
    float c1 = dot(g1, f - vec3(1.0, 0.0, 0.0));
    float c2 = dot(g2, f - vec3(0.0, 1.0, 0.0));
    float c3 = dot(g3, f - vec3(1.0, 1.0, 0.0));
    float c4 = dot(g4, f - vec3(0.0, 0.0, 1.0));
    float c5 = dot(g5, f - vec3(1.0, 0.0, 1.0));
    float c6 = dot(g6, f - vec3(0.0, 1.0, 1.0));
    float c7 = dot(g7, f - vec3(1.0, 1.0, 1.0));

    // The blending, one axis at a time.
    float x00 = c0 + u.x * (c1 - c0);
    float x10 = c2 + u.x * (c3 - c2);
    float x01 = c4 + u.x * (c5 - c4);
    float x11 = c6 + u.x * (c7 - c6);
    float y0 = x00 + u.y * (x10 - x00);
    float y1 = x01 + u.y * (x11 - x01);

    // The factor puts the result back into -1..1: the raw sum of eight dot
    // products of unit vectors cannot reach the ends of the range.
    float value = (y0 + u.z * (y1 - y0)) * 1.15470054;
    return value < -1.0 ? -1.0 : (value > 1.0 ? 1.0 : value);
}

// The noise in octaves, in three dimensions: 0..1.
//
// Each octave is twice as large as the last and half as strong, and the
// sum is divided by the sum of the strengths - so the noise is in 0..1
// whatever the number of octaves is.
//
// This is the CPU's `fbm3` written out: the same octaves, the same stride
// between their seeds, the same halving of the strength. A cloud that is
// made on the CPU and drawn on the card is one cloud.
float cloudFbm3(vec3 p, uint seed, int octaves) {
    if (octaves < 1) octaves = 1;
    float sum = 0.0;
    float amplitude = 1.0;
    float total = 0.0;
    for (int i = 0; i < octaves; ++i) {
        sum += cloudPerlin3(p, seed + uint(i) * kCloudOctaveStride) * amplitude;
        total += amplitude;
        p *= 2.0;
        amplitude *= 0.5;
    }
    if (total <= 0.0) return 0.5;
    return clamp((sum / total) * 0.5 + 0.5, 0.0, 1.0);
}

// THE CHEAP NOISE
//
// The value noise: the value at a point, read from the lattice around it.
//
// It is eight hashes and no directions at all, and it is not as smooth as
// the gradient noise - it has a little square under every octave, and the
// squares are what the fine structure of a cloud wants.
//
// The hash is a different one: the same mix, but out of a smaller number,
// so that the fine structure is not the weather with a different scale. A
// cloud whose detail is a copy of its shape looks like a cloud with a
// pattern on it.
float cloudValue3(vec3 p, uint seed) {
    ivec3 cell = ivec3(floor(p));
    vec3 f = p - vec3(cell);
    vec3 u = cloudFade(f);

    float c0 = cloudHashUnit(cell + ivec3(0, 0, 0), seed);
    float c1 = cloudHashUnit(cell + ivec3(1, 0, 0), seed);
    float c2 = cloudHashUnit(cell + ivec3(0, 1, 0), seed);
    float c3 = cloudHashUnit(cell + ivec3(1, 1, 0), seed);
    float c4 = cloudHashUnit(cell + ivec3(0, 0, 1), seed);
    float c5 = cloudHashUnit(cell + ivec3(1, 0, 1), seed);
    float c6 = cloudHashUnit(cell + ivec3(0, 1, 1), seed);
    float c7 = cloudHashUnit(cell + ivec3(1, 1, 1), seed);

    float x00 = c0 + u.x * (c1 - c0);
    float x10 = c2 + u.x * (c3 - c2);
    float x01 = c4 + u.x * (c5 - c4);
    float x11 = c6 + u.x * (c7 - c6);
    float y0 = x00 + u.y * (x10 - x00);
    float y1 = x01 + u.y * (x11 - x01);
    return y0 + u.z * (y1 - y0);
}

// The fine structure of the cloud.
//
// Two octaves of the cheap noise, folded: the zero lines of the noise become
// the crests, and every octave is held back by the one before it. The result
// is the billow - the heaps and the hollows - and the heaps are the
// cauliflower on the top of a cumulus.
float cloudDetail3(vec3 p, uint seed) {
    float first = 1.0 - abs(cloudValue3(p, seed) * 2.0 - 1.0);
    float second = 1.0 - abs(cloudValue3(p * 2.7, seed + 5171u) * 2.0 - 1.0);
    return clamp(first * 0.75 + second * 0.25, 0.0, 1.0);
}
