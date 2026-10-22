// THE VOLUME OF THE CLOUD
//
// What a cloud is made of: a box, and how much of it there is at any place
// in the box.
//
// It is in a file of its own because two programs want it: the program that
// draws the cloud, and the program that draws the cloud's shadow on the
// ground. A shadow of a cloud that is not the same cloud is a shadow of
// something else.
//
// The noise the volume is made of is in cloud_noise.glsl, and this file
// wants it: the loader puts the files in where the lines are, and the line
// for the noise comes before this one.

// THE BOX OF THE CLOUD
//
// The cloud is a box, and the box is not a cube: a heap of puffs is wide and
// low, and a tower is narrow and tall.
//
// The box is in the cloud's own numbers - the C++ side gives the two corners
// of it - and the body's matrix puts that box in the world. The three
// half-edges of the box in the world are the three columns of the matrix,
// each one as long as the box is wide in that direction.
//
// The half-edges are not the axes of the world: they turn with the cloud, so
// a cloud that is turned in the world is turned in its own noise as well,
// and the shape of it is the same shape from every side.
//
// Everything inside the box is -1..1 on every axis: the middle of the cloud
// is (0,0,0), and the faces of the cloud are the faces of the box.
uniform mat4 uModel;
uniform vec3 uBoxMin;
uniform vec3 uBoxMax;

// The shape of the cloud: 0 a heap of puffs, 1 a layer, 2 streaks, 3 a
// tower. The same four shapes the engine's own cloud has.
uniform int uShape;
// How much of the sky the cloud takes, 0..1.
uniform float uCloudCoverage;
// How ragged the edge of it is, 0..1.
uniform float uCloudSoftness;
// How many octaves the weather is made of.
uniform int uCloudDetail;
// How many times the fine structure fits across the box.
uniform float uCloudDetailScale;
// How much of the cloud there is at all.
uniform float uCloudDensity;
// How thick the cloud is: how much of the light of the sun it takes away as
// it goes through, for a cloud the size of itself.
uniform float uExtinction;

// The weather.
//
// The seed of the noise, as a number: the same seed is the same cloud on
// every machine. It is a number and not an integer because the engine's
// programs have no way of writing an integer into a shader.
uniform float uSeed;
// How many times the weather fits across the box.
uniform float uWeatherScale;
// How far the cloud has drifted, as a part of its own box.
uniform vec2 uWindOffset;

// The volume.
//
// The weather baked into a picture once: one read instead of thirty hashes,
// and it is the same weather the engine's own noise makes.
//
// It is not read unless uVolumeEnabled says so, and the number of the unit
// it is on is the C++ side's to say.
uniform sampler3D uVolume;
uniform int uVolumeEnabled;

// The middle of the box in the world.
vec3 cloudCentre() {
    return (uModel * vec4((uBoxMin + uBoxMax) * 0.5, 1.0)).xyz;
}

// The three half-edges of the box in the world.
void cloudAxes(out vec3 axisX, out vec3 axisY, out vec3 axisZ) {
    // Not a constant: the box is a uniform, and a constant has to be
    // written out before the program runs.
    vec3 extent = (uBoxMax - uBoxMin) * 0.5;
    axisX = uModel[0].xyz * extent.x;
    axisY = uModel[1].xyz * extent.y;
    axisZ = uModel[2].xyz * extent.z;
}

// The point of the world in the box.
vec3 cloudBoxOf(vec3 world) {
    vec3 axisX, axisY, axisZ;
    cloudAxes(axisX, axisY, axisZ);
    vec3 d = world - cloudCentre();
    vec3 lengths = vec3(dot(axisX, axisX), dot(axisY, axisY), dot(axisZ, axisZ));
    vec3 q = vec3(dot(d, axisX), dot(d, axisY), dot(d, axisZ));
    return q / max(lengths, vec3(1e-6));
}

// The direction of the world in the box.
//
// The box is a straight map and not a bent one, so a direction goes through
// it the same way a point does - without the middle of it.
vec3 cloudBoxDirection(vec3 direction) {
    vec3 axisX, axisY, axisZ;
    cloudAxes(axisX, axisY, axisZ);
    vec3 lengths = vec3(dot(axisX, axisX), dot(axisY, axisY), dot(axisZ, axisZ));
    vec3 q = vec3(dot(direction, axisX), dot(direction, axisY), dot(direction, axisZ));
    return q / max(lengths, vec3(1e-6));
}

