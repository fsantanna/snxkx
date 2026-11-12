#!/usr/bin/env python3
# Counts the lines of code (no blanks, no comments) and the tokens of
# source files: Lua (`--`), Atmos (`;;`), C (`//`, `/* */`).
#   tools/count.py file...
import re, sys

TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|`[^`]*`|[A-Za-z_][A-Za-z0-9_]*|\d+(?:\.\d+)?|\S')

def strip (src, kind):
    if kind == "lua":
        src = re.sub(r'--\[(=*)\[.*?\]\1\]', '', src, flags=re.S)
        src = re.sub(r'--[^\n]*', '', src)
    elif kind == "atm":
        src = re.sub(r';;[^\n]*', '', src)
    else:
        src = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
        src = re.sub(r'//[^\n]*', '', src)
    return src

tl = tt = 0
for f in sys.argv[1:]:
    kind = "lua" if f.endswith(".lua") else "atm" if f.endswith(".atm") else "c"
    src = strip(open(f, encoding="utf-8", errors="replace").read(), kind)
    lines = sum(1 for l in src.split("\n") if l.strip())
    toks = len(TOKEN.findall(src))
    tl += lines; tt += toks
    print("%6d %7d  %s" % (lines, toks, f))
print("%6d %7d  total" % (tl, tt))
