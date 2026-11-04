# La fiche d'une référence (`data/references.txt`)

Une entrée par titre, en texte : **seulement des nombres, jamais d'audio**. Le plug-in embarque
cette base et s'en sert hors ligne (AUTO, barre IA, « comme <artiste> », menu RÉF de la
console : styles, artistes et titres mesurés, de A à Z). Plus la fiche est
complète, plus OMNIS en tire : chaque champ ci-dessous dit ce qu'il mesure et à quoi il sert.

## Identité

| Clé | Exemple | Sens | Sert à |
|---|---|---|---|
| `artiste`, `titre` | `SLAYR`, `Sloppy Joe` | noms exacts | reconnaître « comme SLAYR », « sloppy joe » (fautes tolérées) |
| `alias` | `slayer \| slayr` | autres écritures, séparées par `\|` | idem |
| `genres` | `rage \| hyperpop` | **ta** classification (elle gagne toujours) | AUTO vers un style, « genre rage » |
| `proches` | `2hollis \| Nettspend` | artistes proches | un artiste inconnu -> ses proches mesurés |
| `traits` | `808 saturée, voix autotune` | ce qui fait le son, en mots | expliquer pourquoi la réf sonne ainsi |
| `image_credit` | `Photo : Auteur, CC BY-SA 4.0` | d'où vient l'avatar (illustration du cache) | carte « Référence visée » ; `image` : ancienne image (avant le cache) |
| `pochette_credit` | `Pochette du fichier (avatar OMNIS)` | d'où vient l'illustration de la pochette | idem ; absente : OMNIS dessine une pochette d'après la fiche |
| `lien` | `https://…` (page de l'artiste ou du titre) | la musique en ligne | carte « Référence visée » : un clic ouvre le navigateur |

## Les images (photo d'artiste, pochette)

La base ne contient **aucune image** : seulement d'où viennent les illustrations (crédits) et le
lien de la musique. Les illustrations (ci-dessous) sont dans le cache d'OMNIS
(`%APPDATA%\Abrasion Labs\OMNIS\cache` sous Windows, `~/Library/Application Support/Abrasion
Labs/OMNIS/cache` sur Mac). JPG ou PNG en entrée.

```
omnis_refdb image photo.png --artiste "2hollis" --credit "Photo : Auteur, CC BY-SA 4.0" --db references_utilisateur.txt
omnis_refdb pochette pochette.png --artiste "2hollis" --titre "poster boy" --db references_utilisateur.txt
```

L'outil en fait l'avatar (cache) ; le fichier d'origine n'est pas copié.

**Style « emblème »** (par défaut) : aucun filtre sur l'image. Elle sert seulement de modèle ;
l'avatar est refait de zéro avec ~120 formes simples (rectangles, carrés, ellipses, ronds,
traits) posées une à une, comme dans un éditeur d'emblèmes de jeu : grands aplats d'abord,
détails ensuite, le centre (le visage) servi en premier. On reconnaît la pose et les couleurs,
mais c'est une interprétation en formes, pas la photo (250 formes au plus). Artiste en **rond**,
pochette en **carré aux coins arrondis**. `--style hd` (photo à peine retouchée) ou
`--style peint` pour les anciens rendus ; `--refaire` redessine ce que le cache a déjà.

**Avatars OMNIS** (`gfx/OmnisAvatarStylizer`) : chaque photo d'artiste et chaque pochette devient
un avatar 256 × 256 que l'on **reconnaît au premier coup d'œil** :

