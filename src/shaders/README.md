# The shaders

The GLSL the engine draws the world with. One shader in a file, GLSL 330, and
every file says at the top that it is under the Blue Oak Model License 1.0.0.

```
src/shaders/
  scene.vert            the world: the vertex stage
  scene.frag            the world: the fragment stage, where the shadow is
  fog/
    fog.glsl            the fog: the air between the eye and the thing
    README.md           the fog, and how a shader puts it in
  particles/
    particles.vert      the particles: the vertex stage, a quad that faces the eye
    particles.frag      the particles: the fragment stage, a soft round blob
  water/
    water.vert          the water: the vertex stage, where the waves are
    water.frag          the water: the fragment stage, where the light is
  clouds/
    README.md           the clouds: what a cloud shader is given
    clouds.vert         the clouds: the vertex stage, the box
    clouds.frag         the clouds: the fragment stage, the walk through the box
    cloud_noise.glsl    the noise the cloud is made of
    cloud_volume.glsl   the volume: the box, and how much of it there is
    cloudshadow.vert    the shadow of a cloud: the vertex stage, from the sun
    cloudshadow.frag    the shadow of a cloud: the fragment stage, the light
                        that comes through
  shadows/
    shadow_depth.vert   the shadow map's own pass: the vertex stage
    shadow_depth.frag   the shadow map's own pass: the distance is written
    shadow_debug.vert   the shadow map on the screen: the vertex stage
    shadow_debug.frag   the shadow map on the screen: the map is the picture
```

The licence notice goes under the `#version`, and not over it: the version
has to be the first thing in the file, or the driver will not read the shader
at all.

## The folders

Everything that belongs together lives in a folder of its own: the fog, the
water, the particles, the clouds, the shadows. Two people can work on two
shaders at the same time without reaching into the same file, and a folder
says what a thing is without a name having to say it.

## The includes

GLSL has no `#include` of its own, and the loader gives the shaders one:

```
#include "fog/fog.glsl"
```

The loader reads the line, finds the file and puts the file where the line
was. The file is looked for next to the file that asked for it, and then next
to the shader that was loaded - so a shader in a folder of its own asks for
the fog as `../fog/fog.glsl`.

**The line goes after the uniforms.** GLSL wants a thing to be declared
before it is used, and the fog is lit by the sun.

A file that is already in the picture is not put in it twice, so a shader can
ask for the fog as many times as it likes.

The fog is the one thing that is in more than one shader today: the world,
the particles and the water all have air in front of them, and it has to be
the same air in all of them. A fog that is written out three times is a fog
that is three different fogs the first time one of them is changed.

## The shadow

The sun is a directional light, and its shadow is a map of how far away the
light hit something first. The map holds that distance and not the depth of
the window, because a directional light has an orthographic projection: the
distance in the map is the distance in the world, and a shadow is one
comparison of two numbers that mean the same thing.

The map is one layer per cascade - a cascade is a stretch of the scene with
its own map, so that the near ground gets the texels and the far ground does
not steal them. One to four of them, and the first one alone is enough for a
small scene.

### The stretches are chosen per fragment

A stretch is a **sphere** around the middle of the scene, and the stretch a
fragment belongs to is the smallest one that reaches it. That is a question
about the distance to the middle of the scene, and the distance to a point is
not a straight line - **it cannot be interpolated across a triangle**.

A ground is two triangles forty metres across, and the distance to the middle
of the scene is the same at all four of its corners. A vertex stage that
chose the stretch would therefore put the whole ground in the outermost one,
and the shadow of a box in the middle of the scene would fall on a stretch
the box was never drawn into. The stretch is chosen in the fragment stage,
where the distance is the distance and not an approximation of it.

### The three modes

| Mode | What it does |
|---|---|
| off | no shadow at all |
| 1 tap | one read, one comparison: the edge is a stair |
| PCF | sixteen reads in a Poisson disk: the edge is soft over a texel or two |
| PCSS | the blockers are looked for first, and the width of the shadow follows from how far they are |

The default is the filtered one. A stair-stepped edge is the first thing the
eye picks out of a picture, and sixteen reads are cheap.

The disk of taps is **turned a little for every fragment**. Sixteen taps in
the same sixteen places draw the same sixteen rings, and the eye finds them;
turning the disk spreads the taps over the whole circle instead, and what is
left is a grain nobody can point at.

### The bias is in metres

