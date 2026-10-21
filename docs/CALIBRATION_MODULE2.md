# OMNIS — Calibration du Module 2 (Sub Control & Mono Maker)

Fichiers fournis (exports FL Studio 21, 16 bits / 44.1 kHz) :

| Fichier | Durée | Rôle |
|---|---|---|
| `pour_claude.wav` (« mix ») | 116 s | mix utilisateur |
| `pour_claude_plugin.wav` | 78 s | mix utilisateur (morceau différent : corrélation d'enveloppe 0.30) |
| `2hollis - poster boy.flac` | 121 s | référence de style (master commercial) |

Mesures : `omnis_analyze --bands` (réseau LR8 à 60 / 120 / 250 / 500 / 2000 / 6000 Hz).

## 1. Diagnostic des sources

| | mix | pour_claude_plugin | 2hollis |
|---|---|---|---|
| Sonie intégrée | −10.0 LUFS | −11.2 LUFS | **−3.7 LUFS** |
| True Peak | +0.99 dBTP | +1.08 dBTP | +0.74 dBTP |
| PLR (TP − I) | 11.0 dB | 12.3 dB | **4.4 dB** |
| LRA | 11.9 LU | 11.9 LU | 7.7 LU |
| Kurtosis (excès) | +1.1 | +0.6 | **−0.85** (distribution « plateau » = écrêtage/limitation massifs) |
| Plateaux d'écrêtage (≥ 3 éch. à ≥ 0.999) | **3288 / 3384** | 0 | 0 |
| Offset DC | −245 ppm | +190 ppm | **+3000 ppm** (−50 dBFS) |
| Énergie sous 60 Hz | **78 %** | **85 %** | 16 % |
| Corrélation sous 60 Hz (pire fenêtre de 200 ms) | −0.93 | +1.00 | −0.81 |

Lecture :

- **Le mix « pour_claude » écrête déjà numériquement** (3300 plateaux par canal à 0 dBFS) :
  l'export a été fait avec un master qui sature. Pour la suite (clipper / limiteur), il faut des
  exports **32 bits flottants** avec de la marge (crêtes vers −6 dBFS, rien sur le master).
- **Sub énorme et parfois hors phase** dans « pour_claude » : des passages à −0.93 de corrélation
  sous 60 Hz, soit une 808 partiellement annulée en mono (club, téléphone, enceinte Bluetooth).
  C'est exactement le cas que doit corriger le Mono Maker.
- « pour_claude_plugin » a un sub déjà mono (S/M −50 dB) : un traitement mono a probablement déjà
  été appliqué sur ce fichier.
- À sonie égale, les deux mixes ont **+9 à +13 dB de plus à 31–50 Hz** et **−7 à −11 dB de moins
  entre 1.5 et 5 kHz** que la référence 2hollis (courbe tiers d'octave). C'est l'objet du Match EQ
  (module 4) et du réglage Sub Gain.

## 2. Découverte majeure : l'IIR fait exploser les crêtes de cette musique

Premier prototype de l'étage en IIR : low cut Butterworth, crossover LR4 sur le Side et passe-tout
jumeau sur le Mid. C'est la méthode standard à latence nulle, qui garde M et S en phase. Les
réglages et le résultat mono étaient corrects, mais :

| True Peak | avant | IIR (« Analog ») | FIR phase linéaire (« Linear ») |
|---|---|---|---|
| mix | +0.99 | **+4.77** | **+1.58** |
| pour_claude_plugin | +1.08 | +2.47 | +1.05 |
| 2hollis | +0.74 | **+8.53** | **+1.39** |

Isolement des causes (2hollis) : avec le Mono Maker à 0 % et le low cut à 5 Hz, l'IIR monte encore
à +7.9 dBTP. Le responsable est la **rotation de phase du passe-tout grave** : sur un signal dont
le sub et les transitoires ont été écrêtés à plat, déphaser la fondamentale de la 808 (≈ −71° à
50 Hz pour un passe-tout LR4 à 120 Hz) par rapport à ses harmoniques reconstruit des crêtes que le
clipping avait aplaties.

**Décision** : le chemin par défaut est un FIR à phase linéaire. Mid et Side passent dans des
noyaux de module pur, en phase par construction. Le +0.6 dB résiduel vient du retrait légitime
d'énergie Side et sub. L'IIR reste disponible (« Sub Phase = Analog ») comme couleur assumée.

Le rendu C++ retrouve au centième les valeurs du prototype numpy écrit indépendamment
(+1.58 / +1.39 dBTP), ce qui valide la convolution partitionnée et la conception des noyaux.

## 3. Résultat avec les réglages par défaut (Linear, Mono Below 120 Hz LR4 100 %, Low Cut 16 Hz)

| Bande | mix avant → après (corr. / pire fenêtre / S-M) | 2hollis avant → après |
|---|---|---|
| 0–60 Hz | 0.95 / −0.93 / −16.3 dB → **1.00 / +0.97 / −48.5 dB** | 0.95 / −0.81 / −15.9 → **1.00 / +0.99 / −44.5** |
| 60–120 Hz | 0.87 / −0.90 / −11.7 → **0.99 / +0.81 / −25.1** | 0.89 / −0.83 / −12.2 → **0.99 / +0.19 / −22.9** |
| 120–250 Hz | 0.80 / −0.77 / −9.5 → 0.87 / −0.69 / −11.4 | 0.57 / −0.46 / −5.7 → 0.70 / −0.32 / −7.5 |
| 500–2000 Hz | inchangé | inchangé |

Facteurs de crête par bande inchangés à ±0.1 dB (forme d'onde conservée). Sonie : −0.1 LU au plus.

## 4. Choix des défauts

| Paramètre | Défaut | Justification |
|---|---|---|
| Mono Below | **120 Hz** | Au milieu de la plage 90–140 Hz du cahier des charges. Les deux mixes et la référence ont des passages hors phase jusqu'à 120–180 Hz ; 120 Hz les corrige sans toucher la largeur des voix/synthés (250 Hz et plus inchangés). |
| Mono Slope | LR4 | Transition plus douce ; LR8 disponible (fondu sans clic). |
| Mono Amount | 100 % | Le style vise le club : sub mono garanti. |
| Low Cut | **16 Hz** | Retire DC (jusqu'à 3000 ppm dans la référence) et infra-graves inutiles, à −3 dB à 16 Hz seulement : l'énergie utile à 25–31 Hz est préservée. |
| Sub Gain | 0 dB | Neutre ; les mixes ont trop de sub, mais la correction tonale relève du Match EQ. |
| Sub Synth | 0 % | Les mixes fournis ont déjà trop de sub ; l'octave basse sert pour des 808 maigres. |
| Sub Phase | **Linear** | Voir §2. |

## 5. Coût et latence

- Latence : 8704 échantillons à 44.1 kHz (197 ms), 8704 à 48 kHz (181 ms), constante. FL Studio
  la compense automatiquement (PDC).
- CPU : 116 s de mix rendues en 3.9 s avec un tampon de 256 échantillons, outil complet
  (I/O WAV, deux chemins, synthé, mesures) : environ 3.3 % d'un cœur.
- Mémoire : ~3 Mo à 44.1 kHz (3 noyaux + FDL + espace de calcul du thread).