1. cadrage léger : 9 % retirés sur chaque bord, la composition reste ;
2. blocs de texte noir et blanc des coins (macaron « Parental Advisory », mentions) repérés et
   comblés par les pixels voisins (le cadrage seul ne suffit pas : sur FINIS-LES le macaron va
   jusqu'à 12 % du bord) ;
3. bilatéral léger (rayon 2) : le bruit de compression part, les traits restent nets ;
4. 90 % de l'image d'origine gardés, 10 % d'étalonnage pour le thème sombre ;
5. alpha fondu sur les 10 derniers pixels du bord.

Le style « peint » (Kuwahara, cel-shading, encre) reste une option du code.

D'où viennent les images, toujours côté poste de la personne :
- l'image **intégrée à ses FLAC** (bloc `METADATA_BLOCK_PICTURE`) : `omnis_refdb add` et `scan`
  en font l'avatar du titre (pochette) et, s'il y en a une, de l'artiste ;
- une image qu'elle fournit elle-même (`omnis_refdb image` / `pochette`).

**Deezer retiré (04/11/2026)** : les conditions de son API réservent l'accès à un usage non
commercial ; OMNIS étant vendu, les commandes `deezer` et `discographie`, les scripts qui en
dépendaient et les extraits `data/discographies/` ont été supprimés.

Rangement : `<dossier OMNIS>/cache/avatars/<hash>.png` (artistes), `cache/covers/<hash>.png`
(titres), et à côté `<hash>.couleurs` (les deux couleurs dominantes). Rien dans le `.vst3` ni
dans l'installateur : comme la pochette affichée par un lecteur de musique, l'image vient des
fichiers et de la connexion de la personne. Sans avatar, la console dessine une pochette d'après
la courbe de la fiche.

**Mode promo** (`/fbi` dans la barre IA, ou Maj + Alt + 5 clics rapides sur le logo Abrasion
Labs) : plus aucune image, des **monogrammes** à la place (initiales, couronne des 21 bandes de
la fiche, dans les deux couleurs dominantes de la vraie image) ; crédits et liens cachés. Pour
filmer la console (démo, pub, réseaux) sans montrer une seule pochette. Gardé d'une session à
l'autre ; `/fbi` à nouveau pour revenir.

```
omnis_refdb embleme photo.jpg --forme rond --sortie avatar.png   (une image, pour essayer ; --formes 80 : plus abstrait)
omnis_refdb embleme --db references_utilisateur.txt               (anciennes images de images/ -> cache)
```

**Rester réglo :**
- **Photo d'artiste** : seulement une photo sous licence libre (Wikimedia Commons : CC BY,
  CC BY-SA, CC0), avec l'auteur et la licence dans `image_credit`. OMNIS l'affiche toujours
  sous la photo ; l'outil refuse une photo sans crédit. Garder le lien de la page source dans
  un fichier à part (`sources.txt`) pour pouvoir le prouver.
- **Pochette** : elle n'est **pas** sous licence libre, elle appartient au label / à
  l'artiste. Pour ton usage, sur ton PC : oui. **Livrée avec le plug-in vendu : non**, sauf
  accord écrit du label. Sans pochette, OMNIS dessine la sienne à partir de la fiche (teinte
  = tonalité, silhouette = courbe du titre, grain = attaques) : aucune question de droits.

## Le spectre et la sonie (fiche v1)

| Clé | Unité | Sens | Sert à |
|---|---|---|---|
| `lufs` | LUFS | sonie intégrée (cœur du morceau) | volume visé |
| `lra` | LU | plage de sonie | dynamique d'ensemble |
| `tp` | dBTP | crête vraie | plafond, prudence streaming |
| `plr` | dB | crête − sonie | densité (« écrasé » / « aéré ») |
| `sub` | dB | énergie sous 80 Hz | quantité de 808 |
| `mid` | 21 × dB | Mid par demi-octave, 20 Hz – 20 kHz | **courbe du Match EQ** |
| `side` | 21 × dB | Side par demi-octave | largeur par zone (Match EQ Side) |
| `dyn` | 21 × dB | écart P90 − P10 des trames par zone | tenue du multibande |

## La fiche complète (v2, `analysis/MusicDescriptors`)

