# OMNIS — Calibration du Module 3 (oversampling, clipper, limiteur)

## 1. Fichiers

| Fichier | Format | Intégré | TP / SP | Crête L | Écrêtage |
|---|---|---|---|---|---|
| `pour_claude_-6.wav` | 16 bits / 44.1 kHz | −19.0 LUFS | −4.0 / −4.2 | 14.3 dB | aucun |
| `pour_claude_plugin_-6.wav` | 16 bits / 44.1 kHz | −18.0 LUFS | −5.6 / −7.0 | 10.6 dB | aucun |
| `Sloppy Joe - SLAYR.wav` (réf.) | 16 bits, encodeur FL Studio 21 | −6.0 LUFS | **+2.25** / −0.22 | 7.2 dB | aucun |
| `2hollis - poster boy` (réf.) | FLAC 16 bits | −3.7 LUFS | +0.74 / 0.00 | 6.2 dB | aucun |

- `pour_claude_-6` est le même mixage que l'export du module 2, baissé de 9 dB et **sans le
  traitement du master** : son facteur de crête passe de 9.4 à 14.3 dB. L'ancien master retirait donc
  ~5 dB de crêtes (c'est l'origine des 3300 plateaux d'écrêtage mesurés au module 2).
- SLAYR présente un **offset DC de −1.1 % (−39 dBFS)**, anormal pour un master commercial (re-rendu
  via FL Studio ?). Le low cut du Sub Control le supprime.
- Les deux références dépassent 0 dBTP : la pratique du genre accepte les crêtes inter-échantillons.

## 2. Validation des briques (signaux synthétiques)

| Mesure | Résultat |
|---|---|
| Demi-bande 120 dB / tw 0.02 | 12 coefficients, réjection −123 dB (prototype) |
| Aller-retour 2x … 16x | plat à ±0.001 dB jusqu'à 20 kHz |
| Images après montée 8x (sinus 10 kHz) | −124 à −219 dB |
| Anti-repliement à la descente (30 kHz généré en 8x) | < −115 dB |
| Retard de phase à 1 kHz | 4.5 (2x) / 6.6 (4x) / 7.5 (8x) / 7.9 (16x) échantillons |
| **Repliement, écrêtage dur de 6 dB d'un sinus 5 kHz** (pire cas) | **−34.8 dB (1x), −57.0 (2x), −94.4 (8x), −113.4 (16x)** |
| Clipper sous le genou (−12 dBFS) | gain 0.00 dB, produits < −100 dB |
| Dépassement du plafond en sortie de clipper | +0.05 dB (Hard, Gibbs), 0.00 dB (Soft Knee) |
| Limiteur sample peak | 0 échantillon au-dessus du plafond |
| Limiteur true peak (boucle, ISP, bruit rose, +12 dB) | −1.048 à −1.050 dBTP pour un plafond de −1.000 |

Correction apportée en cours de calibration : le bloqueur de DC était d'abord appliqué à toute la
sortie du clipper. Il faisait pencher les plateaux des graves écrêtés et créait +0.5 dB de crête
supplémentaire. Il ne traite plus que la composante Diode (la seule asymétrique), à 3 Hz et au taux
suréchantillonné.

## 3. Calibration sur tes mixes

Gain total identique (drive + gain limiteur), on déplace le travail du limiteur vers le clipper
(`pour_claude_-6`, true peak −1 dBTP) :

| Réglage | Intégré | Crête L | Kurtosis | LRA |
|---|---|---|---|---|
| limiteur seul +13 dB | −10.1 | 8.5 | +0.09 | — |
| clip 6 + lim 7 | −10.0 | 8.3 | +0.09 | — |
| clip 11 + lim 2 | −9.2 | 7.4 | +0.03 | — |
| clip 12 (Hard) + lim 1 | −9.0 | 7.3 | +0.14 | — |
| limiteur seul +17 dB | −9.1 | 7.6 | −0.35 | — |
| clip 16 (Hard) + lim 3 | −6.8 | 5.2 | −0.66 | — |

À gain total égal (13 dB), **confier le travail au clipper rapporte +1.1 LU** par rapport au
limiteur seul (−9.0 contre −10.1 LUFS), et l'écart grandit avec la poussée. Une réduction de
crête instantanée sans temps de retour ne fait pas pomper, là où le limiteur, avec son release, fait
baisser la sonie moyenne. Le kurtosis négatif (distribution « plateau ») est la signature mesurée sur
2hollis (−0.83).

Couper le sub (Sub Gain −6 dB) **fait baisser** la sonie à gains égaux : ce mix a 78 % de son
énergie sous 60 Hz et la pondération K en compte encore une bonne part. L'équilibre tonal (trop de
sub, pas assez de 1.5–5 kHz par rapport aux références) relève du Match EQ (module 4), pas du
clipper.

Les **médiums restent très dynamiques** (crête de 21 dB dans 500–2000 Hz contre 16–18 dB pour les
références) : c'est le rôle du multibande (module 4).

## 4. Les deux modes de livraison

| | Club Aggressive | Safe Audiophile |
|---|---|---|
| Clip Mode / Knee | Soft Knee 50 % | Soft Knee 50 % |
| Clip Drive | 18 dB | 12 dB |
| Limiter Gain | 4 dB | 4 dB |
| Limiter Mode / Ceiling | Sample Peak / −0.3 dBFS | True Peak / −1.0 dBTP |
| **pour_claude** | **−5.2 LUFS**, TP +1.14, PLR 6.4, kurtosis −0.93, LRA 4.4 | **−8.3 LUFS**, TP −1.05, PLR 7.3, LRA 6.5 |
| **pour_claude_plugin** | **−5.1 LUFS**, TP +1.53 | **−7.8 LUFS**, TP −1.05 |
| Références | 2hollis −3.7 / +0.74 ; SLAYR −6.0 / +2.25 | — |

Club : crêtes inter-échantillons tolérées comme dans les références. Une conversion MP3/AAC/Opus
les fera écrêter légèrement (choix artistique du genre). Safe : livrable tel quel pour le streaming ;
les plateformes baisseront de toute façon le volume (−14 LUFS sur Spotify/YouTube).

## 5. Coût et latence de la chaîne (Sub Control + Clipper 8x + Limiteur)

- Latence : **8957 échantillons à 44.1 kHz (203 ms)**, 8976 à 48 kHz, 17920 à 96 kHz.
- CPU : 116 s rendues en 8.3 s par l'outil complet, soit ~7 % d'un cœur (contre 3.9 s pour le Sub
  Control seul au module 2 : le clipper 8x et le limiteur coûtent donc ~4.4 s sur 116 s).
