#!/usr/bin/env python3
# Abrasion Labs // OMNIS — data/sons_a_mesurer.csv : les titres à mesurer en priorité
# (la scène demandée, puis un titre par artiste non mesuré dans chaque famille des
# playlists). Noms, titres, ISRC et liens seulement : aucun son.
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
rows=[]; seen=set()
def add(artist,title,fam,prio,why):
    k=(red(artist),bare(title))
    if k in seen or k in measured: return False
    seen.add(k); i=info.get(k,{})
    rows.append({'Track Name':title,'Artist Name(s)':artist,'Album Name':i.get('sortie',''),'ISRC':i.get('isrc',''),
                 'Genre_Classement':fam,'Priorité':prio,'Pourquoi':why,'Lien Deezer':i.get('lien','')})
    return True
# 1. La scène (artistes demandés) : les titres demandés, puis les plus récents.
scene=list(csv.DictReader(open(ROOT+'data/discographies/scene/titres.tsv',encoding='utf-8'),delimiter='\t'))
asked={'GRY':['BRR',"JSP, C'EST QUOI L'AMOUR",'CABARET','PHARES ÉTEINTS'],'Omyll':["C'EST PAS ÇA",'MALADEUX','JSP'],'euhZ':[],'La Fève':[]}
famFR='Rap Français & New Wave'
for a,titles in asked.items():
    for t in titles: add(a,t,famFR,1,'ta scène : titre demandé')
    own=[r for r in scene if red(r['artiste'])==red(a) and red(a) in red(r['interprète'])]
    own.sort(key=lambda r:r['date'],reverse=True)
    n=sum(1 for r in rows if red(r['Artist Name(s)'])==red(a))
    for r in own:
        if n>=8: break
        if add(a,r['titre'],famFR,1,'ta scène : pour bien viser ce son'): n+=1
# 2. Chaque famille de tes playlists : un titre par artiste, les artistes les plus présents d'abord.
quota={'Rap Français & New Wave':30,'Rap & Hip-Hop International (US/UK)':30,'Électro, House & Dance':14,'Rock & Metal':10,
       'Hyperpop & Expérimental':12,'Pop, R&B & Indie':12,'B.O., Anime & Jeux Vidéo':6}
byArtist=collections.defaultdict(list)
for e in cat:
    if not e.get('titre') or not e.get('artiste'): continue
    f=family(e.get('genres',''))
    if f: byArtist[(f,e['artiste'])].append(e['titre'])
for f,q in quota.items():
    cands=[(a,ts) for (ff,a),ts in byArtist.items() if ff==f]
    cands.sort(key=lambda x:(measuredArtists[red(x[0])]>0, -len(x[1]), red(x[0])))
    n=0
    for a,ts in cands:
        if n>=q: break
        if add(a,ts[0],f,2 if measuredArtists[red(a)]==0 else 3,'style « %s » : artiste pas encore mesuré'%f if measuredArtists[red(a)]==0 else 'style « %s »'%f): n+=1
out=ROOT+'data/sons_a_mesurer.csv'
with open(out,'w',newline='',encoding='utf-8-sig') as fh:
    w=csv.DictWriter(fh,fieldnames=list(rows[0].keys()),quoting=csv.QUOTE_ALL); w.writeheader(); w.writerows(rows)
c=collections.Counter(r['Genre_Classement'] for r in rows)
print(len(rows),'titres ; ISRC', sum(1 for r in rows if r['ISRC']), dict(c))
