# The fog

The air between the eye and the thing. One file, and every shader that has
air in front of it has this air.

```
src/shaders/fog/
  fog.glsl        the fog: how much of it is on the way, and what colour it is
```

## How it is put into a shader

```
#include "fog/fog.glsl"
```

The loader reads the line, finds the file and puts the file where the line
was. A shader in a folder of its own asks for it as `../fog/fog.glsl`.

**The line goes after the uniforms of the shader.** GLSL wants a thing to be
declared before it is used, and the fog is lit by the sun: the sun has to be
declared before the fog is put in.

A shader that has the fog in it needs three uniforms of its own:

| Uniform | What it is |
|---|---|
| `uCameraPosition` | where the eye is: the fog is measured from there |
| `uSunDirection` | towards the sun: the fog is lit by it |
| `uSunColour` | the colour of the sun's light |

and it is given these:

| Uniform | What it is |
|---|---|
| `uFogColour` | the colour of the air |
| `uFogDensity` | how thick it is where it is thickest, 1/m |
| `uFogFalloff` | how fast it thins as it goes up, 1/m |
| `uFogHeight` | the height at which it is thickest, m |
| `uFogScatter` | how much of the sun's light it carries, 0..1 |
| `uFogForward` | how forward it throws that light, 0..0.95 |

## What is in it

### How much of it is on the way

The density of the fog at a height `h`:

```
d(h) = density * exp(-falloff * max(h - fogHeight, 0))
```

and the amount of it between the eye and a fragment is that density
integrated along the line between them - **exactly**, and in two pieces,
because the fog has a floor:

- below the floor the density is the density of the floor, and the integral
  of a constant is the constant times the length;
- above the floor the density falls away with the height, and the integral of
  an exponential is the exponential divided by the rate it falls at.

The line crosses the floor at one place at most, and the two halves of the
line are the two pieces. A ray that looks down into a hollow gets the thick
air of the floor and not an exponential that runs away.

`density` is read as a distance: the fog halves itself every `0.69 / density`
metres. Zero is no fog at all.

### What colour it is

The colour of the light that is in the air:

- the **light of the sky**, which is `uFogColour` and is the same everywhere;
- the **light of the sun**, which the drops of water throw forward, and which
  is the phase function of that scattering.

The phase function has **two lobes**. The forward one is the aureole around
the sun - `g` is how forward it is, and it is `uFogForward`. The backward one
is the light that comes back from behind the drops, and it is what keeps the
fog from going black on the side of the sun that is away from it.

The two are mixed with weights that add up to one, and each of them is one
on the average over the whole of the sky. So the fog is as bright on average
as it would be with no scattering in it at all: **the glow around the sun is
paid for out of the air around it and not out of the light of the sun.**

The light of the sun is rolled off and not cut: a glow that is cut off is a
flat white disc, and a glow that is rolled off is a glow.

### The grain

The picture is eight bits and the fog is a very slow change of the colour,
and the eye finds the steps: a sky with a fog in it is a set of bands. A
little noise - the interleaved gradient noise, different for every fragment
and the same for every frame - turns the bands into a grain, and a grain is
what the sky really has.

## What it costs

- **Nothing at all when there is no fog.** The whole thing is behind one
  test, and the test is the same for every fragment of the frame.
- **One length and one square root** for the line to the fragment.
- **One exponential** for the integral of the density, and **two** for the
  phase function.

The functions are:

| Function | What it gives |
|---|---|
| `fogOpticalDepth(origin, direction, distance)` | the integral of the density along the line |
| `fogAmount(origin, direction, distance)` | how much of the fog is on the way, 0..1 |
| `fogPhase(cosTheta)` | how much of the sun's light comes back from that direction |
| `fogColour(direction)` | the colour of the air in that direction |
| `applyFog(colour, worldPosition)` | the colour of the thing with the air in front of it |
