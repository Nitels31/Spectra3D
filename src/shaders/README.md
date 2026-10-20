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

### The three modes

| Mode | What it does |
|---|---|
| off | no shadow at all |
| 1 tap | one read, one comparison: the edge is a stair |
| PCF | sixteen reads in a Poisson disk: the edge is soft over a texel or two |
| PCSS | the blockers are looked for first, and the width of the shadow follows from how far they are |

The default is the one tap: it is the cheapest, and a scene with a hard sun
has a hard shadow anyway.

### What the C++ side has to give the shader

| Uniform | What it is |
|---|---|
| `uModel` | the body's matrix |
| `uView`, `uProjection` | the camera |
| `uLightViewProjection[4]` | the light's matrix, one per cascade |
| `uCascadeSplits[4]` | where the stretches of the scene end, from the eye |
| `uCascadeCount` | how many of them there are |
| `uCameraPosition` | where the eye is |
| `uSunDirection` | towards the sun |
| `uSunColour`, `uSkyColour`, `uGroundColour` | the light: the sun's, the sky's and the ground's bounce |
| `uAmbient`, `uSpecular`, `uShininess` | how much of each |
| `uFogColour`, `uFogDensity` | the haze |
| `uShadowMode` | off, 1 tap, PCF, PCSS |
| `uShadowTexelSize` | one over the size of the map |
| `uShadowBias` | the bias of the comparison, in the map's units |
| `uShadowRadius` | the widest the penumbra of the PCSS may get, in texels |

| Sampler | What it is |
|---|---|
| `uAlbedoMap` | the picture on the surface; a white texel when there is none |
| `uShadowMap` | the shadow map, read where the texels are |
| `uShadowMapFiltered` | the same map, read with the filter on |

The two shadow samplers are the same map read two ways. The filter belongs to
the sampler and not to the map, so the same picture can be read with the
points of the texels where they are and with them spread out.

## What is not here yet

The files are written; the engine does not use them. The renderer is still
the fixed pipeline, and the part that reads these files, compiles them, draws
the map and draws the scene with them is the next piece of work. So is the
sky, which will need a shader of its own when the scene moves over.

The shadow map is a picture of distances: one channel, `R16` at least, and
`R16F` or `R32F` if the distance needs more room than sixteen bits. The
shadow pass writes the red channel and nothing else, so the other three are
wasted space.

The settings the shadows get in the editor are these:

- **Basic shadows in the project**: off, 1 tap, PCF 4-16 taps, PCSS/VSM.
- **Shadow map size**: 1024, 2048, 4096.
- **Cascades**: 1 to 4.
- **Who casts a shadow**: a flag on the body, not a setting.
