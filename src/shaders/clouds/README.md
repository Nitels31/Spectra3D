# The clouds

The sky's own weather. This folder is where the shader of a cloud lives, and
nothing else: the cloud of the engine today is made on the CPU and drawn with
the scene's program, and the shader that draws it properly is the work of
whoever takes this folder.

```
src/shaders/clouds/
  README.md       what a cloud shader is given and what it has to do
```

## What the engine has today

A cloud is **a mesh**. `src/scene/Cloud.cpp` makes it - a heap of puffs, each
one a little ball of water - and the scene's program draws it like anything
else: lit by the sun, in the shadow map, with the fog of
`../fog/fog.glsl` in front of it.

The settings of a cloud are in `include/assets/CloudSettings.hpp`:

| Setting | What it is |
|---|---|
| `mode` | `Flat` (a picture on a plane) or `Puffy` (a volume) |
| `shape` | `Cumulus`, `Stratus`, `Cirrus`, `Cumulonimbus` |
| `seed` | the same seed is the same cloud on every machine |
| `puffs`, `puffSize` | how many balls and how big they are |
| `spread`, `height` | how wide and how tall the cloud is, in metres |
| `coverage` | how much of the sky it takes, 0..1 |
| `softness` | how much the puffs differ from one another, 0..1 |
| `detail` | how many octaves of noise the shape is made of |
| `resolution` | how finely a puff is made - this is what the cost is made of |
| `color`, `shade` | the colour of it where the sun reaches it, and where it does not |
| `threads` | how many threads the making may use |

The panel that edits them is `src/editor/panels/CloudPanel.cpp`.

## What a cloud shader would be given

The same things the scene's program is given, and two of its own:

| Uniform | What it is |
|---|---|
| `uModel`, `uView`, `uProjection` | the camera and the cloud |
| `uCameraPosition` | where the eye is |
| `uSunDirection`, `uSunColour` | the sun |
| `uSkyColour`, `uGroundColour` | the light of the sky and the bounce of the ground |
| `uAmbient` | how much of it there is |
| `uShadowMap`, `uLightViewProjection[4]`, `uCascadeParams[4]` | the sun's map, if the cloud is in it |
| `uCloudColour`, `uCloudShade` | the two colours of the cloud |
| `uCloudCoverage`, `uCloudSoftness` | how much of the sky it takes, and how ragged its edge is |

And the air in front of it is the same air as everything else:

```
#include "../fog/fog.glsl"
```

The fog is the sky's own weather too, and a cloud that is not in the fog is a
cloud that is not in the world.

## What a cloud shader has to do

- **The light of the sun is not the same on every side of a cloud.** The top
  of it is white and the bottom of it is grey, and the grey is not the
  colour of the cloud - it is the light of the sun that did not reach it.
- **The edge of a cloud is soft and not round.** A puff of spheres is a
  cloud of spheres, and the picture has to hide that.
- **A cloud has a shadow.** The shadow of a cloud on the ground is one of
  the biggest things in a picture of the weather, and it moves.
