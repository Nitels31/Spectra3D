# Spectra3D

Легкий 3D-рушій з **вбудованим редактором у стилі Godot** на
**C++17 + OpenGL 2.1 + GLFW + FreeType**. Працює на **Windows** і **Linux**.

## Структура репозиторію

```
CMakeLists.txt        єдине джерело правди для збірки
ARCHITECTURE.md       шари, правила залежностей, виправлені дефекти
build_windows.bat     обгортка cmake
build_linux.sh        обгортка cmake

src/                 власний код, поділений за шарами
  core/              Math, Log, Str
  platform/          Window (GLFW), Filesystem
  physics/           RigidBody, PhysicsWorld, Material
  assets/            MeshAsset, MeshRegistry, loaders/{glb,obj,ply,fbx}
  render/            Camera, SceneRenderer, UiRenderer, TextAtlas, PrimitiveCache
  ui/                Theme, UiContext
  scene/             BodyFactory, SceneSerializer
  i18n/              Localizer
  editor/            Editor, panels/, Picking, Gizmo, AssetBrowser
  app/               Application, FixedClock, main

third_party/         GLFW, FreeType, cgltf, rapidobj, miniply, ufbx, angelscript
assets/              lang/*.lang, models/*.{glb,obj,ply,fbx}
tools/               glbinfo.cpp, fbxtest.cpp — окремі утиліти для ассетів
build/               весь вивід: spectra3d.exe, glfw3.dll, engine.log, копія assets/
```

`ARCHITECTURE.md` описує шари і правило залежностей. Коротко: залежності йдуть
лише вгору — `core` ← `platform` ← `physics` ← `assets` ← `render`/`ui` ←
`scene`/`i18n` ← `editor` ← `app`. Глобального стану немає (окрім логу).

## Збірка

Потрібні CMake ≥ 3.16 та компілятор C++17 (MinGW-w64 g++ або MSVC).

```bat
build_windows.bat
build\spectra3d.exe
```

```sh
sudo apt install build-essential cmake libglfw3-dev libgl1-mesa-dev
./build_linux.sh
./build/spectra3d
```

Або вручну:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Після збірки CMake копіює `glfw3.dll` (Windows) та `assets/` у `build/`, тож
 exe запускається з будь-якої директорії. Рухливі файли (`engine.log`,
`scene.txt`) пишуться поруч із бінарником, а не в корінь репозиторію.

Опція `-DSPECTRA_BUILD_TOOLS=OFF` прибирає `glbinfo` / `fbxtest`.

## Можливості

### Редактор (Godot-стиль, режими як в Unity)

- **Два режими**: **Створення** (фізика заморожена) і **Грати** (фізика
  працює). Кнопки PLAY / PAUSE / STOP у меню-барі або `P` / `O` для кроку.
  При вході в Play зберігається знімок сцени, при STOP сцена повертається до
  стану редагування.
- **Примітиви**: Box, Sphere, Cylinder, Capsule, Cone, Pyramid, Prism.
  Кнопки «+ …» у докі «Сцена», плюс імпорт моделей.
- **Імпорт моделей `.glb` / `.gltf` / `.obj` / `.ply` / `.fbx`**:
  - `.glb/.gltf` через cgltf — POSITION/NORMAL, індекси, baseColorFactor,
    ієрархія вузлів, авто-нормалі
  - `.obj` через rapidobj — триангуляція, нормалі/flat-shading, кольори з .mtl
  - `.ply` через miniply — бінарний і ASCII, нормалі опційно
  - `.fbx` через ufbx — бінарний і ASCII, ієрархія, авто-нормалі, конверсія
    одиниць до метрів, відкидання гігантських part-ів
  - вкладка «Активи» сканує `assets/models`; фізика — box-hull; авто-нормалізація
    розміру; меші зберігаються у `scene.txt` за іменем і підвантажуються
- **Меню-бар**: Сцена / Проєкт / Перегляд / Створити / Довідка + кнопки
  Play/Pause/Stop та перемикач мови.
- **Док «Сцена»** (ліворуч): дерево об'єктів (3D Root → тип + індекс) і
  вкладка «Активи» зі скануванням файлів.
- **Док «Інспектор»** (праворуч): вкладки Інспектор / Вузол, поле фільтра,
  секції Transform / Shape / Material / Flags / Anchor / State. Числові поля
  підтримують drag-редагування і введення з клавіатури; кнопки WAKE / DELETE.
- **Вʼюпорт**: тулбар (GIZ / MOV / SCL / VEC / GRID, Перспектива 60/70/90),
  навігаційні кульки осей (клік = вирівнювання камери), ray-pick.
- **Gizmo**: MOV — стрілки з конусами, перетягування по світових осях;
  SCL — кубіки-ручки по **локальних осях тіла**; мультиплікативне масштабування
  (Box — по SX/SY/SZ, Sphere — радіус, Mesh — рівномірний SCALE). Вибір ручки
  — за екранною відстанню.
- **Нижня панель**: статус режиму, лічильник тіл, напрямок гравітації,
  консоль журналу.
- **Логи**: рівні INFO/WARN/ERROR, вивід у stderr, файл `build/engine.log`
  і кольоровий журнал у панелі «Результат»; стартова діагностика (версія
  OpenGL, GPU, статус шрифту, кількість тіл).

### Особливості

- **FPS-лічильник** у заголовку вікна, оновлюється раз на секунду.
- **6-напрямкова гравітація** — клавіші `1..6`.
- **Фізика твердого тіла**: OBB і Sphere (усі примітиви зі своїми об'ємами та
  моментами інерції); інтегрування 60 Гц фіксованим кроком; колізії
  тіло-тіло (Sphere-Sphere, Sphere-Box, Box-Box) і тіло-стіна; послідовні
  імпульси з early-exit, тертя, warm starting, корекція проникнення,
  спекулятивні контакти (стійкий спокій), засинання тіл.
- **Spatial hash broadphase** (sort-based, без алокацій): комірки 2 м,
  пари дедуплікуються через uint64 ключі → O(n) замість O(n²).
- **CPU-frustum без glGet** — матриці P·MV рахуються на ЦП, відсікання без
  синхронних запитів до драйвера.
- **Display lists** для статичної геометрії: юніт-примітиви, сітка кожної
  стіни, ребра арени — запікаються один раз.
- **Vertex batching UI**: усі квади/кола/гліфи кадру зводяться у два буфери
  і малюються двома `glDrawArrays`.
- **Текст**: FreeType → текстурний атлас ASCII + кирилиця системного TTF,
  альфа-блендинг, оцінка ширини як fallback.

## Керування

| Клавіша / миша | Дія |
|---|---|
| ЛКМ | вибір об'єкта / drag gizmo / UI |
| ПКМ (тримати) | огляд камерою |
| `W A S D`, `SPACE`/`C` | політ камери (+`SHIFT` — прискорення) |
| `1..6` | напрямок гравітації: `-Y +Y -X +X -Z +Z` |
| `P` / `O` | Play/Stop / крок симуляції |
| `DEL` | видалити виділені об'єкти |
| `F5` / `F9` | зберегти / завантажити сцену |
| `V` | дебаг-вектор гравітації |
| `ESC` | вихід |

## Формат сцени

`scene.txt` (поруч із бінарником) — один рядок на тіло, пробіли як
роздільник: фігура, позиція, швидкість, орієнтація, half-extents, маса,
тертя, реституція, густина, кутова швидкість, масштаб меша, **ім'я меша**
або `-`, статичність/видимість/блокування, якорі та жорсткість пружини.
Моделі шукаються за іменем у `assets/models` — сцена переживає перезапуск.