The bias is how far the surface is pushed towards the light before it is
compared with the map, and it is a **distance in the world** and not a number
in the map's own units. The map holds a distance as a fraction of how deep
the box of its stretch is, and that depth is different in every stretch: a
bias that is right for the near one is a hundred times too big for the far
one.

Two parts, and they do different jobs:

- **the normal offset** pushes the surface along its own normal. The shadow of
  a thing stays touching the thing: a surface that is moved along the
  direction it faces still meets the edge of the shadow where it should, and
  only the stripes a surface draws on itself are gone.
- **the slope** pushes it along the light, and grows with how steep the
  surface is: the steeper the surface, the more the distance to the light
  changes over one texel of the map.

### What the C++ side has to give the shader

| Uniform | What it is |
|---|---|
| `uModel` | the body's matrix |
| `uView`, `uProjection` | the camera |
| `uCameraPosition` | where the eye is |
| `uLightViewProjection[4]` | the light's matrix, one per stretch |
| `uCascadeParams[4]` | how far each stretch reaches, how deep it is, how big one of its texels is |
| `uSceneCentre` | the middle of the scene |
| `uCascadeCount` | how many of the stretches there are |
| `uSunDirection` | towards the sun |
| `uSunColour`, `uSkyColour`, `uGroundColour` | the light: the sun's, the sky's and the ground's bounce |
| `uAmbient`, `uSpecular`, `uShininess` | how much of each |
| `uFogColour` | the colour of the air |
| `uFogDensity` | how thick the fog is where it is thickest, 1/m |
| `uFogFalloff` | how fast it thins as it goes up, 1/m |
| `uFogHeight` | the height at which it is thickest, m |
| `uFogScatter` | how much of the sun's light it carries, 0..1 |
| `uFogForward` | how forward it throws that light, 0..0.95 |
| `uShadowMode` | off, 1 tap, PCF, PCSS |
| `uShadowTexelSize` | one over the size of the map |
| `uShadowBias` | the bias of the comparison, in metres |
| `uShadowNormalTexels` | the offset along the normal, in texels |
| `uShadowRadius` | the widest the penumbra of the PCSS may get, in texels |
| `uShadowDebug` | a picture of the shadow code instead of the picture |

| Sampler | What it is |
|---|---|
| `uAlbedoMap` | the picture on the surface; a white texel when there is none |
| `uShadowMap` | the shadow map, read with the filter on |
| `uShadowMapPoint` | the same map, read texel by texel |

The two shadow samplers are **two sampler objects over one map**. The filter
belongs to the texture and not to the place it is read from, so one texture
bound to two units is filtered the same in both - and the search for the
blockers wants the raw depth of a texel and not the average of it with its
neighbours.

### Looking at the shadow code

`uShadowDebug` draws the picture of the shadow code instead of the picture:
1 the shadow itself, 2 whether the fragment is inside the box of the map,
3 where it is in the map, 4 which stretch it is in, 5 how far it is from the
light, 6 what the map holds where the fragment is, 7 the difference between
the two - white where the fragment is behind what the map holds, which is
where the shadow is.

A shadow that is not what it should be is a question about these numbers and
not about the picture, and the picture cannot answer it.

### The sheet the shadow falls on

`uSurfaceMode` is 1 for a sheet that is **not drawn** and only takes the
shadow: what the eye sees is the shadow on it.

The ground of the editor is a grid of lines, and a shadow on a line is a
shadow nobody can see - so the model stands on nothing and looks like it is
flying. The sheet is a plain quad under the grid that takes the shadow and
nothing else, and the grid is drawn over it.

### The sky

`uSurfaceMode` is 2 for the sky: the picture of the sky with the fog over it,
and no light and no shadow.

The sky is the far distance, and the air between the eye and the sky is the
same air as the air between the eye and everything else. A sky drawn without
the fog stands in front of the fog like a wall, and the horizon is a line.

## The fog

The fog is the air between the eye and the thing, and it is in a file of its
own: `fog/fog.glsl`. **The fog has its own README** - `fog/README.md` - and
that is where it is written down. What is here is what the rest of the
shaders need to know about it.

It is two questions, and they are answered separately: **how much** of it is
on the way, and **what colour** it is.

### How much

The fog is thickest at the bottom of the world and thins as it goes up, so
the amount of it on the way is an integral - and an integral can be done
exactly. The density of the fog at a height `h` is

