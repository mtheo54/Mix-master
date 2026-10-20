# OMNIS — Protocole de test audio

Règle du projet : **aucun étage DSP n'est considéré comme terminé sans validation sur cas
extrêmes**, avec des signaux synthétiques (fournis par `omnis_testgen`) **et** de vrais fichiers
audio.

## 1. Signaux synthétiques (fournis)

`omnis_testgen kit ./kit --sr 48000` génère :

| Fichier | Mesure visée |
|---|---|
| `sweep.wav` + `sweep-inverse.wav` | Réponse impulsionnelle / fréquence / phase par déconvolution (Farina), distorsion harmonique séparée par ordre |
| `impulse.wav` | Latence exacte, pré-ringing des FIR à phase linéaire |
| `pink.wav`, `white.wav` | Réponse moyenne, somme des crossovers (doit être plate), comportement RMS |
| `steps.wav` | Attaque / release / ratio statique des compresseurs (paliers −30 / −10 dBFS à 1 kHz) |
| `drums.wav` | Boucle trap 140 BPM (808 saturée glissée, click, clap, hats) : clipper / limiteur sur matière réaliste |
| `isp.wav` | Crête inter-échantillon : SP −3.01 dBFS, TP 0.00 dBTP |
| `imd-smpte.wav`, `imd-ccif.wav` | Intermodulation, repliement (aliasing) des saturations |
| `hf-sweep.wav` | Cartographie de l'aliasing 1–20 kHz à −1 dBFS |
| `stereo.wav` | Mono / anti-phase / décorrélé : corrélateur, Mono Maker, image M/S |
| `ebu3341-1…5.wav` | Conformité du sonomètre (attendus dans `MANIFEST.txt`) |

Procédure type : `omnis_render signal.wav out.wav --set … --report`, puis `omnis_analyze`.

## 2. Fichiers réels nécessaires pour le Module 2 (crossovers, Sub Control, Mono Maker)

À fournir avant de calibrer les fréquences de coupure M/S et le synthétiseur de sous-harmoniques :

1. **3 mixes non masterisés** (WAV 24 bits ou 32 bits float, 44.1 ou 48 kHz, **sans limiteur
   ni clipper sur le bus master**, crêtes vers −6 dBFS), idéalement :
   - un titre **Digicore / Hyperpop** (type 2hollis / 55asky) ;
   - un titre **trap à 808 longue et saturée** ;
   - un titre **acoustique** (jazz, rock, pop organique) pour le mode Safe Audiophile.
2. **Stems isolés** pour au moins un des titres trap : 808 / basse seule, kick seul, voix lead
   seule. Ils servent à vérifier que le Mono Maker ne déphase pas le sub et que la coupure M/S
   ne vide pas la largeur des voix.
3. **Références masterisées** du même style (extraits de 30 à 60 s, sans perte : WAV / FLAC / AIFF,
   pas de MP3 ni de stream réencodé) — utilisées plus tard pour le Match EQ (module 4) et les
   profils de genre (module 5).
4. Le **DAW et le système** utilisés (ex. Ableton 12 / macOS 14 ARM, FL Studio 2024 / Windows 11)
   et la fréquence d'échantillonnage de travail habituelle.

Seulement des fichiers dont vous avez les droits d'usage ; ils restent hors du dépôt (`.gitignore`
exclut `audio/`).

## 3. Critères d'acceptation (rappel)

- Null-test bit-exact de la chaîne neutre (64 bits et 32 bits).
- Somme des bandes d'un crossover : plate à ±0.01 dB (LR4 : somme passe-tout, phase vérifiée).
- Aliasing des saturations / clippers à 8x : produits repliés < −100 dBFS sur `hf-sweep` à −1 dBFS.
- Limiteur : 0 échantillon inter-échantillon au-dessus du plafond sur `drums`, `isp` et les mixes réels.
- Sonie : EBU Tech 3341 cas 1–5 à ±0.1 LU.
