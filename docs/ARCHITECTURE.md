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

Ordre des étages (plages d'identifiants de paramètres réservées entre parenthèses) :

1. Sub Control & Mono Maker (100–199) ✅
2. Match EQ M/S à phase linéaire, courbe d'intention et tilt (200–299) ✅
3. Inflator : densité et chaleur, 808 et mix séparés (300–399) ✅
4. Imager : largeur stéréo multibande (800–899) ✅
5. Multi-Band Dynamic Processor (400–499) ✅
6. Smart Wave-Shaping / Mastering Clipper (500–599) ✅
7. Lookahead Brickwall Limiter (600–699) ✅
8. Dither TPDF + noise-shaping (700–799)

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

Latence actuelle de la chaîne : 33812 échantillons à 44.1 kHz (767 ms), 34104 à 48 kHz, 68168 à
96 kHz (mesurées). Elle se décompose ainsi :
- Sub Control et Match EQ : FIR 16384 taps chacun, N/2 + une partition = 8704.
- Inflator : séparation 808 / mix en FIR 4096, 2176 ; plus le retard de phase de l'oversampling
  16x, 8.
- Imager : FIR M/S 4096, 2176. Les bandes sont au-dessus de 100 Hz, une résolution de 12 Hz
  suffit.
- Multibande (FIR 8192 : 4352, + anticipation 10 ms et fenêtre RMS 50 ms), clipper (séparation sub/reste à phase linéaire, retard de phase de
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
| Thread audio → thread d'analyse (écoute d'une référence) | `ReferenceCapture` : deux `SpscRingBuffer` (référence + prod), perte plutôt qu'attente si plein | ✅ |
| Thread audio → thread de calcul (réglages du Sub Control) | `TripleBuffer<SubKernelRequest>` | ✅ Module 2 |
| Thread de calcul → DSP (noyaux FIR du Sub Control) | Banque de 3 emplacements à état atomique (`Free → Writing → Ready → Active → Retiring`), fondu d'une trame | ✅ Module 2 |
| Thread de calcul → DSP (noyaux du Match EQ) | Même banque (`FirKernelBank<Request, Designer>`) | ✅ Module 4 |
| Analyse de référence → courbe du Match EQ | `ai::planMatch` → 43 paramètres (`match_*`) | ✅ outils ; câblage plug-in module 5–6 |
| IA → paramètres utilisateur | `beginEdit / performEdit / endEdit` du contrôleur | module 5 |
| DSP → UI (scope, spectre) | `IDataExchange` (SDK ≥ 3.7.9) | module 6 |
| Stem (thread audio) → master (thread du lien) | Mémoire partagée nommée, anneau audio d'atomiques + seqlock (`link/LinkBus.h`) | ✅ |
| Master → stem (ordres) | Même région, seqlock lu par le thread audio du stem (4 essais, sinon ordres précédents) | ✅ gain |

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

## 11. Instances liées

Une instance **master** (bus master) et des instances **stem** (une par piste) d'un même groupe
(1 à 8) partagent une région de mémoire nommée : `link/SharedRegion` (CreateFileMapping « Local\ »
sous Windows, shm_open ailleurs). On a choisi une région nommée plutôt qu'une variable globale,
parce que certains hôtes chargent chaque plug-in dans son propre processus. FL Studio en
« pont » et Bitwig le font.

**Disposition** (`link/LinkBus.h`) :
- 32 emplacements de taille fixe.
- Une mémoire neuve, remplie de zéros, vaut « tous libres ».
- La version de la disposition fait partie du nom de la région : deux versions d'OMNIS ne lisent
  jamais la même mémoire.

**Un emplacement contient :**
- son jeton (64 bits aléatoires) et l'heure de son dernier battement de cœur ;
- son rôle, son élément (`stems::Element` : kick, open hat, piano, 808… ; disposition version 2), son nom
  (celui de la piste dans l'hôte) ;
- un anneau audio de 2 × 65 536 trames `std::atomic<float>`, marqué de la position projet et des
  trames exactes de départ et d'arrêt de la lecture ;
- les ordres du master.

**Atomiques :** tout ce qui est partagé est atomique sans verrou (`is_always_lock_free` vérifié à la
compilation), donc valide entre processus. La cohérence des petits groupes de champs (position de
l'anneau, ordres, nom) passe par une séquence « seqlock » à un seul écrivain.

**Vie et mort :**
- Un emplacement dont le cœur ne bat plus depuis 3 s (hôte planté) est repris par la prochaine
  instance.
- L'ancienne instance vérifie son jeton et n'y touche plus.

**Threads :**
- **Thread audio d'un stem** : écrit son audio et lit ses ordres sans verrou ni appel système.
  L'Endpoint courant lui est prêté par `LinkClient::RtAccess` : un drapeau « occupé » et un
  pointeur atomique en séquentiel cohérent. Un changement de groupe remplace l'Endpoint sans jamais
  le détruire pendant que le thread audio s'en sert.
- **Thread du lien** (50 ms) : rejoint le groupe, bat le cœur. Côté master, il écoute les stems
  (`StemListener`, une signature par stem, seulement pendant la lecture).
- **Retard du master** : s'il a plus de 0.7 s de retard, il saute au plus récent et compte la
  perte.

**Latence :** un stem annonce 0 échantillon : sa chaîne (§13) est sans latence, puis un gain lissé
(30 ms) ordonné par le master. Le rôle est pris à l'activation. Le contrôleur demande
`kLatencyChanged` quand il change.

## 12. Reconnaître l'élément d'un stem (`stems/`)

Chaque stem sait ce qu'il porte : son élément choisit sa chaîne (§13) et ce que le master peut lui
demander. Paramètre `link_stem` (« Stem Element ») : **Auto** ou l'un des 21 éléments ;
afficheur `meter_element` : ce qui a été reconnu.

1. **Le choix de la personne** passe avant tout.
2. **L'écoute** (`stems/ElementClassifier`, thread du lien, seulement pendant la lecture, sur le son
   SEC de la piste, avant sa chaîne : ce que la chaîne fait ne change pas ce qui est reconnu) : trames de
   ~46 ms, six bandes, part de l'énergie dans des pics (note ou bruit), largeur, crête / RMS ; puis
   les coups (montée de 8 dB, ou une bande qui monte de 12 dB, réarmés après une retombée de 3 dB),
   leur temps de montée, leur décroissance de 20 dB, ce que chaque coup ajoute au son d'avant. Des
   règles lisibles donnent un score par élément. Les sons saturés restent reconnus : la saturation se
   lit dans le caractère (crête basse), pas dans l'identité.
3. **Le nom de la piste** (`data/elements.txt`) : l'hôte le donne par `ChannelContext::IInfoListener`
   (la norme VST3), le contrôleur le passe au processeur par message (`omnis.trackName`). Les noms de
   packs sont découpés (« SH_KickHard04 », « ClosedHat », « 808Mafia »). Le nom précise l'écoute dans
   une famille (guitare plutôt que piano) ; si la famille entendue contredit le nom, une écoute nette
   l'emporte et le désaccord est noté.

Les seuils sont des points de départ vérifiés sur des sons synthétiques (kick, 808, snare, clap, hats,
open hat, piano, nappe, voix, boucle, kick et 808 saturés) : à calibrer sur de vrais stems.

## 13. La chaîne d'un stem (`stems/StemSettings`, `stems/StemChain`)

Sur chaque piste en rôle Stem, sans aucune latence. **« Stem Mode »** (`stem_mode`) :
- **Off** (défaut) : le son passe au bit près ;
- **Auto** : OMNIS règle à partir de l'élément reconnu et de ce que l'écoute a mesuré ;
- **Manuel** : la personne règle les doses (`stem_hpf`, `stem_comp`, `stem_sat`, `stem_clean`,
  `stem_width`) ; fréquences et temps restent ceux de l'élément. `stem_trim` vaut dans les deux modes.

**Par famille** (modèles de `templateFor`, doses moyennes, à calibrer sur de vrais stems) :

| | passe-haut | EQ dynamique | compression | saturation | M/S |
|---|---|---|---|---|---|
| kick | aucun (voir plus bas) | carton 400 Hz | 3:1, 4 dB, att. 15 ms | 25 % | mono < 150 Hz |
| snare / clap | 80–120 Hz | 350 / 800 Hz, 5 kHz | 2,5–3:1 | 15–20 % | mono < 150 Hz |
| hats / open hat / cymbale | 300–350 Hz | dureté 5–7 kHz | open hat 2:1 | — | largeur 110–115 % |
| 808 | aucun | « boum » 250 Hz | 2:1, 3 dB, att. 30 ms | 30 % | mono < 200 Hz |
| basse | aucun | 250, 800 Hz | 3:1 | 20 % | mono < 150 Hz |
| instruments | 60–120 Hz | boue ~250, carton ~600, dureté ~3 k | lead, cuivres 2:1 | — | mono partiel < 120–150 Hz |
| voix lead | 80 Hz | 250, 3,5 k, sifflantes 7 k | 3:1, 5 dB | — | centrée |

**Ce que l'écoute a mesuré est respecté (Auto)** :
- crête / RMS basse (son déjà saturé ou écrasé, voulu) : saturation réduite (jusqu'à 20 % de la
  dose), compression divisée par deux ou retirée ;
- piste déjà large (Side à moins de 6 dB du Mid) : pas d'élargissement ;
- instrument qui joue vraiment dans le grave : le passe-haut descend tant que l'octave au-dessus de
  sa coupure porte plus de 15 % de l'énergie ;
- kick, 808, basse : jamais de passe-haut automatique. Sur une vraie 808 écrêtée (stems de
  l'utilisateur), retirer le continu de l'attaque décale la forme d'onde : +3 à +4 dB de crête,
  même à 10 Hz, pour 0,5 % d'énergie sous 25 Hz. Le grondement est l'affaire du master ;
- l'EQ dynamique ne coupe que ce qui déborde par moments (voir 3. ci-dessous) : un son dont la
  mélodie vit à 150–400 Hz garde son corps.

**Bornes fixes** (`clampSettings`, quoi qu'il arrive) : passe-haut ≤ 500 Hz, coupe ≤ 6 dB par bande,
saturation ≤ 50 %, compression ≤ 4:1 et ≤ 8 dB, largeur 0–150 %, trim −24…+12 dB ; NaN neutralisés.

**La chaîne**, dans l'ordre :
1. trim ;
2. passe-haut SVF, deux sections Butterworth (en 12 dB/oct la seconde descend à 5 Hz : on glisse
   d'une pente à l'autre sans changer de topologie ; éteint, il n'est plus traversé) ;
3. EQ dynamique, 3 bandes : l'énergie d'un passe-bande sur le Mid rapportée à celle du reste de la
   piste (5 / 80 ms). 2:1 au-delà du plus haut de deux seuils : l'équilibre d'un bruit rose + marge,
   et l'habitude de la piste (moyenne en dB sur ~4 s) + 3 dB. Une note qui résonne ou une
   accumulation est calmée ; le caractère permanent du son est respecté (un excès permanent qui gêne
   un autre stem sera proposé par le master). Cloche SVF ;
4. compresseur lié en stéréo, seuil = crête récente (relâche 3 s) − profondeur, genou 6 dB,
   compensation = réduction moyenne sur 2 s (le niveau de la piste reste le même, le gain staging
   est l'affaire du master) ;
5. saturation : courbe de l'Inflator (exposant 3) rapportée à la crête récente. Le bas monte, les
   crêtes du grave restent en place ; la partie non linéaire passe seule en 2x (repliement mesuré
   à −108 dB pour un sinus de 9 kHz), le son direct n'a aucun retard ;
6. M/S : Side séparé en Linkwitz-Riley 24 dB/oct, bas réduit (mono), haut × largeur ; le Mid
   n'est jamais touché.

Les réglages sont recalculés à chaque bloc (fonction pure, sans allocation) à partir de l'écoute
publiée par le thread du lien (`LinkClient::rtListening`, triple tampon) et rejoints en ~100 ms ;
Off ↔ actif en fondu de 30 ms. Coût mesuré : 0,4 à 0,9 % d'un cœur par stem à 48 kHz.

**Outil** : `omnis_stems fichier.wav [...] [--render dossier]` montre l'élément reconnu, le
caractère, les réglages Auto, et ce que la chaîne fait sur tout le fichier (niveau, crête, bandes,
compression et EQ dynamique au fil du morceau).

## 14. Le master chef d'orchestre (`stems/StemTimeline`, `stems/Conductor`)

Le master repère qu'un stem en gêne un autre et **propose** une retouche ; rien n'est appliqué sans
l'accord de la personne.

**Écoute alignée sur le temps du projet.** Deux pistes ne se masquent que si elles jouent en même
temps. Le master range ce qu'il entend de chaque stem par blocs de 8192 trames de temps projet
(position envoyée par le stem avec son audio) : le bloc k de deux stems est le même instant du
morceau. Par bloc, une FFT (Hann, ~5 Hz de résolution) : énergie du Mid en 15 bandes serrées dans le
grave, et du Side sous 150 Hz. Un passage rejoué (boucle) est moyenné.

**Masquages** (toutes les 2 s, thread du lien du master) :
- grave : un instrument, une voix, un FX ou un drum (hors kick) dans la zone de la 808, de la basse
  ou du kick (sous 300 Hz, là où elle est à moins de 20 dB de son maximum). Dans son cœur (à 10 dB de
  son maximum), gêne si le gêneur est à moins de 8 dB d'elle ; dans le haut de son corps (ce qui la
  fait entendre sur de petites enceintes), s'il y est plus fort qu'elle ;
- voix lead : un instrument, un FX ou des backs à moins de 3 dB d'elle entre 1,2 et 6 kHz.

Écart mesuré bande par bande, médiane sur les blocs où les deux jouent (au moins 4 s ensemble).

**Retouches proposées**, toujours sur le gêneur : passe-haut 12 dB/oct en haut de la bande gênante
(plateau −3 dB si ce grave est le cœur du gêneur, ≥ 40 % de son énergie) ; cloche −2 ou −3 dB dans
la présence de la voix. Chaque proposition a une clé stable (gêneur, victime, sorte) : acceptée ou
refusée, elle n'est plus reproposée.

**Circuit** :
1. le processeur du master publie le nombre de propositions (`meter_proposals`) ;
2. l'interface demande le texte (message `omnis.getProposals` → `omnis.proposals`) et répond
   (`omnis.answer` : accepter, refuser, annuler) ;
3. une retouche acceptée part au stem dans ses ordres (`orderReserved[0…15]` : 4 retouches, sans
   changer la disposition de la mémoire), renvoyés chaque seconde ;
4. le stem l'ajoute à sa chaîne, juste après le passe-haut, sans latence, en fondu, bornée
   (`clampCorrection`) ; aussi en mode Off (pas en bypass). Un stem qui reprend un emplacement
   n'hérite d'aucune retouche.

Sur la prod de l'utilisateur (808, DRUMS, MELO) : aucune proposition, à raison : aux moments où la
808 joue, la mélo est 28 dB sous elle à 100–150 Hz. `omnis_stems` donne ce bilan bande par bande.

## 15. Le rack (`engine/Engine`) et le limiteur complet (`stages/LimiterStage`)

**Modules allumés / éteints.** Chaque étage a son interrupteur (`sub_on`, `match_on`,
`infl_on`, `img_on`, `mb_on`, `clip_on`, `lim_on`). Éteint, il rend son entrée retardée de
sa propre latence : la latence totale ne change jamais, et tout éteint = entrée retardée au
bit près. Le passage se fait en 20 ms, l'étage continue de tourner (il reste chaud).

**Ordre du rack.** `chain_order` est le rang (0 … 5039) d'une permutation des 7 modules
(code de Lehmer ; 0 = ordre d'origine). Paramètre caché, sans automation : l'interface le
règle quand on glisse un module. En route : fondu vers le son sec, permutation, attente
d'une latence complète (les étages oublient l'ancien ordre), retour en 30 ms.

**Écoute Delta.** `delta` = un module : on n'entend que ce qu'il change (sa sortie moins
son entrée retardée), retardé de la latence des modules qui le suivent pour rester calé.
**Gain Match.** La sortie écoutée est ramenée à la sonie court terme de l'entrée (lissée
sur ~1,5 s). Les deux sont des outils d'écoute : jamais gardés dans la session, et les
mesures portent toujours sur la vraie sortie.

**Limiteur.** Avant la détection, deux étages neutres à 0 :
- **Punch** : un suiveur rapide (0,3 / 15 ms) et un lent (20 / 120 ms) du niveau lié,
  chacun maintenu 25 ms avant de relâcher (une 808 tenue n'est pas modulée : distorsion
  < −60 dB sur un 50 Hz). Gain = (rapide / lent)^(2,5·Punch), borné à ±9 dB : attaques
  montées, queues baissées. Le limiteur remet les crêtes au plafond : sur la boucle de
  drums à +9 dB, Punch 100 % donne +1 dB d'écart crête / sonie (le relief qu'Ozone avait
  de plus, §5 de la recherche), au prix d'autant de sonie.
- **Upward** : les passages calmes montent jusqu'à `lim_upward` dB, selon leur niveau RMS
  sous le plafond (rien à moins de 12 dB, tout à 36 dB dessous, rien sous −70) : un couplet
  à −40 dBFS monte de ~6,5 dB avec 8 dB, la crête du refrain ne bouge pas.
- **Lien stéréo** séparé pour les attaques et le soutenu (`lim_unlink_tr`, `lim_unlink_su`) :
  chaque canal reçoit r·(r_c / r)^u ≤ r_c, la garantie brickwall tient canal par canal.

## 16. La console (`plugin/vst3/OmnisEditor`, `gui/`, `UiFeed`)

**Interface.** VSTGUI dessinée en code d'après la maquette console v11 (1200 × 750) : rubans
(transport, barre IA), rack de 7 modules au format 500, tiroir (tous les réglages | écran du
module), écrans (analyseur 1/6 d'octave pente +4,5 dB/oct, goniomètre), colonne Master. Les
pièces (`gui/Widgets`) lisent leur état par des fonctions et ne se redessinent que quand il
change (minuterie à 30 Hz). Cercles par chemins : le `drawEllipse` du VSTGUI Linux a la
mauvaise échelle. `omnis_guishot` rend chaque page en PNG (vrai processeur sur une boucle de
test, vrai contrôleur) pour vérifier l'interface sans DAW.

**UiFeed.** Le processeur inscrit son flux dans un registre du processus et donne le jeton au
contrôleur par message : mesures des modules (réductions limiteur / clipper / multibande,
crêtes, Gain Match, latence) en triple tampon, sortie entrelacée en file SPSC pour le spectre
et le goniomètre (copiée seulement interface ouverte), et `ProdListener`.

**Barre IA et AUTO.** `engine/ProdListener` écoute l'entrée du master pendant la lecture
(signature, jamais d'audio) et, pour AUTO, 10 s du passage que la personne joue.
`ai/ConsoleActions` transforme la demande comprise en réglages (Match EQ, Punch, upward +
glue, Inflator, clip, Imager) et en sonie à viser ; le contrôleur pousse le limiteur pas à pas
en lisant la sonie court terme de la sortie jusqu'à la cible (±0,4 LU).

**Menu RÉF.** Base fournie + `references_utilisateur.txt`, entrées mesurées seulement :
Styles → style → artiste → titres (« tout le style », « tous les titres de… » en tête), et
Artistes de A à Z → titres ; 30 par colonne. Icônes 20 px dessinées hors écran : la pochette
(fichier de `images/`) ou une pochette tirée de la fiche (teinte = tonalité, silhouette =
courbe Mid, grain = attaques). La photo d'artiste n'est lue que si `image_credit` existe. La
page Match EQ montre la carte « Référence visée » (pochette, photo, crédit, tempo, tonalité,
sonie). Le choix passe
par `planReference` (courbe du Match EQ vers la moyenne des titres, `applyMusic` sur la fiche
complète, sonie de la réf bornée à −14 … −5 LUFS comme AUTO) ; si la prod n'a pas encore été
entendue 8 s, il attend la lecture. AUTO vise la référence choisie avant le style deviné des
mots. Le choix est gardé dans l'état du contrôleur (version 2 : genre + nom).

**A / B** dans l'état du contrôleur (rôle, groupe et élément exclus). **Guide** : 10 étapes,
fichier `tutoriel_vu.txt` dans le dossier de l'utilisateur.

## 17. Le bus de drums (`stems/DrumBus`)

Quatre zones (grave < 120 Hz, corps ~250 Hz, claquement ~2,5 kHz, cymbales > 7 kHz) en
trames de ~5 ms ; un coup = +6 dB sur les 40 ms d'avant, à moins de 30 dB de la zone la plus
forte. Chaque coup est jugé sur 15 ms par ce qui MONTE dans chaque zone (la queue d'un kick
qui sonne encore ne compte pas) : kick (grave dominant, audible : à moins de 24 dB du plus
fort du bus), snare / clap (corps + claquement), hat (cymbales sans corps ; open s'il met
plus de 150 ms à retomber de 15 dB). Présent après 4 coups. Chaîne du bus : kick mono sous
150 Hz, snare +8 % de largeur (sauf bus déjà large), dureté des cymbales calmée vers 7 kHz
(6,5 kHz pour un open hat). Sur le DRUMS de l'utilisateur : 168 kicks, 363 snares / claps,
145 hats, 44 open hats ; sur un bus sans grave : pas de kick.
