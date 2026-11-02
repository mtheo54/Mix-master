#!/usr/bin/env python3
# Abrasion Labs // OMNIS — data/discographies_a_mesurer.csv : pour chaque artiste de
# data/sons_a_mesurer.csv, ses sons populaires d'abord puis le reste, jusqu'à la moitié
# de ses titres (50 au plus), 3000 titres en tout (OMNIS_MAX_TITRES). Source : Deezer /artist/{id}/top?limit=100 (top100.json,
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
order=[]
for r in prev:
    k=red(r['Artist Name(s)'])
    if k not in order: order.append(k)
# 1. Ta scène demandée : toute sa discographie (petite), la plus récente d'abord.
for k in order:
    if k not in SCENE: continue
    own=[x for x in scene if red(x['artiste'])==k and k in red(x['interprète']) and not VARIANT.search(x['titre'])]
    own.sort(key=lambda x:x['date'],reverse=True)
    for n,x in enumerate(own,1): add(x['artiste'],x['titre'],x['sortie'],fam_of[k],x['durée_s'],x['isrc'],x['lien'],'ta scène : toute la discographie',n)
# 2. Les titres de tes playlists.
for r in prev:
    add(r['Artist Name(s)'],r['Track Name'],r['Album Name'],r['Genre_Classement'],r['Durée (s)'],r['ISRC'],r['Lien Deezer'],'tes playlists',0)
# 3. Les populaires de chaque artiste, à tour de rôle (n° 1 de chacun, puis n° 2…),
#    jusqu'à la moitié de ses titres (50 au plus), et MAX titres en tout.
lists={}
for k in order:
    e=top.get(k)
    if k in SCENE or not e: continue
    uniq=[]; u=set()
    for t in e['tracks']:
        if k not in red(t['a']) or VARIANT.search(t['t']): continue
        b2=bare(t['t'])
        if b2 and b2 not in u: u.add(b2); uniq.append(t)
    target=CAP if len(e['tracks'])>=100 else min(CAP,math.ceil(len(uniq)/2))
    lists[k]=(e,uniq[:max(target,min(3,len(uniq)))])
for rank in range(CAP):
    for k,(e,ts) in lists.items():
        if len(rows)>=MAX: break
        if rank<len(ts):
            t=ts[rank]
            add(t['a'] if red(t['a'])==k else e['name'],t['t'],t['al'],fam_of[k],t['d'],t.get('i',''),t['l'],'populaire n° %d'%(rank+1),rank+1)
    if len(rows)>=MAX: break
out=ROOT+'data/discographies_a_mesurer.csv'
with open(out,'w',newline='',encoding='utf-8-sig') as fh:
    w=csv.DictWriter(fh,fieldnames=list(rows[0].keys()),quoting=csv.QUOTE_ALL); w.writeheader(); w.writerows(rows)
secs=sum(int(r['Durée (s)']) if str(r['Durée (s)']).isdigit() else 185 for r in rows)
print(len(rows),'titres ;',len({red(r['Artist Name(s)']) for r in rows}),'artistes ; durée %.0f h'%(secs/3600))
print('taille : format YouTube d\'origine (~160 kb/s) %.0f Go ; FLAC 16 bits (~1000 kb/s) %.0f Go ; à 50 Mo/titre %.0f Go'%(secs*160/8/1e6,secs*1000/8/1e6,len(rows)*0.05))
print(collections.Counter(r['Genre_Classement'] for r in rows).most_common())
