# Spectra3D Script SDK

Everything you need to embed **Spectra3D Script** into your engine (C++).

```
include/sps/     the public C API (7 headers)
include/spl.hpp   the C++ reader and writer of the .spl format
lib/libsps.a     the static library: the compiler + the VM + the API
bin/             spsc.exe (the compiler) and spsvm.exe (the runner)
example/         a mini engine and a game script
assets/          an example of the .spl data format
```

## The compiler

The compiler is **inside the library** — the host compiles the scripts itself:

```cpp
sps_context_exec_file(context, "assets/scripts/player.sps");   // compiles and runs
```

So the engine needs nothing else to run the `.sps` scripts.

`bin/spsc.exe` is the same compiler as a command line tool, for the standalone
scripts and the tests:

```bash
spsc.exe assets/scripts/player.sps -o assets/scripts/player.spc
```

`bin/spsvm.exe` runs a compiled script without the engine — useful to test
the logic of a script.

```bash
spsvm.exe assets/scripts/player.spc
```

> **Important:** the scripts of the game use the classes of the engine
> (`Entity`, `Scene`, ...), and those names are registered by the host at
> run time. The standalone `spsc.exe` does not know them, so `--check` of such
> a script reports `undefined variable 'Scene'`. The scripts of the game are
> compiled by the engine itself — that is the way to go, and it is also what
> makes the hot reload possible:

```cpp
// the engine compiles the scripts on the fly
sps_context_exec_file(context, "assets/scripts/player.sps");

// the reload of the scripts without the restart of the engine
SpsChunk* chunk = sps_compile_file(context, "assets/scripts/player.sps");
sps_context_run(context, chunk);
```

## Integrating (5 lines)

```cpp
#include "sps/sps.h"

SpsContext* context = sps_context_new();
sps_context_set_error_handler(context, on_error, NULL);
sps_register_function(context, "log", my_log, NULL);
sps_context_exec_file(context, "assets/scripts/player.sps");
```

### CMake

```cmake
target_include_directories(MyEngine PRIVATE ${SPS_SDK}/include)
target_link_libraries(MyEngine PRIVATE ${SPS_SDK}/lib/libsps.a)
```

### Plain Makefile

```make
CXXFLAGS += -I$(SPS_SDK)/include
LDFLAGS  += $(SPS_SDK)/lib/libsps.a
```

The library is built with the C11 runtime of MinGW / MSYS2 and has no
dependencies. If your engine uses a different runtime, rebuild the library
from the sources of the language (the `src/` folder of the project).

## What the API gives you

| Header                  | What it contains                                        |
|-------------------------|---------------------------------------------------------|
| `sps/sps.h`             | the main header, it includes the rest                  |
| `sps/sps_types.h`       | `SpsValue`, `SpsContext`, the list of the types         |
| `sps/sps_value.h`       | the constructors and the conversions of the values       |
| `sps/sps_context.h`     | the context, the errors, the global variables            |
| `sps/sps_function.h`    | the registration of the native functions, the calls      |
| `sps/sps_class.h`       | the classes of the host: the objects, the methods, the properties |
| `sps/sps_compiler.h`    | the compilation, the diagnostics, the bytecode           |

* The **context** is an independent instance of the VM with its own globals.
* The **native functions** are called from the scripts by their name.
* The **classes of the host** give the scripts your own objects (userdata)
  with the methods, the constructor and the properties.
* The **calls into the script** let the host call a function of the script.
* The **error handler** receives the errors instead of a fatal exit.
* The **hot reload** is `sps_compile_file` + `sps_context_run`.

## The example

`example/host_example.cpp` is a mini engine: it registers the classes
`Entity`, `Scene`, `Camera`, `Light`, `Rigidbody`, `Collider`, `Trigger`,
`AudioSource`, `Label`, `Button`, `Vector3`, `Color`, `Shader`, `Material` and
the functions of the input, the sound, the time, the physics and the events.

`example/game.sps` is the script of a game: the player, the coins, the
triggers, the shaders and the sound.

```bash
g++ -std=c++17 -O2 -Iinclude example/host_example.cpp lib/libsps.a -o host_example
./host_example example/game.sps
```

## The scripts

The scripts of the game are the `.sps` files. The engine runs them directly
(`sps_context_exec_file`) — the compilation happens on the fly, which is also
what makes the hot reload possible:

```cpp
// the reload of the scripts without the restart of the engine
SpsChunk* chunk = sps_compile_file(context, "assets/scripts/player.sps");
sps_context_run(context, chunk);
```

## The data format

`.spl` is the compact format of Spectra3D: the keys are not quoted, the
comments are allowed, the commas are optional.

```
{
    // the name of the level
    name = "level_01"
    version = 3
    player = { hp = 100, name = 'hero' }
}
```

The C++ header `include/spl/spl.hpp` of the language project reads and writes
it (it replaces `json.hpp`).

## The documentation

The full reference of the engine API — the classes, the functions and the
hooks of the script — is in `docs/engine-api.md` of the language project.

### AUTHORS
Nitels31 (Spectra 3D)