# OMNIS — Rapport technique complet

*Abrasion Labs — état au 03/11/2026, branche `ccr-7529d80a-n1wygq`. Ce document s'adresse à un
ingénieur du son ou à un développeur DSP qui reprend le projet ou l'audite. Les chiffres viennent
du code et de mesures relancées pour ce rapport (sources citées à chaque section).*

---

## 0. Résumé

OMNIS est un plug-in **VST3 stéréo de mastering et de mixage** (C++20, VSTGUI), destiné à être vendu
~150 € sur https://onirique.store, et intégré à l'installateur ONIRIQUE.

**La chaîne.**
- Un rack de **7 étages réordonnables** : Sub Control → Match EQ M/S → Inflator → Imager →
  Multibande → Clipper deux bandes → Limiteur true peak.
- Traitement en double précision, phase linéaire quand c'est utile.
- Latence constante annoncée à l'hôte.

**Le pilotage.**
- Une **barre de texte en français** (« plus de punch, garde ma 808 », « comme GRY »). Elle comprend
  la demande, règle les étages et mène la sonie par la mesure.
- Tout se fait **en local, sans réseau ni service payant**.

**Les données.**
- Une **base de références** : des signatures mesurées, jamais d'audio.
- Un menu de références filtrable par **famille puis sous-genre**.

**Les outils d'écoute et de contrôle.**
- **Diagnostic du mix.**
- **Écoutes simulées** : écouteurs, téléphone, TikTok, voiture, club, plateformes, binaural.
- Un **mode travail à faible latence**.
- Une **vérification avant export**.

**Les autres fonctions.**
- Des **instances liées** : master et stems, avec un « chef d'orchestre » qui propose des retouches
  anti-masquage.
- Une interface en **deux langues**, français et anglais.

**État de la qualité.**

| Contrôle | Résultat |
|---|---|
| Tests unitaires | 206 / 206 |
| Tests plug-in | 20 / 20 |
| Validateur Steinberg | 47 / 47 |
| `ctest` (dont « aucune image livrée ») | 4 / 4 |

- La CI construit Linux, macOS universel et Windows, puis produit l'installateur Inno Setup et le
  paquet `OMNIS-pour-ONIRIQUE`.

**Ce qui n'est pas fait.**
- Aucune écoute en conditions réelles dans FL Studio par l'utilisateur sur la dernière version.
- Pas de dither.
- Signature Apple et Windows non faite.
- Système de licence non décidé.
- Provenance d'une partie des mesures à régulariser avant de les livrer (§17).

---

## 1. Chiffres clés

| Élément | Valeur |
|---|---|
| Code (source/, tools/, tests/) | ~36 400 lignes C++ / CMake, 128 fichiers dans `source/` |
| Paramètres | 144 (ids stables), dont 130 réglables, le reste en mesures en lecture seule |
| Étages | 7, ordre libre (5040 permutations, code de Lehmer) |
| Fréquences d'échantillonnage | toutes ; noyaux FIR proportionnels (résolution en Hz constante) |
| Latence, mode Qualité max | 33 812 éch. à 44,1 kHz (767 ms) · 34 104 à 48 kHz (710 ms) · 68 168 à 96 kHz (710 ms) |
| Latence, mode Travail | 4 230 éch. à 44,1 kHz (96 ms) · 4 312 à 48 kHz (90 ms) · 6 408 à 96 kHz (67 ms) |
| CPU (rendu hors ligne, 48 kHz, 1 cœur, E/S WAV comprises) | 12,6 % au repos · 15,6 % tous étages actifs · 1,3 % tous éteints |
| Base de références livrée | 426 titres mesurés (`data/references.txt`, ~0,5 Ko / titre) : les 91 d'origine + la base perso de l'utilisateur fusionnée le 04/11 (335 nouveaux, 3 remesurés) |
| Catalogue (noms, alias, Deezer) | 17 324 lignes (`data/catalogue.txt`) |
| Sous-genres | ~440 artistes classés, 180+ sous-genres distincts (`data/styles_artistes.txt`) |
| Traduction anglaise | 424 lignes (`data/i18n_en.txt`) |
| Historique | 111 commits sur la branche |

**Les mesures CPU et de latence** ont été relancées pour ce rapport avec `omnis_render` sur
`drums.wav` (kit `omnis_testgen`, 15,2 s, conteneur Linux 4 cœurs).
- Le rendu hors ligne calcule les noyaux FIR immédiatement sur le thread audio. Le temps réel les
  calcule sur un thread de fond, donc le chiffre en DAW est du même ordre ou inférieur.

---

## 2. Architecture logicielle

### 2.1 Couches

```
plugin/vst3   OmnisProcessor (audio) · OmnisController (UI, IA) · OmnisEditor (VSTGUI)   ← seul code lié au SDK
engine        Engine : rack de Stage, PDC, bypass, Delta, Gain Match, garde-fou de crête, ListenSim
stages        Sub · MatchEq · Inflator · Imager · Multiband · Clipper · Limiter
dsp           FFT · UPOLS · FirKernelBank · HIIR · Oversampler · ADAA · LookaheadGain · SVF · LR
analysis      BS.1770 · TruePeak · LRA · Stéréo · SpectralSignature · MusicDescriptors
ai            Lexique · compréhension · ConsoleActions · références · diagnostic · apprentissage
link, stems   Mémoire partagée entre instances · chaînes de stems · chef d'orchestre
core          SPSC · TripleBuffer · OnePoleSmoother · AudioBuffer · FTZ · I18n
```

- `omnis_core` ne dépend que de la bibliothèque standard.
- Les outils en ligne de commande et les tests appellent **le même code** que le DAW. Ce qui est
  mesuré au banc est exactement ce qui tourne dans le plug-in.

### 2.2 Règles temps réel (thread audio)

- **Aucune allocation**, aucun verrou, aucune entrée / sortie dans `process()`. Tout est dimensionné
  dans `prepare()`, appelé depuis `setupProcessing()`.
- **Double précision** de bout en bout. Les conversions f32 ↔ f64 se font seulement aux bords.
- **FTZ / DAZ** pendant `Engine::process()`, via `ScopedNoDenormals`.
- **Pas de `-ffast-math`** : il casserait la reproductibilité des null-tests et la gestion des NaN.
- **Entrées NaN / inf neutralisées** (`sanitizeInput`).
- **Porte entre threads** (`OmnisProcessor::GateHold`). Elle a été ajoutée après un plantage de FL
  Studio au bouton du slot : l'hôte repréparait l'instance pendant `process()`, et ASan relevait un
  use-after-free dans le clipper. Désormais, toute réallocation ferme la porte et attend la sortie de
  `process()`.
  - Le correctif est vérifié sous ASan et TSan (`tests/TestPluginSlotToggle.cpp`).

### 2.3 Communication entre threads

| Lien | Mécanisme |
|---|---|
| Hôte → DSP | Files d'automation VST3. Bloc découpé aux offsets exacts (2048 points max par bloc). |
| DSP → contrôleur | Paramètres de sortie (~30 Hz) et `UiFeed` : un triple tampon pour les mesures, une SPSC pour le spectre et le goniomètre. |
| Thread audio → thread de calcul FIR | `TripleBuffer<Request>`. |
| Calcul → DSP | `FirKernelBank` : 3 emplacements atomiques (`Free → Writing → Ready → Active → Retiring`), fondu d'une trame. |
| IA → paramètres | `beginEdit / performEdit / endEdit` : visible, automatisable et annulable dans le DAW. |
| Instances entre elles | Mémoire partagée nommée, atomiques sans verrou et seqlock (`link/LinkBus.h`). |

### 2.4 Latence (PDC)

- Latence = **somme des latences des étages, constante pour un taux donné**.
- Chaque étage annonce son **pire cas** (16× de suréchantillonnage, plus long noyau) et complète par
  un retard.
  - Raison : un changement de latence en lecture impose `restartComponent(kLatencyChanged)`, que les
    hôtes gèrent mal (coupure, reconstruction du graphe).
  - Seul le passage volontaire en mode Travail change la latence, et il prévient l'hôte.
- Le chemin sec est retardé d'autant : bypass, Delta et modules éteints restent alignés à
  l'échantillon.
  - Test : bypass basculé tous les 3 blocs → erreur < 1e-15.

### 2.5 État et automation

- **État binaire versionné** (`'OMNS'`, version, paires id → valeur normalisée). Les ids inconnus
  sont ignorés et les absents reprennent leur défaut : presets compatibles dans les deux sens.
- **Formatage en locale « C » forcée** : un hôte en locale française ne transforme pas `-3.5` en
  `-3,5`.
- **Paramètres non gardés dans la session** : `delta`, `gain_match` et `listen` (écoute simulée).
  Ce sont des outils d'écoute, ils ne doivent jamais fausser un export.
- **Mode de latence** (`low_latency`) : gardé dans la session, mais non automatisable.

### 2.6 Temps réel / hors ligne

