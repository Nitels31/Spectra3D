#pragma once

namespace s3d {

// A value noise, deterministic and seedable: the same seed is the same picture
// on every machine, so a landform made from it is the same landform everywhere.
//
// It is a picture and not a measurement, so the hash behind it is cheap and
// does not care how it turns out.
float valueNoise(float x, float y, unsigned seed);

// The same noise in octaves: one is a cloud, four are a rock, and the sum is
// what a piece of ground wants.
float fbm(float x, float y, unsigned seed, int octaves = 4);

} // namespace s3d