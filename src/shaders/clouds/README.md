# The clouds

The sky's own weather. This folder is where the shader of a cloud lives.

```
src/shaders/clouds/
  README.md           what a cloud shader is given and what it has to do
  clouds.vert         the cloud: the vertex stage, the box
  clouds.frag         the cloud: the fragment stage, the walk through the box
  cloud_noise.glsl    the noise the cloud is made of
  cloud_volume.glsl   the volume: the box, and how much of it there is
  cloudshadow.vert    the shadow of a cloud: the vertex stage, from the sun
  cloudshadow.frag    the shadow of a cloud: the fragment stage, the light
                      that comes through
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

## What a cloud shader has to do

- **The light of the sun is not the same on every side of a cloud.** The top
  of it is white and the bottom of it is grey, and the grey is not the
  colour of the cloud - it is the light of the sun that did not reach it.
- **The edge of a cloud is soft and not round.** A puff of spheres is a
  cloud of spheres, and the picture has to hide that.
- **A cloud has a shadow.** The shadow of a cloud on the ground is one of
  the biggest things in a picture of the weather, and it moves.

## The cloud that is in this folder

A cloud is a **volume**, and a volume is not drawn - it is walked through.

The shader draws the box the cloud is in, and then walks a ray through it:
at every step it asks the sky how much cloud is at that place, asks how much
of the sun's light can get through the cloud to that place, and puts the
two together. The Beer-Lambert law is what takes the light away as the ray
goes deeper, and it is the same law that makes the air eat the colour of the
far things.

There is no puff in it and no sphere: the shape of the cloud comes out of the
noise and out of the envelope, and the edge of it is eaten into by a finer
noise, so the edge is soft and ragged and not round.

### The box

The cloud is a box, and the box is not a cube: a heap of puffs is wide and
low, and a tower is narrow and tall.

The box is in the cloud's own numbers - `uBoxMin` and `uBoxMax` - and the
body's matrix puts that box in the world. The three half-edges of the box
in the world are the three columns of the matrix, each one as long as the
box is wide in that direction, and they turn with the cloud: a cloud that is
turned in the world is turned in its own noise as well, and the shape of it
is the same shape from every side.

Everything inside the box is -1..1 on every axis: the middle of the cloud is
(0,0,0), and the faces of the cloud are the faces of the box.

### The front faces are culled

The cloud is drawn with its front faces taken away, so that the back faces
are the ones on the screen.

The back faces are there even when the eye is inside the box, and that is
the whole reason for it: a cloud that is walked into has to still be a cloud
when the eye is in it, and a box whose front faces are the ones on the
screen is not there at all once the eye is inside it.

### The weather, and the detail

The **weather** is where the cloud is thick and where it is thin. It is the
engine's own noise - Perlin's gradient noise in three dimensions - written
out again for the card in `cloud_noise.glsl`: the same hash, the same twelve
directions, the same curve between the lattice points, the same octaves and
the same stride between their seeds.

It matters that they are the same. A cloud that is a different shape on the
card than it is in the engine is a cloud that is not the cloud that was
made, and nobody would know where the difference came from.

The weather can also be **a picture**: a three-dimensional texture, baked
once from the same noise by the C++ side, and read with one fetch instead
of thirty hashes. That is the fast path, and it is the one to use when
there is more than one cloud in the picture.

The **detail** is the fine structure at the edges of the cloud: the ragged
fringe, the wisps, the cauliflower. It is a cheaper noise - it only has to
look like noise - and it is worked out only where there is something for it
to eat into, which is the whole of its cost.

### The base and the top of the cloud

The base of a heap of puffs is a line: the water in it that is cold enough
to fall out of it does not fall out of it halfway up.

**The top of it is not a plane and not a curve: it is the shape of the
weather.** The cloud reaches as high as the weather lets it reach at that
place, and the weather is different in every place - so the top of the
cloud is a set of lumps, and that is where the cauliflower comes from. A
cloud whose top is drawn by a curve is a cloud with a smooth top, and no
such thing is in the sky.

### The fine structure cuts and it does not fade

The fine structure eats into the density of the cloud, and it eats in
**hard**: the cloud is cut off at the level of the erosion and not faded
into it.

This is the whole difference between a cloud and a puff of smoke. A cloud
whose edge fades away is a cloud of smoke, and the cauliflower of a real
cloud is a set of hard steps - the light ends at them and the sky begins.

### The four shapes are four densities and four silhouettes

| Shape | Density | The top of it |
|---|---|---|
| a heap of puffs | 1.0 | as high as the weather lets it |
| a layer | 0.85 | a flat thing in the middle of the box |
| streaks of cirrus | 0.5 | a thin veil |
| a tower | 1.0 | the top of the box, and an anvil |

**The cirrus is barely there, and that is the point of it.** It is a few
crystals of ice and the sun goes straight through it: the sky is blue where
the cirrus is and white where it is thick. A cloud that is opaque
everywhere is a cloud of one kind, and the sky has four.

### The streaks are drawn out along the wind

The coordinates of the weather are turned into the wind's own frame, and
the one along the wind is made short - and the lumps of the weather are
stretched with it.

A streak of cirrus is a streak because the wind drew it out, and without
this it is a lump like any other.

### The bar across the weather

The weather has to be turned into "how much cloud there is here", and the
turning is a bar put across it: above the bar there is cloud and below it
there is none.

**The bar is in the units of the weather's own variation, and that matters
more than it looks.** The noise of the engine is a *flat* one: it sits
about the middle of its range and the lumps in it are small - the weather
of a cloud with four octaves in it moves by about a tenth of its range. A
bar of a fixed size is a bar that is wrong the moment the weather is made
of a different number of octaves: a cloud made of a flat noise with a fixed
bar is either nothing at all or the whole sky.

So the bar is worked out from the number of octaves - each octave is half
as strong as the one before it, and the sum of them is the smaller for it -
and the coverage says how much of the sky is on the other side of it.

### The envelope is a limit and not a shape

The envelope says how far the cloud reaches at a height, and it only keeps
the cloud from spreading past the box it is in.

This is the whole difference between a cloud and a smear: a shape that cuts
the cloud off with a smooth curve puts a smooth curve in the picture, and
the eye finds it at once. **The edge of a cloud is made of the weather** -
of the lumps of it and of the fine structure that eats into them - and
nothing here is allowed to draw a curve instead.

### The light

Four things are mixed at every step of the walk:

- **The light through the cloud.** The sun's light has to come through the
  cloud to reach a point inside it, and how much of it comes through is
  found by walking a few steps towards the sun and adding up what is in the
  way. This is what makes the top of a cloud white and the bottom of it
  grey: the grey is not a colour, it is the light of the sun that did not
  reach it.
- **The bounce.** The light that comes out of a cloud has usually been
  inside it first. Three octaves of it are worked out: the light of the sun
  itself, the light that has bounced once, and the light that has been
  round the cloud many times - which is what lights the inside of it, and
  which is isotropic, because a light that has been round a cloud many
  times goes in every direction.
- **The powder.** The thin edges of the cloud are darker than the thick
  middle of it. A cloud lit from behind has a white rim and a darker one
  inside the rim.
- **The sky.** The light from above and the bounce off the ground, mixed by
  the height of the point in the cloud.

And the **phase**: how much of the light of the sun comes back at the eye
depends on the angle. Two lobes, like the fog's: the forward lobe is the
light that comes straight back at the eye - the silver lining round the
edge of a cloud when the sun is behind it - and the backward lobe is what
keeps the cloud from going black on the side of the sun that is away from
it.

The phase is **a quarter of what it is in the fog**, and the reason is that
a cloud is not a drop of water: it is a great many of them, and the light
that comes out of it has been round inside it many times. The phase says how
much of the sun's light comes back at the eye, and it is not the whole of
the brightness of the cloud.

### The two colours of the cloud

The sun's light is the colour where the sun reaches the cloud (`uCloudColour`)
and the light of the sky is the colour where it does not (`uCloudShade`). A
cloud at sunset is warm where the sun is on it and cold where it is not.

### The depth, and the blend

The depth that is written is the depth of the **first piece of the cloud**
the ray found, and not the depth of the back of the box: the back of the
box is under the ground that the cloud is above, and the ground would hide
the cloud.

The blend is **premultiplied**, the same as the water's:
`glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)`. The colour is written ready
to be added to the picture, and the bottom comes through what is left of the
transparency.

## What the C++ side has to give the shader

### The cloud's own program

| Uniform | What it is |
|---|---|
| `uModel`, `uView`, `uProjection` | the camera and the cloud |
| `uCameraPosition` | where the eye is |
| `uSunDirection`, `uSunColour` | the sun |
| `uSkyColour`, `uGroundColour` | the light of the sky and the bounce of the ground |
| `uAmbient` | how much of it there is |
| `uShadowMap`, `uLightViewProjection[4]`, `uCascadeParams[4]`, `uSceneCentre`, `uCascadeCount`, `uShadowEnabled`, `uShadowBias` | the sun's map: a cloud in the shadow of a mountain is lit by the sky and by nothing else |
| `uCloudColour`, `uCloudShade` | the two colours of the cloud |
| `uCloudCoverage`, `uCloudSoftness` | how much of the sky it takes, and how ragged its edge is |
| `uBoxMin`, `uBoxMax` | the box of the cloud, in the cloud's own numbers |
| `uShape` | 0 a heap of puffs, 1 a layer, 2 streaks, 3 a tower |
| `uCloudDetail` | how many octaves the weather is made of |
| `uCloudDetailScale` | how many times the fine structure fits across the box |
| `uCloudDensity` | how much of the cloud there is at all |
| `uExtinction` | how thick the cloud is |
| `uSeed` | the seed of the noise |
| `uWeatherScale` | how many times the weather fits across the box |
| `uWindOffset` | how far the cloud has drifted, as a part of its own box |
| `uVolume`, `uVolumeEnabled` | the weather as a picture, and whether it is there |
| `uSteps`, `uLightSteps`, `uLightStep` | the quality: the steps along the ray, the steps towards the sun, and how big a step towards the sun is |
| `uScatter` | how much the cloud throws the light of the sun about |

And the air in front of it is the same air as everything else:

```
#include "../fog/fog.glsl"
```

The fog is the sky's own weather too, and a cloud that is not in the fog is a
cloud that is not in the world.

### The shadow of a cloud

`cloudshadow.vert` and `cloudshadow.frag` are the same volume seen from the
sun: the box is drawn with the sun's own matrix - the one the shadow map is
drawn with - and the ray of the sun is walked through the cloud, and what
comes out the other side is written into the map.

What is written is the light that comes through: one is a clear sky and zero
is a cloud thick enough to be a night. The world's shader reads that map
where the sun's light is and multiplies the sun's light by it, and the
shadow of the cloud is on the ground.

The face of the box that the sun is looking at is found in the shader and
not by the culling: the box is drawn from the other side, so the faces are
the other way round from the way they are in the picture, and the fixed
pipeline cannot know which way the sun is.

## What the clouds cost

The picture is made of fragments, and a fragment of a cloud is made of
arithmetic. What is below is where the arithmetic was taken out.

- **The walk stops when the light is gone.** A thick cloud is opaque long
  before the ray has come out the other side of it, and everything behind
  the front of it cannot be seen any more. This is the single biggest
  saving in the shader.
- **The walk towards the sun is only made where there is cloud.** A wisp is
  lit by the sky and by nothing else, and the walk is the most expensive
  thing in the shader.
- **The fine structure is only worked out where there is something for it
  to eat into.** It is two octaves of a noise, and a cloud is mostly not
  there.
- **The light towards the sun does not see the fine structure.** It is a
  small part of the thickness of the cloud and it is the most expensive
  thing in the shader.
- **The shadow of the sun's map is one tap**, and the world asks it the
  same question sixteen times when the shadows are the soft ones.
- **The walk does not start at the edge of the first step but somewhere
  inside it**, and where it lands is different for every pixel. The
  interleaved gradient noise, the same one the fog's grain uses. Without it
  the picture of a cloud with a few steps in it is a set of shells.
- **The weather is a picture and not a noise** when there is more than one
  cloud in the picture: one read instead of thirty hashes.

## What is left for the C++ side

The shaders are here and they are not called by anything yet. The renderer
has to:

- load the two programs and give the uniforms above;
- draw the **box** of the cloud and not the mesh of the puffs, with the
  front faces culled for the cloud and the back ones for its shadow;
- draw the shadow of a cloud into a map of its own, one layer per stretch of
  the scene, and give the world's shader that map to read;
- give the cloud's shadow to the ground: the world's shader multiplies the
  sun's light by what the cloud's shadow map says.

The flat cloud - the picture on a plane - keeps the scene's program: it is a
picture and not a volume, and a volume is the other thing.
