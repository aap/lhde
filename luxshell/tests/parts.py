"""Every tile luxshell has (luxshell/tiles/NAME.lua) can be added back from the sidebar menu: runtime.lua's PARTS or TRANSIENT
names it by id (the clock has its own item), FRIENDLY gives it a name and KNOWN_ORDER a place. Prints ok/FAIL lines and a
count; exit status 1 on a failure. Usage: python3 -I luxshell/tests/parts.py (from the top of the tree)."""
import os
import re
import sys

src = open("luxshell/runtime.lua", encoding="utf-8").read()


def block(name):
    m = re.search(r"^local " + name + r" = (\{.*?\n?.*?\})\s*(?:--.*)?$", src, re.S | re.M)
    return m.group(1) if m else ""


parts = set(re.findall(r'id = "(\w+)"', block("PARTS") + block("TRANSIENT")))
friendly = set(re.findall(r'(\w+) = "', re.search(r"^local FRIENDLY = \{(.*?)\}", src, re.S | re.M).group(1)))
order = re.findall(r'"(\w+)"', re.search(r"^local KNOWN_ORDER = \{(.*?)\}", src, re.M).group(1))
fails = 0
for f in sorted(os.listdir("luxshell/tiles")):
    if not f.endswith(".lua"):
        continue
    t = f[:-4]
    for what, ok in (("in the parts list", t in parts or t == "clock"), ("a friendly name", t in friendly), ("a place in KNOWN_ORDER", t in order)):
        print(("ok   " if ok else "FAIL ") + t + ": " + what)
        fails += not ok
print("%d failed" % fails)
sys.exit(1 if fails else 0)
