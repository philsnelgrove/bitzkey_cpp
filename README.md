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
`--year-length`, `--windowed`. The configuration panel's ADVANCED button opens the advanced rules, the same 137
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

Both versions share every rule, including food-aware breeding, prey switching, light competition
between plants, and the rewind history's memory budget of about 1 GB (see the Python README). Seeds 1, 2 (with a population cap, custom mutation rates, sparse grass and a 60 x 50 world) and
7 have been checked identical over 150 to 300 turns. Seed 7 has also been checked identical over 300 turns with ten
advanced rules changed, among them a territory, a diet, an experience, the seed reach and the brain's temperature.
With seasons and climate, seeds 7 and 2 (as above) and seed 7 with all six climate rules changed have been checked
identical over 400 turns, more than two years.

## Speed

The benchmark world used for the Python optimisations (seed 1, 100 x 100, turns 121 to 150, about
1,500 to 2,300 animals): Python 286 ms per turn, C++ 12.5 ms per turn, about 23 times faster.

## Layout

- `src/core/`: the simulation: `PyRandom` (Python's generator), `PyFormat` (Python's number
  conventions), `Genome`, `Brain`, `Insights`, `Census`, `Exchange`, `Simulation`, `Rules` (the
  advanced rules).
- `src/cli/`: the command-line runner.
- `src/ui/`: the window app: `Canvas` (software drawing with pygame's shapes), `Window`, `Ui`
  (scaling and fonts), `Look`, `Dialog`, `Setup`, `Advanced`, `Inspector` (a Bitzkey's details, for the
  sidebar and the graph's tooltip), `Designer`, `ColourGraph`, `App`.
- `src/ui/Icons.cpp`: the species icons (tree, leaf, apple, drumstick), drawn from shapes.
- `tools/screens.cpp`: draws every screen off-screen to BMP files, for checking the interface.
- `tests/`: unit tests (doctest).
- `parity/`: the Python trace script.
- `packaging/`: the installer script, the app icon and its generator, and the resource file.