- **Temps réel** : les noyaux FIR (jusqu'à 16 384 taps, FFT 32 768 points) sont calculés sur un
  thread de fond. Le thread audio ne fait que des compare-exchange.
- **Hors ligne** (`kOffline`, outils, tests) : calcul immédiat, rendu **déterministe**, identique au
  bit près quel que soit le découpage en blocs.
- **Mode Travail** : **ignoré en rendu hors ligne**. L'export se fait toujours en Qualité max
  (`OmnisProcessor::wantsLowLatency()`).

---

## 3. La chaîne, étage par étage

Ordre par défaut : celui ci-dessous.

**Garanties communes à tous les étages.**
- Réglages neutres = entrée retardée, **au bit près** ou à < 1e-12 (null-tests).
- Interrupteur ON / OFF avec un fondu de 20 ms. Éteint, l'étage rend son entrée retardée de sa
  propre latence, donc la latence totale ne bouge pas.

### 3.1 Sub Control & Mono Maker (`stages/SubControlStage`)

**Rôle :** couper l'infra, mettre le bas en mono, doser le sub.
- **Low Cut** 5–60 Hz (défaut 16 Hz).
- **Mono Below** 20–300 Hz (défaut 120 Hz), **Mono Amount** 0–100 %, **Mono Slope** 24 / 48 dB/oct.
- **Sub Gain** −12 / +6 dB autour de **Sub Freq** 30–150 Hz.
- **Sub Synth** 0–100 % : sous-harmonique synthétisée, gain π/4·1,429.
- **Sub Phase** : Linear / Analog.

**Implémentation.**
- Le filtre est un FIR **à phase linéaire de 16 384 taps** à 44,1 / 48 kHz, soit ~2,7 Hz de
  résolution, nécessaire pour un low cut propre à 16 Hz. Il passe à 32 768 jusqu'à 100 kHz, 65 536
  jusqu'à 200 kHz, et ainsi de suite.
- Convolution UPOLS par blocs de N/32. Le noyau est recalculé à chaque changement de réglage, sur le
  thread de fond.
- **Mode Analog** : IIR TPT à latence nulle.
  - Low cut Butterworth d'ordre 4 sur L / R.
  - Crossover LR4 / LR8 sur le Side, avec un passe-tout jumeau sur le Mid.
  - Plateau SVF pour le sub.
  - Caractère « console » (rotation de phase du grave), retardé pour garder la latence constante.
- **Pourquoi la phase linéaire par défaut** : sur la référence 2hollis, déjà écrêtée, la version IIR
  fait monter le true peak de +7,8 dB, contre +0,6 dB en phase linéaire, pour le même résultat
  mono.
- **Latence :** N/2 + une partition, soit 8 704 échantillons à 48 kHz.

### 3.2 Match EQ M/S à phase linéaire (`stages/MatchEqStage`, `MatchKernelBank`)

**Rôle :** rapprocher l'équilibre tonal d'une référence, séparément en Mid et en Side.
- **21 bandes par canal M/S**, espacées d'une demi-octave de 20 Hz à 20,48 kHz, chacune ±12 dB.
- **Match Amount** 0–100 % et **Tilt** ±6 dB.
- Les 43 paramètres `match_*` sont écrits par l'IA (`ai::planMatch` / `planReference`), ou à la main.

**Implémentation.**
- Courbe interpolée entre les nœuds, puis FIR **16 384 taps** à phase linéaire (même longueur que le
  Sub), via `LinearPhaseDesigner` (réponse en module → FFT inverse → fenêtrage).
- **Latence :** 8 704 échantillons à 48 kHz.
- **Garde-fous de l'intention** (`ai/`) :
  - la courbe est bornée ;
  - la sonie de la référence est bornée à −14…−5 LUFS ;
  - le sub de la prod est protégé (« garde tonale ») : la 808 est l'identité des prods de
    l'utilisateur, on ne la sacrifie pas pour ressembler.

### 3.3 Inflator (`stages/InflatorStage`)

**Rôle :** densité et chaleur sans monter les crêtes.

**Courbe**, impaire, avec g(1) = 1 et g'(1) = 0 :

    g(u) = u + (u − u^m) / (m − 1)

- La pente à l'origine vaut m/(m−1) : de +6 dB (m = 2, Curve −50) à +1,6 dB (m = 6, Curve +50).
- **Warmth** : décentrage f(x + b) − f(b). Il crée des harmoniques paires ; le continu est retiré
  par un bloqueur à 5 Hz.

**Deux bandes indépendantes** : la 808, sous **Split** (60–300 Hz, défaut 120 Hz), et le reste.
- Réglages : **Inflate 808** et **Inflate Mix** 0–100 %, Drive ±12 dB, Curve ±50, Output.
- La séparation est à phase linéaire complémentaire : haut = entrée retardée − bas. Elle est exacte
  par construction.

**Structure parallèle exacte au repos :**

    sortie = sec + Σ effet·[(k0 − 1)·bande + harmoniques(bande)]

- Seule la partie **non linéaire** passe par le suréchantillonnage (1×…16×, défaut 8×). Le signal
  d'origine ne traverse jamais les filtres d'oversampling.

**Latence :** séparation FIR 4 096 (2 176 éch.), plus le retard de phase de l'oversampling 16×.

### 3.4 Imager (`stages/ImagerStage`)

**Rôle :** largeur stéréo multibande.
- **4 bandes**, coupures 250 / 2 000 / 8 000 Hz (réglables), largeur de chaque bande 0–200 %.
- Le Side est pondéré en fréquence par une courbe de largeur à transitions en cosinus surélevé
  (±1/3 d'octave), appliquée en **FIR M/S de 4 096 taps** à phase linéaire.
- Les bandes commencent au-dessus de 100 Hz : 12 Hz de résolution suffisent.
- **Latence :** 2 176 éch. à 48 kHz.

### 3.5 Multibande (`stages/MultibandStage`)

**Rôle :** tenir la 808 et le mix dans le temps (« glue »).
- **4 bandes** : Sub / Low / Mid / High.
- **Coupures** 120 / 800 / 5 000 Hz par défaut, en séparation **à phase linéaire FIR 8 192**
  (~5 Hz de résolution).
- **Par bande** : seuil, ratio 1–20:1, attaque 1–300 ms, relâchement 10–3 000 ms, makeup ±12 dB.
- **Genou doux** de 6 dB.

**Détecteur.**
- RMS sur 50 ms pour le Sub (une période d'un 20 Hz), 20 ms pour les autres bandes.
- Le gain requis passe par un `LookaheadGain` : anticipation de 10 ms, minimum glissant puis
  moyenne. L'attaque est donc lisse, sans dépassement.

**Latence :** N/2 + B de la séparation, plus l'anticipation, plus la fenêtre RMS la plus longue.

### 3.6 Clipper deux bandes (`stages/ClipperStage`)

**Rôle :** écrêtage de mastering propre, la 808 traitée à part.

**Pourquoi deux bandes.** Mesuré sur les mixes de l'utilisateur (`docs/CALIBRATION_MODULE3.md` §6) :
- un clipper large bande écrête la 808, qui porte 78 % de l'énergie ;
- la voix et les snares qui « roulent » sur sa forme d'onde sont alors intermodulées, avec +8 dB de
  distorsion dans les médiums.

**Séparation.**
- Bande sub / reste, en LR à phase linéaire complémentaire. **Split** 60–300 Hz, 120 Hz par défaut.

**Traitement de chaque bande.**
- Suréchantillonnage 1×…16× (défaut 8×), HIIR à phase minimale : aucun pré-écho avant l'écrêtage.
- Courbes **Hard / Soft Knee / Diode** (fondues sans clic), avec **ADAA du 1er ordre**
  (Parker–Zavalishin–Le Bihan 2016).
  - Moyenne exacte de f sur [u[n−1], u[n]].
  - Repliement réduit de ~20 dB à suréchantillonnage égal.
  - Coût : un demi-échantillon de retard au taux suréchantillonné, et −0,14 dB à 20 kHz en 8×.
- Diode asymétrique (r = 1,25, asymétrie 1,9 dB) : harmoniques paires.

**Retrait du continu.**
- **Diode** : seul l'offset de la composante asymétrique est retiré. Un passe-haut sur tout le
  signal ferait pencher les plateaux des graves écrêtés et monterait les crêtes.
- **Résidu** : sa moyenne est retirée par deux pôles à 0,7 Hz (`kResidualDcHz`).
  - Un passe-haut du 1er ordre faisait dépasser la crête de +1 dB : mesuré et corrigé.

**Réglages.**
- **Clip Drive** 0–24 dB, **Ceiling** −12…0 dBFS (défaut 0), **Knee** 0–100 %.
- **Sub Drive** ±12 dB : il pousse la 808 seule.

**Sub Make-Room** (désactivé par défaut).
- Il impose |g·bas + haut| ≤ plafond en baissant **uniquement** le gain de la bande sub, avec une
  anticipation de 5 ms et un release de 80 ms.
- Il est **borné à −6 dB** (`kGuardFloor`). Sans borne, la 808 perdait 14 dB : version V2, retour
  « plus de sub ».

**Garde-fou de crête en bout de chaîne** (`Stage::limitsPeaks` / `setPeakGuard`).
- Il s'active quand aucun étage allumé après le clipper ne garantit la crête, c'est-à-dire limiteur
  éteint ou placé avant. Il combine trois mécanismes :
  - le plancher de partage de marge ;
  - un gain d'ensemble à anticipation (`guard_`, LookaheadGain) ;
  - une butée finale.
- Il corrige le rebond de la redescente de l'oversampling, mesuré jusqu'à +2 dB avant correction.

### 3.7 Limiteur true peak (`stages/LimiterStage`)

**Calculateur de gain à garantie brickwall** (min-hold + moyenne), en six étapes :
1. **Détection true peak** liée stéréo : interpolation ×4, même FIR Kaiser que le TruePeakMeter
   (β = 7, 48 taps par phase), retard D = 24 éch.
2. Gain requis r[n] = min(1, plafond / crête[n]).
3. **Attaque instantanée** et remontée exponentielle. **Auto Release** : a_rel = a_base / (1 +
   4·amount·activité), où l'activité est la part du temps en réduction (τ = 500 ms). Une crête isolée
   relâche vite ; une limitation soutenue (808 longue) relâche jusqu'à 5× plus lentement, ce qui évite
   de moduler les médiums au rythme du sub.
4. Minimum glissant sur L + 1 (L = lookahead de 10 ms).
5. Moyenne rectangulaire sur L + 1 : la rampe d'attaque dure exactement L, sans clic.
6. Gain appliqué au signal retardé de L + D. La garantie est démontrée dans l'en-tête ; une marge de
   0,05 dB est prise pour l'inter-échantillon.

**Étages optionnels avant la détection** (neutres à 0, au bit près).
- **Punch** :
  - suiveurs rapide (0,3 / 15 ms) et lent (20 / 120 ms), maintenus 25 ms ;
  - gain = (rapide/lent)^(2,5·Punch), borné à ±9 dB ;
  - une 808 tenue n'est pas modulée : distorsion < −60 dB sur un 50 Hz ;
  - Punch 100 % donne +1 dB de PLR sur une boucle de drums à +9 dB. C'était le relief qu'Ozone avait
    en plus.
- **Upward** 0–12 dB : les passages calmes montent selon leur RMS sous le plafond.
  - Aucun gain à moins de 12 dB du plafond, tout le gain à 36 dB dessous, rien sous −70 dB.
- **Unlink** séparé pour les transitoires et le soutenu. Chaque canal reçoit r·(r_c/r)^u ≤ r_c : la
  garantie tient canal par canal.

**Réglages :**
- **Gain** 0–24 dB ;
- **Ceiling** −6…0 dB (défaut −1 dBTP) ;
- **Release** 1–1 000 ms (défaut 60 ms) ;
- **Auto Release** (défaut 100 %).

**Latence :** L + D = 480 + 24 éch. à 48 kHz.

**Limite connue :** à réglages extrêmes (gain ≥ 18 dB sur des transitoires très denses), le true
peak de sortie peut dépasser l'idéal de quelques dixièmes de dB, jusqu'à ~0,5 dB dans le pire cas
torturé. La marge de 0,05 dB couvre les cas musicaux mesurés.

### 3.8 Ce qui n'existe pas encore dans la chaîne

- **Dither TPDF / noise-shaping** : prévu (8ᵉ étage), non implémenté. Pour un export 16 bits, il
  faut aujourd'hui le dither du DAW.
- EQ dynamique par bande dans le rack master, et anti-résonances spectral : non implémentés.

---

## 4. Briques DSP (`source/dsp/`)

| Brique | Détails |
|---|---|
| `Fft` | Radix-2 complexe, double précision, sans dépendance. |
| `StereoConvolver` | Convolution FFT partitionnée uniforme (UPOLS), 2 canaux, noyau échangeable à la frontière de trame. Test : identique à la convolution directe. |
| `LinearPhaseDesigner` | Réponse en module → FIR symétrique (FFT inverse, fenêtrage). |
| `FirKernelBank<Request, Designer>` | Calcul des noyaux sur thread de fond, 3 emplacements atomiques, fondu d'une trame. Test « le thread de calcul applique un nouveau noyau sans bloquer l'audio ». |
| `Halfband` / `Oversampler` | HIIR (méthode L. de Soras), polyphase IIR, phase minimale. Étage 1 : 12 coefficients, transition 0,02, bande passante 0,46·fs, rejet 120 dB. Étages suivants : ~6 / ~4 / ~3 coefficients. Rejet ultrasonore mesuré > 115 dB à la descente. Facteur changeable en lecture sans allocation. |
| `Waveshaper` | Hard / Soft Knee (C¹) / Diode, avec leurs antidérivées pour l'ADAA. |
| `LookaheadGain` | Minimum glissant + moyenne glissante. Garantie « jamais au-dessus du gain requis ». |
| `SvfFilter` | TPT SVF (Zavalishin) : LP / HP / BP / cloche / plateaux, Q Butterworth. |
| `Crossover` | Linkwitz-Riley LR4 / LR8 et réseau N bandes avec passe-tout de compensation (stems, sans latence). |

---

## 5. Le moteur (`engine/Engine`)

- **Rack et ordre** :
  - `chain_order` 0…5039 code une permutation des 7 modules ;
  - changement d'ordre : fondu vers le sec, permutation, attente d'une latence complète, retour en
    30 ms.
- **Modules entièrement éteints, sautés** (optimisation CPU) :
  - un module éteint et sorti de son fondu n'est plus calculé (`cold`) ;
  - rallumé, il recalcule `warmup` échantillons (filtres pleins) avant de fondre ;
  - tests : rallumage, et convergence < 1e-5 après 7 s (mémoire lente du limiteur).
- **Bypass** : fondu de 20 ms ; les étages continuent de tourner, aucun transitoire au
  réengagement.
- **Delta** : on n'entend que ce que change un module, soit sa sortie moins son entrée retardée.
  Le signal reste retardé de la latence des modules qui suivent, donc calé.
- **Gain Match** : la sortie écoutée est ramenée à la sonie court terme de l'entrée, lissée sur
  ~1,5 s. Écoute seulement.
- **Monitoring** Stéréo / Mid / Side, lissé sur 15 ms. **Les mesures portent toujours sur la vraie
  sortie**, avant le monitoring et avant l'écoute simulée.
- **Mode Travail** (`low_latency`) :
  - noyaux FIR 16× plus courts (1 024 au minimum) ;
  - lookahead du limiteur et du multibande réduit à 3 ms ;
  - fenêtres RMS ≤ 10 ms ;
  - environ 90 ms au lieu de 710 ms à 48 kHz ;
  - le grave est moins précis : moins de résolution sous ~50 Hz pour le low cut et la séparation de
    la 808 ;
  - **l'écran LAT clignote en rouge « TRAVAIL »** tant qu'il est actif ;
  - l'export repasse automatiquement en Qualité max.

---

## 6. Mesures (`source/analysis/`)

- **Sonie** ITU-R BS.1770-4 / EBU R128 :
  - pondération K recalculée par transformée bilinéaire pour tout taux ;
  - sous-blocs de 100 ms ; momentary 400 ms, short-term 3 s ;
  - intégrée par un histogramme de 8 000 cases de 0,01 LU, qui gardent la somme exacte des
    puissances : mémoire bornée sur une session de plusieurs heures ;
  - **LRA** EBU 3342.
  - Conformité testée sur les signaux EBU 3341 (1 à 5) et 3342.
- **True Peak** BS.1770 annexe 2 :
  - ×4, sinc fenêtré Kaiser (β = 7, 48 taps par phase), phase 0 identité exacte (TP ≥ SP par
    construction) ;
  - plat à ±0,01 dB jusqu'à 0,454·fs.
- **Stéréo** : corrélation dynamique et rapport Side / Mid ; corrélation **sous 120 Hz** pour le
  diagnostic.
- **SpectralSignature** : spectre Mid / Side moyen sur 21 nœuds, dynamique par bande, coupure haute
  détectée (référence lossy). L'intro calme est exclue du « cœur » du morceau.
- **MusicDescriptors** (« fiche ») : tempo, tonalité, note de la 808, attaques, crête / sonie, PLR,
  largeur, perte en mono. Seulement des nombres.

---

## 7. Écoutes simulées (`engine/ListenSim`)

**18 préréglages + Off**, menu ÉCOUTE (affiché en rouge quand une écoute est active) :
- **écouteurs et casques** : AirPods 2/3, AirPods Pro 2, AirPods Max, Beats Studio Pro, Sony
  WH-1000XM5, Galaxy Buds2 Pro, JBL Tune 510BT, Sennheiser HD 600, Audio-Technica M50x ;
- **haut-parleurs et lieux** : haut-parleur de téléphone, TikTok sur téléphone, voiture, sono de club ;
- **Spatial binaural** ;
- **plateformes**, normalisation de sonie : Spotify −14, Deezer −15, YouTube −14, Apple Music −16
  LUFS.

**Implémentation.**
- Biquads « cookbook » : quelques filtres par modèle, pour le bas, les médiums et les aigus.
- **Téléphone** : passe-haut haut placé, bande étroite, mono.
- **TikTok** : la recette du téléphone, avec une légère spatialisation (image un peu élargie, pièce
  discrète) et la normalisation à −14 LUFS. Le codec n'est pas simulé.
- Binaural : deux haut-parleurs virtuels à ±30°, avec le retard et l'ombre de la tête sur l'oreille
  opposée, plus les premières réflexions d'une pièce.

**Ce que c'est, honnêtement.**
- Ce sont des **approximations d'OMNIS** établies d'après les caractéristiques publiées. **Ce ne sont
  pas des mesures**, aucun fabricant n'y est associé, et les marques ne servent qu'à désigner les
  modèles (`LICENSE_NOTICES.md`).
- Le binaural n'est **ni Dolby Atmos ni le rendu d'une marque**.
- Les cibles des plateformes sont les valeurs couramment rapportées, et elles peuvent changer.

**Garanties.**
- Jamais appliqué au rendu hors ligne.
- Jamais gardé dans la session.
- Jamais pris en compte par les mesures.
- Signalé par la vérification avant export.

---

## 8. L'intelligence (`source/ai/`, `OmnisController::ask`)

### 8.1 Comprendre la demande

- **Lexique en données** (`data/lexique_fr.txt`) : 89 concepts. Chaque concept est soit une
  `qualite` (ce qu'on veut : « plus brillant », « crispy »), soit un `defaut` (ce dont on se plaint :
  « c'est boueux »). Les axes et valeurs y sont associés ; pour un défaut, la correction appliquée est
  l'inverse.
- **Argot / SMS / TikTok** (`data/argot.txt`, 95 lignes).
- **Robustesse** : fautes de frappe, nuances (« un poil », « beaucoup »), négations.
  - « la 808 est trop dirty » : la 808 dit seulement *où* agir.
  - « la 808 est trop forte » la baisse toujours, « plus de 808 » la monte toujours (tests).
- **Intentions spécialisées** : `MatchIntent` (« comme X »), `DynamicsIntent`, `ConsoleActions`. La
  demande comprise devient des réglages du Match EQ, de Punch, d'upward + glue, de l'Inflator, du
  clip et de l'Imager, plus une sonie cible.
- **Sonie menée par la mesure** : le contrôleur pousse le gain du limiteur pas à pas, en lisant la
  sonie court terme de la sortie, jusqu'à la cible ±0,4 LU.

### 8.2 AUTO et l'écoute de la prod

- `ProdListener` écoute l'entrée du master pendant la lecture : signature seulement, jamais d'audio.
- AUTO prend les 10 s les plus fortes jouées et vise :
  - la référence choisie dans le menu RÉF ;
  - sinon le style deviné des mots ;
  - sinon la moyenne de la base.
- **Choix d'une référence** (`planReference`) :
  - courbe du Match EQ vers la moyenne des titres choisis ;
  - `applyMusic` sur la fiche ;
  - sonie bornée à −14…−5 LUFS ;
  - si la prod n'a pas encore été entendue 8 s, OMNIS attend la lecture.

### 8.3 Menu RÉF et filtre de genres (nouveau)

- **Contenu** : la base livrée plus `references_utilisateur.txt` du dossier de l'utilisateur, titres
  mesurés seulement.
- **Navigation** : Styles → style → artiste → titres, ou Artistes A–Z → titres ; 30 par colonne.
- **Filtre en entonnoir à deux niveaux** :
  - **famille** de l'utilisateur (`data/genres_classement.txt`, 7 familles + « Autres ») ;
  - puis **sous-genre précis** (`data/styles_artistes.txt`, embarqué) ;
  - exemple : *Rap Français & New Wave › plugg fr (12)*. « Toute la famille » est en tête de chaque
    sous-menu. Le menu RÉF affiche « Filtre : famille › style ».
- **Priorité de l'utilisateur** : les sous-genres **s'ajoutent** à son classement, ils ne le
  remplacent jamais. Une correction se fait dans `styles_artistes.txt` (artiste) ou
  `titres_genres.txt` (titre).
- **Co-crédits** : « GRy, iNESS M », « A x B », « A feat. B ». Chaque artiste connu apporte ses
  sous-genres.
- **Code** : `referenceFilters()` (familles + sous-genres comptés), `referenceTree(famille, style)`.
- **Test** : chaque sous-genre restreint bien sa famille.
- **Sous-genres couverts, entre autres** :
  - rap : new wave fr, plugg fr, rage fr, drill fr, ny drill, uk drill, chicago drill, opium,
    pluggnb, cloud rap, boom bap (fr / us), g-funk, jazz rap, emo rap, dark trap, psyché trap,
    hard clipping, digicore ;
  - électro : french touch, électro house, gabber, hardstyle, phonk, drift phonk, uk garage ;
  - hyperpop (fr) ;
  - rock et metal : nu-metal, shoegaze ;
  - B.O. : ost jeu vidéo, ost anime ;
  - et d'autres.
- **127 artistes restent « à classer »** (annexe B) : surtout l'underground de la liste 500, que je
  préfère ne pas classer au hasard.

### 8.4 Diagnostic du mix (`ai/MixDiagnosis`, page DIAGNOSTIC)

- **Ce qui est comparé** : la prod entendue, face à la référence choisie ou, à défaut, à la moyenne
  de la base mesurée.
- **Trois niveaux** : *Important*, *Conseil*, *Info*.
- **Contrôles Important** :
  - le mix touche déjà 0 dBFS (avec le pourcentage d'échantillons écrêtés) ;
  - mix déjà très fort et écrasé (LUFS et PLR) ;
  - grave non mono (corrélation sous 120 Hz).
- **Contrôles Conseil** :
  - perte en mono ;
  - sub / 808 trop fort ou manquant face à la référence ;
  - bas-médium qui s'accumule (250–450 Hz) ;
  - médiums 1–2 kHz, aigus 3 kHz et plus ;
  - présence 4–6 kHz ;
  - attaque du grave (punch).
- **Contrôles Info** :
  - air au-dessus de 10 kHz ;
  - note tenue par la 808.
- **Choix assumés** : le diagnostic dit ce qu'il voit et ce qu'OMNIS peut faire. Il ne corrige rien
  sans qu'on le demande. Certains « défauts » sont voulus (saturation, sub énorme), et c'est
  l'utilisateur qui tranche.

### 8.5 Sécurité de l'usage

- **« Annule »** : historique des actions de l'IA, 20 pas (`rememberForUndo` / `undoLast`).
- **« Remets tout à zéro »** : tous les réglages reviennent à leur défaut.
- **Vérification avant export** (`exportProblems()`, bandeau dans l'analyseur) :
  - « PRÊT À EXPORTER » ou « PAS ENCORE PRÊT » ;
  - signale Monitor Mid / Side, Delta, Gain Match, mode Travail, écoute simulée, bypass ;
  - signale un true peak > 0 dBTP (distorsion au décodage) ou > −1 dBTP (conseil streaming) ;
  - signale une sonie > −6 LUFS.
- **`/fbi`** : mode promo (caché).

### 8.6 Apprentissage local

- Les retours de l'utilisateur ajustent ses préférences, dans son dossier de données.
- Un export **anonyme** est prévu pour une mise à jour globale (`docs/APPRENTISSAGE.md`).
- Le canal de remontée dépend de ce que permet ONIRIQUE : question ouverte.

---

### 8.7 Adaptation dynamique et harmonique par la mesure (`ai/ReferenceDynamics`)

**Principe.** Quand une référence est choisie (menu RÉF, « comme X », AUTO), la topologie des
étages non linéaires et leurs constantes de temps découlent de l'**écart mesuré entre la prod
entendue et la référence**. Il n'y a aucune table par genre : les genres ne servent qu'à filtrer le
menu RÉF (§8.3) et de repli pour AUTO sans référence (§8.2).
- Aucun paramètre ajouté : les 144 paramètres et l'état `'OMNS'` sont inchangés.
- Aucun algorithme DSP touché.
- Appelé par `planReference`, `planAuto` (titres choisis ou repli) et `planRequest` (demande qui
  cite une référence). Dans ce dernier cas, les mots de la demande (`applyAxes`) passent après et
  l'emportent.

**Audit avant ce travail** (ce que `planReference` / `applyMusic` exploitaient) :

| Paramètre | Avant | Maintenant |
|---|---|---|
| `sub_phase` | ignoré | PLR / sonie de la réf |
| `clip_mode`, `clip_knee` | ignorés | « mur » de la réf + écart de PLR |
| `clip_sub_room`, `clip_sub_drive` | ignorés | part du sub de la prod, écart de sub |
| `clip_drive` | +3 dB si la réf est écrêtée à > 0,5 % | inchangé |
| `infl_low`, `infl_curve` | ignorés (seulement par les mots « saturation ») | 808 qui plafonne face à une réf dense |
| `infl_warmth` | ignoré | bas-médium de la réf plus dense et stable |
| `mb_*` | ignorés (seulement « densité », même réglage sur les 4 bandes) | dynamique des nœuds par bande, micro-dynamique, tempo |
| `lim_release`, `lim_auto_release` | ignorés | tempo, « pompe », sub tenu |
| `lim_unlink_tr` | ignoré | largeur et dynamique des côtés de la réf |
| `lim_punch`, `lim_upward`, mono du grave | exploités (micro-dynamique, sections, corrélation du grave) | inchangé |

Mesures de la base qui ne servaient pas au réglage avant ce travail :
- `dyn` (P90 − P10 de chacun des 21 nœuds) ;
- `plr` ;
- `lra` ;
- `bpm` et `note_808`, qui n'étaient qu'affichés.

**Notation.**

| Grandeur | Définition |
|---|---|
| PLRr, PLRp | PLR de la réf, PLR de la prod (true peak − sonie intégrée) |
| ΔPLR | PLRp − PLRr |
| Sp[b], Sr[b] | dynamique (P90 − P10 des trames de 0,37 s) moyennée sur les nœuds de la bande b. Bandes selon les coupures courantes du multibande ; nœuds 20 et 28 Hz exclus. |
| μ[b] | micro-dynamique de la fiche (crête 10 ms − niveau 400 ms) par zone |
| s | part de l'énergie Mid sous la coupure du clipper : rapport de puissances, indépendant du niveau |
| rampe(x, a, b) | vaut 0 en a et 1 en b, linéaire et bornée entre les deux |

**Grandeurs de base.**
- mur = rampe(PLRr, 8,5 → 6,5) : 0 pour une réf aérée, 1 pour un mur.
- écart = rampe(ΔPLR, 1 → 3) : la part de l'écart de PLR qui reste à combler.

**Règles.**

| Cible | Règle |
|---|---|
| `sub_phase` | Linear si PLRr < 7,5 ou LUFSr > −7,5. Analog si PLRr > 9,5, PLRp > 9,5 et μr[sub] ≥ 4,5 dB. Sinon inchangé. |
| `clip_mode` | Soft Knee si PLRr ≥ 8,5. Sinon Hard (Diode si le bas-médium relatif de la réf dépasse celui de la prod de 1,5 dB) quand mur = 1 et ΔPLR > 3 ; Soft Knee avant. |
| `clip_knee` | 50 + 30·rampe(PLRr, 8,5 → 11,5) au-dessus de 8,5, sinon 50·(1 − mur). Continu en 8,5. |
| `clip_sub_room` | 100·mur·écart·rampe(s_prod, 0,65 → 0,80). Vaut 0 si PLRr ≥ 8,5. |
| `clip_sub_drive` | mur·écart·min(6, ½·\|écart de sub\|) dB. L'écart de sub est l'énergie sous 80 Hz relative au corps, prod − réf. |
| `mb_<b>_ratio` | r = Sp / max(2Sr − Sp, Sp/4), borné à 4:1 (3:1 pour le sub). r = 1 si Sp ≤ Sr. |
| `mb_<b>_thr` | niveau moyen estimé de la bande : LUFSp + 10·log10(part de la bande). |
| `mb_<b>_makeup` | (1 − 1/r)·Sp/4, borné à 6 dB. |
| `mb_<b>_attack` | gain = μr − μp. Si gain ≥ 0 : a0 + (80 − a0)·rampe(gain, 0 → 4). Sinon : a0·(1 − ½·rampe(−gain, 0 → 4)). a0 = défaut de la bande. |
| `mb_<b>_release` | 60000/BPM/2 (croche) pour le sub et le bas, /4 (double-croche) au-dessus. BPM de la prod, sinon celui de la réf. |
| `lim_release` | double-croche + (croche − double-croche)·pompe, borné à 20–400 ms. |
| `lim_auto_release` | 100 − 80·pompe·(1 − sub tenu). Donne 20 % pour une réf qui pompe, 100 % pour un sub tenu. |
| `infl_low` | 60·k, avec k = rampe(s_prod, 0,65 → 0,80)·écart·rampe(LUFSr, −10 → −7)·rampe(s_réf, 0,50 → 0,70). Écrit seulement si k > 0,05, jamais en dessous de la valeur courante. |
| `infl_curve` | −30·k : la pente vers −50, c'est-à-dire une courbe plus dense. Même condition que `infl_low`. |
| `infl_warmth` | 40·rampe(écart de niveau 226–640 Hz, 0 → 3 dB)·rampe(écart de dynamique, 0 → 3 dB). Écrit seulement si > 0,05. |
| `lim_unlink_tr` | 70·rampe(Side − Mid au-dessus de 5 kHz, −12 → −6 dB)·rampe(μr[aigu], 6 → 9 dB). |

**Deux grandeurs composées.**
- **Pompe** = rampe(`pompe` de la fiche, 1,5 → 3,5 dB). C'est une mesure calée sur le tempo
  (`MusicDescriptors`, 04/11) : l'enveloppe de 10 ms du bas-médium et du médium est repliée sur la
  période du temps (et sur sa moitié et son double). L'origine est placée au maximum des attaques
  repliées. La mesure vaut le niveau en fin de temps (65–95 %) moins celui du début (8–30 %).
  - Un sidechain creuse le début et remonte : la valeur est positive.
  - Un kick ou une 808 qui décroît donne l'inverse : la valeur est négative.
  - Repères : prods trap de l'utilisateur −3,5 à +1,1 dB ; *poster boy* +2,8 (son limiteur écrase
    chaque coup) ; boucle house avec une nappe en sidechain −6 dB : +2,3 ; −16 dB : +4,5.
  - Fiche ancienne, sans la clé : on garde l'indice d'avant, rampe((Sr[bas] + Sr[médium])/2 − LRAr,
    8 → 12).
- **Sub tenu** = rampe(Sr[sub], 20 → 14 dB), à condition que la réf ait une note de 808 tenue.

**Pourquoi r = Sp / (2Sr − Sp).** Le seuil est posé au niveau moyen de la bande, et la moitié haute
de sa dynamique (Sp/2) est divisée par r. Pour que la dynamique résultante vaille celle de la réf :

    Sp/2 + Sp/(2r) = Sr

**Topologie et couleur ne s'écrivent pas pareil.**
- **Topologie** (phase, clipper, multibande, release, lien) : valeurs **absolues**. Rechoisir une
  référence ne cumule pas.
- **Couleur** (Inflator) : écrite seulement si la mesure la demande, et jamais en dessous de la
  valeur courante. Un réglage fait à la main n'est pas effacé par le choix d'une référence.

**Calibration des ancrages sur la base mesurée** (429 titres : 91 livrés + 338 de la base perso).

| Mesure | p10 | p50 | p90 |
|---|---|---|---|
| PLR | 8,3 | 10,3 | 13,3 dB |
| LUFS | −13,6 | −9,6 | −7,5 |
| part du sub (< 120 Hz) | 0,38 | 0,67 | 0,86 |
| Sr[sub] | 12,6 | 26,9 | 40,7 dB |
| Side − Mid > 5 kHz | −20 | −11 | −6 dB |

Conséquences des ancrages de la spécification :
- PLRr < 6,5 dB : **5 titres sur 429** (dont *poster boy*) ;
- PLRr < 7,5 : 12 titres ;
- PLRr ≥ 8,5 : 383 titres.

La branche « mur » (Hard + Make-Room) est donc rare par construction. La plupart des références
mènent au Soft Knee avec un genou de 50 à 80 %, et le Sub Make-Room reste coupé.

**Ce qu'un ingé doit savoir (limites).**
1. **« Attaques »** : le descripteur `coups_par_s` compte les coups par seconde, il ne mesure pas la
   hauteur des transitoires. L'attaque du multibande est donc pilotée par la micro-dynamique par zone
   (`micro`), la seule mesure de crête d'attaque disponible.
2. **« Pompe »** : depuis le 04/11, elle est mesurée sur le tempo (clé `pompe`). Mais **aucune
   fiche existante ne l'a encore** : elles ont été mesurées avant, et l'audio est sur le PC de
   l'utilisateur. Tant qu'une fiche n'est pas remesurée (`docs/REFERENCES.md`, « Remesurer pour la
   pompe »), OMNIS se sert de l'ancien indice. Celui-ci est non nul pour 72 titres sur 429 et ne
   reconnaît pas *One More Time*.
   - Limites de la mesure : il faut un tempo net. Un tempo lu au double ou à la moitié est couvert,
     puisque la mesure prend le maximum sur les trois périodes, ce qui la biaise un peu vers le
     haut (d'où l'ancrage à 1,5 dB).
   - Un limiteur très agressif donne aussi une pompe (*poster boy*). C'est voulu : c'est le
     comportement qu'on reproduit.
3. **Seuils du multibande** : le niveau de bande est estimé à partir de la sonie et de la part
   spectrale du Mid. La pondération K et l'absence du Side décalent l'estimation de quelques dB. Le
   ratio, lui, ne dépend que du rapport des dynamiques.
4. **Dynamique du sub** : P90 − P10 compte aussi les silences entre les notes de 808. Une prod à 808
   hachée paraît plus « dynamique » qu'elle ne l'est, d'où la borne de 3:1 sur le sub.
5. **`sub_phase` Analog** : j'exige en plus que la **prod** ait de la marge (PLRp > 9,5). La rotation
   de phase s'applique à la prod : sur une 808 déjà écrêtée, elle fait monter la crête (+7,8 dB
   mesurés).
6. **Rien n'a été écouté** : les tests vérifient les décisions sur des signatures synthétiques
   (`tests/TestReferenceDynamics.cpp`). Le dosage se valide à l'oreille sur les prods de
   l'utilisateur.

**Diagnostic relatif à la référence.** L'alerte « mix déjà très fort et écrasé » se déclenche quand
les deux conditions suivantes sont vraies :
- LUFSp > max(−9, LUFSr) ;
- PLRp < min(8, PLRr).

Face à une réf à −4 LUFS et 4,5 dB de PLR, une prod à −6 / 6 n'est plus signalée ; elle l'est de
nouveau au-delà de la réf. Face à la moyenne de la base (−9,4 LUFS, PLR 10,2), la règle d'origine
(−9 / 8) reste inchangée.

**Tests** (5) :
- deux références opposées donnent deux topologies opposées (clipper, Make-Room, Inflator 808,
  auto-release, release sur le tempo, ratios, lien stéréo) ;
- la continuité du genou et du Make-Room quand le PLR de la réf baisse ;
- une prod non entendue ne change rien ;
- le diagnostic relatif ;
- le compte rendu en anglais.

## 9. Instances liées, stems, chef d'orchestre

- **Rôles** : Seul / Stem / Master, en groupes 1–8.
- **Mémoire partagée nommée** :
  - `Local\` sous Windows, `shm_open` ailleurs ;
  - 32 emplacements, anneau audio de 2 × 65 536 trames `std::atomic<float>`, seqlock ;
  - battement de cœur : un emplacement mort depuis 3 s est repris ;
  - la version de disposition est dans le nom de la région.
  - Compatible avec les hôtes qui isolent chaque plug-in dans un processus (pont FL, Bitwig).
- **Stem** :
  - **0 latence** ;
  - élément reconnu par l'écoute (21 éléments) et par le nom de piste (`IInfoListener`,
    `data/elements.txt`) ;
  - chaîne par famille : trim, passe-haut SVF, EQ dynamique 3 bandes, compresseur, saturation 2×,
    M/S LR4 ;
  - bornes fixes ;
  - Off = au bit près ;
  - 0,4 à 0,9 % d'un cœur par stem.
- **Master chef d'orchestre** : écoute alignée sur le temps du projet, par blocs de 8 192 trames.
  - **Masquages détectés** :
    - dans le grave (808, basse, kick) ;
    - dans la présence de la voix lead (1,2–6 kHz).
  - **Retouches** : il **propose** un passe-haut ou une cloche −2 / −3 dB sur le gêneur. Rien n'est
    appliqué sans accord. Une proposition acceptée ou refusée n'est plus reproposée.
- **Bus de drums** : kick, snare / clap, hat et open hat reconnus par leurs coups, chacun traité.
  - Sur le DRUMS de l'utilisateur : 168 kicks, 363 snares / claps, 145 hats, 44 open hats.
- **Reste à calibrer** : les seuils de reconnaissance et de masquage ont été vérifiés sur des sons
  synthétiques et sur une prod de l'utilisateur. Il faut maintenant de vrais stems avec un vrai
  masquage.

---

## 10. Interface (`OmnisEditor`, `gui/`)

**Généralités.**
- **Rendu** : VSTGUI dessiné en code (maquette console v11, 1200 × 750). Aucune image ni police
  livrée : polices système.
- **Mise à jour** : minuterie à 30 Hz, et seules les pièces qui changent sont redessinées.

**Disposition.**
- **Rubans du haut** : AUTO, A / B, A>B, BYPASS, MATCH (Gain Match), DELTA, Stéréo / Mid / Side, « ? »
  (guide), et l'écran **LAT** qui bascule le mode Travail.
- **Barre IA** : champ de texte, **entonnoir** (filtre famille › sous-genre), menu **RÉF** (pastille
  de l'artiste), APPLIQUER, et la réponse d'OMNIS.
- **Rack** de 7 modules au format 500 : bouton principal, ON, mini-écran d'état (« GR −7.5 dB »,
  « CLIP −0.4 dB »…). Glisser un module le déplace dans le rack.
- **Tiroir** : tous les réglages du module et son écran (réponse, courbe, réduction).
- **Analyseur** : 1/6 d'octave, pente +4,5 dB/oct, avec le **bandeau d'export** ; **goniomètre**.
- **Colonne Master** :
  - entrée et sortie ;
  - crêtes L / R, LUFS et GR ;
  - LUFS-S / I, True Peak, crête max, PLR, LRA, corrélation et GR, chaque mesure avec un verdict
    (FORT, OK, DENSE, MONO) ;
  - ÉCOUTE ;
  - instances liées (CHEF / STEM) ;
  - **DIAGNOSTIC DU MIX**.

**Pages et guide.**
- **10 pages** : 7 modules, Piste, Chef, Diagnostic.
- **Guide** de première ouverture : 10 étapes.

**Vérification sans DAW.**
- `omnis_guishot` rend chaque page en PNG avec le vrai processeur et le vrai contrôleur, sur une
  boucle de test. Les menus déroulants ne sont pas rendus.

---

## 11. Deux langues (`core/I18n`)

- **Principe** : le code écrit en français ; `data/i18n_en.txt` (français TAB anglais, embarqué) donne
  l'anglais.
- **Application** : `i18n::tr` est appelé à l'affichage, y compris dans `Look::text()`. La recherche
  se fait dans cet ordre :
  1. correspondance exacte ;
  2. remplacement de fragments (≥ 6 octets ou ponctuation, le plus long d'abord, aux limites de mots) ;
  3. cache.
- **Chaînes de format** : traduites avant `snprintf` (helpers `fmt` / `format`).
- **Choix de la langue**, dans cet ordre :
  1. commande `/en` ou `/fr` dans la barre IA ;
  2. `langue.txt` du dossier de l'utilisateur ;
  3. `OMNIS_LANG` ;
  4. langue du système.
- **Tests** : `TestI18n`. `TestMain` force le français.

---

## 12. Données (`data/`)

| Fichier | Contenu | Embarqué |
|---|---|---|
| `references.txt` | 426 signatures mesurées : LUFS, LRA, TP, PLR, sub, Mid / Side / dynamique sur 21 nœuds, fiche musicale, durée | oui |
| `catalogue.txt` | artistes, alias (laf, b2o / kopp…), titres, ids Deezer | oui |
| `genres_classement.txt` | les familles de l'utilisateur → mots de genre | oui |
| `styles_artistes.txt` | artiste → sous-genres précis (~440 artistes) | oui |
| `titres_genres.txt` | genre d'un titre précis (fourni par l'utilisateur) | via `omnis_refdb titres` |
| `lexique_fr.txt`, `argot.txt` | compréhension | oui |
| `elements.txt` | noms de pistes → éléments | oui |
| `i18n_en.txt` | traduction | oui |
| `artistes_proches.txt` | voisinage d'artistes (les champs `proches` des fiches) | non (outils) |
| `attendu_500.csv`, `spotifetch.csv`, `sons_a_mesurer.csv` (1 269), `discographies_a_mesurer.csv` (6 000), `spotify_500.csv`, `remesure_36.txt` | listes de mesure côté utilisateur | non |

- **Embarquement** : `cmake/OmnisEmbedData.cmake` transforme les fichiers en tableaux C++. Le
  plug-in ne lit **aucun fichier livré à côté de lui**.
- **Fichiers de l'utilisateur** : seuls ceux de son dossier de données
  (`%APPDATA%\Abrasion Labs\OMNIS`, `~/Library/Application Support/Abrasion Labs/OMNIS`).
- **Règles** :
  - aucun son dans le dépôt (`.gitignore`) ;
  - **aucune image livrée** : test ctest `omnis_no_images`. Avatars et pochettes viennent du cache
    local de l'utilisateur (`gfx/AvatarCache.h`) : emblème géométrique tiré de l'image Deezer
    publique, ou pochette générée depuis la fiche.

---

## 13. Outils (`build/bin/Release/`)

| Outil | Usage |
|---|---|
| `omnis_render in.wav out.wav --set cle=valeur --report [--latence-basse] [--ecoute N]` | rendu hors ligne bit-exact (clés : `--params`) |
| `omnis_testgen kit dossier --sr 48000` | signaux de test : EBU 3341, sweeps, IMD SMPTE / CCIF, drums, HF… |
| `omnis_analyze [--diagnostic]` | mesures et diagnostic d'un fichier |
| `omnis_compare a.wav b.wav` | distorsion ajoutée, écarts d'octave, sonie, crête |
| `omnis_ask "phrase"` | la barre IA hors plug-in |
| `omnis_stems` | élément reconnu, réglages Auto et effet de la chaîne sur un stem |
| `omnis_refdb` | base de références (voir ci-dessous) |
| `omnis_guishot dossier` | captures PNG de toutes les pages |
| `validator` | validateur VST3 de Steinberg |

**Commandes de `omnis_refdb`.**
- `scan dossier --attendu liste.csv --db …` : mesure les fichiers d'un dossier.
  - **Contrôle de durée** : max(8 s, 6 %) ; 0 = durée non contrôlée.
  - **Artiste** : il reconnaît n'importe quel artiste co-crédité et renomme l'entrée selon l'artiste
    de la liste.
  - **Genres** : il ajoute les mots de genre de la famille (colonne `Genre_Classement`).
- `deezer [--id | --photo | --sans-photo]` : avatars et ids.
- `embleme` : avatar emblème.
- `titres` : import des genres par titre.
- Les discographies se mesurent aussi avec cet outil.

**Scripts** :
- `tools/refdb/*.py` génèrent les listes ; une durée inconnue y est écrite 0, jamais 185 s ;
- `copier_titres.ps1` copie les titres listés vers un dossier court ;
- `tools/codemap/codemap.py` régénère `docs/CODEMAP.md`.

---

## 14. Construction, CI, distribution

- **Build** : CMake + Ninja, C++20.
  - La GUI est optionnelle (`OMNIS_BUILD_GUI`).
  - Cross-compilation MinGW possible (documentée).
- **CI** (`.github/workflows/ci.yml`) :
  - matrice Ubuntu / macOS (universel arm64 + x86_64) / Windows ;
  - tests, validateur, installateur **Inno Setup** (`installer/OMNIS.iss`) ;
  - job `package` : artefact **`OMNIS-pour-ONIRIQUE`** (VST3 Windows + Mac, `omnis_refdb.exe`,
    `LICENSE_NOTICES.md`, `REFERENCES.md`, notice d'intégration, `MANIFEST.txt` avec SHA-256) ;
  - dernière exécution complète verte : run 101.
- **Installation** (`installer/INTEGRATION_ONIRIQUE.md`) :
  - copier le dossier `OMNIS.vst3` entier dans le dossier VST3 standard ;
  - rien d'autre : aucune clé de registre, aucun service, aucun redistribuable, aucun réseau ;
  - la désinstallation garde le dossier de l'utilisateur.
- **Identité** :
  - éditeur Abrasion Labs, https://onirique.store ;
  - `com.abrasionlabs.omnis` ;
  - UID processeur `2E5FA3CD-D9AA49CF-933A79CD-1EAA3F2B` ;
  - catégorie `Fx|Mastering`.
- **Signature** : non faite.
  - **macOS** : Developer ID et notarisation obligatoires, sinon Gatekeeper bloque (99 $/an).
  - **Windows** : certificat de signature de code conseillé (SmartScreen).

---

## 15. Qualité et tests

- **Tests unitaires : 206**, un fichier par domaine.
  - **Null-tests** au bit près ou < 1e-12 pour chaque étage neutre et pour la chaîne entière.
  - **Repliement** : clipper selon le suréchantillonnage, Inflator 4× sans repliement, rejet
    ultrasonore > 115 dB.
  - **Garanties** :
    - brickwall true peak ;
    - garde-fou du clipper en bout de chaîne ;
    - continu asymétrique.
  - **Conformité** EBU 3341 / 3342 et BS.1770.
  - **Robustesse** :
    - découpage en blocs indifférent (rendu identique au bit près) ;
    - NaN / inf de l'hôte interceptés, la chaîne continue de jouer ;
    - automation au point près ;
    - dénormaux (FTZ / DAZ) ;
    - mode Travail : latence 8× plus basse, à peu près le même son.
  - **IA** : compréhension (fautes, négations, argot), intentions, lexique, références, apprentissage,
    diagnostic.
  - **Autres** : stems, lien, chef d'orchestre, bus de drums, écoutes simulées, i18n, avatars,
    catalogue livré.
- **Tests plug-in : 20** :
  - état et presets ;
  - barre IA (remise à zéro, annuler, export) ;
  - lien ;
  - menus RÉF (famille, sous-genre) ;
  - bouton du slot sous ASan / TSan.
- **Validateur Steinberg** : 47 / 47.
- **Protocole d'acceptation DSP** : `docs/PROTOCOLE_DE_TEST.md`. Calibrations sur les prods de
  l'utilisateur et sur Ozone : `docs/CALIBRATION_MODULE2/3/4.md`.
- **Historique des masters V1 → V6** de l'utilisateur, avec mesures et avis d'écoute :
  `docs/BILAN.md` §2.

---

## 16. Limites connues (à dire à un ingé)

1. **Latence de 710 ms** en Qualité max. C'est le prix de la phase linéaire à 2,7 Hz de résolution.
   - Inaudible pour un master, gênante pour jouer en direct, d'où le mode Travail (~90 ms) avec son
     avertissement rouge.
   - On pourrait regagner ~200 ms en fusionnant les FIR du Sub et du Match EQ en un seul.
2. **True peak** : la garantie est démontrée pour le limiteur. À réglages extrêmes, on mesure jusqu'à
   ~0,5 dB au-dessus de l'idéal dans le pire cas torturé.
   - Si le clipper est en bout de chaîne, le garde-fou tient la crête d'échantillon et le plafond,
     pas un true peak garanti.
3. **Pas de dither.**
4. **Références** : une partie des fichiers mesurés est lossy (YouTube, streaming).
   - La signature détecte la coupure haute, mais l'air au-dessus de 16 kHz d'une référence lossy
     n'est pas fiable.
   - Il faut 8 s d'écoute de la prod avant qu'une référence s'applique.
5. **Écoutes simulées** : des approximations, pas des mesures de casques ; binaural générique.
6. **Plafond de sonie sur les prods de l'utilisateur** (~−9,5 LUFS propre). Sur son passage le plus
   fort, la 808 dépasse la sonie de 12 dB en crête.
   - Atteindre −6 LUFS impose de la distorsion ou un changement de balance ; ses références font ce
     choix.
   - La voie prévue est de saturer la 808 seule : Inflator 808 et Sub Drive du clipper existent ;
     leur dosage reste à valider à l'oreille.
7. **Stems et chef d'orchestre** : seuils à calibrer sur de vrais stems. Les retouches acceptées ne
   sont pas encore gardées à la réouverture du projet.
8. **Rien n'a été écouté ici** : le conteneur n'a pas de sortie audio.
   - Le comportement dans FL Studio (slot, polices Segoe UI / Consolas, Direct2D, glisser un module)
     doit être vérifié par l'utilisateur.
9. **Pas d'AU** (Logic) ni d'AAX (Pro Tools).
10. **Compréhension** : un lexique en données, robuste mais fermé. Le modèle de langue local embarqué
    (1–2 Go accepté par l'utilisateur) n'est pas fait.

---

## 17. Juridique et provenance

- **`LICENSE_NOTICES.md`** contient :
  - les licences des tiers : VST3 SDK 3.8.0 (MIT), VSTGUI 4.15 (type BSD 3 clauses), stb / dr_libs
    (outils seulement) ;
  - les marques ;
  - les écoutes simulées : marques citées seulement pour désigner les modèles ;
  - les avatars et données en ligne ;
  - la provenance des mesures.
- **HIIR** : seule la méthode de L. de Soras est utilisée, réimplémentée ; le code n'est pas repris.
- **Manuels de logiciels tiers** (Pro-Q, Pro-L, Ozone, Gullfoss, Trackspacer, Decapitator, KClip) :
  seuls des principes, reformulés. Rien n'est copié.
- **Base livrée** : depuis le 04/11, la notice dit la vérité. Les mesures viennent de fichiers
  téléchargés (YouTube et autres) sans licence ni autorisation des ayants droit. Aucun audio n'est
  gardé, seulement des nombres.
- **Base perso** : 338 titres mesurés de la même façon, **fusionnés dans la base livrée le 04/11**
  avec l'accord explicite de l'utilisateur (426 titres livrés). Liste complète :
  `docs/references_utilisees.csv`.
- **Risque** (ce n'est pas un avis juridique) : la notice protège de la fausse déclaration, pas de
  l'origine des fichiers. Avant la vente, la voie propre est de remesurer la base livrée sur des
  fichiers achetés, puis de corriger la notice. La question est à poser à un juriste.
- **Données personnelles** : aucune connexion réseau du plug-in, aucune télémétrie.
  - Les avatars passent par l'API publique Deezer, interrogée **par l'outil**, sur le poste de
    l'utilisateur, jamais par le plug-in vendu.

---

## 18. Points ouverts et décisions attendues

| Sujet | Ce qu'il faut décider ou faire |
|---|---|
| Licence / protection | Comportement à choisir : clé hors ligne, essai, limites. Rien n'est en place. |
| ONIRIQUE | Leur outil permet-il les données personnalisées, l'hébergement d'un fichier de données, les mises à jour ? Cela conditionne la remontée de l'apprentissage. |
| Provenance | Fusionner ou non la base perso, trier la base livrée, mettre à jour la notice. |
| Signature | Developer ID Apple + notarisation, certificat Windows. |
| Dither | Implémenter TPDF + noise-shaping (8ᵉ étage). |
| Mesures | Remesurer les 36 refus « 185 s » (`data/remesure_36.txt`), les 82 titres sans fichier, les 133 fichiers hors liste. |
| Sous-genres | Classer les 127 artistes de l'annexe B (ou laisser l'utilisateur le faire). |
| Écoute | Masters « 808 tenue » (m6, SLAYR, poster boy) et la dernière version dans FL Studio. |
| Produit | AU ; EQ dynamique et anti-résonances dans le rack ; retouches du chef gardées ; modèle de langue local. |

---

## Annexe A — Paramètres (résumé)

| Groupe | Clés |
|---|---|
| Global | `bypass`, `input_gain`, `output_gain`, `monitor`, `ref_capture`, `chain_order`, `low_latency`, `gain_match`, `delta`, `listen` |
| Lien | `link_role`, `link_group`, `link_stem` |
| Sub | `low_cut`, `mono_freq`, `mono_amount`, `mono_slope`, `sub_gain`, `sub_freq`, `sub_synth`, `sub_phase`, `sub_on` |
| Match EQ | `match_amount`, `match_tilt`, `match_m_<Hz>` ×21, `match_s_<Hz>` ×21, `match_on` |
| Inflator | `infl_low`, `infl_high`, `infl_drive`, `infl_curve`, `infl_warmth`, `infl_output`, `infl_split`, `infl_os`, `infl_on` |
| Imager | `img_split_low/mid/high`, `img_width_low/lmid/hmid/high`, `img_on` |
| Multibande | `mb_split_low/mid/high`, `mb_<bande>_thr/ratio/attack/release/makeup` ×4, `mb_on` |
| Clipper | `clip_mode`, `clip_drive`, `clip_ceiling`, `clip_knee`, `clip_os`, `clip_split`, `clip_sub_drive`, `clip_sub_room`, `clip_on` |
| Limiteur | `lim_gain`, `lim_ceiling`, `lim_release`, `lim_auto_release`, `lim_punch`, `lim_upward`, `lim_unlink_tr`, `lim_unlink_su`, `lim_on` |
| Stem | `stem_mode`, `stem_hpf`, `stem_comp`, `stem_sat`, `stem_clean`, `stem_width`, `stem_trim` |
| Mesures | `meter_m/s/i/tp/corr/lra`, `meter_ref_s`, `meter_ref_lufs`, `meter_stems`, `meter_element`, `meter_proposals` |

Bornes et défauts exacts : `omnis_render --params`.

## Annexe B — Artistes sans sous-genre (« à classer »)

Ces artistes sont dans la base livrée, dans la base perso ou dans la liste 500, mais n'ont pas
encore de ligne dans `data/styles_artistes.txt`.
- Ils restent filtrables par **famille**, mais pas par sous-genre.
- Ce sont surtout des artistes de l'underground, que je n'ai pas classés au hasard.
- Pour les classer, ajouter une ligne « Artiste ; sous-genre, sous-genre » ; pour un titre précis,
  utiliser `data/titres_genres.txt`.


**127 artistes**, avec leur source entre parenthèses :

- 15.grammes (liste 500, ta base)
- 55ASKY (liste 500, ta base)
- 6minaprès (liste 500, ta base)
- 7rusk (liste 500, ta base)
- 9lives (liste 500, ta base)
- A-JEY (base livrée)
- Abcrap (liste 500, ta base)
- abel31 (liste 500, ta base)
- Advm (liste 500, ta base)
- AETHĘR (liste 500, ta base)
- ALP (base livrée)
- Arcade Player (liste 500)
- ARENCI (liste 500)
- baby hayabusa (liste 500, ta base)
- BBY GOYARD (base livrée)
- BES (liste 500, ta base)
- BloodyWraith Pee (base livrée)
- Bouzbor (liste 500, ta base)
- Boxxy (liste 500, ta base)
- BU$HI (liste 500, ta base)
- CesTenS (liste 500, ta base)
- Chavo (liste 500)
- Cikey (liste 500, ta base)
- Cochise (liste 500)
- César (liste 500, ta base)
- Dom Corleo (liste 500, ta base)
- Dreamville (liste 500, ta base)
- El Hindi (base livrée)
- Emile (liste 500, ta base)
- Empty7 (liste 500, ta base)
- Enock (liste 500)
- FLY (base livrée)
- G10KK (base livrée)
- GADBOY (liste 500, ta base)
- Geek Music (liste 500, ta base)
- Gemen (liste 500, ta base)
- Gouzsman (liste 500)
- Grace Grace (liste 500, ta base)
- Guydelafonsdal (liste 500, ta base)
- Haiko (liste 500, ta base)
- Harrison (base livrée)
- Haybabe (liste 500, ta base)
- Haycci (liste 500, ta base)
- Headband Andy (liste 500, ta base)
- Hotel Blue (liste 500, ta base)
- HXG (base livrée, liste 500)
- Izumø (liste 500, ta base)
- Jace! (base livrée)
- jahhde (liste 500, ta base)
- Jeeno (liste 500, ta base)
- Jeune CN (liste 500, ta base)
- Jeunesaint (liste 500, ta base)
- JJ ODB (liste 500, ta base)
- joaqm (liste 500, ta base)
- Jody (liste 500, ta base)
- Joga (liste 500, ta base)
- Jxsh (liste 500, ta base)
- Karma (liste 500, ta base)
- Keezy (base livrée)
- kurffew (liste 500, ta base)
- Kyloaded (base livrée)
- L!L! (liste 500)
- La Goutte au Nez (base livrée)
- Lamo (liste 500, ta base)
- Lesram (liste 500, ta base)
- Lester (liste 500, ta base)
- Lil Purpx (liste 500, ta base)
- Lil Shine (base livrée)
- Lila Hart (base livrée)
- Long Beatz (base livrée)
- Loonzii (liste 500, ta base)
- Loptik (liste 500, ta base)
- Lucboy (liste 500, ta base)
- Luccv (liste 500, ta base)
- lucky (liste 500, ta base)
- lul bam (base livrée)
- Lybro (liste 500)
- Lyonzon (base livrée)
- Lyre (liste 500, ta base)
- Mali (base livrée)
- Malo (liste 500, ta base)
- Moji x Sboy (liste 500, ta base)
- Mora (liste 500, ta base)
- morphée. (liste 500, ta base)
- nash (liste 500, ta base)
- nate sib (liste 500, ta base)
- Nechie (base livrée)
- Nidjo Sensei (base livrée)
- Nosky (liste 500, ta base)
- OENN (liste 500, ta base)
- Pepo (liste 500)
- Pilar (liste 500, ta base)
- poissonchat (liste 500, ta base)
- Poupy (liste 500)
- Pxrselow (liste 500, ta base)
- RAMA (liste 500, ta base)
- Ratu$ (liste 500, ta base)
- Rei Afaa (liste 500, ta base)
- Roland Cristal (liste 500, ta base)
- RomancePlanet (liste 500)
- S.KEY.S (liste 500, ta base)
- Saymooon (liste 500)
- Saïf (liste 500, ta base)
- Sed (liste 500, ta base)
- SheyKo (liste 500, ta base)
- Siji (liste 500, ta base)
- snorunt (liste 500, ta base)
- SOVAJON (liste 500)
- Spider ZED (liste 500, ta base)
- Stains On A Road (base livrée)
- Tan the Terrible & GOON DES GARCONS (base livrée)
- Tarik Azzouz (liste 500, ta base)
- THBW (liste 500, ta base)
- Tim Kta (liste 500, ta base)
- TKKF (liste 500, ta base)
- Toly (liste 500, ta base)
- Tsune (liste 500, ta base)
- Tydi S (liste 500, ta base)
- Unfamouslouie (liste 500, ta base)
- Video Game Players (liste 500, ta base)
- Vilhelm. (liste 500, ta base)
- Wall.E (liste 500, ta base)
- Willyod (liste 500, ta base)
- X.I (liste 500, ta base)
- YH 261 (liste 500, ta base)
- YUZMV (liste 500, ta base)
- Zepo (liste 500, ta base)
