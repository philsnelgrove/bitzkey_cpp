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
parser.add_argument("--seed", type=int, required=True)
parser.add_argument("--grass-density", type=float, default=50)
parser.add_argument("--population-cap", type=int, default=None)
parser.add_argument("--mind-mutation", type=float, default=50)
parser.add_argument("--body-mutation", type=float, default=50)
parser.add_argument("--year-length", type=int, default=360)
parser.add_argument("--turns", type=int, default=200)
parser.add_argument("--out", type=Path, required=True)
parser.add_argument("--rule", action="append", default=[], metavar="KEY=VALUE",
                    help="change one of the advanced rules; may be repeated")
options = parser.parse_args()

sys.path.insert(0, str(options.python_project))
from tools import Rules  # noqa: E402
from tools.Simulation import Simulation  # noqa: E402

changes = {}
for rule in options.rule:
    key, _, value = rule.partition("=")
    if key not in Rules.BY_KEY or not value:
        parser.error(f"unknown rule {rule}")
    changes[key] = float(value)
if problems := Rules.problems({**Rules.DEFAULTS, **changes}):
    parser.error(problems[0])
Rules.apply(changes)


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


world = Simulation(options.width, options.height, options.population, options.seed, options.grass_density,
                   (), options.population_cap, options.mind_mutation, options.body_mutation, options.year_length)
with options.out.open("w") as out:
    out.write(trace_line(world) + "\n")
    for _ in range(options.turns):
        world.advance()
        out.write(trace_line(world) + "\n")
        if world.end_reason:
            break
