// A game script of Spectra3D: the scene, the player, the triggers, the sound.

var scene = Scene("level_01")
var player = Entity("player")
var coin = Entity("coin")

// --- the appearance ---
player.position = vec3(0, 1, 0)
player.size = vec3(2, 2, 2)
player.rotation = vec3(0, 0, 0)
player.color = color(0.2, 0.7, 1.0, 1.0)
player.material = loadMaterial("assets/player.mat")
player.texture = loadTexture("assets/player.png")
player.tag = "hero"

coin.position = vec3(4, 0.5, 0)
coin.color = color(1.0, 0.8, 0.0, 1.0)
coin.tag = "pickup"

// --- the physics ---
var body = Rigidbody(player)
body.gravity = 9.81
body.mass = 70.0
body.velocity = vec3(0, 0, 0)

var collider = Collider(player)
collider.shape = "box"
collider.size = vec3(2, 2, 2)
collider.isTrigger = false

// --- the trigger of the coins ---
var zone = Trigger(coin)
zone.size = vec3(3, 2, 3)
zone.onEnter = "on_coin"

// --- the sound ---
var music = AudioSource()
music.sound = "theme.ogg"
music.volume = 0.5
music.loop = true
music.play()

// --- the shaders ---
var water = Shader("assets/water.shader")
water.setFloat("uTime", 0.0)
water.setVec3("uColor", vec3(0.1, 0.4, 0.9))
water.setInt("uSteps", 4)
water.setTexture("uNoise", "assets/noise.png")

var surface = Material("assets/water.mat")
surface.shader = water
surface.color = color(0.1, 0.4, 0.9, 1.0)
surface.shininess = 64.0
surface.setFloat("uRipple", 0.25)

// --- the camera and the light ---
var camera = Camera()
camera.fov = 70.0
camera.far = 500.0

var sun = Light()
sun.type = "directional"
sun.intensity = 0.9
sun.range = 50.0

// --- the interface ---
var label = Label()
label.text = "Coins: 0"
label.position = vec3(10, 10, 0)

var button = Button()
button.text = "Play"
button.onClick = "on_play"

// --- the events of the game ---
on("coin_taken", "on_coin_taken")
on("enemy_spawned", "on_enemy")

// --- the start ---
def on_start() {
    log("the scene", scene.name, "is ready")
    log("the player is at", player.position)

    // the timers of the script
    after(0.5, "on_timer")
    every(1.0, "on_tick")

    // the ray: the coin is straight ahead
    var hit = raycast(vec3(0, 1, 0), vec3(0, 0, 1), 100.0)
    if hit != nil {
        log("the ray found:", hit[0].name, hit[3])
    }
}

// --- the frame ---
def on_update(delta) {
    if downKey("space") {
        player.move(0, 1, 0)
        playSound("jump.wav", 0.8, 1.0)
    }
    if downKey("a") {
        player.move(-1, 0, 0)
    }
    if downKey("d") {
        player.move(1, 0, 0)
    }
    body.velocity = vec3(0, body.velocity.y - body.gravity * delta, 0)
    if body.velocity.y < 0 {
        body.velocity = vec3(0, 0, 0)
    }
}

// --- the timers ---
def on_timer() {
    log("the timer is fired")
}

def on_tick() {
    log("the tick", frame())
}

// --- the triggers ---
def on_coin(entity) {
    log("the coin is taken:", entity.name)
    label.text = "Coins: 1"
    stopSound("jump.wav")
    emit("coin_taken", entity)
}

// --- the events ---
def on_coin_taken(entity) {
    log("the event is fired:", entity.name)
}

def on_enemy(value) {
    log("an enemy is spawned", value)
}

// --- the interface ---
def on_play() {
    log("the play button is pressed")
    showMessage("the game is started")
}

// --- the helpers ---
def helper(value) {
    return value * 2
}

def start() {
    log("the script is ready")
    stopSound("theme.ogg")
    log("the helper gives", callFunction("helper", 21))
    log("the math:", lerp(0, 10, 0.5), wrap(370, 0, 360), sign(-4))
    log("the screen:", screenToWorld(400, 300))
    return scene.count()
}
