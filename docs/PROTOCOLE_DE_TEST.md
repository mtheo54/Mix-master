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

## 2. Fichiers réels — Module 2 (reçus, voir `CALIBRATION_MODULE2.md`)

Reçus : 2 mixes (exports FL Studio 21, 16 bits / 44.1 kHz) et la référence 2hollis « poster boy ».
Ils ont fixé les défauts du Sub Control et conduit au choix de la phase linéaire.

## 3. Fichiers — Module 3 (reçus, voir `CALIBRATION_MODULE3.md`)

Reçus : les 2 mixes réexportés sans traitement de master (16 bits, crêtes −4 / −7 dBFS) et la
référence Sloppy Joe « SLAYR ». Pour le module 4 (Match EQ, multibande), les stems restent utiles.

### Rappel de la demande initiale

Le clipper et le limiteur se calibrent sur la marge réelle du mix : un export qui écrête déjà
(cas de `pour_claude.wav` : 3300 plateaux à 0 dBFS) ne permet pas de mesurer ce que fait le clipper.

1. **Les 2 mêmes morceaux réexportés depuis FL Studio** :
   - format **WAV 32 bits float** (Export → WAV → *32-bit float*), 44.1 kHz comme le projet ;
   - **rien sur le master** (Fruity Limiter / Soft Clipper désactivés), et le volume du master
     baissé jusqu'à des crêtes vers **−6 dBFS** (la crête exacte importe peu en 32 bits float) ;
   - sans dither (inutile en 32 bits float).
2. **Stems d'un des deux morceaux**, depuis ton propre projet FL (Export → *Split mixer tracks*) :
   808 / basse, kick, voix lead, et le reste regroupé (instrus). Tes stems sont préférables à
   des multipistes existants : ils correspondent au mix analysé et tu en détiens les droits.
3. **Une 2ᵉ référence** 2hollis ou du même univers (lossless), idéalement un titre au sub plus
   chargé que « poster boy » (morceau club / 808 dominante).

Seulement des fichiers dont tu as les droits d'usage ; ils restent hors du dépôt (`.gitignore`
exclut `audio/` et les formats audio).

## 4. Critères d'acceptation (rappel)

- Null-test bit-exact de la chaîne neutre (64 bits et 32 bits).
- Somme des bandes d'un crossover : plate à ±0.01 dB (LR4 : somme passe-tout, phase vérifiée).
- Aliasing des saturations / clippers à 8x : produits repliés < −100 dBFS sur `hf-sweep` à −1 dBFS.
- Limiteur : 0 échantillon inter-échantillon au-dessus du plafond sur `drums`, `isp` et les mixes réels.
- Sonie : EBU Tech 3341 cas 1–5 à ±0.1 LU.
