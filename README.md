# Abrasion Labs // OMNIS

Processeur de mixage / mastering intelligent, **VST3 natif sans framework** (ni JUCE, ni iPlug2) :
SDK Steinberg + moteur DSP C++20 en double précision, piloté à terme par IA (références audio,
prompts en langage naturel, profils de genres) et habillé d'une interface NanoVG / OpenGL
rétro-futuriste.

> **État : Modules 1 à 4 / 6 terminés, module 5 commencé** : architecture, squelette VST3,
> mesures normalisées, banc de test, filtres TPT/SVF et crossovers, **Sub Control & Mono Maker** à
> phase linéaire ([calibration](docs/CALIBRATION_MODULE2.md)), **oversampling HIIR, clipper ADAA et
> limiteur true peak** ([calibration](docs/CALIBRATION_MODULE3.md)), puis **Match EQ, adaptation à
> une référence et tenue multibande** ([calibration](docs/CALIBRATION_MODULE4.md)), **Inflator** (densité
> et chaleur) et **Imager** (largeur stéréo multibande).

Chaîne du master, dans l'ordre :

```
entrée ─► gain d'entrée
       ─► 1. Sub Control   coupe l'infra, mono sous 120 Hz
       ─► 2. Match EQ      couleur spectrale Mid/Side en 21 points, tilt
       ─► 3. Inflator      densité et chaleur, 808 et mix réglés à part
       ─► 4. Imager        largeur stéréo en 4 bandes
       ─► 5. Multibande    4 bandes : tient la 808 et les médiums dans le temps
       ─► 6. Clipper       écrête les pics restants, sub et reste séparés
       ─► 7. Limiteur      plafond true peak final
       ─► gain de sortie ─► mesures (LUFS, crête, stéréo) ─► sortie
```

## Module 2 : Sub Control & Mono Maker

| Paramètre | Plage | Défaut | Rôle |
|---|---|---|---|
| Low Cut | 5–60 Hz | 16 Hz | Passe-haut 24 dB/oct : DC et infra-graves |
| Mono Below | 20–300 Hz | 120 Hz | Le Side est retiré sous cette fréquence (sub mono, compatible club / vinyle / téléphone) |
| Mono Amount | 0–100 % | 100 % | Dosage du Mono Maker |
| Mono Slope | 24 / 48 dB/oct | 24 | Pente Linkwitz-Riley (bascule sans clic) |
| Sub Gain / Sub Freq | −12…+6 dB / 30–150 Hz | 0 dB / 70 Hz | Plateau grave sur le Mid |
| Sub Synth | 0–100 % | 0 % | Octave basse synthétisée (diviseur type dbx 120) pour 808 maigres |
| Sub Phase | Linear / Analog | Linear | FIR à phase linéaire (crêtes préservées) ou IIR « console » |

Pourquoi la phase linéaire par défaut : sur la référence 2hollis, la version IIR classique fait
monter le true peak de **+7.8 dB** (rotation de phase de la 808 écrêtée), la version FIR de
**+0.6 dB** pour un sub aussi mono. Latence constante de l'étage : 197 ms à 44.1 kHz (chaîne complète : 668 ms), compensée par le DAW.
Coût : ~3 % d'un cœur.

---

## Module 3 : Clipper & Limiteur

| Paramètre | Plage | Défaut | Rôle |
|---|---|---|---|
| Clip Mode | Hard / Soft Knee / Diode | Soft Knee | Courbe d'écrêtage (Diode : asymétrique, harmoniques paires) |
| Clip Drive | 0–24 dB | 0 dB | Poussée dans le clipper |
| Clip Ceiling | −12–0 dBFS | 0 dBFS | Niveau d'écrêtage |
| Clip Knee | 0–100 % | 50 % | Douceur du genou (Soft Knee) |
| Clip Oversampling | 1x–16x | 8x | Suréchantillonnage (HIIR phase minimale) |
| Clip Split | 60–300 Hz | 120 Hz | Séparation sub / reste (phase linéaire) : la 808 s'écrête sans intermoduler la voix |
| Clip Sub Drive | −12…+12 dB | 0 dB | Poussée supplémentaire de la bande sub (« 808 ultra-clippée, sub propre ») |
| Clip Sub Make-Room | 0–100 % | 0 % | La bande sub cède la place quand la somme dépasse le plafond (au plus −6 dB) ; désactivé par défaut, il vidait le sub |
| Limiter Gain | 0–24 dB | 0 dB | Poussée dans le limiteur |
| Limiter Ceiling | −6–0 dBTP | −1.0 dBTP | Plafond true peak (toujours) |
| Limiter Release | 1–1000 ms | 60 ms | Relâchement de base |
| Limiter Auto Release | 0–100 % | 100 % | Release adaptatif : jusqu'à 5x plus lent en limitation soutenue |

