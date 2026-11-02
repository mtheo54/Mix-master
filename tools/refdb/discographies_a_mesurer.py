#!/usr/bin/env python3
# Abrasion Labs // OMNIS — data/discographies_a_mesurer.csv : pour chaque artiste de
# data/sons_a_mesurer.csv, ses sons populaires d'abord puis le reste, jusqu'à la moitié
# de ses titres (50 au plus), 3000 titres en tout (OMNIS_MAX_TITRES).
# Version triée : 10 sons par artiste (tes titres, puis ses plus populaires), 15 pour ta
# scène, les artistes les plus présents dans tes playlists d'abord. Source : Deezer /artist/{id}/top?limit=100 (top100.json,
# rempli par l'outil de collecte), plus la discographie de la scène. Noms seulement.
#   python3 tools/refdb/discographies_a_mesurer.py chemin/vers/top100.json
import csv,json,math,re,sys,unicodedata,os,collections
ROOT=os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','..')+os.sep
CAP=50
MAX=int(os.environ.get("OMNIS_MAX_TITRES","3000"))  # titres au plus
def red(s):
    s=unicodedata.normalize('NFKD',s).encode('ascii','ignore').decode().lower().replace('$','s'); return re.sub(r'[^a-z0-9]','',s)
def bare(t): return red(re.sub(r'\s*[\(\[](feat|ft|with|prod)[^\)\]]*[\)\]]','',t,flags=re.I))
VARIANT=re.compile(r'\b(live|remaster(ed)?|instrumental|acapella|a cappella|karaoke|slowed|sped up|speed up|reverb|edit|version|remix|mix|demo|interlude|skit|intro|outro)\b',re.I)
top=json.load(open(sys.argv[1],encoding='utf-8'))
prev=list(csv.DictReader(open(ROOT+'data/sons_a_mesurer.csv',encoding='utf-8-sig')))
scene=list(csv.DictReader(open(ROOT+'data/discographies/scene/titres.tsv',encoding='utf-8'),delimiter='\t'))
SCENE={'gry','omyll','euhz','lafeve'}
rows=[]; seen=set()
def add(artist,title,album,fam,dur,isrc,link,why,rank):
    k=(red(artist),bare(title))
    if not k[1] or k in seen: return False
    seen.add(k)
    rows.append({'Track Name':title,'Artist Name(s)':artist,'Album Name':album,'ISRC':isrc,'Genre_Classement':fam,
                 'Rang':rank,'Pourquoi':why,'Durée (s)':dur,'Lien Deezer':link})
    return True
fam_of={red(r['Artist Name(s)']):r['Genre_Classement'] for r in prev}
PER=int(os.environ.get("OMNIS_PAR_ARTISTE","10"))      # sons par artiste
PER_SCENE=int(os.environ.get("OMNIS_PAR_SCENE","15"))  # ta scène : un peu plus, pas toute la discographie
# Les titres de tes playlists, par artiste (ceux que tu écoutes passent en premier).
mine=collections.defaultdict(list)
for r in prev: mine[red(r['Artist Name(s)'])].append(r)
# Ordre des artistes : ta scène, puis les plus présents dans tes playlists.
artists=sorted(mine, key=lambda k: (k not in SCENE, -sum(1 for r in mine[k] if r['Pourquoi'] != 'ta scène : titre demandé'), k))
for k in artists:
    quota=PER_SCENE if k in SCENE else PER
    if len(rows)+min(quota,3) > MAX: break
    n=0
    if k in SCENE:
        # Titres demandés d'abord, puis les plus récents.
        for r in mine[k]:
            if n<quota and r['Pourquoi']=='ta scène : titre demandé' and add(r['Artist Name(s)'],r['Track Name'],r['Album Name'],r['Genre_Classement'],r['Durée (s)'],r['ISRC'],r['Lien Deezer'],'ta scène : titre demandé',n+1): n+=1
        own=[x for x in scene if red(x['artiste'])==k and k in red(x['interprète']) and not VARIANT.search(x['titre'])]
        own.sort(key=lambda x:x['date'],reverse=True)
        for x in own:
            if n>=quota or len(rows)>=MAX: break
            if add(x['artiste'],x['titre'],x['sortie'],fam_of[k],x['durée_s'],x['isrc'],x['lien'],'ta scène : récent',n+1): n+=1
        continue
    for r in mine[k]:
        if n>=quota or len(rows)>=MAX: break
        if add(r['Artist Name(s)'],r['Track Name'],r['Album Name'],r['Genre_Classement'],r['Durée (s)'],r['ISRC'],r['Lien Deezer'],'tes playlists',n+1): n+=1
    e=top.get(k)
    if not e: continue
    for t in e['tracks']:
        if n>=quota or len(rows)>=MAX: break
        if k not in red(t['a']) or VARIANT.search(t['t']): continue
        if add(t['a'] if red(t['a'])==k else e['name'],t['t'],t['al'],fam_of[k],t['d'],t.get('i',''),t['l'],'populaire',n+1): n+=1
out=ROOT+'data/discographies_a_mesurer.csv'
with open(out,'w',newline='',encoding='utf-8-sig') as fh:
    w=csv.DictWriter(fh,fieldnames=list(rows[0].keys()),quoting=csv.QUOTE_ALL); w.writeheader(); w.writerows(rows)
secs=sum(int(r['Durée (s)']) if str(r['Durée (s)']).isdigit() else 185 for r in rows)
print(len(rows),'titres ;',len({red(r['Artist Name(s)']) for r in rows}),'artistes ; durée %.0f h'%(secs/3600))
print('taille : format YouTube d\'origine (~160 kb/s) %.0f Go ; FLAC 16 bits (~1000 kb/s) %.0f Go ; à 50 Mo/titre %.0f Go'%(secs*160/8/1e6,secs*1000/8/1e6,len(rows)*0.05))
print(collections.Counter(r['Genre_Classement'] for r in rows).most_common())
