#!/usr/bin/env python3
# =============================================================================
#  Abrasion Labs // OMNIS
#  tools/carnet/build_carnet.py — Le carnet OMNIS pour la page du plug-in.
#
#  Une seule page HTML, autonome (aucune police, aucun script, aucune requête
#  vers l'extérieur : sa règle de sécurité l'interdit), construite depuis ce que
#  le plug-in livre déjà :
#    - data/styles_artistes.txt : familles, sous-genres, artistes ;
#    - tools/carnet/musicbrainz.json : identifiants et albums MusicBrainz (données
#      CC0, sans genres votés) ;
#    - tools/carnet/descriptions.json : repères par style ;
#    - tools/carnet/exemple_usages.json : l'exemple de la vue IA (produit par le
#      vrai juge d'OMNIS, marqué « exemple » dans la page).
#  Aucun titre mesuré, aucune donnée d'une personne : chacun charge les siens
#  dans son navigateur (onglets « IA · usages » et « Mes données »).
#
#  python3 tools/carnet/build_carnet.py            -> site/carnet/index.html
#  python3 tools/carnet/build_carnet.py --check    -> échoue si la page n'est pas à jour
# =============================================================================
import collections
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(REPO, "site", "carnet", "index.html")


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def build():
    mb = json.loads(read(os.path.join(HERE, "musicbrainz.json")))
    desc = json.loads(read(os.path.join(HERE, "descriptions.json")))
    example = json.loads(read(os.path.join(HERE, "exemple_usages.json")))
    fams, entries, fam = [], [], None
    for line in read(os.path.join(REPO, "data", "styles_artistes.txt")).split("\n"):
        l = line.strip()
        m = re.match(r"#\s*---\s*(.+?)\s*-{3,}", l)
        if m:
            fam = m.group(1)
            fams.append(fam)
            continue
        if not l or l.startswith("#") or ";" not in l or fam is None:
            continue
        name, styles = [x.strip() for x in l.split(";", 1)]
        rec = mb.get(name, {})
        e = {"n": name, "f": fam, "s": [s.strip() for s in styles.split(",") if s.strip()], "t": [],
             "m": rec.get("m"), "ok": bool(rec.get("ok")), "al": rec.get("al", [])}
        if rec.get("h"):
            e["h"] = True
        entries.append(e)
    by = collections.OrderedDict((f, collections.Counter()) for f in fams)
    for e in entries:
        for s in e["s"]:
            by[e["f"]][s] += 1
    data = {"fams": [f for f in fams if by[f]], "styles": {f: [s for s, _ in c.most_common()] for f, c in by.items() if c},
            "desc": desc, "a": entries}
    page = read(os.path.join(HERE, "template.html"))
    for key, value in (("/*DATA*/null", data), ("/*EXAMPLE*/null", example)):
        assert page.count(key) == 1, key
        # « </ » échappé : aucun texte de la base ne peut fermer la balise <script>.
        page = page.replace(key, json.dumps(value, ensure_ascii=False, separators=(",", ":")).replace("</", "<\\/"))
    return page, len(entries)


def main():
    page, n = build()
    if "--check" in sys.argv:
        if not os.path.exists(OUT) or read(OUT) != page:
            print("site/carnet/index.html n'est pas à jour : python3 tools/carnet/build_carnet.py")
            return 1
        print("carnet à jour (%d artistes)" % n)
        return 0
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(page)
    print("site/carnet/index.html : %d artistes, %d Ko" % (n, len(page.encode("utf-8")) // 1024))
    return 0


if __name__ == "__main__":
    sys.exit(main())
