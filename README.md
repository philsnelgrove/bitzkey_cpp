# Bitzkey (C++)

A C++20 port of the Python Bitzkey artificial ecology. The Python project in `../bitzkey` is
untouched; this is a separate project.

The simulation core is a faithful port: the same rules, constants and order of operations, with
Python's random number generator re-implemented exactly (Mersenne Twister, `randrange`,
`shuffle`, `gauss`), along with Python's round-half-to-even and compensated `sum()`. So a world
with the same seed and settings plays out turn for turn identically in both versions. The parity
check below compares them.

## Status

- **Stage 1, the simulation core: done.** Genome, brains, temperament, ecology, herding,
  territory, seasons and climate zones, rewindable history, census, insights, brain report, and JSON export/import
  compatible with the Python version's files.
- **Stage 2, the window: done.** Configuration panel (with quit confirmation, import box,
  drag-and-drop, file picker, paste), designer, world view, inspector, insights panel, colour
  graph (selection, timeline scrubbing, zoom, sorting by trait, hover tooltips, a selection
  shared with the map), export list and dialogs, using SDL2 and SDL_ttf
  with the same Windows fonts as the Python version.

## Run the app

```powershell
.\build\Release\bitzkey.exe            # fullscreen; add --windowed for a window
```

Launch options as in the Python version: `--width`, `--height`, `--population`,
`--grass-density`, `--seed`, `--population-cap`, `--mind-mutation`, `--body-mutation`,
`--year-length`, `--windowed`. The configuration panel's ADVANCED button opens the advanced rules, the same 139
as in the Python version, with the same keys, limits, steps and panel. The controls are the Python version's, plus `+` and `-` to change the speed: 1x
(a turn every 0.24 s, as in Python), 2x, 4x, 8x, and MAX, which simulates as many turns as fit
in each frame. `I` opens the insights panel (behaviour, life, temperament and the keenest
group for each action, by species); it scrolls with the wheel or its scroll bar, and selecting a
Bitzkey closes it. Reports and exports are written to `Documents\Bitzkey\reports` and
`Documents\Bitzkey\exports`. SDL2.dll and SDL2_ttf.dll are copied beside the executable by the
build; keep them with it. The Visual C++ runtime is linked in, so nothing else is needed.

## Build

Needs Visual Studio Build Tools 2026 (or Visual Studio) with the C++ workload, which includes CMake.

```powershell
.\build.ps1
```

This configures with CMake (the `windows` preset), builds Release, and runs the tests. In VS
Code, with the C/C++ and CMake Tools extensions, open this folder: it configures from
`CMakePresets.json`, and you can build and run from the status bar or the Run panel.

## Installer

```powershell
.\build_installer.ps1                  # or .\build_installer.ps1 -Version 1.1.0 to override VERSION
```