Pas de mode figé « Club » / « Safe » : l'intensité (sonie visée, plafond) vient de la demande de la
personne, et l'algorithme cherche la répartition la plus propre pour l'atteindre (`omnis_tune`,
précurseur du pilotage par IA). Repliement d'un écrêtage dur de 6 dB à 5 kHz : −35 dB en 1x,
**−94 dB en 8x** ; intermodulation d'une voix par une 808 écrêtée : **−85 dB** grâce aux deux bandes.

---

## Module 4 (partie 1) : Match EQ & adaptation à une référence

« Ressembler, pas copier ». OMNIS écoute la référence citée dans la demande et n'en garde que les
traits **persistants** : balance large Mid/Side, énergie du sub (indépendamment de la note de la
808), dynamique par bande, sonie. Il déplace ensuite la prod **en partie** vers cette référence,
avec des bornes qui protègent son identité, et explique ce qu'il fait.

| Paramètre | Plage | Défaut | Rôle |
|---|---|---|---|
| Match Amount | 0–100 % | 100 % | Dosage de la courbe d'intention |
| Match Mid / Side 20 Hz … 20k (2 × 21 nœuds) | ±12 dB | 0 dB | Courbe M/S à phase linéaire, une demi-octave entre nœuds, écrite par l'adaptation (ou à la main) |

