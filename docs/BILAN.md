# OMNIS : bilan du projet (22 octobre 2026)

## En bref

- **Le son.** OMNIS est un plug-in VST3 de mastering. Il égalise, tient la 808, écrête et limite,
  le tout en qualité studio.
- **L'IA.** Il lit ta demande en français, même avec des fautes. Il sait de quel artiste ou de
  quel genre tu parles, il écoute une référence, et il rapproche ta prod de ce son **sans la
  copier ni la détruire**.
- **Où on en est.** Le moteur et l'IA « hors ligne » fonctionnent et sont testés : 128 tests du
  moteur, 4 tests du plug-in, le validateur officiel Steinberg (537 tests).
- **Ce qui manque surtout :**
  - une version Windows que tu peux installer dans FL Studio ;
  - l'interface : boutons, champ de texte pour ta demande ;
  - la saturation dédiée de la 808, seule voie réaliste vers −6 LUFS sur tes prods.

---

## 1. Ce que fait OMNIS aujourd'hui

### La chaîne de son (dans l'ordre)

| Étage | Ce qu'il fait | État |
|---|---|---|
| **Sub Control & Mono Maker** | Coupe l'infra-grave, met le sub en mono, peut renforcer ou synthétiser la 808. Sans rotation de phase. | ✅ |
| **Match EQ** | Égaliseur Mid/Side en 21 points (20 Hz – 20 kHz). C'est lui qui rapproche ta prod de la référence. | ✅ |
| **Inflator** | Densité et chaleur : la 808 et le mix réglés à part, crêtes inchangées. Neutre par défaut. | ✅ (l'IA ne s'en sert pas encore) |
| **Imager** | Largeur stéréo en 4 bandes, sans toucher au centre (voix, 808) | ✅ (l'IA ne s'en sert pas encore) |
| **Multibande « tenue »** | 4 bandes. Tient les coups de la 808 dans le temps (comme les références) sans changer l'équilibre. | ✅ |
| **Clipper** | Écrêtage propre, suréchantillonné jusqu'à 16x, sub et reste séparés : la 808 n'abîme pas la voix. | ✅ |
| **Limiteur** | Plafond « true peak », relâchement qui s'adapte. | ✅ |
| **Tilt** | Penche tout le spectre d'un geste (plus brillant / plus sombre), dans le Match EQ | ✅ |
| **Instances liées** | Un OMNIS « Master » sur le master et un OMNIS « Stem » par piste (808, kick, voix…) : le master voit et écoute chaque piste, un stem n'ajoute aucune latence et applique le gain que le master lui donne | ✅ le lien ; ❌ le cerveau du mix |
| Saturation pilotée par l'IA | « crispy », 808 saturée : l'Inflator existe, l'IA doit apprendre à le doser | ❌ à faire |
| Dither | Pour l'export 16 bits | ❌ à faire |

Latence totale : 767 ms, compensée automatiquement par FL Studio.

### L'intelligence (sans internet, sans coût)

- **Comprend ta demande.**
  - Les fautes : « propr », « crispi », « jveu ke sa sonne + brilan ».
  - Les nuances : « ça manque de crispy », « c'est trop brillant », « c'est pas propre », « pas
    trop fort », « un poil », « vraiment ».
  - Les ordres : « garde ma 808 » protège le sub.
  - La base de mots est un fichier qu'on agrandit sans toucher au code (44 notions,
    environ 420 expressions).
- **Connaît tes artistes.** Il connaît les 1911 titres et 1247 artistes de tes playlists, avec
  tes 8 familles (rap US, rap FR, hyperpop…). « ken carsonn », « fein de travis scott »,
  « comme la feve » : il trouve.
- **Se sert des artistes proches.** « comme Yeat » sans aucun son de Yeat : il vise les artistes
  proches de Yeat qu'il a déjà écoutés, sinon le profil du genre. Les fiches d'artistes viennent
  d'une recherche Gemini (`docs/GEMINI_ARTISTES.md`).
- **Ne fait jamais semblant.** S'il n'a encore écouté aucun son d'un style, il le dit et te
  demande un son ou un artiste proche.
