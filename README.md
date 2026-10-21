# Abrasion Labs // OMNIS

Processeur de mixage / mastering intelligent, **VST3 natif sans framework** (ni JUCE, ni iPlug2) :
SDK Steinberg + moteur DSP C++20 en double précision, piloté à terme par IA (références audio,
prompts en langage naturel, profils de genres) et habillé d'une interface NanoVG / OpenGL
rétro-futuriste.

> **État : Modules 1 et 2 / 6 terminés** : architecture, squelette VST3, mesures normalisées, banc
> de test, puis filtres TPT/SVF, crossovers Linkwitz-Riley et l'étage **Sub Control & Mono Maker**
> à phase linéaire, calibré sur des mixes réels ([rapport](docs/CALIBRATION_MODULE2.md)).

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
**+0.6 dB** pour un sub aussi mono. Latence constante : 197 ms à 44.1 kHz, compensée par le DAW.
Coût : ~3 % d'un cœur.

---

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
  stages/     étages de la chaîne (Sub Control & Mono Maker + banque de noyaux FIR)
  analysis/   pondération K, sonie BS.1770-4, true peak, stéréo, statistiques
  params/     table unique des paramètres (DSP, VST3, outils, IA)
  engine/     Engine + interface Stage (chaîne, PDC, bypass, monitoring, mesures)
  plugin/vst3 processeur, contrôleur, état, factory
tools/        testgen, analyze, render (+ WAV I/O, signaux, rapport)
tests/        tests unitaires sans dépendance
docs/         ARCHITECTURE.md, PROTOCOLE_DE_TEST.md, CALIBRATION_MODULE2.md
```

## Feuille de route

1. ✅ **Architecture & squelette VST3 / DSP**
2. ✅ **Crossovers & filtres** : SVF TPT (Zavalishin/Simper), Linkwitz-Riley LR4/LR8 et réseau N bandes
   à somme passe-tout, FFT et convolution partitionnée UPOLS, Sub Control & Mono Maker M/S à phase linéaire
3. Oversampling & Clipper — HIIR polyphase 8x/16x, Hard / Soft-Knee Cubic / Diode, limiteur lookahead True Peak
4. Match EQ & FFT — Blackman-Harris 8192, convolution partitionnée, profils de référence
5. Moteur IA & embeddings — ONNX Runtime C++ / RTNeural, MLP 40–60 paramètres
6. UI NanoVG — IPlugView OpenGL, oscilloscope phosphore, matrice LED, champ de prompt

Chaque module DSP critique sera calibré sur des fichiers audio réels : voir
[`docs/PROTOCOLE_DE_TEST.md`](docs/PROTOCOLE_DE_TEST.md).

## Licences

Code OMNIS : © 2026 Abrasion Labs. SDK VST3 : licence MIT (Steinberg Media Technologies GmbH),
téléchargé à la compilation, non redistribué dans ce dépôt. VST est une marque déposée de
Steinberg Media Technologies GmbH.