```
d(h) = density * exp(-falloff * max(h - fogHeight, 0))
```

and the amount between the eye and a fragment is that density integrated
along the line between them.

The integral is done **in two pieces**, because the fog has a floor: below
the floor the density is the density of the floor, and above it the density
falls away with the height. The line crosses the floor at one place at most,
and the two halves of the line are the two pieces.

`density` is read as a distance: the fog halves itself every `0.69 /
density` metres. Zero is no fog at all.

### What colour

The fog is the colour of the light that is in the air: the light of the sky,
which is the same everywhere, and the light of the sun, which the drops of
water throw forward.

The throwing forward is the phase function, and it has **two lobes**: the
forward one is the aureole around the sun and the backward one is the light
that comes back from behind the drops. They are mixed with weights that add
up to one, and each of them is one on the average over the whole of the sky
- so the glow around the sun is paid for out of the air around it and not
out of the light of the sun.

`scatter` is how much of that glow the fog carries, and `forward` is how
forward it throws it. They are the difference between a fog that hides the
sun and a fog that carries it.

### The grain

The picture is eight bits and the fog is a very slow change of the colour,
and the eye finds the steps. A little noise - the interleaved gradient noise,
different for every fragment - turns the bands into a grain.

### The grid

The grid of the editor is drawn by the fixed pipeline, and the fixed pipeline
has a fog of its own: `GL_FOG`. It is the **exponential** one and not the
straight line, because that is the curve the shader's fog is - the light of
a thing is eaten by the air on the way in `e^(-k*d)`. The density given to
it is the density of the air where the eye is, because the grid is measured
from the eye and the shader's fog is too.

What the fixed pipeline cannot do is the height of the fog: it has no way to
say that the air is thinner higher up. The grid is on the ground, where the
air is thickest and where the two agree most.

## What the shaders cost

The picture is made of fragments, and a fragment is made of arithmetic. What is
below is where the arithmetic was taken out, and why each place was worth it.

### The shadow is not asked for where the sun cannot reach

A surface that faces away from the sun has no direct light and no shine, and
it is dark whether the sun is behind a mountain or not. So the map is not
read there.

The back of every object in the picture is a piece of the picture that stops
paying for the shadow - and with the soft shadows that is sixteen reads and
thirty-two, which is the most expensive thing in the frame. The shadow on the
water is the same: one read, and the read is skipped where the sun's light
cannot reach the water at all.

The pictures of the shadow are exempt: they are about the shadow and not about
the light, and a picture of the shadow that is missing half of it is worse
than no picture.

### A power that is not a power

`pow` on a driver is a logarithm and an exponential, and every place where
the exponent is a whole number is a power that can be written out as
multiplications:

| Where | What it is now |
|---|---|
| the fifth power of the Fresnel | two multiplications |
| the twelfth power of the sun's glow | four multiplications |
| the three-halves power of the fog's phase | a square root and two multiplications |
| the square root of the sky's gradient | a square root and not `pow(up, 0.55)` |

### The sky is not asked for where it cannot be seen

The water gives back two hundredths of the light when it faces the eye square
on, and two hundredths of a sky is less than one quantum of the picture - so
the sky is not computed there. The water straight below the eye is the piece
of water that is looking at the eye, and it is a good half of the water in a
picture seen from above.

### A square root that is not needed

The stretch of the scene a fragment is in is chosen by the distance to the
middle of the scene, and the question is "is this inside the sphere" - which
has the same answer squared. The distance is squared on the way in and the
root is never taken.

### The fog is not asked for when there is no fog

The whole of the fog is behind one test - is there any of it - and the test is
the same for every fragment of the frame. A scene without fog pays nothing
for it: not the length of the line, not the exponential of the integral and
not the phase function.

### The pictures of the shadow are one test and not seven

`uShadowDebug` is read once and not seven times, and the seven pictures
inside it are only looked at when it is on.

## The particles

A particle is a quad that faces the eye, and the quad is four corners around
the place the particle is. The corners are the same four numbers for every
particle, and the centre, the size and the colour are the same for all four
corners of one particle - so the corners are in a buffer of their own and the
rest is read once per particle. That is what the divisor on the attributes is
for.

### What the C++ side has to give the shader

