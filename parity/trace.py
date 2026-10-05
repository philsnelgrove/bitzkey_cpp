"""Write the Python simulation's turn-by-turn trace, in the same format as `bitzkey_cli --trace`,
so the two versions can be compared line by line.

It only reads the Python project; nothing there is changed.

    python parity/trace.py --seed 7 --turns 200 --out python_trace.txt
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
parser.add_argument("--turns", type=int, default=200)
parser.add_argument("--out", type=Path, required=True)
options = parser.parse_args()

sys.path.insert(0, str(options.python_project))
from tools.Simulation import Simulation  # noqa: E402


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
                   (), options.population_cap, options.mind_mutation, options.body_mutation)
with options.out.open("w") as out:
    out.write(trace_line(world) + "\n")
    for _ in range(options.turns):
        world.advance()
        out.write(trace_line(world) + "\n")
        if world.end_reason:
            break
