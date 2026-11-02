#!/usr/bin/env python3
# Abrasion Labs // OMNIS — data/sons_a_mesurer.csv : les titres à mesurer en priorité
# dans un budget de 28 Go de FLAC (la scène demandée, puis tes playlists et les proches
# de ta scène à tour de rôle). Noms, titres, ISRC et liens seulement : aucun son.
import csv, gzip, re, unicodedata, collections, sys
import os
ROOT=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..') + os.sep
def red(s):
    s=unicodedata.normalize('NFKD',s).encode('ascii','ignore').decode().lower().replace('$','s')
    return re.sub(r'[^a-z0-9]','',s)
def bare(t): return red(re.sub(r'\s*[\(\[](feat|ft|with)[^\)\]]*[\)\]]','',t,flags=re.I))
def parse(p):
    out=[];cur=None
    for line in open(p,encoding='utf-8'):
        line=line.rstrip('\n')
        if line.startswith('['): cur={'measured':True}; out.append(cur)
        elif cur is not None and ' = ' in line:
            k,v=line.split(' = ',1); cur[k.strip()]=v.strip()
        elif cur is not None and line.startswith('# connu de nom'): cur['measured']=False
    return out
refs=parse(ROOT+'data/references.txt'); cat=parse(ROOT+'data/catalogue.txt')
measured={(red(e.get('artiste','')),bare(e.get('titre',''))) for e in refs if e.get('titre')}
measuredArtists=collections.Counter(red(e.get('artiste','')) for e in refs)
# ISRC / album / lien
info={}
def load(rows):
    for r in rows:
        k=(red(r['artiste']),bare(r['titre']))
        if k not in info: info[k]=r
load(csv.DictReader(open(ROOT+'data/discographies/scene/titres.tsv',encoding='utf-8'),delimiter='\t'))
load(csv.DictReader(gzip.open(ROOT+'data/discographies/playlist/titres.tsv.gz','rt',encoding='utf-8'),delimiter='\t'))
fam_words=[]
for line in open(ROOT+'data/genres_classement.txt',encoding='utf-8'):
    if line.startswith('#') or '=>' not in line: continue
    f,w=line.split('=>'); w=[x.strip() for x in w.split(',') if x.strip()]
    if w: fam_words.append((f.strip(),set(w)))
def family(genres):
    gs=set(x.strip() for x in genres.split('|') if x.strip())
    best=None
    for f,w in fam_words:
        n=len(gs&w)
        if n and (best is None or n>best[1]): best=(f,n)
    return best[0] if best else None

# Taille d'un FLAC 16 bits / 44,1 kHz : un master moderne et fort se compresse mal,
# ~1000 kb/s (prudent) -> 125 ko par seconde. Durée inconnue : 185 s (moyenne mesurée).
KBPS = 1000
BUDGET_GO = 28.0
def size_mo(seconds): return seconds * KBPS / 8 / 1000
rows=[]; seen=set(); total=[0.0]
def add(artist,title,fam,prio,why):
    k=(red(artist),bare(title))
    if k in seen or k in measured: return False
    i=info.get(k,{})
    dur=int(i['durée_s']) if i.get('durée_s','').isdigit() else 185
    mo=size_mo(dur)
    if total[0]+mo/1000>BUDGET_GO: return False
    seen.add(k); total[0]+=mo/1000
    rows.append({'Track Name':title,'Artist Name(s)':artist,'Album Name':i.get('sortie',''),'ISRC':i.get('isrc',''),
                 'Genre_Classement':fam,'Priorité':prio,'Pourquoi':why,'Durée (s)':dur,'Taille estimée (Mo)':round(mo,1),
                 'Cumul (Go)':round(total[0],2),'Lien Deezer':i.get('lien','')})
    return True
