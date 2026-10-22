# OMNIS — Architecture

Ce document fixe les décisions structurantes du Module 1. Les modules suivants s'y conforment.

## 1. Couches

```
┌───────────────────────────────────────────────────────────────────────────┐
│ plugin/vst3   OmnisProcessor (thread audio)   OmnisController (thread UI) │  ← seul code dépendant du SDK
├───────────────────────────────────────────────────────────────────────────┤
│ engine        Engine : chaîne de Stage, PDC, bypass, monitoring, mesures  │
├───────────────────────────────────────────────────────────────────────────┤
│ analysis      K-weighting · LoudnessMeter · TruePeakMeter · StereoMeter   │
│ params        table unique des paramètres                                 │
│ core          SPSC · TripleBuffer · OnePoleSmoother · AudioBuffer · FTZ   │
└───────────────────────────────────────────────────────────────────────────┘
        ▲                                   ▲
        │ même code                         │ même code
   tools/render (hors ligne)           tests/ (unitaires)
```

`omnis_core` ne dépend de rien d'autre que la bibliothèque standard. Ce que mesure le banc de test
(outils, tests) est donc **exactement** ce qui tourne dans le DAW.

## 2. Flux du signal

```
entrée f32/f64 ─► double ─┬─► [retard = latence chaîne] ─────────────────────────────► sec
                          └─► Input Gain ─► Étage 1 … Étage N ─► Output Gain ────────► traité
sortie = sec + mix·(traité − sec)        mix lissé 20 ms : 1 = actif, 0 = bypass
       ─► mesures (LUFS M/S/I, LRA, True Peak, corrélation, S/M)
       ─► matrice de monitoring Stéréo / Mid / Side (lissée 15 ms)
       ─► conversion vers le format hôte
```

Ordre prévu des étages (plages d'identifiants de paramètres réservées entre parenthèses) :

1. Sub Control & Mono Maker (100–199)
2. Match EQ M/S à phase linéaire, courbe d'intention (200–299) ✅
3. Excitation harmonique M/S (300–399)
4. Multi-Band Dynamic Processor (400–499) ✅
5. Smart Wave-Shaping / Mastering Clipper (500–599)
6. Lookahead Brickwall Limiter (600–699)
7. Dither TPDF + noise-shaping (700–799)

Mesures en lecture seule : 1000–1099. Global : 0–99.

## 3. Règles temps réel (thread audio)

- **Aucune allocation** : tout est dimensionné dans `Stage::prepare()` / `Engine::prepare()`, appelés
  depuis `setupProcessing()` (composant inactif, thread non temps réel).
- **Aucun verrou, aucune I/O, aucun appel système bloquant.** `std::sort` (introsort, en place) est
  le seul algorithme de la STL utilisé dans `process()`.
- **Double précision** du début à la fin ; conversion f32 ↔ f64 uniquement aux bords.
- **FTZ/DAZ** activés pour la durée de `Engine::process()` puis restaurés (`ScopedNoDenormals`).
- **Pas de `-ffast-math`** : il réordonne les sommes (null-tests non reproductibles) et casse la
  gestion NaN/inf.

## 4. Latence (PDC)

Latence actuelle de la chaîne : 29452 échantillons à 44.1 kHz (668 ms), 29744 à 48 kHz, 59456 à
96 kHz. Elle se décompose ainsi : Sub Control et Match EQ (FIR 16384 taps chacun : N/2 + une
partition = 8704), multibande (FIR 8192 : 4352, + anticipation 10 ms et fenêtre RMS 50 ms), clipper (séparation sub/reste à phase linéaire, retard de phase de
l'oversampling 16x, anticipation du partage de marge) et limiteur (lookahead 10 ms + détecteur
true peak).

- Latence totale = Σ latences des étages, **constante pour une fréquence d'échantillonnage donnée**.
- Un étage dont le coût varie avec un réglage (FIR 4096 vs 16384 taps, oversampling 8x vs 16x)
  **annonce son pire cas** et complète par un retard. Motif : changer la latence en lecture
  impose `restartComponent(kLatencyChanged)`, géré de façon inégale par les hôtes (coupure,
  reconstruction du graphe). La latence n'étant pas une contrainte d'OMNIS, on la paie une fois.
- Le chemin sec est retardé de la même quantité : bypass et futurs mélanges dry/wet sont alignés
  à l'échantillon (test : bypass basculé tous les 3 blocs sur 2185 échantillons de latence →
  erreur < 1e-15).
- En bypass, les étages **continuent de tourner** : enveloppes et historiques FIR restent chauds,
  aucun transitoire au réengagement.

## 5. Paramètres

`params/Parameters.{h,cpp}` est la **source unique** : identifiant stable, clé texte (CLI, presets,
vecteur IA), bornes, défaut, unité, pas, libellés, temps de lissage, décimales d'affichage.

