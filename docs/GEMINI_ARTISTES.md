# Lier les artistes : le texte à envoyer à Gemini

## À quoi ça sert

Quand tu demandes « comme Yeat » et qu'OMNIS n'a encore écouté aucun son de Yeat, il ne devine
pas. Il cherche les **artistes proches de Yeat qu'il a déjà écoutés** et vise leur son. S'il n'en
trouve aucun, il prend le profil du genre ; si le genre est inconnu, il te pose la question.

Pour ça, il faut une fiche par artiste : sa famille, ses styles, ses artistes proches et ses
traits sonores. C'est ce que Gemini va faire.

## Comment faire

1. Ouvre Gemini en **recherche approfondie** (Deep Research).
2. Colle le texte ci-dessous.
3. Sous « Artistes : », colle **un lot** de `docs/gemini_lots_artistes.txt`. Il y a 13 lots de
   100 artistes, les plus présents dans tes playlists d'abord.
4. Renvoie-moi la réponse telle quelle. Je la range dans `data/artistes_proches.txt` et je
   l'importe avec `omnis_refdb artistes data/artistes_proches.txt --db data/catalogue.txt`.

Tu peux t'arrêter après 2 ou 3 lots : ce sont les artistes que tu cites le plus.

## Ce que je vérifie à l'import

- Un artiste marqué `inconnu` est mis de côté et listé, ce qui repère les noms inventés.
- Quand la famille ne correspond pas à tes playlists, je te le signale. **Ton classement fait
  foi.**
- Un artiste proche ne sert qu'à partir du moment où OMNIS a écouté un de ses sons. Une erreur de
  Gemini ne change donc jamais le son toute seule.

---

## Le texte à copier

```text
Tu es documentaliste musical. Pour chaque artiste de la liste en bas, fais une recherche
approfondie (biographie, articles, presse spécialisée, Genius, Wikipédia, RYM, interviews,
artistes avec qui il collabore) et rends UNE ligne exactement dans ce format :

Artiste ; famille ; styles ; proches : A | B | C ; traits : trait, trait, trait

RÈGLES STRICTES
1. Uniquement des informations vérifiées. N'invente RIEN : ni artiste, ni style, ni proche.
   Si tu ne trouves pas l'artiste avec certitude (homonymes, trop peu d'infos), écris
   seulement :  Artiste ; inconnu
2. Recopie le nom de l'artiste EXACTEMENT comme dans la liste (majuscules, $, chiffres).
3. famille = UN seul mot parmi : rap us | rap fr | electro | rock | hyperpop | pop | bo | autre
   (rap us = rap anglophone, US et UK ; rap fr = rap francophone, France, Belgique, Suisse,
   Québec ; bo = musiques de films, anime, jeux vidéo).
4. styles = 1 à 3 mots courts, en minuscules, séparés par des virgules. Utilise de préférence :
   trap, rage, drill, plugg, pluggnb, cloud, jerk, boom bap, new wave, melodique, emo rap,
   punk trap, phonk, hyperpop, digicore, experimental, house, techno, uk garage, drum and bass,
   dubstep, afro, rnb, soul, jazz rap, lofi, indie, metal, punk, shoegaze, synthpop, ambient.
5. proches = 3 à 6 artistes RÉELS dont le SON est le plus proche (production, voix, énergie),
   du plus proche au moins proche, séparés par « | ». Pas seulement des gens du même pays :
   ceux dont la musique ressemble. Prends d'abord des artistes de la liste quand c'est juste.
6. traits = 2 à 5 traits sonores courts qui décrivent comment sa musique SONNE, par exemple :
   808 saturée, 808 propre, sub énorme, kick qui claque, hi-hats rapides, synthés stridents,
   synthés aériens, guitare, piano, autotune fort, voix en avant, voix chuchotée, voix criée,
   flow rapide, très fort (loud), dynamique, stéréo large, sombre, brillant, lo-fi,
   distorsion volontaire, ambiance planante.
7. Pas de tableau, pas de puces, pas de gras, pas de commentaire, pas d'introduction :
   seulement les lignes, une par artiste, dans l'ordre de la liste.

EXEMPLES
Yeat ; rap us ; rage, trap ; proches : Ken Carson | Destroy Lonely | Homixide Gang | Playboi Carti ; traits : 808 saturée, cloches, autotune fort, synthés stridents
La Fève ; rap fr ; new wave, cloud ; proches : Rounhaa | Khali | Zamdane ; traits : voix posée, prods aériennes, 808 propre
Zorglub Gang ; inconnu

Artistes :
```

---

## Le format, pour le code

`Artiste ; famille ; styles ; proches : A | B | C ; traits : …`

Le lecteur (`ai::parseArtistLink`) tolère :
- les puces, les numéros (« 12. ») et le gras ;
- les champs nommés dans le désordre (« Famille : », « Traits sonores : ») ;
- des proches séparés par des virgules quand il n'y a pas de « | ».

Dans la base, la fiche devient une entrée d'artiste sans titre (`proches = A | B`,
`traits = …`).

Ordre de repli dans `understandRequest` :
1. artiste ou titre écouté ;
2. **proches écoutés** : ceux qu'il cite, puis ceux qui le citent, 3 artistes au plus ;
3. profil moyen de ses genres ;
4. question.
