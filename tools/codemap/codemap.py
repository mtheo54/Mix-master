#!/usr/bin/env python3
# Abrasion Labs // OMNIS — docs/CODEMAP.md : la carte du code (fichier -> rôle,
# classes, fonctions publiques), pour qu'un agent trouve où agir sans relire
# les sources. Analyse statique locale (expressions régulières), sans dépendance.
#   python3 tools/codemap/codemap.py      (refait docs/CODEMAP.md)
import os, re

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
DIRS = ['source', 'tools', 'tests', 'cmake']
SKIP = {'third_party', 'build'}

def role(lines):
    """La description du bandeau d'en-tête (« //  chemin — rôle », suite comprise)."""
    out = []
    for i, l in enumerate(lines[:14]):
        m = re.match(r'\s*(?://|#)\s+(\S+\.(?:h|cpp|py|cmake))\s+[—-]\s+(.*)', l)
        if m:
            out.append(m.group(2).strip())
            for nxt in lines[i + 1:i + 3]:
                t = re.sub(r'^\s*(?://|#)\s?', '', nxt).strip()
                if not t or t.startswith('=') or not re.match(r'\s*(?://|#)', nxt): break
                out.append(t)
            break
    if not out:  # sinon : la 1re phrase de commentaire utile
        for l in lines[:12]:
            t = re.sub(r'^\s*(?://|#!?)\s?', '', l).strip()
            if t and not t.startswith(('=', '/usr', 'Abrasion Labs')) and re.match(r'\s*(?://|#)', l):
                out.append(t); break
    s = ' '.join(out)
    return (s[:220] + '…') if len(s) > 220 else s

TYPE = re.compile(r'^\s*(?:class|struct)\s+(\w+)(?:\s+final)?\s*(?::\s*public\s+([\w:]+))?\s*\{', re.M)
ENUM = re.compile(r'^\s*enum class\s+(\w+)', re.M)
FREE = re.compile(r'^(?:inline\s+|static\s+|constexpr\s+|\[\[nodiscard\]\]\s+)*[\w:<>,\s\*&]+?\s+(\w+)\s*\([^;{]*\)\s*(?:noexcept\s*)?(?:->\s*[\w:<>]+\s*)?[;{]', re.M)
METHOD_DEF = re.compile(r'^[\w:<>,\s\*&]*?\b(\w+)::(\w+)\s*\(', re.M)
TEST = re.compile(r'OMNIS_TEST\("([^"]+)"\)')

def summarize(path, text):
    lines = text.split('\n')
    parts = []
    if path.endswith('.h'):
        types = [m.group(1) + (f' : {m.group(2)}' if m.group(2) else '') for m in TYPE.finditer(text)]
        enums = ENUM.findall(text)
        frees = [n for n in FREE.findall(text) if n not in ('if', 'for', 'while', 'switch', 'return', 'sizeof') and not n[0].isupper() or n in ()]
        if types: parts.append('types : ' + ', '.join(dict.fromkeys(types)))
        if enums: parts.append('enums : ' + ', '.join(dict.fromkeys(enums)))
        fr = [f for f in dict.fromkeys(frees)][:14]
        if fr: parts.append('fonctions : ' + ', '.join(fr))
    elif path.endswith('.cpp') and '/tests/' in '/' + path:
        t = TEST.findall(text)
        if t: parts.append(f'{len(t)} tests : ' + ' ; '.join(x[:70] for x in t[:6]) + (' ; …' if len(t) > 6 else ''))
    elif path.endswith('.cpp'):
        cmds = re.findall(r'command == "(\w+)"', text)
        if cmds: parts.append('commandes : ' + ', '.join(dict.fromkeys(cmds)))
    return role(lines), parts, len(lines)

def main():
    files = []
    for d in DIRS:
        for dirpath, dirnames, names in os.walk(os.path.join(ROOT, d)):
            dirnames[:] = sorted(x for x in dirnames if x not in SKIP)
            for f in sorted(names):
                if f.endswith(('.h', '.cpp', '.py', '.cmake')):
                    files.append(os.path.relpath(os.path.join(dirpath, f), ROOT).replace(os.sep, '/'))
    # Un module = X.h + X.cpp sur une seule ligne (le rôle vient du .h).
    modules = {}
    for rel in files:
        stem = rel.rsplit('.', 1)[0]
        text = open(os.path.join(ROOT, rel), encoding='utf-8', errors='replace').read()
        desc, parts, n = summarize(rel, text)
        m = modules.setdefault(stem, {'ext': [], 'desc': '', 'parts': [], 'n': 0})
        m['ext'].append(rel.rsplit('.', 1)[1])
        m['n'] += n
        own = re.sub(r'\W', '', desc.lower()) == re.sub(r'\W', '', rel.split('/', 1)[-1].lower())
        if desc and not own and (not m['desc'] or rel.endswith('.h')): m['desc'] = desc
        m['parts'] += parts
    out = ['# OMNIS — carte du code', '',
           '> Générée par `python3 tools/codemap/codemap.py` (ne pas éditer à la main). '
           'Une ligne par module (X.h + X.cpp) : rôle, types, fonctions, tests. '
           'Chercher ici (grep) avant d\'ouvrir des sources, puis n\'ouvrir que les fichiers utiles.', '']
    current = None
    for stem in sorted(modules):
        m = modules[stem]
        folder, name = stem.rsplit('/', 1)
        if folder != current:
            out += ['', f'## {folder}/', '']
            current = folder
        line = f'- `{name}.{"/".join(sorted(m["ext"], reverse=True))}` ({m["n"]} l.) — {m["desc"] or "?"}'
        if m['parts']: line += ' · ' + ' · '.join(m['parts'])
        out.append(line)
    open(os.path.join(ROOT, 'docs', 'CODEMAP.md'), 'w', encoding='utf-8').write('\n'.join(out) + '\n')
    print(f'docs/CODEMAP.md : {len(files)} fichiers, {len(modules)} modules')

if __name__ == '__main__':
    main()