| Uniform | What it is |
|---|---|
| `uView`, `uProjection` | the camera |
| `uCameraRight`, `uCameraUp` | the camera's own axes, in the world: the quad is turned to face the eye with them and not with the world's axes |
| `uCameraPosition` | where the eye is: the fog is measured from there |
| `uSunDirection`, `uSunColour` | the sun: the fog is lit by it |
| `uFogColour`, `uFogDensity`, `uFogFalloff`, `uFogHeight`, `uFogScatter`, `uFogForward` | the same air as everything else in the picture |
| `uAdditive` | 0 for a particle that is painted over the picture, 1 for one that is added to it |

| Attribute | What it is |
|---|---|
| `aCentre` | where the particle is |
| `aSizeAlpha` | how big it is, how see-through it is |
| `aColour` | what colour it is |
| `aCorner` | which corner of the quad this vertex is |

### Two passes, and the fog is different in each

The two kinds of particle are drawn in two passes, because they are blended
differently: one is drawn over what is behind it and one is added to it, and
a blend function is state and not a picture.

The fog is different in the two as well, and it has to be. A puff of smoke is
painted over the picture, so it takes the colour of the air in front of it. A
spark is light, and the air scatters light away - so a spark in a thick mist
is a dimmer spark and not a brighter one. `uAdditive` is what tells the
shader which of the two it is drawing.

### The particles are not lit

A fire is its own light, and a shadow map of a puff of smoke is a shadow of a
puff of smoke. There is no light in the particles' shaders and no shadow.

## The water

The water is a surface of its own, with a program of its own: `water.vert`
and `water.frag`. It is not a mode of the scene's program, because the water
is transparent and the world is not, and a picture that is half of one and
half of the other is a picture nobody can look at.

### The waves

Four waves, each one travelling in its own direction with its own length and
its own height, crossing each other. Four *fixed* waves: the same surface
every time, and a surface that moves.

The shape of a wave is not a sine but a **Gerstner wave**: a sharp crest and a
flat trough, because the water at the top of the wave is thrown forward. The
cost of that is that the normal is not the slope of the height any more - the
wave moves the points sideways as well - so the normal is worked out of the
two directions the surface runs in, which is what the derivative of the
surface is.

The speed of a wave follows from its length: a long wave travels faster than
a short one, and the speed of the real thing is `sqrt(g * k)` for the wave
number `k`. It is the one number in the shader that comes out of the physics
and not out of a picture.

**The waves are in the world and not in the mesh.** The mesh is put in the
world first and the waves are worked out where it landed, so two sheets of
water in the same place have the same waves in them. A wave that belonged to
the mesh would be a different wave in every body.

**The sheet has to be cut.** The waves are worked out per point, so a sheet
with four corners has four waves in it and a sheet with a hundred has a
hundred. A water surface is a grid, and the finer it is the finer the waves
are.

### The light

What the eye sees of water is three things mixed together:

- the light that comes back out of the water - what is under it, tinted by
  the water on the way;
- the light that comes off the water - the sky, and the sun in the sky;
- the shine of the sun on the surface.

How much of each depends on the angle, and the rule for that is **Fresnel**:
water gives back about two hundredths of the light that hits it straight on
and almost all of it when it is seen at a grazing angle.

The **sky in the water** is a picture the shader makes - a gradient from the
horizon to the zenith and the sun in it - and not the engine's skybox. The
skybox is six ordinary textures, and the water would have to look up all six
of them to find the one texel it wants, and the sky in the water is blurred
by the waves anyway. What the water needs from the sky is its *colour*.

The **foam** is in two kinds: the water is white where the wave folds over
itself, and white where it is shallow - where the wave has risen above the
bottom. The fold is worked out of the same derivative the normal is: the area
of the piece of water that was flat is now smaller than it was, and the water
has to go somewhere.

### The colour of the water

The water is not one colour. It is bright where it is thin and dark where it
is deep, and how fast it goes from the one to the other is how fast the light
is swallowed on the way down.

The **depth** of the water at a place is the depth the body was given, less
what the shore takes off it and more what a wave has raised. The **shore** is
the edge of the sheet: the grid knows where its own edge is, and the water is
thin there.

What comes back of the bottom is the light of the bottom with the water's
own colour taken out of it - and the red is the first to go, which is why a
bottom that is a long way under is a blue one.

### The ripples

The mesh carries the waves, and the mesh is a grid: anything smaller than a
cell of it is made in the fragment stage, per fragment, or the water is a
sheet of glass with four big waves in it. Four waves, each shorter than the
one before and turned a little further from the wind, and the slopes of all
four added to the slope of the wave in the mesh.

