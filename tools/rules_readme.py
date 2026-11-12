"""Writes README.md beside each repo's rules file: every rule by section, connected rules as subsections."""
import glob, os, re, subprocess, sys

line_of = {}
ROOT = r"C:\Users\mikdr\Documents\bitzkey-git"
SCRATCH = os.path.dirname(os.path.abspath(__file__))

INTRO = """Bitzkeys began as an arena of sorts.  It was a test to see who would be the last colour standing.  The problem we encountered was, that omnivores always won.
So, we started implementing rules... some of them are natural laws, and some of them are instinct.  The goal of these rules was to bring balance to the world.  The first such rules nerfed omnivores by buffing the food gathering of grazers and predators.
The coming years brought numerous tweaks, balances and special case considerations.  What follows below, is a complete list of every rule and it's definition.  These are all configurable in the sim client.
"""


def cpp_rules():
    """(key, group, default, minimum, maximum, step, label, hint) from the C++ CLI's listing, in its order."""
    out = subprocess.run([os.path.join(ROOT, "bitzkey-cpp", "build", "Release", "bitzkey_cli.exe"), "--rules"],
                         capture_output=True, text=True, encoding="utf-8", check=True).stdout
    rules = []
    for line in out.splitlines():
        m = re.match(r"(\S+)  (\S+)  (\S+)  (\S+)\.\.(\S+) by (\S+)  (.*?): (.*)$", line)
        if not m:
            sys.exit(f"unparsed: {line}")
        rules.append(m.groups())
    return rules


def py_rules():
    code = ("import json, tools.Rules as R\n"
            "print(json.dumps([[r.key, r.group, R.shown(r, R.DEFAULTS[r.key]), R.shown(r, r.minimum), R.shown(r, r.maximum), R.shown(r, r.step), r.label, r.hint] for r in R.RULES]))")
    out = subprocess.run([os.path.join(ROOT, "bitzkey", ".venv", "Scripts", "python.exe"), "-c", code], cwd=os.path.join(ROOT, "bitzkey"),
                         capture_output=True, text=True, encoding="utf-8", check=True, env={**os.environ, "PYTHONPATH": "."}).stdout
    import json
    return [tuple(rule) for rule in json.loads(out)]


def clusters(sources, names, declaration):
    """Each rule constant's block: constants declared under one comment, with no blank line between, belong together."""
    block_of, block = {}, 0
    global line_of
    for path in sources:
        last = "blank"
        for raw in open(path, encoding="utf-8"):
            line = raw.strip()
            kind = "blank" if not line else "comment" if line.startswith(("//", "#")) else "code"
            if kind == "blank" or (kind == "comment" and last == "code"):
                block += 1
            last = kind
            if kind == "code" and declaration(line):
                for name in re.findall(r"\b([A-Z][A-Z0-9_]+)\s*(?:=|\{|\[)", line) + re.findall(r"\b([A-Z][A-Z0-9_]+)\s*,", line.split("=")[0]):
                    if name in names and name not in block_of:
                        block_of[name] = (path, block)
                        line_of[name] = (path, raw)
    return block_of


def write(rules, block_of, target):
    base = lambda key: key.split(".")[0]
    groups = []
    for rule in rules:
        if rule[1] not in groups:
            groups.append(rule[1])
    numbers, lines = {}, ["# Rules", "", INTRO]
    # First pass: number everything, so connections can point at their numbers.
    layout = []
    for g, group in enumerate(groups, 1):
        members = [rule for rule in rules if rule[1] == group]
        seen, items = set(), []
        for rule in members:
            if rule[0] in seen:
                continue
            block = block_of.get(base(rule[0]))
            table = lambda key: ".".join(key.split(".")[:2]) if key.count(".") >= 2 else None  # a table's row, as one experience
            kin = [other for other in members if other[0] != rule[0] and other[0] not in seen and
                   (table(other[0]) == table(rule[0]) if table(rule[0]) or table(other[0]) else
                    base(other[0]) == base(rule[0]) or (block is not None and block_of.get(base(other[0])) == block))]
            seen.update([rule[0]] + [other[0] for other in kin])
            items.append((rule, kin))
        for i, (rule, kin) in enumerate(items, 1):
            numbers[rule[0]] = f"{g}.{i}"
            for k, other in enumerate(kin, 1):
                numbers[other[0]] = f"{g}.{i}.{k}"
        layout.append((g, group, items))
    total = len(rules)
    lines.append(f"There are {total} rules in {len(groups)} sections. Each gives its default, the range it may be set within and the step it moves by. "
                 "Rules declared together in the code, because they shape one behaviour, are listed as numbered subsections of the first of them; "
                 "where such a rule sits in another section, the connection is noted beneath it.\n")
    lines.append("## Contents\n")
    for g, group, items in layout:
        lines.append(f"{g}. [{group}](#{g}-{group.lower()}) ({sum(1 + len(kin) for _, kin in items)} rules)")
    lines.append("")

    def entry(level, rule):
        key, _, default, low, high, step, label, hint = rule
        text = [f"{'#' * level} {numbers[key]} {label} (`{key}`)", "", hint[0].upper() + hint[1:] + ("" if hint.endswith(".") else "."), "",
                f"Default **{default}**; from {low} to {high}, in steps of {step}."]
        block = block_of.get(base(key))
        here = line_of.get(base(key))
        elsewhere = [other for other in rules if other[1] != rule[1] and here is not None and line_of.get(base(other[0])) == here]
        if elsewhere:
            text += ["", "Connected: " + "; ".join(f"{numbers[o[0]]} {o[6]} (`{o[0]}`)" for o in elsewhere) + "."]
        return text + [""]

    for g, group, items in layout:
        lines += [f"## {g}. {group}", ""]
        for rule, kin in items:
            lines += entry(3, rule)
            for other in kin:
                lines += entry(4, other)
    text = "\n".join(lines).rstrip() + "\n"
    open(target, "w", encoding="utf-8", newline="\n").write(text)
    print(target, total, "rules")


py = py_rules()
py_sources = sorted(glob.glob(os.path.join(ROOT, "bitzkey", "tools", "*.py")))
py_blocks = clusters(py_sources, {rule[0].split(".")[0] for rule in py}, lambda line: re.match(r"[A-Z][A-Z0-9_]*(\s*,\s*[A-Z][A-Z0-9_]*)*\s*(:[^=]*)?=", line) is not None)
print("Python constants not found:", sorted({rule[0].split(".")[0] for rule in py} - set(py_blocks)))
write(py, py_blocks, os.path.join(ROOT, "bitzkey", "tools", "README.md"))
words = {rule[0]: (rule[6], rule[7]) for rule in py}  # labels may hold ': ', which the C++ listing can't split
cpp = [rule[:6] + words[rule[0]] for rule in cpp_rules()]
for repo in ("bitzkey-cpp", "bitzkey_daemon"):
    write(cpp, py_blocks, os.path.join(ROOT, repo, "src", "core", "README.md"))  # the Python mirror's grouping, which finds every rule
