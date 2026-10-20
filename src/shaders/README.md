# The shaders

The GLSL the engine draws the world with. One shader in a file, GLSL 330, and
every file says at the top that it is under the Blue Oak Model License 1.0.0.

```
src/shaders/
  scene.vert            the world: the vertex stage
  scene.frag            the world: the fragment stage, where the shadow is
  shadows/
    shadow_depth.vert   the shadow map's own pass: the vertex stage
    shadow_depth.frag   the shadow map's own pass: the distance is written
    shadow_debug.vert   the shadow map on the screen: the vertex stage
    shadow_debug.frag   the shadow map on the screen: the map is the picture
```

The licence notice goes under the `#version`, and not over it: the version
has to be the first thing in the file, or the driver will not read the shader
at all.

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
| `uFogColour`, `uFogDensity` | the haze |
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