- **Écoute une référence.**
  - Dans FL Studio, tu joues un titre sur l'entrée « Reference » du plug-in. Il faut au moins
    60 s, les écoutes s'additionnent, et seul le cœur du morceau compte (pas l'intro).
  - Ou à partir d'un fichier : un titre de 2 min est analysé en 0,2 s.
  - Il ne garde **que des chiffres**, jamais l'audio.
- **Rapproche sans copier.**
  - Il ne comble qu'une partie de l'écart avec la référence : 50 % par défaut, jamais plus de
    85 %.
  - Chaque zone a une limite (le sub bouge de 3 dB au plus) pour protéger l'identité de ta prod.
  - Il explique ce qu'il fait : « la référence a 12 dB de sub en moins, on n'en retire que 3 ».
- **Apprend.**
  - Chaque correction que tu fais ajuste le sens des mots chez toi.
  - Un mot inconnu, utilisé deux fois de la même façon, devient un mot connu.
  - Il existe aussi un export anonyme pour améliorer tous les plug-ins ; il faudra un point de
    collecte (voir §4).

### Les outils (en ligne de commande, pour moi et pour tester)

| Outil | Rôle |
|---|---|
| `omnis_analyze` | Sonie, crêtes, dynamique, stéréo d'un fichier |
| `omnis_compare` | Mesure la distorsion ajoutée et le changement d'équilibre entre deux fichiers |
| `omnis_tune` | Trouve le réglage le plus propre pour une sonie, ou le plus fort sans détruire la prod |
| `omnis_match` | Prod + demande (+ référence) → master |
| `omnis_ask` | Montre ce que le plug-in comprend d'une phrase |
| `omnis_refdb` | Base de références : ajouter un son, un dossier, une liste d'artistes, un export de playlists |
| `omnis_learn` | Apprentissage local, export anonyme, fusion des retours |

---

## 2. L'histoire de tes masters (V1 → V6)

Tous les masters partent de `pour_claude_-6.wav` (et `pour_claude_plugin_-6.wav`).

Comment lire le tableau :
- **Propreté** : la distorsion ajoutée. Plus le chiffre est négatif, plus c'est propre.
- **Équilibre changé** : le plus grand écart, en dB, entre une octave du master et la même octave
  de ta prod.
  - En V1–V3, c'est un défaut : on voulait garder ta balance.
  - À partir de V4, une partie est **voulue**, puisqu'on rapproche la prod de la référence.

| Version | Ce qui a changé | Sonie | Crête | Écart de volume entre passages | Propreté | Équilibre changé | Ton avis |
|---|---|---|---|---|---|---|---|
| Prod d'origine | — | −19.0 LUFS | −4.0 dBTP | 12.1 LU | — | — | — |
| **V1 Safe** | Premier réglage prudent | −8.3 | −1.1 | 6.5 | −20.8 dB | 2.7 dB | « très propre » |
| **V1 Club** | Premier réglage fort | −5.2 | **+1.1** | 4.4 | **−14.2 dB** | 4.2 dB | « trop distordu » |
| **V2** | Clipper en deux bandes, la 808 cède la place aux autres sons | −5.2 | −0.35 | 2.4 | −15.3 dB | **10.8 dB** | « catastrophique, plus de sub » |
| **V3** | Sub réparé : le plus fort possible sans changer ta balance | −9.8 | −0.35 | 8.0 | −28.7 dB | 2.1 dB | « pas mal » |
| **V4** | Match EQ : rapproché de SLAYR / poster boy | −9.7 / −9.6 | −0.35 | 6.8 / 6.4 | −27.0 / −26.7 dB | 3.9 / 4.1 dB (voulu) | pas encore écouté |
| **V5** | « comme 2hollis, garde ma 808, un poil crispy » (test de la compréhension) | −9.8 | −0.35 | 8.3 | −28.2 dB | 2.2 dB | jamais envoyé |
| **V6** | V4 + multibande : la 808 tenue comme dans les références | −9.3 / −9.2 | −0.35 | **5.2** | −24.5 / −24.6 dB | 4.5 / 4.7 dB (voulu) | pas encore écouté |

Pour `pour_claude_plugin` : V1 Club −5.1 LUFS, V2 −5.1, V3 −9.5, V4 (vers SLAYR) −9.2 LUFS.

**Ce que ça raconte :**
- La V1 Club était forte, mais la 808 écrêtée salissait la voix et les médiums (« trop
  distordu »).