- Identifiants **immuables** une fois diffusés (sessions DAW, automation).
- Valeurs normalisées selon la convention VST3, y compris pour les choix discrets
  (`plain = min(stepCount, floor(norm·(stepCount+1)))`).
- Formatage / saisie en locale « C » forcée : un hôte en locale française ne doit pas transformer
  `-3.5` en `-3,5`.
- État binaire versionné (`'OMNS'`, version, paires id → valeur normalisée) : les identifiants
  inconnus sont ignorés, les absents reprennent leur défaut → presets compatibles dans les deux sens.

## 6. Automation

`OmnisProcessor::process()` collecte tous les points des files d'automation du bloc (capacité fixe
2048), les trie par offset, puis découpe le bloc aux instants exacts des changements. Chaque segment
est traité avec les valeurs en vigueur, puis lissé par l'Engine (one-pole, temps à 99 % défini
par paramètre). Les rafales au-delà de la capacité sont réduites au dernier point de chaque paramètre.

## 7. Communication entre threads

| Lien | Mécanisme | Statut |
|---|---|---|
| Hôte → DSP (paramètres) | Files d'automation VST3, découpage échantillon-précis | ✅ Module 1 |
| DSP → contrôleur (mesures) | Paramètres de sortie en lecture seule, ~30 Hz | ✅ Module 1 |
| Thread audio → thread d'analyse / IA (snapshots audio) | `SpscRingBuffer<T>` (wait-free, perte plutôt qu'attente si plein) | primitive prête, câblage module 4–5 |
| Thread audio → thread de calcul (réglages du Sub Control) | `TripleBuffer<SubKernelRequest>` | ✅ Module 2 |
| Thread de calcul → DSP (noyaux FIR du Sub Control) | Banque de 3 emplacements à état atomique (`Free → Writing → Ready → Active → Retiring`), fondu d'une trame | ✅ Module 2 |
| Thread de calcul → DSP (noyaux du Match EQ) | Même banque (`FirKernelBank<Request, Designer>`) | ✅ Module 4 |
| Analyse de référence → courbe du Match EQ | `ai::planMatch` → 43 paramètres (`match_*`) | ✅ outils ; câblage plug-in module 5–6 |
| IA → paramètres utilisateur | `beginEdit / performEdit / endEdit` du contrôleur | module 5 |
| DSP → UI (scope, spectre) | `IDataExchange` (SDK ≥ 3.7.9) | module 6 |

Décision importante : les **réglages** prédits par l'IA passent par le contrôleur
(`performEdit`), donc par l'hôte. Ils sont ainsi visibles, automatisables et annulables (undo) dans
le DAW. Le triple buffer est réservé aux données **qui ne sont pas des paramètres** (noyaux FIR de
16384 taps, courbes de référence), trop volumineuses pour l'automation.

## 8. Rendu temps réel et hors ligne

`Engine::setRealtimeMode()` (appelé par le processeur selon `processMode`) choisit le mode avant
`prepare()` :

- **Temps réel** : les calculs lourds (noyaux FIR de 16384 taps, une FFT de 32768 points par
  changement de réglage) tournent sur un thread de fond. Le thread audio ne fait que des
  compare-exchange et applique le nouveau noyau à la frontière de trame suivante.
- **Hors ligne** (`kOffline` de l'hôte, outils, tests) : calcul immédiat, rendu **déterministe**,
  identique bit à bit quel que soit le découpage en blocs.

## 9. Mesures

- **Sonie** : pondération K recalculée par transformée bilinéaire pour toute fréquence
  d'échantillonnage ; sous-blocs de 100 ms ; momentary 400 ms, short-term 3 s ; intégrée par
  histogramme de 8000 cases de 0.01 LU contenant la somme exacte des puissances (mémoire bornée
  sur une session de plusieurs heures, erreur limitée à la case du seuil relatif) ; LRA (EBU 3342)
  par un second histogramme sur les valeurs short-term.
- **True Peak** : sur-échantillonnage x4, FIR sinc fenêtré Kaiser (β = 7, 48 taps/phase), phase 0
  identité exacte (TP ≥ SP par construction), plat à ±0.01 dB jusqu'à 0.454·fs.
- Les mesures portent sur la sortie master **avant** le monitoring : écouter le Side seul ne fausse
  pas la lecture LUFS.

## 10. Deux modes de livraison

Chaque fois qu'un étage expose un compromis entre couleur extrême et sécurité de diffusion,
deux profils seront proposés :

- **Club / Distortion Aggressive** — masters à −5 / −4 LUFS intégrés, clipping assumé,
  True Peak toléré au-dessus de 0 dBTP si choisi explicitement.
- **Safe Audiophile** — plafond True Peak (−1 dBTP par défaut), PLR préservé, aucune ISP.

Le paramètre de mode arrive avec le clipper et le limiteur (module 3).
