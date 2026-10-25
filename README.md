# Bitzkey (C++)

The Bitzkey project was started years ago, to teach my best friend programming. He wanted to learn about Python and AI, so it seemed like a natural project. Since then, my friend has passed away. So I finished this to honour him. I open-sourced it for the same reason. So, Sam Bunn, wherever you are, may you live on through Bitzkeys!

Bitzkeys is now multilingual.  The choice of languages was somewhat arbitrary since I speak none of them (well, a little French...).  If you'd like me to add a language, please leave a comment to that effect.

Also, the desktop and daemon version of Bitzkey is now distributable, cross platform.  So if you happen to have extra computers just lying around, you can run them as Bitzkey workers and explore even larger worlds!

This is the Windows desktop version: the same simulation as the Python original, rewritten in C++ to run much faster on your own computer. It is best run on a high-end gaming laptop, whose graphics card can do the Bitzkeys' thinking. For reading the code and researching how Bitzkeys work, see the [Python version](https://github.com/philsnelgrove/bitzkey), the code research version; for larger worlds, the [Linux daemon](https://github.com/philsnelgrove/bitzkey_daemon), the larger-scale version.

Download the installer from the [latest release](https://github.com/philsnelgrove/bitzkey_cpp/releases/latest).

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
(a turn every 0.24 s, as in Python), 2x, 4x, 8x, 16x, 32x, 64x and MAX, which is 128x (the faster speeds run several turns a frame; a world too big to keep up runs as fast as it can, at most 25 ms of turns
in each frame). SAVE WORLD in the map's footer, SAVE AND LEAVE when `Esc` asks to leave, and LOAD WORLD on the
configuration panel save and load whole worlds as in the Python version, in the same file format (`src/core/WorldFile.*`), so a
world saved by either loads in the other and carries on turn for turn the same; saves go in `worlds/` in the data folder. `I` opens the insights panel (behaviour, life, temperament and the keenest
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
rule; repeat it for more), `--rule-at TURN:KEY=VALUE` (change one once the world reaches TURN, as
the map's ADVANCED button does), `--deploy-at TURN:X,Y:FILE` (deploy the Bitzkeys
exported in FILE at a cell once the world reaches TURN, as the catalog's DEPLOY does),
`--load FILE` (carry on a saved world, by its own settings and rules), `--save FILE` (save the whole world at the end)
and `--rules` (list them, with their defaults and limits). `parity/trace.py` takes `--load` and `--save` too, so a world
saved by one version can be traced on in both. Run
with `--help` for details.

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

Both take `--rule KEY=VALUE` to compare worlds under changed advanced rules, and
`--rule-at TURN:KEY=VALUE` to change them part way through, as the map's ADVANCED button does,
and `--deploy-at TURN:X,Y:FILE` to deploy Bitzkeys part way through, as the catalog's DEPLOY does. `src/core/Rules.cpp`
lists the rules in the order of the Python table (`tools/Rules.py`). `bitzkey_cli --rules` prints
them in the same format as this Python loop, so the two lists can be compared line by line:

```powershell
..\bitzkey\.venv\Scripts\python.exe -c "import sys; sys.path.insert(0, '../bitzkey'); from tools import Rules as R; [print(f'{r.key}  {r.group}  {R.shown(r, R.get(r.key))}  {R.shown(r, r.minimum)}..{R.shown(r, r.maximum)} by {R.shown(r, r.step)}  {r.label}: {r.hint}') for r in R.RULES]"
```

Both versions share every rule, including the two-part turn in which every animal senses, thinks and chooses at once and then acts in turn, food-aware breeding, prey switching, light competition
between plants, the rewind history of the latest 100 turns held in memory within about 1 GB (both advanced rules, with a memory warning), the colour graph remembering the dead by colour alone, groups spawning in their home zone, the brain report's climate probes, faster learning for the clever (a boosted learning rate, replayed experiences and longer traces), the catalog (choosing several with Shift and Ctrl, and OPEN CATALOG on the map and the graph to look through it), the exemplar cards beside the map with their menus of 50 statistics, a new world with the Bitzkeys picked on the graph, map zoom with Bitzkeys that zoom along, marquee and Ctrl-click selection with reticles, a graph zoom pinned to the present, zooming into the height around the pointer and panning by right-drag as the map does, seasons turning in every zone at once, movement trails, gestation with larger litters for small bodies, and stronger predators (one kind within 25%, rivalry only within a species, triple weapons (omnivores' 2.25 times, grazers' 1.5 times), harder blows, rewards for feasting and surviving blows, and for every easing of hunger, and grazers that defend only when strong), primal fear of stronger hunters (predators looming two steps larger, and feeling half their fear, less the hungrier, none as they starve), species flips stepping along the chain by which bit flips (and rolled afresh half the time), mutations a quarter of births by default, a hunting instinct with longer prey sight and chases, predators' young always predators with full-sized litters, grazers always prey to predators and omnivores only when the predator would win, species evolving along the chain grazer, omnivore, predator (eating experience passing to hunting), bodies eaten in portions (each a whole feast, fresh for three turns after death), the death report and brain inspection, predators thinking twice as well and learning twice as fast (omnivores one and a half times), five times as much learnt from hunting, attacking and feasting and half as much from eating plants, gentler climate damage (2% a step), the advanced panel's word on natural law, the map's ADVANCED button (pausing to change the running world's rules, from its next turn), the in-game catalog's DEPLOY (painting 5 of each chosen around each click, until a right-click or Esc), predators breeding only as the prey in their home climate band allow (Prey per predator), sensed as home food (88 senses), breeding the greatest pleasure and courting into reach the next (Breeding joy 4, Courting joy 2.5, a quarter by libido; no penalty for failing), meat ten times as filling as grass and plants a fifth of meat (grass growing back twice as fast), uprooting half a feast, predators' bloodlust, carrion-eaters' craving, and the alert action (to a hunter, or a fresh kill), senses for prey carrying young and for a pack-mate's kill alert, prey worth ten meals to a breeding hunter, pack-mates half a mouth, partners close enough within two cells, and the predators' mating call (to breed, or else to court; half strength) with a short rest between their litters, omnivores sizing up predators, predators never hunting their own kind (until predators glut the world past 1,200), territories wearing down reluctance by half, and omnivores hunting grazers only while they are half the food about them, predators seeking mates six cells off, omnivores' milder mating call, and hunters breeding at half the cost, and grazers defending with half their weapons, omnivores three quarters, and predator packs no rivals to one another, and the graph remembering how each Bitzkey died, what killed it and what the killer was doing (the death report gathering every death back to the start: causes, who killed whom, the killers' activities and the deadliest killers), and litters no larger than the parents can feed (Newborn food), and sight tripled (3 cells a step of the trait), reaching as far for herds, crowding and territory, with the mating call reaching twice its sight, and grazers breeding only as the plants in their home climate band allow (Plants per grazer), and squares holding 10 spaces (each Bitzkey one plus its size; Square space) shared by births, moves and spawns, with a popup listing a hovered square's Bitzkeys to select, meat digested at half speed (Meat digestion), and full hunger wearing 10% of health away a turn rather than killing (Starving damage), and animals feeble in infancy (the first 10% of the lifespan) and old age (from 70%), every body trait and their health halved (Feeble youth, Feeble share), and omnivores living alone, in neither herd nor pack (Solitary omnivores), and courting open, and paying, only toward a partner it could then breed with, with scarce prey shrinking predators' litters rather than stopping their breeding, and predators courting and hunting only toward rows no further from their home temperature, as herds herd, and primal fear doubled (overcoming as often as the fight is lopsided, predators two thirds as susceptible, and the dullest always freezing), and litters limited by what the parents could give rather than what they pay, at 5% of a stomach a young (Newborn food), and cowardly omnivores (always breaking when struck) and predators never overcome by primal fear, and omnivores' litters following the plants and prey in their home band, half each, and omnivores without spines, and bodies that rot away leaving the seed of a new plant one time in ten (Corpse plant), and grazers breeding at half the cost and predators taking spines half as hard, and carrion-eaters (above the middle of the carrion trait) shying from hunting and scaling into cowards, and scarce species breeding more cheaply (below 100 living, down to a third of the cost at 10 or fewer), and predators' infancy half as long, and breeding more sensitive to sparse food (hunters stopped below their home-food need as grazers are, full litters only at four times the need, and no hunter breeding while fewer than 500 of its prey live), and predators' full litters only at eight times their prey need, and grazers' cheap breeding from 300 living down to a ninth of the cost, and predators turning on one another as they glut the world (other kinds from 600, their own kind past 1,200: Predator glut) or as their prey die out (other kinds below 1,000 prey, their own below 500), and the colour graph's popup for the dead (their genome, and how and when they died) and the catalog's DESIGNER (a design joining the catalog, genome alone), and selected Bitzkeys' sights pulsing to twice their size and back every two seconds, and infants cared for (parents paying for every young's start, grown animals feeding nearby infants of their kind, infants keeping close to their kind), and the command-line runner's --diagnose writing every species' fate a turn (FILE.ecology.tsv: deaths by cause and age, condition, climate, breeding), and rewards earned by outcomes, not attempts (curiosity only for a move made, alert joy only when heeded, attack joy only for a blow that goes its way, learning weights for successes only, no death penalty for slow deaths, every move paying for its distance, defending damped by tolerance as attacking is, and feebleness no lesson), and the 96-bit genome's third strand, the social background (version 3 exports), drawn behind the species icon, holding sociability, empathy and tolerance with loyalty, dominance, trust, kinship and a neurodivergence gene, while the dot gains memory (and the revisit action), resilience and moodiness (87 senses), breeding limited by crowding of its own kind alone by default, litters shrinking only for their own kind (food, the hunger reserve and the rest between litters all off, each a rule), intellect sharpened by good experiences and lessons, temperament barely relapsing, stronger climate unease and grass growing twice as fast (see the Python README). Seeds 1, 2 (with a population cap, custom mutation rates, sparse grass and a 60 x 50 world) and
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
If the GPUs fail while a world runs, most often by running out of memory as its brains' pools grow, every brain is
brought back from them as far as it still can be and the world carries on thinking on the CPU, saying so in the
header, rather than stop. Its rewind history starts again from the next turn.

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

## Workers

A world's thinking can be shared with other computers. The computer running the world is the main service. The others are **workers**, which hold some of its brains and do their deciding and learning, the costliest part of a turn. Either role can be filled by this app or by the [Linux daemon](https://github.com/philsnelgrove/bitzkey_daemon), in any mix: they share the same code for this, in `src/net/`.

**Using workers.** While a world runs, **WORKERS** (in the footer, green while workers are sharing) lists each worker with:

- its state;
- its ping time;
- the cores, memory and GPUs it shares;
- its measured speed;
- how many brains it holds.

Add a worker by its IP address, with its port if it isn't 50000 (in the port box, or as `192.168.1.20:50001`), and optionally a message for its screen. A link in the message can be clicked there. The world's brains are shared out by the pace each machine keeps on real turns, its network included, and this app keeps a share: as much as its pace earns, scaled by the host share (100% by default, set under Host share; at 0 this computer only runs the world). Every brain is brought back once every 100 turns (set under Checkpoints), a share of them each turn. If a worker fails, its brains go to the others as they were then. A worker that is removed, or that stops, hands its brains back first, and saving the world gathers them all. As on the GPU, a world thinking through workers keeps no history to rewind.

**Being a worker.** **BECOME A WORKER**, on the configuration panel (or **BECOME A WORKER...** on the WORKERS screen), asks for:

- the port to listen on;
- the cores to share;
- the memory to share;
- the GPUs to share.

The world is let go, and the app shows only a gray worker screen. That screen gives the main service's address and message, and lets you change the port and the shares at any time. **STOP BEING A WORKER** hands the brains back and returns to the configuration panel. Closing the window hands them back too, and the app opens as a worker again next time.

**The firewall.** Becoming a worker, changing its port, and stopping each ask Windows for approval (the usual administrator prompt). Once approved, Windows' firewall lets the main service in on that port, under a rule named "Bitzkey worker". A router in front of the computer needs the port forwarded by hand. The main service needs no open port: it connects out to its workers.

## The probe

The probe lets a program, or an assistant, look at the world while it runs: any Bitzkey, the herds, and the turn-by-turn tracks of the Bitzkeys it is asked to watch. It only reads the world, never changes it, and answers between turns, so it never slows a turn part way.

It is off unless asked for. Start the app with `--probe PORT`, or put `{"port": 49200}` in `probe.json` in the Bitzkey folder in Documents (beside `settings.json`). It listens on that port of this computer only (127.0.0.1), and answers JSON:

| Route | What it shows |
|---|---|
| `GET /probe/state` | the turn, season, map size and how many of each species live |
| `GET /probe/animals?species=&x=&y=&r=&limit=` | living Bitzkeys in brief, of a species, or within `r` steps of a cell |
| `GET /probe/animal/ID` | one Bitzkey in full: what it sensed and the odds it chose by at its last choice, its mind, home and the temperature where it stands, its parents, and its herd |
| `GET /probe/family/ID` | its parents and theirs, as far as the world still holds them, and its living young and theirs |
| `GET /probe/herds?species=&members=1` | the herds as they stand: herd-mates within one another's sight, and theirs, each herd's size, middle, spread and mean home temperature. A herd keeps its number from one look to the next while most of it holds together, with how far it has drifted, and which herds it split from or which merged into it |
| `POST /probe/watch` `{"ids": [...], "keep": turns}` | watch these Bitzkeys: each turn, where each is, what it did, its health, hunger and how far from home |
| `GET /probe/watch` | the tracks of those watched, until they die (720 turns of each kept, unless told otherwise) |
| `DELETE /probe/watch?ids=` | stop watching these, or all |

For example, `curl http://127.0.0.1:49200/probe/herds?species=grazer`. The daemon serves the same probe at `/api/probe/...`.

## Layout

- `src/net/`: workers: a main service's pool (`Pool`), worker mode (`Worker`), the brains a worker holds and the batches
  it is sent (`Brains`), and their binary form (`Wire`). Copied to and from the daemon, as the core is.
- `src/core/`: the simulation: `PyRandom` (Python's generator), `PyFormat` (Python's number
  conventions), `Genome`, `Brain`, `Insights`, `Census`, `Exchange`, `Simulation`, `Rules` (the
  advanced rules), `GpuBrains` (brains on the GPUs, through Vulkan).
- `shaders/`: the GPU shaders, and
  `src/core/shaders/` their compiled SPIR-V.
- `src/cli/`: the command-line runner.
- `src/ui/`: the window app: `Canvas` (software drawing with pygame's shapes), `Window`, `Ui`
  (scaling and fonts), `Look`, `Dialog`, `Setup`, `Advanced`, `Inspector` (a Bitzkey's details, for the
  sidebar and the graph's tooltip), `Designer`, `ColourGraph`, `App`, `WorkersScreen` and `WorkerScreen` (with
  `Fields`, their text boxes and share form).
- `src/ui/Icons.cpp`: the species icons (tree, leaf, apple, drumstick), drawn from shapes.
- `tools/screens.cpp`: draws every screen off-screen to BMP files, for checking the interface.
- `tests/`: unit tests (doctest).
- `parity/`: the Python trace script.
- `packaging/`: the installer script, the app icon and its generator, and the resource file.

## Licence

Copyright (C) 2026 Phil Snelgrove. Bitzkey is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License, version 3 or (at your option) any later version, as published by the Free
Software Foundation. It is distributed in the hope that it will be useful, but without any warranty; see `LICENSE`.