They fade out with the distance: a ripple that is smaller than a pixel is a
shimmer and not a ripple, and the far water is the mirror it should be.

### The bend of the light

The bottom of the water is seen through the surface, and the surface is a
sheet of moving water, and a sheet of moving water bends what is under it.

So the picture of the scene is **copied into a texture** before the water is
drawn over it, and the water reads the copy: the same pixels the water is
drawn over, so that what shows through the water is the world that is really
there. The copy is out of the window's own buffer, and it is made only when
something is going to read it.

The bend itself is the difference between the way the light would have come
and the way it does come, in the eye's own frame - because the eye's frame is
the frame the screen is in.

### What the C++ side has to give the shader

| Uniform | What it is |
|---|---|
| `uModel`, `uView`, `uProjection` | the camera and the sheet |
| `uCameraPosition` | where the eye is: the fog and the Fresnel are measured from there |
| `uSunDirection`, `uSunColour` | the sun |
| `uSkyColour` | the light that comes from the sky, on the water itself |
| `uSkyHorizon`, `uSkyZenith` | the sky the water reflects |
| `uShallowColour`, `uDeepColour` | the colour of the water where it is thin and where it is deep |
| `uDepthFade` | how fast the one colour becomes the other, 1/m |
| `uDepthColour` | 0 for one colour everywhere |
| `uWaterDepth` | how deep the water is, in metres |
| `uClarity` | how fast the water swallows the light, 1/m |
| `uOpacity` | how much of the water's own colour there is |
| `uShoreFade` | how much the water thins towards its own edge, 0..1 |
| `uReflectivity` | how much of the sky the water gives back |
| `uFresnelPower`, `uFresnelIntensity` | the Fresnel: how sharp its edge is and how much of it there is |
| `uSpecular`, `uShininess` | the shine of the sun on the surface |
| `uFoam`, `uFoamColour` | the foam, and the colour of it |
| `uDetailStrength`, `uDetailScale`, `uDetailSpeed` | the ripples that are too small to be in the mesh |
| `uRefraction`, `uRefractionStrength` | the bend of the light, and how much of it there is |
| `uSceneColour` | the picture of the scene, taken before the water was drawn over it |
| `uViewport` | where that picture is in the window: its corner and its size in pixels |
| `uDarkening` | how much the low places of the water are darkened |
| `uTime` | the time of the water, in seconds: the waves are a function of it |
| `uWaveHeight` | how high the waves are, in metres |
| `uWaveLength` | how long they are, in metres: a crest to a crest |
| `uWaveSpeed` | how fast they travel: one is the speed of the real thing |
| `uWaveSteepness` | how sharp the crests are, 0..1 |
| `uWindDirection` | the way the wind blows, in the plane of the water |
| `uShadowMap` | the shadow map, read with the filter on: the same picture the world is drawn with |
| `uLightViewProjection[4]` | the sun's matrix, one per stretch of the scene |
| `uCascadeParams[4]` | how far each stretch reaches, how deep it is, how big one of its texels is |
| `uSceneCentre` | the middle of the scene |
| `uCascadeCount` | how many of the stretches there are |
| `uShadowEnabled` | 0 for no shadow at all |
| `uShadowBias` | the bias of the comparison, in metres |
| `uShadowNormalTexels` | the offset along the normal, in texels |
| `uFogColour`, `uFogDensity`, `uFogFalloff`, `uFogHeight` | the same air as everything else in the picture |

| Attribute | What it is |
|---|---|
| `aPosition` | where the point of the sheet is |

### The sheet is a grid

The waves are worked out per point, and a sheet with four corners has four
waves in it and a sheet with twenty-five thousand has twenty-five thousand.
The grid is the grid of the whole world - it runs from -1 to 1 in both
directions and the sheet is scaled to it - so that two sheets of water in the
same place have the same waves in them, and a wave that belonged to the sheet
would be a different wave in every one of them.


### The blend is premultiplied

The water is drawn with `glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)`: the
colour is written ready to be added to the picture, and the bottom comes
through what is left of the transparency.

It is the only way the reflection survives. The sky in the water is light that
comes *off* the surface, and the surface does not swallow it - but a mixed
alpha multiplies the reflection by the transparency as well, and a shallow
river seen along its length would lose the mirror that is the whole point of
looking at it along its length.

### The water is level, and it is not in the shadow

