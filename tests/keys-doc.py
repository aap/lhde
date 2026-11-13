"""docs/KEYS.md lists luxwm's default keys: every binding of luxwm/wm.c's default_keys table appears in it under its written
name (Mod4+r as Super+R, Mod1+F4 as Alt+F4, Mod4+Shift+Tab as Shift+Tab next to Super+Tab, Super_L start as "Super (pressed
and released alone)"), and every Super+key the document's "free" table names is not bound by default. Prints ok/FAIL lines
and a count; exit status 1 on a failure. Usage: python3 -I tests/keys-doc.py (from the top of the tree)."""
import re
import sys

wm = open("luxwm/wm.c", encoding="utf-8").read()
doc = open("docs/KEYS.md", encoding="utf-8").read()
table = re.search(r"default_keys\[\] = \{(.*?)NULL \};", wm, re.S).group(1)
bindings = [s.split(' ')[0] for s in re.findall(r'"([^"]*)"', table)]
fails = 0
bound = set()


def written(key):
    if key in ("Super_L", "Super_R"):
        return "Super (pressed and released alone)"
    parts = key.split("+")
    names = {"Mod4": "Super", "Mod1": "Alt", "Control": "Ctrl", "space": "Space"}
    out = [names.get(p, p) for p in parts]
    last = out[-1]
    if len(last) == 1:
        out[-1] = last.upper()
    return "+".join(out)


for key in bindings:
    name = written(key)
    bound.add(name)
    shown = name
    if name.endswith("Shift+Tab"):          # "Alt+Tab, Super+Tab; with Shift backwards"
        shown = name.replace("Shift+", "")
        ok = shown in doc and "with Shift backwards" in doc
    elif re.fullmatch(r"Super\+[1-4]", name):
        ok = "Super+1 .. Super+4" in doc
    else:
        ok = name in doc
    print(("ok   " if ok else "FAIL ") + key + " as " + name)
    fails += not ok

free = re.search(r"\| letters \| [^|]* \| ([^|]*) \|", doc)
if not free:
    print("FAIL no letters row in the free Super table"); fails += 1
else:
    for letter in free.group(1).replace("*", "").split():
        if "Super+" + letter in bound:
            print("FAIL Super+" + letter + " is listed free but bound"); fails += 1
    print("ok   the free letters are unbound: " + free.group(1).replace("*", "").strip())

print("%d failed" % fails)
sys.exit(1 if fails else 0)
