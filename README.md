# Abrasion Labs // OMNIS

Processeur de mixage / mastering intelligent, **VST3 natif sans framework** (ni JUCE, ni iPlug2) :
SDK Steinberg + moteur DSP C++20 en double précision, piloté à terme par IA (références audio,
prompts en langage naturel, profils de genres) et habillé d'une interface NanoVG / OpenGL
rétro-futuriste.

> **État : Modules 1 à 4 / 6 terminés, module 5 commencé** : architecture, squelette VST3,
> mesures normalisées, banc de test, filtres TPT/SVF et crossovers, **Sub Control & Mono Maker** à
> phase linéaire ([calibration](docs/CALIBRATION_MODULE2.md)), **oversampling HIIR, clipper ADAA et
> limiteur true peak** ([calibration](docs/CALIBRATION_MODULE3.md)), puis **Match EQ, adaptation à
> une référence et tenue multibande** ([calibration](docs/CALIBRATION_MODULE4.md)).

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
2. Activer **Listen Reference**, jouer le passage voulu (au moins 5 s), désactiver.
3. OMNIS a mesuré la référence et, en même temps, la prod. Pendant l'écoute, il affiche les
   secondes écoutées et la sonie de la référence (paramètres en lecture seule). Seuls des nombres
   sont gardés :
   - dans la base de la personne : `references_utilisateur.txt`, dans le dossier de données
     d'OMNIS (`%APPDATA%\Abrasion Labs\OMNIS` sous Windows) ;
   - dans la session : la référence et la prod.

Le thread audio ne fait que copier les échantillons dans deux files sans verrou. L'analyse tourne
sur son propre thread et donne exactement la signature d'une analyse de fichier (testé).

### Comprendre la demande, références, apprentissage ([détails](docs/APPRENTISSAGE.md))

- **Fautes comprises** (clé phonétique + Damerau-Levenshtein) : « propr », « crispi »,
  « jveu ke sa sonne + brilan ».
- **Nuances** : lexique en données (`data/lexique_fr.txt`, 44 concepts, ~420 expressions) et petite
  grammaire. Exemples : « ça manque de crispy », « c'est trop brillant », « c'est pas propre »,
  « pas trop fort », « garde ma 808 », « un poil », « vraiment ».
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
data/         lexique_fr.txt, references.txt, catalogue.txt (compilés dans le plug-in, éditables),
              genres_classement.txt (familles de playlists -> mots de genre)
third_party/  dr_libs (décodeurs MP3 / FLAC, domaine public)
  analysis/   pondération K, sonie BS.1770-4, true peak, stéréo, statistiques, signature spectrale
  params/     table unique des paramètres (DSP, VST3, outils, IA)
  engine/     Engine + interface Stage (chaîne, PDC, bypass, monitoring, mesures)
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
