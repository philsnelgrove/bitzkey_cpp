# The keeper's handbook

How to read a Bitzkey world without help. Everything here comes from watching real worlds, and each number is what was seen then, not a law. Rules change; when this disagrees with the code, trust the code.

## A visit, in ten minutes

1. Open Bitzkey Reports and connect, or open a save. For a running world, type its port (49200 unless `probe.json` says otherwise).
2. Read **Summary**. It lists what stands out, good news and bad.
3. Read **Diary**. Has anything been done for the first time since you last looked?
4. Read **Tools & teaching**, the table of how each craft came to be known.
5. If something looks wrong, go to the tab for it: **Population**, then **Deaths**, then **Condition**.
6. Press SAVE. Saved reports let you compare later, and one report alone can't show a trend.

## What is normal

- **Fresh worlds often die early**, sometimes in the first winter. This isn't a fault. Try another seed, or start from an established world.
- **Numbers swing.** Species crash and recover. Check the Population chart before worrying about a single low count.
- **Tool use goes up and down.** Crafts are learned in life and not inherited, so a young population knows fewer. A drop after a bad winter may mean only that the old died.
- **Most armed animals never made their weapon.** In one world 77% of wielders knew no weapon craft: they picked up gear others had dropped.
- **Curiosity can breed down to almost nothing.** Omnivores reached about 0.02. Any reward multiplied by a trait is worthless once the trait has gone, so check a trait's value before trusting a rule that depends on it.
- **Brains jam.** Neurons pinned at either end are inherited detectors as well as a fault, and a good share is usual. Worry when the Summary says brains are running away.

## How to read the signs of learning

- **Discovered, or taught?** The Tools & teaching table says. An animal that knows a craft with no tries found it by chance, and was not taught.
- **A line of teachers** is what culture looks like. A longest chain of 3 or more means a recipe has outlived the animal who found it.
- **Mastered** means ten tries' worth. A student also inherits its teacher's tries, so it can be mastered without being practised. Look for **made a taught recipe** in the Diary, which means the recipe was actually used.
- **A guru** is an elder teaching a student. The Diary records the first one, and the Summary shows it.
- **Migrating ahead of winter:** in Herds & migration and Climate, or `bitzkey_cli --migration`, anticipation shows as animals leaving a band before it gets cold. Grazers have done this by turnover, not travel: births shift, while survivors hardly move. So ask how they moved, as well as whether.

## Judging your own changes

- Change one rule at a time, and write down the turn. Each world keeps its own saved rules, so changing a default doesn't touch a running world.
- Save a report just before the change. Compare after at least a year, and better several: tools and teaching move over generations.
- Small counts mislead. Fewer than about 30 animals is a hint, not a result.
- A scene you noticed is not a pattern. One fire was a fluke; a thousand-to-one chance happens in a large world.
- A "no" is a real result. Record it as carefully as a "yes".

## When a world is in trouble

| What you see | Look at | Rules that usually bear on it |
|---|---|---|
| One species falling fast | Deaths, by cause | depends on the cause: hunger means grass or grazing, cold means climate damage |
| Hungry everywhere | Climate & grass | grass growth, population cap |
| Predators eating each other | Deaths, who killed whom | too few prey: often a grazer crash first |
| Brains running away | Brains | learning rate, value scale |
| Crafts vanishing | Tools & teaching, sources | discovery chance, craft joy |

Before changing anything, ask whether the world might recover alone. It often does, and a change can't be cleanly undone once the world has lived with it. Keep a copy of a save before any experiment you care about.

## Where things live

- **Worlds and reports:** the Bitzkey folder in Documents. A world file carries its rules, history and diary.
- **The server's world:** `/var/lib/private/bitzkey_daemon/`, with its probe at `/api/probe/...`.
- **Every question the probe can answer:** the Probe section of the README. Try `curl http://127.0.0.1:49200/probe/diary`.
- **Fragile things:** a world saved by a newer version may not open in an older one. Back up before updating.

## Regard, factions and reputation

Ask `/probe/regard` whether animals strike enemies more than chance, stay near friends and the skilled, keep clear of the dangerous, trust friends' calls, and how many hostile or friendly colours they hold. Its blow and alert counts restart with the program.


- `/probe/society` asks whether families, tribes, colour factions and leaders are forming. Read each distance against `strangers_mean_steps`.