- La V2 était propre, mais la 808 avait perdu 8 dB (« plus de sub »).
- Depuis la V3, ta balance est respectée, mais on plafonne autour de −9.5 LUFS.
- La V4 rapproche la couleur de la référence.
- La V6 rapproche aussi la tenue dans le temps : 5.2 LU entre passages calmes et forts, contre
  3.6 chez SLAYR.

---

## 3. Ce que j'ai appris de tes prods (mesuré)

- **Ta 808 est ton identité.** Elle représente 78 % de l'énergie du morceau, avec un sub de 9 à
  13 dB plus fort que dans tes références.
- **Pourquoi on plafonne vers −9.5 LUFS.** Sur ton passage le plus fort, les crêtes dépassent la
  sonie de 12 dB. Pour −6 LUFS sous −0.3 dBTP, il faudrait moins de 5.7 dB ; poster boy est à
  4.4 dB. Retirer 6 dB de crêtes, c'est forcément de la distorsion ou un changement de balance.
  Tes références font ce choix : leur son est saturé.
- **Ce qui ne marche pas, mesuré :**
  - un limiteur plus lent ;
  - moins de sub (−5 dB ne fait gagner que 0.2 LU) ;
  - tolérer plus d'écart (+0.6 LU seulement) ;
  - tenir la 808 (rapproche la référence, mais n'ajoute pas de sonie).
- **Ce qui pourrait marcher :** saturer volontairement la 808, et seulement elle. Une partie de
  son énergie devient des harmoniques qu'on entend (le son « rage »), sans toucher la voix.

---

## 4. Ce qui manque (par priorité)

| # | Quoi | Pourquoi | Taille |
|---|---|---|---|
| 1 | **Une version Windows téléchargeable** | La CI compile déjà OMNIS sous Windows, mais ne met pas le `.vst3` à disposition. Tu n'as encore jamais pu l'essayer dans FL Studio. | petit |
| 2 | **Saturation dédiée de la 808** (« crispy », grain) | Seule voie réaliste vers −6 LUFS sur tes prods | moyen |
| 3 | **Interface** (module 6) | Champ pour taper ta demande, bouton « appliquer » après l'écoute d'une référence, affichage de ce qu'il a compris | gros |
| 4 | **Remplir la base de références** | 91 titres mesurés, surtout rap fr et rap us. Il en manque en rock, electro, pop et rage (2 titres seulement). | dépend de toi |
| 5 | **Modèle de compréhension local** (module 5) | Comprendre n'importe quelle phrase, sans internet, dans tes 1 à 2 Go | gros |
| 6 | **Mise à jour pour tous** | Point de collecte des retours anonymes + fichier de données signé. Dépend de ce qu'Onirique permet. | moyen |
| 7 | Réduire la latence | 668 ms ; fusionner deux égaliseurs retirerait environ 200 ms | petit |
| 8 | Dither, préréglages, version macOS testée | Finitions avant la vente | moyen |

---

## 5. Ce que j'attends de toi

1. **Écouter V4 et V6**, à côté de la V3. Est-ce que ça « ressemble » sans copier ? La 808 tenue
   te plaît, ou elle est écrasée ?
2. **Les références audio** que tu as prévues (titres achetés, tes prods…), nommées
   « Artiste - Titre » si possible.
3. **Onirique** : leur outil de développeur peut-il envoyer des données, héberger un fichier,
   pousser des mises à jour ?
4. **Me dire par quoi on continue** : je recommande 1 (version Windows), puis 2 (saturation de
   la 808).

---

## 6. Les fichiers que je t'envoie

| Fichier | C'est quoi |
|---|---|
| `00 - ORIGINAL - pour_claude.wav` / `..._plugin.wav` | Tes deux mixes, tels que tu me les as envoyés |
| `00 - ORIGINAL -6dBFS - ...` | Les mêmes, exportés à −6 dBFS (le point de départ de tous les masters) |
| `V1 - ... Safe / Club` | Premiers masters (WAV) |
| `V2` → `V6` | Une version par étape, nommée avec ce qui a changé et sa sonie |

Les V2 à V6 sont en **FLAC** : un format sans perte, deux fois plus léger que le WAV. Il est
identique au bit près au master d'origine (vérifié), et FL Studio l'ouvre directement.