famFR='Rap Français & New Wave'
scene=list(csv.DictReader(open(ROOT+'data/discographies/scene/titres.tsv',encoding='utf-8'),delimiter='\t'))
sceneArtists=[r for r in csv.DictReader(open(ROOT+'data/discographies/scene/artistes.tsv',encoding='utf-8'),delimiter='\t')]
def own(a):
    o=[r for r in scene if red(r['artiste'])==red(a) and red(a) in red(r['interprète'])]
    o.sort(key=lambda r:r['date'],reverse=True); return o
# 1. Ta scène (demandée) : titres demandés, puis jusqu'à 25 titres chacun (les plus récents).
asked={'GRY':['BRR',"JSP, C'EST QUOI L'AMOUR",'CABARET','PHARES ÉTEINTS'],'Omyll':["C'EST PAS ÇA",'MALADEUX','JSP'],'euhZ':[],'La Fève':[]}
for a,titles in asked.items():
    for t in titles: add(a,t,famFR,1,'ta scène : titre demandé')
    n=sum(1 for r in rows if red(r['Artist Name(s)'])==red(a))
    for r in own(a):
        if n>=25: break
        if add(a,r['titre'],famFR,1,'ta scène : plusieurs titres pour un profil sûr'): n+=1
# 2. Tes playlists (les noms que tu tapes) et les proches de ta scène, à tour de rôle :
#    1er titre de chaque artiste, puis le 2e, etc., les artistes les plus présents d'abord.
byArtist=collections.defaultdict(list)
for e in cat:
    if not e.get('titre') or not e.get('artiste'): continue
    f=family(e.get('genres',''))
    if f: byArtist[(f,e['artiste'])].append(e['titre'])
pools=[]
for (f,a),ts in byArtist.items(): pools.append((f,a,ts,'tes playlists'))
askedKeys={red(a) for a in asked}
for r in sceneArtists:
    a=r['artiste']
    if red(a) in askedKeys: continue
    ts=[x['titre'] for x in own(a)]
    if ts: pools.append((famFR,a,ts,'proche de ta scène'))
pools.sort(key=lambda x:(measuredArtists[red(x[1])]>0,-len(x[2]),red(x[1])))
for rank in range(0,40):
    added=0
    for f,a,ts,src in pools:
        if rank<len(ts):
            why=('%s : 1er titre de cet artiste'%src) if rank==0 else ('%s : titre n° %d de cet artiste'%(src,rank+1))
            added+=add(a,ts[rank],f,2 if rank==0 else 3 if rank<3 else 4,why)
    if not added or total[0]>=BUDGET_GO-0.03: break
out=ROOT+'data/sons_a_mesurer.csv'
with open(out,'w',newline='',encoding='utf-8-sig') as fh:
    w=csv.DictWriter(fh,fieldnames=list(rows[0].keys()),quoting=csv.QUOTE_ALL); w.writeheader(); w.writerows(rows)
c=collections.Counter(r['Genre_Classement'] for r in rows)
print(len(rows),'titres, %.1f Go estimés'%total[0],'; ISRC',sum(1 for r in rows if r['ISRC']),'; artistes',len({red(r['Artist Name(s)']) for r in rows}))
print(dict(c)); print(collections.Counter(r['Priorité'] for r in rows))
# Ordres de grandeur, pour décider.
def tot(rs): return sum(size_mo(int(r['durée_s']) if r['durée_s'].isdigit() else 185) for r in rs)/1000
catT=[e for e in cat if e.get('titre')]
print('Tout ce que tes playlists citent (%d titres) : %.0f Go'%(len(catT),len(catT)*size_mo(185)/1000))
print('Toute la scène (%d titres) : %.0f Go'%(len(scene),tot(scene)))
pl=list(csv.DictReader(gzip.open(ROOT+'data/discographies/playlist/titres.tsv.gz','rt',encoding='utf-8'),delimiter='\t'))
print('Discographies complètes de la playlist (%d titres) : %.0f Go'%(len(pl),tot(pl)))