| Règle | Défaut |
|---|---|
| Ressemblance (part de l'écart refermée) | 50 %, « un peu » 30 %, « exactement » 80 %, jamais plus de 85 % |
| Sub (≤ 100 Hz) | ±3 dB (« garde ma 808 » : ±1 dB) |
| Ailleurs | −6 / +4 dB ; jamais de boost d'une bande vide |
| Largeur (Side − Mid, > 120 Hz) | ±4 dB ; jamais d'élargissement d'une bande quasi mono |

```bash
$B/omnis_match prod.wav reference.wav --demande "comme la réf mais garde ma 808, fort" --render master.wav
$B/omnis_match prod.wav --demande "comme 2holis, un poil plus crispy" --render master.wav   # référence de la base
```

**Tenue multibande** (paramètres 400–444) : 4 bandes à phase linéaire (120 Hz / 800 Hz / 5 kHz),
compresseur à anticipation par bande, réglé d'après la *variation* de chaque bande dans la
référence ; la compensation rend à chaque bande son énergie (équilibre tonal inchangé). Sur
`pour_claude` → SLAYR : LRA 12.1 → 5.2 LU (référence 3.6).

| Paramètre | Plage | Défaut | Rôle |
|---|---|---|---|
| MB Split Low / Mid / High | 60–250 / 250–2500 / 2500–12000 Hz | 120 / 800 / 5000 Hz | Séparations Linkwitz-Riley à phase nulle |
| MB *bande* Threshold | −60–0 dBFS RMS | 0 | Seuil |
| MB *bande* Ratio | 1–20:1 | 1 (neutre) | Ratio, genou 6 dB |
| MB *bande* Attack / Release | 1–300 / 10–3000 ms | 10–30 / 80–250 ms | Au-delà de 10 ms d'attaque, le transitoire passe (punch) |
| MB *bande* Makeup | ±12 dB | 0 | Compensation |

### Écouter une référence dans FL Studio (sans rien télécharger)

1. Mettre OMNIS sur le master. Dans le Mixer, envoyer la piste de la référence (un titre acheté, sa
   propre prod…) vers l'entrée auxiliaire **Reference** d'OMNIS (sidechain), sans la router vers
   le master.
2. Activer **Listen Reference** et jouer au moins **60 s** de la référence, puis désactiver. Les
   écoutes s'additionnent : on peut jouer le couplet, couper, puis le refrain. Seul le cœur du
   morceau compte : les passages à plus de 10 dB sous les plus forts (intro, breaks) sont écartés.
3. OMNIS a mesuré la référence et, en même temps, la prod. Pendant l'écoute, il affiche les
   secondes écoutées et la sonie de la référence (paramètres en lecture seule). Seuls des nombres
   sont gardés :
   - dans la base de la personne : `references_utilisateur.txt`, dans le dossier de données
     d'OMNIS (`%APPDATA%\Abrasion Labs\OMNIS` sous Windows) ;
   - dans la session : la référence et la prod.

Le thread audio ne fait que copier les échantillons dans deux files sans verrou. L'analyse tourne
sur son propre thread et donne exactement la signature d'une analyse de fichier (testé).

Un **fichier** de référence s'analyse en entier, en parallèle sur tous les cœurs, avec un résultat
identique au bit près à l'analyse séquentielle. Mesures sur 4 cœurs :

| Cas | Avant | Après |
|---|---|---|
| Un titre de 2 min | 1089 ms | **223 ms** |
| `omnis_refdb scan`, 4 titres (8 min) | — | **1.0 s** |

Le gros du gain vient de la crête vraie (2.2x plus rapide, un fichier découpé en segments exacts).

### Inflator (étage 3)

Rend un son plus dense et plus plein sans faire monter ses crêtes.
- **La courbe** donne du gain aux passages faibles et s'aplatit en haut, sans cassure : la crête
  reste à sa place. **Curve** règle sa forme : à −50, très dense et doux ; à +50, presque
  linéaire puis franc.
- **Warmth** décentre la courbe pour ajouter des harmoniques paires (le « chaud »). Le continu
  qu'elle crée est retiré.
- **Deux bandes, réglées séparément :** **Inflate 808**, sous **Split** (120 Hz par défaut), et
  **Inflate Mix**, au-dessus.
- **Exact au repos :** le son d'origine ne traverse ni la séparation ni l'oversampling. Seul ce
  que la courbe ajoute y passe. À 0 %, qui est le défaut, la sortie est l'entrée, au bit près.
- **Mesures, mix à 100 % (courbe par défaut) :**
  - sinus à pleine échelle : +0.92 dB de RMS, crête inchangée ;
  - sinus à −20 dBFS : +2.50 dB ;
  - suréchantillonnage 4x : la 5e harmonique d'un 9 kHz ne se replie pas (< −70 dB).

### Imager (étage 4)

Largeur du Side dans 4 bandes : sous 250 Hz, 250 Hz – 2 kHz, 2 – 8 kHz, au-dessus de 8 kHz.
- 0 % = mono, 100 % = inchangé, 200 % = Side +6 dB.
- **Le Mid n'est jamais touché** : élargir n'enlève rien au centre (voix, 808).
- **Transitions douces** sur ±1/3 d'octave.
- **Phase linéaire** : le Side reste aligné sur le Mid.

### Tilt

Le réglage **Tilt** (−6 à +6 dB) penche tout le spectre d'un seul geste, en Mid et en Side :
- +T dB à 20 kHz, −T dB à 20 Hz ;
- une droite en octaves autour de 640 Hz (le milieu de 20 Hz – 20 kHz), constante au-delà.

Il s'ajoute au noyau du Match EQ : aucune latence en plus. C'est un réglage à la main, que
Match Amount ne dose pas. Attention : un tilt positif baisse aussi le sub (−1.5 dB à 57 Hz pour
+3 dB).

### Instances liées : un master et des stems ([détails](docs/ARCHITECTURE.md#11-instances-liées))

Dans FL Studio :
1. Mettre OMNIS sur chaque piste du Mixer qui porte un élément : 808, kick, drums, mélodie, voix.
   Régler **Link Role = Stem**, **Stem Type** selon la piste et **Link Group = 1**.
2. Mettre OMNIS sur le master : **Link Role = Master**, même groupe.
3. Le master affiche **Linked Stems**, le nombre de pistes reliées. Pendant la lecture, il écoute
   chacune (signature : balance, sub, dynamique).

Comment ça marche :
- **Un stem n'ajoute aucune latence** : rien à phase linéaire sur chaque piste. Il laisse passer
  le son, au gain que le master lui donne.
- **Il envoie ce qui part vers le mix** (audio + position dans le projet) dans une mémoire
  partagée nommée. Ça fonctionne aussi quand l'hôte isole chaque plug-in dans son propre
  processus.
- **Le son du mix passe toujours par la console de l'hôte.** Le master s'en sert pour décider ; il
  ne le ressort pas, sinon il serait compté deux fois.
- **Les groupes 1 à 8** séparent deux projets ouverts en même temps.

Changer de rôle change la latence : le plug-in demande à l'hôte de le réactiver.

Aujourd'hui, le master **voit et écoute** ses stems, et un stem **applique le gain** que le master
lui ordonne. La partie qui décide (équilibre 808 / kick / voix, EQ et dynamique par piste) est
l'étape suivante. Elle se calibrera sur de vrais stems.

### Comprendre la demande, références, apprentissage ([détails](docs/APPRENTISSAGE.md))

- **Fautes comprises** (clé phonétique + Damerau-Levenshtein) : « propr », « crispi »,
  « jveu ke sa sonne + brilan ».
- **Nuances** : lexique en données (`data/lexique_fr.txt`, 48 concepts, ~880 expressions en français
  et en anglais) et petite grammaire. Exemples : « ça manque de crispy », « c'est trop brillant »,
  « c'est pas propre », « pas trop fort », « garde ma 808 », « un poil », « vraiment », « make it
  brighter », « not enough punch », « turn the bass down a bit ».
- **SMS et argot** (`data/argot.txt`, ~360 formes) : « tro », « bcp », « jveux », « ++ », « de ouf »,
  « clean af », « wesh », « mdr », « no cap »… réécrits avant la lecture ; les tics et les avis sans
  direction (« fire », « chanmé ») sont retirés au lieu d'être pris pour des mots inconnus.
- **Sujet et niveau** : « la 808 est trop forte » baisse la 808 sans toucher la sonie du master ;
  « the vocals are too quiet » monte la voix ; « la basse et le kick sont trop forts » baisse les deux.
- **Base de références** (`data/references.txt`) : signatures mesurées, jamais d'audio. Recherche
  par titre, artiste, alias ou genre. Un artiste **connu de nom** utilise le profil de ses genres
  en attendant un son ; s'il est **inconnu**, le plug-in demande un artiste proche ou un son, et
  ajoute ce son à la base. Lecture WAV, MP3 et FLAC, en local.
- **Apprentissage** : chaque correction ajuste le sens des mots chez la personne, et un mot inconnu
  devient un concept. Un export anonyme permet une fusion communautaire, qui ne retient que les
  tendances confirmées par 10 personnes et plus.

```bash
$B/omnis_ask "ça manque de crispy, genre slayer" [--log retours.tsv --correction "presence=+1" --note 1]
$B/omnis_refdb add titre.flac --artiste "2hollis" --titre "poster boy" --genres "rage" --db refs.txt
$B/omnis_refdb scan mes_refs/ --genres "rage" --db refs.txt      # « Artiste - Titre.mp3|flac|wav »
$B/omnis_refdb noms artistes.txt --db refs.txt                    # « Artiste ; genre, genre ; alias »
$B/omnis_refdb artistes data/artistes_proches.txt --db refs.txt      # « Artiste ; famille ; styles ; proches : A | B ; traits : … »
$B/omnis_refdb csv playlists.csv --classes data/genres_classement.txt --db refs.txt   # export de playlists
$B/omnis_learn local retours.tsv --lexique lexique_utilisateur.txt
```

## Ce que contient le Module 1

| Domaine | Contenu | Validation |
|---|---|---|
| **Plug-in VST3** | Processeur + contrôleur, stéréo, **32 et 64 bits natifs**, automation **échantillon-précise** (découpage du bloc aux points d'automation), bypass hôte (`kIsBypass`), état versionné et extensible, latence annoncée (PDC) | Validateur Steinberg : **47/47** (standard) et **537/537** (`-e`, étendu) |
| **Moteur DSP** | `omnis::Engine` indépendant du SDK : chaîne d'étages (`Stage`), PDC interne, bypass aligné en temps et commuté sans clic, trims In/Out lissés, monitoring Stéréo / Mid / Side lissé | Null-test **bit-exact** 64 et 32 bits, bypass aligné à 1e-15 près sur 2185 échantillons de latence, invariance au découpage en blocs |
| **Temps réel** | File SPSC lock-free, triple buffer wait-free, lisseur one-pole, tampons alignés 64 octets, garde FTZ/DAZ (x86 + ARM64), ligne de retard | Tests concurrents (2 M éléments, 200 k publications), **ASan + UBSan + TSan propres** |
| **Mesures** | Sonie **ITU-R BS.1770-4 / EBU R128** (M, S, I, LRA), **True Peak** x4 (FIR Kaiser 48 taps/phase, plat à ±0.01 dB jusqu'à 0.454·fs), corrélation de phase dynamique, ratio Side/Mid, statistiques (crête, RMS, DC, skewness, kurtosis) | **EBU Tech 3341 cas 1 à 5** à 44.1 / 48 / 96 kHz (±0.1 LU), coefficients K = tables BS.1770 à 1e-10, ISP fs/4 = 0.00 dBTP |
| **Banc de test** | `omnis_testgen` (17 signaux), `omnis_analyze` (rapport texte / JSON), `omnis_render` (rendu hors ligne avec le moteur du plug-in) | — |

Les mesures LUFS M/S/I, True Peak, corrélation et LRA sont exposées comme **paramètres en lecture
seule** : en attendant l'interface du module 6, l'éditeur générique de n'importe quel DAW les
affiche déjà en temps réel. La sonie intégrée repart de zéro à chaque lancement du transport.

## Installer dans FL Studio (Windows)

1. Ouvrir la dernière exécution verte de la CI (onglet *Actions* du dépôt), télécharger
   l'artefact **OMNIS-Windows-VST3** (zip). Clic droit sur le zip → Propriétés → *Débloquer*.
2. Copier le dossier `OMNIS.vst3` dans `C:\Program Files\Common Files\VST3\`.
3. FL Studio : *Options → Manage plugins → Find installed plugins*. Si le plug-in ne se charge
   pas : installer *Microsoft Visual C++ Redistributable 2015–2022 (x64)*.
4. Master : *Rôle* Master. Pistes : *Rôle* Stem, même *Groupe*, *Mode* Auto.

**Sans la CI** (les minutes GitHub Actions du mois épuisées : l'exécution échoue en 2 s sans
journal), on construit OMNIS soi-même, gratuitement :
1. Installer *Visual Studio Build Tools 2022* (gratuit, charge « Développement Desktop en C++ »),
   *CMake* et *Git*.
2. Dans « x64 Native Tools Command Prompt » :
   `git clone` du dépôt, puis
   `cmake -S . -B build -G "Visual Studio 17 2022" -A x64` et
   `cmake --build build --config Release --target OMNIS`.
3. Le plug-in est dans `build\VST3\Release\OMNIS.vst3` : le copier comme à l'étape 2.

### La base de références ([la fiche de chaque titre](docs/REFERENCES.md))

Chaque titre de référence a sa fiche en nombres : spectre Mid / Side, sonie, et (fiche complète)
tempo, tonalité, note de la 808, écart couplet / refrain, punch par zone, grave mono, attaques,
brillance, écrêtage. Pour compléter les fiches depuis tes sons : `omnis_refdb.exe` (artefact
*OMNIS-Windows-outils*), ou la référence écoutée dans FL.

### La console ([détails](docs/ARCHITECTURE.md#16-la-console))

- **Ruban du haut** : AUTO (joue le passage le plus fort : OMNIS écoute 10 s et règle la
  chaîne vers ton style), A / B / copie, BYPASS, MATCH (écoute au même volume que l'original,
  clignote), DELTA (n'entendre que ce que le module ouvert change), Stéréo / Mid / Side, « ? »
  (le guide), latence.
- **Barre IA** : écris ta demande (« plus de punch, garde ma 808 », « genre rage », « moins
  boueux »…). OMNIS écoute la prod, règle les modules, mène le volume à la cible et dit ce
  qu'il a fait.
- **Menu RÉF** (à côté de la barre IA) : **Styles → artiste → un de ses sons** (ou tout le
  style, ou tous les titres de l'artiste), et les artistes de A à Z ; y compris ceux que tu as
  ajoutés (`references_utilisateur.txt`). Chaque son a sa pochette (la vraie si tu l'as mise,
  sinon dessinée d'après sa fiche), l'artiste sa photo libre avec son crédit ; la page Match EQ
  montre la **référence visée** ([images](docs/REFERENCES.md#les-images-photo-dartiste-pochette)). Elle s'applique tout de suite (courbe du Match EQ, punch,
  grave mono, écart couplet / refrain, écrêtage, volume) et AUTO la vise ensuite. Le choix est
  gardé avec le projet ; « Aucune » rend la main aux mots.
- **Rack** : 7 modules (Sub, Match EQ, Inflator, Imager, Multiband, Clipper, Limiter), chacun
  avec son gros bouton, son voyant ON et son état. Clic : ouvrir. Glisser par le nom :
  changer l'ordre.
- **Tiroir** : TOUS les réglages du module ouvert et son écran (nœuds du Match EQ et
  séparations du multibande à glisser, courbes, réductions).
- **Colonne Master** : entrée / sortie, bargraphe, mesures avec leur verdict en mots, rôle,
  groupe, stems liés, pages Chef d'orchestre et Piste (stem : élément reconnu, et dans un bus
  de drums chaque élément nommé avec ce qui en est fait).
- **Guide** à la première ouverture (10 étapes), relancé par « ? ».

## Compilation

Prérequis : CMake ≥ 3.28, un compilateur C++20 (GCC 12+, Clang 15+, Apple Clang 15+, MSVC 2022), Git.

```bash
# Téléchargement automatique du SDK VST3 v3.8.0 (licence MIT)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Le bundle est produit dans `build/VST3/Release/OMNIS.vst3` et le validateur Steinberg tourne
automatiquement après chaque compilation du plug-in.

| Option CMake | Défaut | Rôle |
|---|---|---|
| `OMNIS_VST3_SDK_DIR` | *(vide)* | SDK VST3 local au lieu du téléchargement |
| `OMNIS_BUILD_VST3` | `ON` | `OFF` = moteur + outils + tests seuls, sans SDK |
| `OMNIS_BUILD_TOOLS` / `OMNIS_BUILD_TESTS` | `ON` | Outils de banc de test / tests unitaires |
| `SMTG_CREATE_PLUGIN_LINK` | `OFF` | Lien symbolique automatique dans le dossier VST3 utilisateur |
| `OMNIS_NATIVE_ARCH` | `OFF` | `-march=native` (machine de dev uniquement, jamais pour une diffusion) |

macOS universel : ajouter `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`.

## Outils de banc de test

```bash
B=build/bin/Release

$B/omnis_testgen list                       # catalogue des signaux
$B/omnis_testgen kit ./kit --sr 48000       # tout le kit + MANIFEST.txt
$B/omnis_testgen drums -o drums.wav         # un signal

$B/omnis_analyze mix.wav                    # LUFS I/M/S, LRA, TP, PLR, PSR, crest, kurtosis, corrélation…
$B/omnis_analyze mix.wav --bands            # + énergie, corrélation (et pire fenêtre), S/M, crête par bande
$B/omnis_analyze mix.wav --json

$B/omnis_compare mix.wav master.wav         # distorsion non linéaire ajoutée (pondérée K, par région)
$B/omnis_tune mix.wav --target -6 --full    # réglage le plus propre pour atteindre une sonie
$B/omnis_tune mix.wav --target -6 --max-clean   # le plus fort possible sans détruire la prod
$B/omnis_match prod.wav ref.wav --demande "…"   # ressembler à une référence (voir module 4)

$B/omnis_render --params                    # clés de paramètres
$B/omnis_render mix.wav out.wav --set input_gain=-3dB --set monitor=Side --report
```

`omnis_render` exécute **exactement** le moteur du plug-in et compense la latence : la sortie est
alignée échantillon par échantillon sur l'entrée (null-test possible dans n'importe quel éditeur).

## Arborescence

```
source/
  core/       primitives temps réel (SPSC, triple buffer, lisseur, tampons alignés, FTZ/DAZ)
  dsp/        SVF TPT, crossovers Linkwitz-Riley, FFT, convolution partitionnée stéréo
  stages/     étages de la chaîne (Sub Control, Match EQ, Multibande, Clipper, Limiteur + noyaux FIR)
  ai/         compréhension de la demande (fautes, lexique), base de références, intention
              d'adaptation, apprentissage (précurseur du module 5)
data/         lexique_fr.txt, argot.txt, references.txt, catalogue.txt (compilés dans le plug-in, éditables),
              genres_classement.txt (familles de playlists -> mots de genre)
third_party/  dr_libs (décodeurs MP3 / FLAC, domaine public)
  analysis/   pondération K, sonie BS.1770-4, true peak, stéréo, statistiques, signature spectrale
  params/     table unique des paramètres (DSP, VST3, outils, IA)
  engine/     Engine + interface Stage (chaîne, PDC, bypass, monitoring, mesures)
  link/       instances liées : mémoire partagée nommée, master et stems d'un groupe
  plugin/vst3 processeur, contrôleur, état, factory
tools/        testgen, analyze, render, compare, tune, match, ask, refdb, learn (+ WAV I/O, signaux, rapport)
tests/        tests unitaires sans dépendance
docs/         ARCHITECTURE.md, PROTOCOLE_DE_TEST.md, CALIBRATION_MODULE2/3/4.md, APPRENTISSAGE.md
```

## Feuille de route

1. ✅ **Architecture & squelette VST3 / DSP**
2. ✅ **Crossovers & filtres** : SVF TPT (Zavalishin/Simper), Linkwitz-Riley LR4/LR8 et réseau N bandes
   à somme passe-tout, FFT et convolution partitionnée UPOLS, Sub Control & Mono Maker M/S à phase linéaire
3. ✅ **Oversampling & Clipper** : demi-bandes HIIR conçues à la volée (1x–16x), clipper ADAA
   Hard / Soft Knee / Diode à deux bandes, limiteur brickwall true peak à release adaptatif
4. ✅ **Match EQ & multibande** : Match EQ M/S à phase linéaire, signature de référence,
   adaptation partielle bornée et expliquée, tenue multibande à anticipation
5. 🔶 Moteur IA : ✅ compréhension de la demande hors ligne, base de références, apprentissage local
   et logique communautaire ; à venir : modèle de compréhension local (embarqué, sans internet),
   prédicteur de paramètres, serveur de mise à jour (consentement, validation, paquets signés)
6. UI NanoVG — IPlugView OpenGL, oscilloscope phosphore, matrice LED, champ de prompt

Chaque module DSP critique sera calibré sur des fichiers audio réels : voir
[`docs/PROTOCOLE_DE_TEST.md`](docs/PROTOCOLE_DE_TEST.md).

## Licences

Code OMNIS : © 2026 Abrasion Labs. SDK VST3 : licence MIT (Steinberg Media Technologies GmbH),
téléchargé à la compilation, non redistribué dans ce dépôt. VST est une marque déposée de
Steinberg Media Technologies GmbH.
