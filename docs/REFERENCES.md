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
| `image`, `image_credit` | `2hollis.png`, `Photo : Auteur, CC BY-SA 4.0` | photo de l'artiste **sous licence libre** et son crédit | menu RÉF et carte « Référence visée » ; sans crédit, la photo n'est pas montrée |
| `pochette`, `pochette_credit` | `la_feve_album.jpg`, `Pochette : Deezer` | pochette du titre et sa source | idem ; absente : OMNIS dessine une pochette d'après la fiche |
| `lien` | `https://www.deezer.com/track/…` | la musique en ligne | carte « Référence visée » : un clic ouvre le navigateur |

## Les images (photo d'artiste, pochette)

La base ne contient que des **noms de fichiers** ; les images sont dans le dossier `images/`
d'OMNIS (`%APPDATA%\Abrasion Labs\OMNIS\images` sous Windows, `~/Library/Application
Support/Abrasion Labs/OMNIS/images` sur Mac). PNG conseillé (JPG lu sous Windows et Mac).

```
omnis_refdb image photo.png --artiste "2hollis" --credit "Photo : Auteur, CC BY-SA 4.0" --db references_utilisateur.txt
omnis_refdb pochette pochette.png --artiste "2hollis" --titre "poster boy" --db references_utilisateur.txt
```

L'outil copie l'image dans `images/` sous un nom propre et l'écrit dans la fiche.

**Depuis Deezer, d'un coup** (API publique, sans clé ni compte ; il faut internet et `curl`,
présent dans Windows 10 et 11) :

```
omnis_refdb deezer --artiste "La Fève" --db references_utilisateur.txt
omnis_refdb deezer --artiste "La Fève" --titre "Titre" --db references_utilisateur.txt
```

L'outil trouve l'artiste (accents et fautes tolérés ; à égalité, le plus suivi), télécharge
sa photo, puis pour chaque titre de cet artiste dans la base : la pochette de l'album et le
lien du titre. Crédits écrits : « Image : Deezer », « Pochette : Deezer ». Un artiste encore
absent de la base y entre en fiche de nom : ses images et son lien seront gardés quand son
son sera mesuré. Seul l'outil va sur internet : le plug-in reste hors ligne, il ne lit que
les fichiers.

**Toute la discographie, et celle des proches** :

```
omnis_refdb discographie playlist.csv --artistes "euhZ|Gry" --proches 1 --sortie discographies
```

- Les artistes : ceux de la playlist (export CSV : seule la colonne « Artist Name(s) » est
  lue, plusieurs artistes séparés par « ; ») et ceux de `--artistes`.
- `--proches 1` ajoute les artistes proches selon Deezer (20 par artiste, connus ou non ;
  `--proches 2` : les proches des proches).
- Pour chacun : toutes ses sorties (albums, EP, singles) et tous leurs titres, un titre repris
  gardé une fois (sa première sortie).
- Résultat : `artistes.tsv` (fans, sorties, titres, d'où il vient), `titres.tsv` (artiste,
  titre, interprète, sortie, type, date, durée, ISRC, lien), `introuvables.txt` (pas sur
  Deezer, homonymes). Fichiers texte à ouvrir dans Excel / LibreOffice.
- Nom trouvé seulement s'il est le même (accents, casse, espaces mis à part) ou à une lettre
  près : « omyll » ne devient pas « Omellette ». Un petit artiste a parfois plusieurs fiches
  Deezer du même nom : toutes gardées, marquées « homonyme à vérifier ».
- Long (la limite de Deezer est 50 demandes / 5 s) mais reprend où il s'était arrêté : les
  réponses sont gardées dans un cache (`--cache dossier`, par défaut le dossier temporaire).
- Ne contient que des noms et des liens, jamais d'audio. Les titres d'artistes absents de
  Deezer (SoundCloud seulement…) n'y sont pas.

**Rester réglo :**
- **Photo d'artiste** : seulement une photo sous licence libre (Wikimedia Commons : CC BY,
  CC BY-SA, CC0), avec l'auteur et la licence dans `image_credit`. OMNIS l'affiche toujours
  sous la photo ; l'outil refuse une photo sans crédit. Garder le lien de la page source dans
  un fichier à part (`images/sources.txt`) pour pouvoir le prouver.
- **Images Deezer** (photo et pochettes) : pas sous licence libre non plus. Pour ton usage,
  avec « Deezer » affiché et le lien vers la musique : oui. **Dans le plug-in vendu : non**,
  sans un accord avec Deezer (conditions de son API) et les labels.
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
| `bpm` | BPM | autocorrélation du flux des attaques, 85 – 170 (un trap à 142 n'est pas lu à 71) | info ; plus tard : temps de relâche calés sur le tempo |
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

## État de la base (30/10)

91 titres ; **2 avec la fiche complète** (2hollis – poster boy, SLAYR – Sloppy Joe : leur audio
était disponible ici). Les 89 autres attendent d'être re-mesurés depuis l'audio.
Vérifié sur des fichiers dont le tempo est dans le nom : 135, 118 et 142 BPM retrouvés exactement.