The waves are worked out in the world, so the water is level whatever the
mesh's own normal says. A water surface that is tilted is a water surface
that is falling over.

**The water does not cast a shadow.** A sheet of water in the shadow map is a
sheet of water that shadows itself: the map holds a flat plane and the water
is waves, and every trough of it is under the plane. The water is drawn and
it takes the shadow of a thing - but it is not in the map.

### The shadow on the water, and what it costs

The water takes the shadow of the sun's map, the stretches of the scene and
the bias - and asks the map **one tap**.

The world asks it sixteen times when the shadows are the soft ones, and the
water covers a good half of the screen, so sixteen reads there are sixteen
reads over half the picture. What the shadow takes away of the water is the
shine of the sun, and the sun is in the water whether the shadow is soft or
not. One read is enough to know whether the sun is there.

And the read is **skipped where the sun's light cannot reach the water at
all**: a surface that faces away from the sun is dark whether the sun is
behind a mountain or not, and the far side of a wave is a good part of the
water in a picture. The read of the map is the expensive part, and there it
is not made.

The shadow takes away the sun's light and **not the sky's**: the sky is still
in the water in the shadow, because the sky is not behind the mountain. The
shadow of a thing on a lake is a darker patch of lake and not a hole in it.

## The clouds

A cloud is a **volume**, and a volume is not drawn - it is walked through.

The shader draws the box the cloud is in and walks a ray through it: at every
step it asks the sky how much cloud is at that place, asks how much of the
sun's light can get through the cloud to that place, and puts the two
together. The light is taken away by what it goes through, and that is the
same law that makes the air eat the colour of the far things.

There is no puff in it and no sphere: the shape comes out of the noise and
out of the envelope, and the edge of it is eaten into by a finer noise, so
the edge is soft and ragged and not round.

**THE NOISE IS THE ENGINE'S OWN NOISE.** The weather of the cloud is
Perlin's gradient noise in three dimensions, written out again for the card
in `clouds/cloud_noise.glsl`: the same hash, the same twelve directions, the
same octaves. A cloud that is a different shape on the card than it is in
the engine is a cloud that is not the cloud that was made.

**THE BOX IS THE CLOUD'S OWN BOX, AND THE MATRIX PUTS IT IN THE WORLD.**
The three half-edges of the box are the three columns of the body's matrix,
so the box turns with the cloud and the shape of it is the same shape from
every side.

**THE FRONT FACES ARE CULLED.** The back faces are the ones on the screen,
and they are there even when the eye is inside the cloud - which is what
makes the cloud still be a cloud when the eye is in it.

**THE LIGHT IS FOUR THINGS.** The light through the cloud (the sun's light
has to come through it to reach a point inside), the bounce (the light that
comes out of a cloud has usually been inside it first), the powder (the thin
edges are darker than the thick middle) and the sky (the light from above
and the bounce off the ground). And the phase: two lobes, like the fog's,
and the forward one is the silver lining round the edge of a cloud when the
sun is behind it.

**THE DEPTH THAT IS WRITTEN IS THE CLOUD'S OWN.** The depth of the first
piece of the cloud the ray found, and not the depth of the back of the box:
the back of the box is under the ground that the cloud is above, and the
ground would hide the cloud.

**THE BLEND IS PREMULTIPLIED**, the same as the water's.

The shadow of a cloud on the ground is the same cloud seen from the sun: the
box is drawn with the sun's matrix, the ray of the sun is walked through the
cloud, and what comes out the other side is what reaches the ground.

The folder's own README has the whole of it: the uniforms, the cost and
what is left for the C++ side.

## The map itself
The shadow map is a picture of distances: one channel, `R16` at least, and
`R16F` or `R32F` if the distance needs more room than sixteen bits. The
shadow pass writes the red channel and nothing else, so the other three are
wasted space.

`ShadowMap::readLayer` reads a stretch of the map back, in the map's own
units. It is what the tools that look at the shadows use, and it is a read
and not a copy: the picture is where it is, and the tools ask it for the
numbers.

The settings the shadows get in the editor are these:

- **Basic shadows in the project**: off, 1 tap, PCF 4-16 taps, PCSS/VSM.
- **Shadow map size**: 1024, 2048, 4096.
- **Cascades**: 1 to 4.
- **The sun and the bias**: in a dialog of their own, because a menu row
  cannot be dragged.
- **Who casts a shadow**: a flag on the body, not a setting.