| Clé | Unité | Comment c'est mesuré | Ce qu'OMNIS en fait |
|---|---|---|---|
| `duree` | s | son analysé | fiabilité |
| `bpm` | BPM | autocorrélation du flux des attaques, 85 – 170 (un trap à 142 n'est pas lu à 71) | **relâchements** du multibande et du limiteur calés sur la croche / double-croche |
| `cle`, `cle_confiance` | `Fm`, 0 – 1 | chroma 120 Hz – 2 kHz contre les 24 profils majeur / mineur ; absente si pas nette | info (« la réf est en Fa mineur, toi en Sol ») |
| `sub_hz` | Hz | pic 30 – 110 Hz le plus fréquent sur les blocs forts : la note de la 808 | info (« 808 réf en Fa, la tienne en Sol ») |
| `lufs_court` | 3 × LUFS | sonie 3 s : calme (P10), médiane, fort (P95) | **écart couplet / refrain** -> upward si ta prod est bien plus contrastée |
| `micro` | 4 × dB | crête 10 ms − niveau 400 ms dans 4 zones (grave, bas-médium, médium, aigu) | **Punch** : la réf attaque plus -> Punch monte ; plus dense -> upward |
| `correlation` | −1 – 1 | corrélation gauche / droite | largeur, risques de phase |
| `correlation_grave` | −1 – 1 | idem sous 120 Hz | **grave mono** si la réf l'est et pas toi |
| `perte_mono` | dB | ce que le son perd sommé en mono | compatibilité mono |
| `coups_par_s` | /s | attaques dans n'importe quelle zone (les hats comptent) | densité rythmique |
| `brillance` | Hz | centre de gravité du spectre au-dessus de 200 Hz | info, « plus brillant » |
| `crete` | dBFS | crête échantillon | idem `tp` |
| `ecretage` | % | part des échantillons à −0,1 dBFS ou plus | une réf **écrêtée exprès** (rage) -> clip assumé |
| `pompe` | dB | enveloppe 10 ms du bas-médium et du médium repliée sur la période du temps (et sa moitié, son double), origine au maximum des attaques : niveau en fin de temps (65 – 95 %) moins début (8 – 30 %). Absente si pas de tempo net | **auto-release** du limiteur baissé (jusqu'à 20 %) et relâchement vers la croche quand la réf pompe (sidechain) : 1,5 dB -> rien, 3,5 dB -> tout |

Repères de `pompe` : prods trap sans sidechain −3,5 à +1,1 dB ; *poster boy* (limiteur qui écrase
chaque coup) +2,8 ; boucle house, nappe en sidechain −6 dB : +2,3 ; −16 dB : +4,5.

Une fiche v1 (sans ces clés) reste lisible : OMNIS se sert simplement de ce qu'il a.

## Compléter les fiches

Seul l'audio permet de mesurer la fiche v2 : elle se fait là où sont les sons (ton PC), jamais
dans le dépôt ni sur le VPS.

- **Dans FL Studio** : la référence jouée sur l'entrée « Reference » d'OMNIS (Listen Reference,
  60 s au moins) est mesurée avec sa fiche complète et entre dans ta base.
- **D'un coup, un dossier** : l'outil `omnis_refdb.exe` (artefact *OMNIS-Windows-outils* de la
  CI). Les fichiers nommés « Artiste - Titre.mp3 / .flac / .wav » ; une entrée déjà connue
  est re-mesurée en gardant ses alias, genres, proches et traits :

  ```
  omnis_refdb scan "D:\Références" --db references.txt
  omnis_refdb fiche "D:\Références\SLAYR - Sloppy Joe.wav"   (voir la fiche sans rien écrire)
  ```

Les fichiers sans perte (WAV, FLAC) sont les plus justes ; un MP3 / YouTube coupe l'aigu
(repéré et écarté : `detectCutoff`).

### Remesurer pour la pompe (04/11/2026)

La clé `pompe` est nouvelle : les fiches mesurées avant ne l'ont pas. OMNIS se sert alors de
l'ancien indice (moins fiable). Pour la remplir, relance le même `scan` sur les mêmes dossiers
avec le nouvel outil (artefact *OMNIS-pour-ONIRIQUE* ou *OMNIS-Windows-outils* de la dernière CI
verte, ou une recompilation de `build-core`). Chaque titre déjà connu est remesuré ; ses genres,
alias, pochettes et liens restent. Sauvegarde la base d'abord :

```
copy C:\OMNIS_mesure\references_utilisateur.txt C:\OMNIS_mesure\references_utilisateur_avant_pompe.txt
C:\Users\psudo\OMNIS\Mix-master\build-core\Release\omnis_refdb.exe scan "D:\Claude" --db C:\OMNIS_mesure\references_utilisateur.txt --attendu C:\Users\psudo\OMNIS\Mix-master\data\attendu_500.csv
```

(Un autre dossier de sons : même commande avec son chemin.) Vérifier ensuite :
`findstr /c:"pompe =" C:\OMNIS_mesure\references_utilisateur.txt | find /c "pompe"` donne le
nombre de fiches qui ont la mesure. Pour voir un titre : `omnis_refdb.exe fiche "fichier"` affiche
sa ligne « pompe ». La base livrée (`data/references.txt`) se remesure de la même façon avec
`--db` vers elle, seulement à partir de fichiers obtenus légalement.

## Profils de genre livrés (04/11/2026)

Le plug-in ne contient plus aucune fiche de titre : seulement `data/profils_genres.txt`,
généré depuis les mesures brutes `data/references.txt` (qui ne sont pas intégrées) :

```
omnis_refdb profils data/references.txt --sortie data/profils_genres.txt --min 3 --rapport docs/classement_titres.csv
```

- **Un profil = une moyenne** d'au moins 3 titres (`--min`), par famille (`genres_classement.txt`)
  et par sous-genre (`styles_artistes.txt`). Tempo, tonalité et note de la 808 sont effacés.
- **Chaque titre va dans UN sous-genre**, celui des sous-genres de son artiste dont le profil
  mesuré est le plus proche (k-moyennes restreintes, 8 passes) : les titres rage de La Fève vont
  en rage fr, ses titres plugg en plugg fr. Le détail titre par titre :
  `docs/classement_titres.csv`. Un sous-genre de moins de 3 titres n'a pas de profil (ses titres
  restent dans la famille).
- Les artistes restent **connus de nom** avec les genres de leurs titres (aucun chiffre) :
  « comme GRY » vise le profil new wave fr (le plus précis ; la famille entière ne le noie pas).
- Titres sans famille (artistes à classer) : hors profils. Les classer dans
  `data/styles_artistes.txt` (la section donne la famille), puis régénérer.
- Les références que le client ajoute (son `references_utilisateur.txt`) gardent leurs noms et
  s'affichent comme avant : ce sont ses fichiers, sur son poste.

## État de la base (30/10)

**04/11/2026 : 426 titres** (base perso de 338 titres fusionnée ; liste : `docs/references_utilisees.csv`). Avant : 91 titres ; **2 avec la fiche complète** (2hollis – poster boy, SLAYR – Sloppy Joe : leur audio
était disponible ici). Les 89 autres attendent d'être re-mesurés depuis l'audio.
Vérifié sur des fichiers dont le tempo est dans le nom : 135, 118 et 142 BPM retrouvés exactement.

## Sons à mesurer en priorité

`data/sons_a_mesurer.csv` (refait par `python3 tools/refdb/sons_a_mesurer.py`) : **1269 titres,
~28 Go en FLAC** (estimation prudente : 1000 kb/s). Dans l'ordre :
ta scène (GRY, Omyll, euhZ, La Fève : titres demandés puis jusqu'à 25 chacun), puis tes
playlists et les proches de ta scène à tour de rôle (le 1er titre de chaque artiste, puis le 2e…).
La colonne « Cumul (Go) » dit où t'arrêter si tu veux moins. Ordres de grandeur : tout ce que
tes playlists citent ≈ 45 Go, toute la scène ≈ 105 Go, les discographies complètes ≈ 4 To.
Colonnes au format des exports de playlists (« Track Name », « Artist Name(s) », « Album Name »,
« ISRC », « Genre_Classement ») + priorité, raison, durée, taille, cumul.

**Plus large** : `data/discographies_a_mesurer.csv` (`python3 tools/refdb/discographies_a_mesurer.py top100.json`) :
**6000 titres au plus** (`OMNIS_MAX_TITRES`) pour les 647 artistes : **10 sons chacun**
(`OMNIS_PAR_ARTISTE` : tes titres d'abord, puis ses plus populaires), **15 pour ta scène**
(`OMNIS_PAR_SCENE` : titres demandés puis les plus récents), puis la place restante en sons
populaires de plus, à tour de rôle. Sans live / remix / instrumental / slowed. ~325 h de son :
~23 Go au format d'origine des plateformes, ~146 Go en FLAC 16 bits. Mesurer par lots puis supprimer.

Une fois les fichiers sur ton PC (achetés ou obtenus légalement, en FLAC ou WAV de préférence),
nommés « Artiste - Titre » :

```
omnis_refdb scan dossier_des_sons/ --db references_utilisateur.txt --attendu data/discographies_a_mesurer.csv
```

`--attendu` : la durée de chaque fichier est comparée à celle de la liste (±8 s ou ±6 %). Un
téléchargement automatique qui est tombé sur un homonyme (GRY, Omyll…) n'a pas la bonne durée :
il n'est pas mesuré (« sans doute pas le bon son »). Un fichier « GRy, iNESS M - Titre » est rangé
sous le nom de la liste (« GRY »). Pour SPOTIFETCH : `data/spotifetch.csv` (name,artist), avec les
crédits exacts des plateformes pour les petits artistes.

Seules les mesures entrent dans la base ; les fichiers restent chez toi (jamais dans le dépôt).