// The size of the cloud in metres: the scale the optical depth is measured
// in.
//
// It is there so that a cloud twice as big is the same cloud and not twice
// as thick. A cloud is a thing of the world and not a thing of the picture,
// and a bigger cloud of the same shape should look like the same cloud
// drawn larger.
float cloudScale() {
    vec3 axisX, axisY, axisZ;
    cloudAxes(axisX, axisY, axisZ);
    return max((length(axisX) + length(axisY) + length(axisZ)) / 3.0, 0.01);
}

// One axis of the box: where the ray goes through it.
//
// The slab test: the two ends of the stretch of the axis the ray is in. The
// ray is in the box between the two of them, and the box is only where all
// three axes agree.
bool cloudSlab(float o, float d, inout float t0, inout float t1) {
    if (abs(d) < 1e-6) {
        // The ray is level with this axis: it is either in the box the whole
        // way or it is not in it at all.
        return o >= -1.0 && o <= 1.0;
    }
    float a = (-1.0 - o) / d;
    float b = (1.0 - o) / d;
    t0 = max(t0, min(a, b));
    t1 = min(t1, max(a, b));
    return t0 <= t1;
}

// Where the ray goes through the box: the two ends of the stretch of it.
//
// The numbers are metres: the ray is in the world, and the box is a map of
// the world and not a place of its own.
bool cloudBoxHit(vec3 qo, vec3 qd, out float t0, out float t1) {
    t0 = -1e20;
    t1 = 1e20;
    if (!cloudSlab(qo.x, qd.x, t0, t1)) return false;
    if (!cloudSlab(qo.y, qd.y, t0, t1)) return false;
    if (!cloudSlab(qo.z, qd.z, t0, t1)) return false;
    return t0 <= t1;
}

// THE SHAPE OF THE CLOUD
//
// The width of the cloud at a height, as a part of how wide it is at the
// bottom.
//
// These are the same four curves the engine's own cloud uses, so that the
// volume and the heap of puffs have one silhouette between them.
float cloudProfile(float t) {
    if (uShape == 1) {
        // A layer: the same width the whole way, and a little less at the
        // top.
        return 1.0 - 0.30 * t * t;
    }
    if (uShape == 2) {
        // Streaks: they thin out towards the top.
        return 1.0 - 0.85 * t;
    }
    if (uShape == 3) {
        // A tower: narrow at the bottom and wide at the top.
        return 0.45 + 0.55 * t;
    }
    // A heap of puffs: full width at the bottom and rounded on top.
    float dome = sqrt(max(0.0, 1.0 - t * t));
    return 0.25 + 0.75 * dome;
}

// How much of the cloud there is at a height.
//
// A heap of puffs has a base: the water in it that is cold enough to fall
// out of it does not fall out of it halfway up, and the bottom of the heap
// is a line. The top of it is a wisp. A streak of cirrus has no base at
// all, and a tower has an anvil on it.
float cloudHeightGradient(float t) {
    float bottom;
    float top;
    if (uShape == 1) {
        // A layer: soft at both ends.
        bottom = smoothstep(0.0, 0.25, t);
        top = 1.0 - smoothstep(0.80, 1.0, t);
    } else if (uShape == 2) {
        // Streaks: no base at all, and thin towards the top.
        bottom = 1.0;
        top = 1.0 - smoothstep(0.25, 1.0, t);
    } else if (uShape == 3) {
        // A tower: a base that is a line, and an anvil on the top.
        bottom = smoothstep(0.0, 0.04, t);
        top = 1.0 - smoothstep(0.93, 1.0, t);
    } else {
        // A heap of puffs: a base that is a line, and a soft top.
        bottom = smoothstep(0.0, 0.04, t);
        top = 1.0 - smoothstep(0.70, 1.0, t);
    }
    return bottom * top;
}

// The envelope of the cloud: full inside the shape of it, and nothing
// outside it.
//
// The same envelope the engine's own cloud uses, so that the volume and the
// heap of puffs are the same heap.
float cloudEnvelope(vec3 q) {
    float t = q.y * 0.5 + 0.5;
    float allowed = max(cloudProfile(t), 0.001);
    float radial = length(q.xz);
    return 1.0 - smoothstep(allowed * 0.45, allowed, radial);
}

// How much of the weather it takes to be cloud at all.
//
// The coverage is how much of the sky the cloud takes, and this is the
// number that says it: a high bar is a thin cloud with a fringe of small
// puffs, and a low one is a cloud that fills the sky.
//
// The thirty hundredths are the engine's own: the same fraction of the same
// noise, so that the fringe of the volume and the fringe of the puffs are
// the same fringe.
float cloudFill(float weather) {
    float threshold = 1.0 - uCloudCoverage * 0.90;
    return clamp((weather - threshold + 0.30) / 0.60, 0.0, 1.0);
}

