#pragma once

namespace s3d {

// THE NOISE
//
// Real noise, in the sense that matters for ground: it is continuous, it has
// no steps in it, and the same seed gives the same picture on every machine.
//
// The old noise was a hash of a float - a sine of a product, wrapped into
// 0..1. That is a picture of noise, not noise: the hash has no neighbours, so
// the value at one point says nothing about the value next to it, and the
// ground came out as a field of cubes with rounded corners. What is here
// instead is Perlin's gradient noise: a lattice of points, each with a
// direction, and the value between two of them is read from the directions
// they point in. The value at a point depends on its neighbours, which is
// exactly what ground does.
//
// Everything is deterministic and seedable: the same seed is the same
// landscape on every machine, so a project that is shared is the same project.

// A number in 0..1 for a lattice point.
//
// The hash is on the INTEGERS of the lattice, not on the coordinates. A hash
// of a float is a hash of a rounding, and the rounding moves when the ground
// is scaled - the same landform would be a different one at another size.
unsigned int hashLattice(int x, int y, unsigned int seed);

// The unit vector at a lattice point: one of eight directions, chosen by the
// hash. A lattice point always has the same direction, for every seed and on
// every machine.
void latticeGradient(int x, int y, unsigned int seed, float& gx, float& gy);

// Perlin's gradient noise: -1..1.
//
// Zero at the lattice points, and it fades to zero there, so the ground has
// no seams in it where the samples happen to land.
float perlinNoise(float x, float y, unsigned int seed);

// The noise in octaves: each one twice as large as the last and half as
// strong. This is fractal Brownian motion, and it is what a piece of ground
// wants - one shape with smaller shapes on it. 0..1.
float fbm(float x, float y, unsigned int seed, int octaves = 4, float lacunarity = 2.0f,
          float gain = 0.5f);

// The same octaves, folded: the zero lines of the noise become the crests,
// and every octave is held back by the one before it. This is what makes a
// mountain a mountain and not a lump. 0..1.
float ridgedFbm(float x, float y, unsigned int seed, int octaves = 4, float lacunarity = 2.0f,
                float gain = 0.5f);

// The octaves with the sign taken off: round hills and round valleys, which
// is what sand and dunes are made of. 0..1.
float billowFbm(float x, float y, unsigned int seed, int octaves = 4, float lacunarity = 2.0f,
                float gain = 0.5f);

// Cellular noise: the distance to the nearest of a scattering of points. The
// cracks between the cells are what it is good at. 0..1.
float cellularNoise(float x, float y, unsigned int seed);

// The noise, read at coordinates that another noise has moved.
//
// The warp is two noises reading two more noises, and the shape that comes
// out has edges that bend and folds in on themselves. Without it the ground
// looks like a sponge; with it, it looks like something the wind has been
// working on for a long time. 0..1.
float warpedFbm(float x, float y, unsigned int seed, int octaves = 4, float strength = 1.0f);

} // namespace s3d