This builds and tests Release, then packages `dist\Bitzkey-Setup-<version>.exe` with
[Inno Setup 6](https://jrsoftware.org/isinfo.php) (`winget install JRSoftware.InnoSetup`). The
installer needs no administrator rights: it installs for the current user into
`%LOCALAPPDATA%\Programs\Bitzkey` (or for everyone, if run as administrator), adds a Start menu
shortcut and an optional desktop one, and registers an uninstaller in Settings > Apps. It carries
the app, `bitzkey_cli.exe`, the SDL DLLs, this README and the SDL licences. Uninstalling leaves
`Documents\Bitzkey` alone. Silent installs work too: `Bitzkey-Setup-1.0.11.exe /VERYSILENT`.

The version is in the `VERSION` file. The installer's name, the app's version details (in
Explorer's Properties) and the installer itself all take it from there. It goes up with each
release: the last number for fixes and balance changes, the middle one for new features, and the
first for changes that break saved files.

The script is `packaging\bitzkey.iss`. The icon, `packaging\bitzkey.ico`, is drawn by
`packaging\make_icon.py` (run it with the Python project's interpreter) and built into
`bitzkey.exe` through `packaging\bitzkey.rc`.

## Run without a window

```powershell
.\build\Release\bitzkey_cli.exe --seed 1 --turns 400
```

Options: `--width`, `--height`, `--population`, `--seed`, `--grass-density`, `--population-cap`,
`--mind-mutation`, `--body-mutation`, `--year-length`, `--import FILE`, `--turns`, `--report-every`,
`--trace FILE`, `--export FILE`, `--brain-report FILE`, `--rule KEY=VALUE` (change an advanced
rule; repeat it for more) and `--rules` (list them, with their defaults and limits). Run with
`--help` for details.

```powershell
.\build\Release\bitzkey_cli.exe --seed 1 --turns 400 --rule CORPSE_TURNS=60 --rule GRAZING.grazer=1.5
```

## Checking the port against Python

`parity/trace.py` runs the Python version (read-only, from `../bitzkey`) and writes one line per
turn: the turn, living counts by species, deaths, Bitzkeys spawned, grass, a position checksum,
and the exact sums of hunger and health. `bitzkey_cli --trace` writes the same lines from C++.

```powershell
.\build\Release\bitzkey_cli.exe --seed 7 --turns 150 --trace parity\cpp.txt
..\bitzkey\.venv\Scripts\python.exe parity\trace.py --seed 7 --turns 150 --out parity\py.txt
fc parity\cpp.txt parity\py.txt
```

Both take `--rule KEY=VALUE` to compare worlds under changed advanced rules. `src/core/Rules.cpp`
lists the rules in the order of the Python table (`tools/Rules.py`). `bitzkey_cli --rules` prints
them in the same format as this Python loop, so the two lists can be compared line by line:

```powershell
..\bitzkey\.venv\Scripts\python.exe -c "import sys; sys.path.insert(0, '../bitzkey'); from tools import Rules as R; [print(f'{r.key}  {r.group}  {R.shown(r, R.get(r.key))}  {R.shown(r, r.minimum)}..{R.shown(r, r.maximum)} by {R.shown(r, r.step)}  {r.label}: {r.hint}') for r in R.RULES]"
```

Both versions share every rule, including the two-part turn in which every animal senses, thinks and chooses at once and then acts in turn, food-aware breeding, prey switching, light competition
between plants, the rewind history of the latest 100 turns held in memory within about 1 GB (both advanced rules, with a memory warning), the colour graph remembering the dead by colour alone, groups spawning in their home zone, the brain report's climate probes, faster learning for the clever (a boosted learning rate, replayed experiences and longer traces), the catalog (choosing several with Shift and Ctrl, and OPEN CATALOG on the map and the graph to look through it), the exemplar cards beside the map with their menus of 43 statistics, a new world with the Bitzkeys picked on the graph, map zoom with Bitzkeys that zoom along, marquee and Ctrl-click selection with reticles, a graph zoom pinned to the present, seasons turning in every zone at once, movement trails, gestation with larger litters for small bodies, and stronger predators (one kind within 25%, rivalry only within a species, triple weapons, harder blows, rewards for feasting and surviving blows, and grazers that defend only when strong), primal fear of stronger hunters, a hunting instinct with longer prey sight and chases, predators' young always predators with full-sized litters, stronger climate unease and grass growing twice as fast (see the Python README). Seeds 1, 2 (with a population cap, custom mutation rates, sparse grass and a 60 x 50 world) and
7 have been checked identical over 150 to 300 turns. Seed 7 has also been checked identical over 300 turns with ten
advanced rules changed, among them a territory, a diet, an experience, the seed reach and the brain's temperature.
With seasons and climate, seeds 7 and 2 (as above) and seed 7 with all six climate rules changed have been checked
identical over 400 turns, more than two years.

## Speed

The benchmark world used for the Python optimisations (seed 1, 100 x 100, turns 121 to 150, about
1,500 to 2,300 animals): Python 286 ms per turn, C++ 12.5 ms per turn, about 23 times faster.

## Thinking on the GPU

Every brain can think and learn on the GPU instead of the CPU, through Vulkan compute, on any make
of GPU. The app looks for one as it starts: where it finds one, the advanced rule Think on the GPU
(World group) is on by default; where it finds none, the rule is greyed out, saying why, and the
brains think on the CPU. The command-line tool thinks on the CPU, the exact reference, unless run
with `--gpu`. The header says which GPUs are thinking, or why none could and the CPU carries on.

- Every brain lives in the GPUs' own memory, in pools of one depth, shared out among all the GPUs
  found; each turn only the senses go up and only the values come back. All the brains value
  their options in one batch, then learn in one batch: each its TD(lambda) lesson and replays, in
  order, and at the turn's end what its herd taught it.
- On the GPU a brain works in floats, not doubles, so a run differs from the same seed on the CPU
  and from one GPU to another: the CPU stays the exact, Python-matched reference, and the unit tests
  check the GPU's thinking and learning against it.
- A brain's copy on the CPU is brought back only when something reads its weights: breeding, an
  export, the catalog, the brain report.
- With brains changing only on the GPU, the history keeps only the present: there is no rewinding.
- Vulkan is loaded when a world asks for the GPU, so the app runs without it. Discrete GPUs are used
  when there are any, else integrated ones. `BITZKEY_GPU=off` keeps the CPU; `BITZKEY_GPU=all` adds
  integrated GPUs beside discrete ones.

The animals also sense the world on every CPU core at once, each writing only to itself, and a world
thinking on the GPU keeps no history to copy.

On an RTX 5070 Ti Laptop GPU: a 100 x 100 world of a few hundred animals ran at 4.9 ms a turn on
the GPU against 48.7 ms on the CPU; a 400 x 400 world of about 3,000 animals at 30 ms against 409 ms;
and a 700 x 700 world of about 8,000 animals (11,000 Bitzkeys with the plants) at 94 ms, about 11
turns a second. Worlds may be up to 1,000 x 1,000 cells, starting with up to 20,000 Bitzkeys.

To see where the time goes, run the command-line tool with `--profile`, which prints each part of a
turn's average (upkeep, sense, think, learn, choose, act, after, record), and set
`BITZKEY_GPU_PROFILE=1` to have the GPU's own share broken down as the program ends.

The shaders are in `shaders/`, and are compiled ahead of time into `src/core/shaders/` so that
building needs no Vulkan SDK; after changing one, run `.\tools\compile_shaders.ps1`, which needs the
SDK's `glslc`.

## Layout

- `src/core/`: the simulation: `PyRandom` (Python's generator), `PyFormat` (Python's number
  conventions), `Genome`, `Brain`, `Insights`, `Census`, `Exchange`, `Simulation`, `Rules` (the
  advanced rules), `GpuBrains` (brains on the GPUs, through Vulkan).
- `shaders/`: the GPU shaders, and
  `src/core/shaders/` their compiled SPIR-V.
- `src/cli/`: the command-line runner.
- `src/ui/`: the window app: `Canvas` (software drawing with pygame's shapes), `Window`, `Ui`
  (scaling and fonts), `Look`, `Dialog`, `Setup`, `Advanced`, `Inspector` (a Bitzkey's details, for the
  sidebar and the graph's tooltip), `Designer`, `ColourGraph`, `App`.
- `src/ui/Icons.cpp`: the species icons (tree, leaf, apple, drumstick), drawn from shapes.
- `tools/screens.cpp`: draws every screen off-screen to BMP files, for checking the interface.
- `tests/`: unit tests (doctest).
- `parity/`: the Python trace script.
- `packaging/`: the installer script, the app icon and its generator, and the resource file.