// THE WEATHER OF THE CLOUD
//
// Where the cloud is thick and where it is thin.
//
// Two ways of having it. The one that is fast is the volume: the weather is
// baked into a picture once, and the walk reads it - one read instead of
// thirty hashes, and it is the weather the engine's own noise makes.
//
// The one that is slow is the noise, worked out again for every step of
// every ray. It is there so that a cloud can be drawn with nothing baked
// at all, and it is the engine's own noise written out for the card.
float cloudWeather(vec3 q) {
    if (uVolumeEnabled != 0) {
        // The picture of the weather, with the cloud drifting in it. The
        // drift is in the picture's own units, and it is a part of the box.
        vec3 uvw = q * 0.5 + 0.5;
        uvw.xz += uWindOffset;
        return texture(uVolume, uvw).r;
    }
    // The noise, worked out again. The drift is scaled with the weather, so
    // that it drifts the same amount of the cloud whichever way the weather
    // is made.
    vec3 p = q * uWeatherScale;
    p.xz += uWindOffset * uWeatherScale;
    return cloudFbm3(p, uint(max(uSeed, 0.0)), max(uCloudDetail, 1));
}

// THE DENSITY OF THE CLOUD
//
// How much of the cloud there is at a point of its box: 0..1.
float cloudDensity(vec3 q) {
    float envelope = cloudEnvelope(q);
    if (envelope <= 0.0) return 0.0;

    float fill = cloudFill(cloudWeather(q));

    // The fine structure is only worked out where there is something for it
    // to eat into, and that is the whole of the cost of it: the detail is
    // two octaves of a noise, and a cloud is mostly not there.
    //
    // It eats into the edges and not into the middle, and that is what
    // makes the edges ragged: a cloud with a hard edge is a ball of cotton.
    if (fill > 0.0) {
        float detail = cloudDetail3(q * uCloudDetailScale, uint(max(uSeed, 0.0)) + 91u);
        fill = clamp(fill - detail * uCloudSoftness * (1.0 - fill), 0.0, 1.0);
    }

    float t = q.y * 0.5 + 0.5;
    return fill * envelope * cloudHeightGradient(t) * uCloudDensity;
}

// THE THICKNESS OF THE CLOUD
//
// How much of the cloud the light of the sun has to come through, at a
// point: the weather of it and not the fine structure.
//
// The fine structure is left out on purpose. It is a small part of the
// thickness of the cloud, and it is the most expensive thing in the shader,
// and the light of the sun does not notice it.
//
// This is the second walk of the frame - the one towards the sun - and it is
// the reason the fine structure is left out of it.
float cloudThickness(vec3 q) {
    float envelope = cloudEnvelope(q);
    if (envelope <= 0.0) return 0.0;
    float t = q.y * 0.5 + 0.5;
    return cloudFill(cloudWeather(q)) * envelope * cloudHeightGradient(t);
}

// THE PHASE FUNCTION
//
// How much of the light of the sun comes to the eye from the direction the
// eye is looking.
//
// The cloud is made of very small drops of water, and a drop throws the
// light of the sun forward. That is why the edge of a cloud is white when
// the sun is behind it, and flat and grey when the sun is somewhere else.
//
// There are two lobes and not one. The forward lobe is the light that comes
// straight back at the eye - the silver lining. The backward lobe is the
// light that comes back from behind the drops, and it is what keeps the
// cloud from going black on the side of the sun that is away from it.
//
// The powers are written out: the third power of a root is three
// multiplications, and a pow on a driver is a logarithm and an exponential.
float cloudPhase(float cosTheta) {
    const float g = 0.76;
    const float g2 = g * g;
    float forward = 1.0 + g2 - 2.0 * g * cosTheta;
    float forwardRoot = sqrt(max(forward, 1e-4));
    float ahead = (1.0 - g2) / (forwardRoot * forwardRoot * forwardRoot);

    const float b = 0.28;
    float backward = 1.0 + b * b + 2.0 * b * cosTheta;
    float backwardRoot = sqrt(max(backward, 1e-4));
    float behind = (1.0 - b * b) / (backwardRoot * backwardRoot * backwardRoot);

    return 0.75 * ahead + 0.25 * behind;
}
