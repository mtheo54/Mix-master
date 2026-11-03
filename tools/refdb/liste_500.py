#!/usr/bin/env python3
# Abrasion Labs // OMNIS — liste de 500 titres pour une playlist Spotify (écoute par
# l'entrée auxiliaire, aucun fichier) : les 190 premiers titres déjà faits
# (data/spotifetch.csv), toute la discographie de GRY et Omyll, puis le son le plus
# populaire de chacun des autres artistes, les plus suivis sur Deezer d'abord.
#   python3 tools/refdb/liste_500.py chemin/vers/top100.json  [déjà_faits=190] [total=500]
import csv,json,re,sys,os,unicodedata
ROOT=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..')+os.sep
DONE=int(sys.argv[2]) if len(sys.argv)>2 else 190
TOTAL=int(sys.argv[3]) if len(sys.argv)>3 else 500
def red(s):
    s=unicodedata.normalize('NFKD',s).encode('ascii','ignore').decode().lower().replace('$','s'); return re.sub(r'[^a-z0-9]','',s)
def bare(t): return red(re.sub(r'\s*[\(\[](feat|ft|with|prod)[^\)\]]*[\)\]]','',t,flags=re.I))
VARIANT=re.compile(r'\b(live|remaster(ed)?|instrumental|acapella|karaoke|slowed|sped up|speed up|reverb|edit|version|remix|mix|demo|interlude|skit|intro|outro)\b',re.I)
top=json.load(open(sys.argv[1],encoding='utf-8'))
spot=list(csv.DictReader(open(ROOT+'data/spotifetch.csv',encoding='utf-8')))
full=list(csv.DictReader(open(ROOT+'data/discographies_a_mesurer.csv',encoding='utf-8-sig')))
fans={}
for p in [ROOT+'data/discographies/scene/artistes.tsv',ROOT+'data/discographies/playlist/artistes.tsv']:
    for r in csv.DictReader(open(p,encoding='utf-8'),delimiter='\t'):
        if r.get('fans','').isdigit(): fans[red(r['artiste'])]=max(fans.get(red(r['artiste']),0),int(r['fans']))
rows=[]; seen=set()
def add(name,artist,why):
    k=(bare(name),red(artist.split(',')[0]))
    if k in seen or len(rows)>=TOTAL: return
    seen.add(k); rows.append({'name':name,'artist':artist,'why':why})
def scene(r): return any(red(x) in ('gry','omyll') for x in r['artist'].split(','))
for r in spot[:DONE]:
    if not scene(r): add(r['name'],r['artist'],'déjà fait (1-%d)'%DONE)
done=len(rows)
# GRY et Omyll : toute leur discographie (crédits exacts), refaite en tête de la
# nouvelle playlist même si elle était dans les premiers (téléchargements ratés).
for r in spot:
    if scene(r): add(r['name'],r['artist'],'GRY / Omyll')
# Puis le son le plus populaire de chaque artiste pas encore là, les plus suivis d'abord.
have={red(r['artist'].split(',')[0]) for r in rows}
artists=[]
for r in full:
    a=r['Artist Name(s)']
    if red(a) not in have and a not in artists: artists.append(a)
artists.sort(key=lambda a:-fans.get(red(a),0))
for a in artists:
    e=top.get(red(a))
    if not e: continue
    for t in e['tracks']:
        if red(a) in red(t['a']) and not VARIANT.search(t['t']):
            add(t['t'],e['name'],'son le plus populaire'); break
with open(ROOT+'data/spotify_500.csv','w',newline='',encoding='utf-8') as f:
    w=csv.writer(f); w.writerow(['name','artist'])
    for r in rows: w.writerow([r['name'],r['artist']])
# Les nouveaux seulement (après les DONE déjà faits), pour la playlist Spotify.
with open(ROOT+'data/spotify_nouveaux.csv','w',newline='',encoding='utf-8') as f:
    w=csv.writer(f); w.writerow(['name','artist'])
    for r in rows[done:]: w.writerow([r['name'],r['artist']])
print(len(rows),'titres ;',len(rows)-done,'nouveaux ;',sum(1 for r in rows if r['why']=='GRY / Omyll'),'GRY / Omyll ;',len({red(r['artist'].split(",")[0]) for r in rows}),'artistes')
