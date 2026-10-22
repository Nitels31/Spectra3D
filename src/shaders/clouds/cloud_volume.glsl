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
// The way the wind blows. The streaks of a cloud are drawn out along it.
uniform vec2 uWindDirection;

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

// THE BASE AND THE TOP OF THE CLOUD
//
// They are a part of the height of the box, and the top of them is where
// the weather says the cloud ends.
//
// The base of a heap of puffs is a line: the water in it that is cold
// enough to fall out of it does not fall out of it halfway up.
//
// The top of it is NOT a plane and NOT a curve: it is the shape of the
// weather, and it is the whole of the cauliflower. A cloud whose top is
// drawn by a curve is a cloud with a smooth top, and no such thing is in
// the sky.
void cloudHeightRange(float fill, out float base, out float top) {
    if (uShape == 1) {
        // A layer: a flat thing in the middle of the box.
        base = 0.05;
        top = 0.70 + 0.25 * fill;
    } else if (uShape == 2) {
        // Streaks: a thin veil, low in the box.
        base = 0.0;
        top = 0.20 + 0.40 * fill;
    } else if (uShape == 3) {
        // A tower: it reaches the top of the box and its anvil is flat.
        base = 0.0;
        top = 0.88 + 0.12 * fill;
    } else {
        // A heap of puffs: it reaches as high as the weather lets it.
        base = 0.0;
        top = 0.35 + 0.65 * fill;
    }
}

// The envelope of the cloud: how far it reaches at a height.
//
// It is a LIMIT and not a shape. The weather says where the cloud is, and
// the envelope only keeps the cloud from spreading past the box it is in.
//
// This is the whole difference between a cloud and a smear: a shape that
// cuts the cloud off with a smooth curve puts a smooth curve in the
// picture, and the eye finds it at once. The edge of a cloud is made of the
// weather - of the lumps of it and of the fine structure that eats into
// them - and nothing here is allowed to draw a curve instead.
float cloudEnvelope(vec3 q) {
    float t = q.y * 0.5 + 0.5;
    float allowed = max(cloudProfile(t), 0.001);
    float radial = length(q.xz) / allowed;
    return 1.0 - smoothstep(0.80, 1.0, radial);
}

// How much of the weather it takes to be cloud at all.
//
// The coverage is how much of the sky the cloud takes, and this is the
// number that says it: a high bar is a thin cloud with a fringe of small
// puffs, and a low one is a cloud that fills the sky.
//
// THE BAR IS IN THE UNITS OF THE WEATHER ITSELF, and that matters more
// than it looks. The noise of the engine is a FLAT one: it sits about the
// middle of its range and the lumps in it are small - the weather of a
// cloud with four octaves in it moves by about a tenth of the range. A bar
// of a fixed size is a bar that is wrong the moment the weather is made of
// a different number of octaves, and a cloud made of a flat noise with a
// fixed bar is either nothing at all or the whole sky.
//
// So the bar is put in the units of the weather's own variation, and the
// variation is worked out from the number of octaves: each octave is half
// as strong as the one before it, and the sum of them is the smaller for
// it.
float cloudSpread() {
    // The variation of the gradient noise of the engine, in the units of
    // its own range, and with the octaves taken into account.
    return 0.2 / sqrt(float(max(uCloudDetail, 1)));
}

float cloudFill(float weather) {
    float spread = cloudSpread();
    // The bar: the middle of the weather is the middle of the sky, and the
    // coverage says how much of the sky is on the other side of it.
    float bar = (0.5 - uCloudCoverage) * 2.5 * spread;
    float threshold = 0.5 + bar;
    // And the cloud is solid where the weather is well above the bar, and
    // it thins out over one spread.
    return clamp((weather - threshold) / spread, 0.0, 1.0);
}

// How much cloud there is, for the shape of it.
//
// A heap of puffs is a solid thing. A layer is thinner. A streak of cirrus
// is barely there at all: it is a few crystals of ice and the sun goes
// straight through it, and that is why the sky is blue where the cirrus is
// and white where it is thick.
float cloudShapeDensity() {
    if (uShape == 1) return 0.85;  // a layer
    if (uShape == 2) return 0.50;  // streaks of cirrus
    return 1.0;
}

// How far the cloud is drawn out along the wind.
//
// The lumps of the weather are stretched with the wind, and a cloud that is
// drawn out is not a lump any more: it is a streak.
float cloudStretch() {
    if (uShape == 2) return 8.0;  // cirrus
    if (uShape == 1) return 2.0;  // a layer
    return 1.0;
}

// The weather of the cloud
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

    // THE STREAKS.
    //
    // The coordinates of the weather are turned into the wind's own frame,
    // and the one along the wind is made short - and the lumps of the
    // weather are stretched with it.
    //
    // A streak of cirrus is a streak because the wind drew it out, and
    // without this it is a lump like any other.
    float stretch = cloudStretch();
    if (stretch > 1.0) {
        vec2 wind = uWindDirection;
        vec2 side = vec2(-wind.y, wind.x);
        vec2 along = vec2(dot(p.xz, wind), dot(p.xz, side));
        along.x /= stretch;
        p.xz = along.x * wind + along.y * side;
    }

    return cloudFbm3(p, uint(max(uSeed, 0.0)), max(uCloudDetail, 1));
}

// THE DENSITY OF THE CLOUD
//
// How much of the cloud there is at a point of its box: 0..1.
float cloudDensity(vec3 q) {
    float envelope = cloudEnvelope(q);
    if (envelope <= 0.0) return 0.0;

    float fill = cloudFill(cloudWeather(q));
    if (fill <= 0.0) return 0.0;

    // THE FINE STRUCTURE.
    //
    // It eats into the density, and it eats in HARD: the cloud is cut off
    // at the level of the erosion and not faded into it.
    //
    // This is the whole difference between a cloud and a puff of smoke. A
    // cloud whose edge fades away is a cloud of smoke, and the cauliflower
    // of a real cloud is a set of hard steps - the light ends at them and
    // the sky begins.
    float detail = cloudDetail3(q * uCloudDetailScale, uint(max(uSeed, 0.0)) + 91u);
    float cut = detail * uCloudSoftness * (1.0 - fill);
    fill = clamp((fill - cut) / max(1.0 - cut, 0.001), 0.0, 1.0);

    // The height of the point in the cloud, and how much of the cloud there
    // is at that height.
    float base, top;
    cloudHeightRange(fill, base, top);
    float y = q.y * 0.5 + 0.5;
    if (y <= base || y >= top) return 0.0;
    float t = (y - base) / max(top - base, 0.001);

    // The cloud is thin at its base and thinner towards its top, and the
    // gradient is what makes the light of the top of it brighter than the
    // light of the bottom of it.
    float gradient = smoothstep(0.0, 0.05, t) * (1.0 - t * 0.8);

    return fill * envelope * gradient * uCloudDensity * cloudShapeDensity();
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

    float fill = cloudFill(cloudWeather(q));
    if (fill <= 0.0) return 0.0;

    float base, top;
    cloudHeightRange(fill, base, top);
    float y = q.y * 0.5 + 0.5;
    if (y <= base || y >= top) return 0.0;
    float t = (y - base) / max(top - base, 0.001);
    float gradient = smoothstep(0.0, 0.05, t) * (1.0 - t * 0.8);

    return fill * envelope * gradient * cloudShapeDensity();
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
