# Generates src/sounds.atm from the sounds that init() of the original
# creates (ext/SNKRX/main.lua).
import re
src = open('ext/SNKRX/main.lua').read()
out = [';; The sounds of the original, as `init()` of `main.lua` creates them.',
       ';; (Generated from the original by tools/sounds.py.)', '']
seen = set()
for m in re.finditer(r"^\s*([a-z_0-9]+) = Sound\('([^']+)', (s|\{tags = \{music\}\})\)", src, re.M):
    name, f, tag = m.groups()
    if name in seen:
        continue
    seen.add(name)
    kind = ':music' if 'music' in tag else ':sfx'
    out.append(f'val {name} = sound("{f}", {kind})')
open('src/sounds.atm', 'w').write('\n'.join(out) + '\n')
