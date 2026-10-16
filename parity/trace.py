"""Write the Python simulation's turn-by-turn trace, in the same format as `bitzkey_cli --trace`,
so the two versions can be compared line by line.

It only reads the Python project; nothing there is changed.

    python parity/trace.py --seed 7 --turns 200 --out python_trace.txt
    python parity/trace.py --seed 7 --rule CORPSE_TURNS=60 --rule GRAZING.grazer=1.5 --out python_trace.txt
"""

import argparse
import sys
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
parser.add_argument("--python-project", type=Path,
                    default=Path(__file__).resolve().parents[2] / "bitzkey",
                    help="the Python Bitzkey project (default: ../bitzkey beside this project)")
parser.add_argument("--width", type=int, default=100)
parser.add_argument("--height", type=int, default=100)
parser.add_argument("--population", type=int, default=90)
parser.add_argument("--seed", type=int, default=None)
parser.add_argument("--grass-density", type=float, default=50)
parser.add_argument("--population-cap", type=int, default=None)
parser.add_argument("--mind-mutation", type=float, default=25)
parser.add_argument("--body-mutation", type=float, default=25)
parser.add_argument("--year-length", type=int, default=360)
parser.add_argument("--turns", type=int, default=200)
parser.add_argument("--out", type=Path, required=True)
parser.add_argument("--load", type=Path, default=None, help="carry on the world saved in this file, rather than start one")
parser.add_argument("--save", type=Path, default=None, help="save the whole world at the end, to load and carry on")
parser.add_argument("--rule", action="append", default=[], metavar="KEY=VALUE",
                    help="change one of the advanced rules; may be repeated")
parser.add_argument("--rule-at", action="append", default=[], metavar="TURN:KEY=VALUE",
                    help="change a rule once the world reaches TURN, as the map's ADVANCED does; may be repeated")
parser.add_argument("--deploy-at", action="append", default=[], metavar="TURN:X,Y:FILE",
                    help="deploy the Bitzkeys exported in FILE at cell X,Y once the world reaches TURN, as the catalog's "
                         "DEPLOY does; may be repeated")
options = parser.parse_args()

sys.path.insert(0, str(options.python_project))
from tools import Rules  # noqa: E402
from tools.Exchange import import_bitzkeys  # noqa: E402
from tools.Simulation import Simulation  # noqa: E402
from tools.WorldFile import load_world, save_world  # noqa: E402

changes = {}
for rule in options.rule:
    key, _, value = rule.partition("=")
    if key not in Rules.BY_KEY or not value:
        parser.error(f"unknown rule {rule}")
    changes[key] = float(value)
if problems := Rules.problems({**Rules.DEFAULTS, **changes}):
    parser.error(problems[0])
Rules.apply(changes)
later: dict[int, dict[str, float]] = {}
for rule in options.rule_at:
    turn, _, rest = rule.partition(":")
    key, _, value = rest.partition("=")
    if key not in Rules.BY_KEY or not value or not turn.isdigit():
        parser.error(f"unknown rule {rule}")
    later.setdefault(int(turn), {})[key] = float(value)
deployments: dict[int, list[tuple[int, int, list]]] = {}
for deployment in options.deploy_at:
    turn, place, path = deployment.split(":", 2)
    x, y = place.split(",")
    deployments.setdefault(int(turn), []).append((int(x), int(y), import_bitzkeys(Path(path).read_text(encoding="utf-8"))))


def trace_line(world: Simulation) -> str:
    alive = world.living
    counts = [0, 0, 0, 0]
    for agent in alive:
        counts[("plant", "grazer", "omnivore", "predator").index(agent.species)] += 1
    places = sum(agent.x * 1000 + agent.y for agent in alive)
    hunger = sum(agent.hunger for agent in alive)
    health = sum(agent.health for agent in alive)
    return (f"{world.turn} {len(alive)} {counts[0]} {counts[1]} {counts[2]} {counts[3]} {world.deaths} "
            f"{world.next_identifier} {sum(world.grass.values())} {places} {float(hunger)!r} {float(health)!r}")


if options.load:
    world = load_world(options.load)
    changes = Rules.current()  # the saved world's rules, which changes later in the run build on
else:
    if options.seed is None:
        parser.error("--seed is needed to start a world")
    world = Simulation(options.width, options.height, options.population, options.seed, options.grass_density,
                       (), options.population_cap, options.mind_mutation, options.body_mutation, options.year_length)
with options.out.open("w") as out:
    out.write(trace_line(world) + "\n")
    for _ in range(options.turns):
        if world.turn in later:  # as the map's ADVANCED does
            changes.update(later[world.turn])
            Rules.apply(changes)
            world.rules_changed()
        for x, y, bitzkeys in deployments.get(world.turn, ()):  # as the catalog's DEPLOY does
            world.deploy(bitzkeys, x, y)
        world.advance()
        out.write(trace_line(world) + "\n")
        if world.end_reason:
            break
if options.save:
    save_world(world, options.save)
