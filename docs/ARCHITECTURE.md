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
| Piste / bus (thread audio) → toutes les instances (contrôleur) | Mémoire partagée nommée : trames d'empreinte marquées de leur numéro, nom et reconnaissance sous seqlock (`link/LinkBus.h`) | ✅ |

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

## 11. Pistes et bus (`link/`) : OMNIS écoute sans toucher au son

Réglage **RÔLE** (`role`, id 15) : **Master** (le mastering), **Bus** ou **Piste**. En Bus et Piste,
le son passe au bit près, la latence annoncée est 0 (le contrôleur demande `kLatencyChanged`, le
processeur change de rôle tout de suite derrière sa porte, `switchRole`). Le chaînage de stems,
les ordres du master et le chef d'orchestre des premières versions sont retirés (08/11).

Toutes les instances Bus / Piste partagent une région de mémoire nommée (`link/SharedRegion` :
CreateFileMapping « Local\ » sous Windows, shm_open ailleurs ; une région nommée parce que certains
hôtes chargent chaque plug-in dans son propre processus). Disposition `link/LinkBus.h`, version 3
dans le nom de la région ; une mémoire neuve, à zéro, vaut « tout libre ».

**Un emplacement (64 en tout) contient :** jeton (64 bits aléatoires) et battement de cœur (repris
après 3 s de silence), rôle, nom de la piste dans l'hôte, ce qui a été reconnu (élément, écoute
seule, confiance, désaccord avec le nom, kick / snare / hat / open hat entendus dans un bus) sous une
séquence « seqlock », et un anneau de **1024 trames d'empreinte** : l'énergie de six bandes
(20–60, 60–150, 150–400, 400–2000, 2000–6000, 6000–20 000 Hz, passe-bandes RBJ sur le Mid) par
trame de 2048 échantillons, numérotée par la position dans le projet (`link/BandFrames`). Chaque
trame est marquée de son numéro APRÈS ses valeurs ; une trame entamée (départ, saut de la tête) est
jetée. Tout est atomique sans verrou (`is_always_lock_free` vérifié à la compilation).

**Threads :**
- **Thread audio** (Bus / Piste) : copie l'entrée vers la sortie, écrit l'empreinte dans son
  emplacement et pousse le son (pendant la lecture) dans une file sans attente. L'Endpoint lui est
  prêté par `LinkClient::RtAccess` (drapeau « occupé » + pointeur atomique) : jamais détruit sous lui.
- **Thread du lien** (50 ms) : rejoint la région (réessaie chaque seconde), bat le cœur avant et
  après le travail lourd, écoute au plus ~1 s de son par tour (`ElementClassifier`, et `DrumBus`
  pour un bus), publie nom et reconnaissance toutes les 500 ms (ou tout de suite si la piste est
  renommée).
- **Contrôleur** (toutes les instances, Master compris) : `link::Observer` lit la région sans
  prendre d'emplacement ; `stems::buildComposition` au plus une fois par seconde (page COMPOSITION,
  barre IA « composition »).

## 12. Reconnaître l'élément d'une piste (`stems/Element`, `stems/ElementClassifier`)

25 éléments en 5 groupes : **DRUMS** (kick, snare, clap, hi-hat, open hat, cymbale, percu, boucle),
**808 / BASSE**, **MÉLO** (piano, guitare, cuivres, cordes, nappe, lead / synthé, pluck), **VOIX**,
**EFX** (reverse cymbal, riser, impact, autre EFX). L'élément vient :

1. **de l'écoute** (thread du lien, son de la piste pendant la lecture) : trames de ~46 ms, six
   bandes, part de l'énergie dans des pics (note ou bruit), largeur, crête / RMS ; les coups (montée,
   décroissance de 20 dB, ce que chaque coup ajoute) ; et les **montées** pour les EFX : une reverse
   cymbal est un bruit aigu qui monte de 12 dB ou plus sur plus de 300 ms et s'arrête net (−20 dB en
   ~70 ms) ; un riser monte longtemps (> 1,5 s) ; un impact est un gros coup large et rare qui traîne
   (> 0,8 s). Une montée n'est ni une cymbale frappée ni une nappe. Des règles lisibles donnent un
   score par élément ;
2. **du nom de la piste** (`data/elements.txt`, donné par l'hôte via `ChannelContext::IInfoListener`
   puis le message `omnis.trackName`) : noms de packs découpés (« SH_KickHard04 », « RevCrash »,
   « 808Mafia ») ; la forme la plus longue gagne (« rev crash » = reverse, pas cymbale). Le nom
   précise l'écoute dans un groupe ; si le groupe entendu le contredit, une écoute nette l'emporte et
   le désaccord est noté. Le nom d'un bus donne son groupe (`familyFromName` : DRUMS, MELO, EFX…).

Seuils vérifiés sur des sons synthétiques (tests) : à calibrer sur de vrais stems.

## 13. Qui est dans quel bus (`stems/Composition`)

VST3 ne dit pas à un plug-in ce qui entre dans son bus : OMNIS le retrouve à l'écoute. Le bus reçoit
la somme de ses pistes ; les filtres de l'empreinte sont linéaires, donc l'empreinte du bus est à peu
près la somme pondérée de celles de ses pistes. Pour chaque bus, sur les ~26 dernières secondes de
lecture commune :

- **moindres carrés positifs** (descente de coordonnées) : chaque piste est un régresseur, avec deux
  copies décalées d'une trame (latence mal compensée jusqu'à ~43 ms) ; chaque bande pèse autant ;
- **borne physique** : une piste du bus n'y pèse jamais plus que le bus ; son gain total est au plus
  1,25 × le 10e centile du rapport bus / piste sur trois trames. Une piste qui n'est pas dans le bus
  joue aussi quand le bus se tait : sa borne tombe à presque rien, elle ne « bouche » pas le trou
  d'une piste du bus sans OMNIS ;
- **décision** : part ≥ 3 % dans au moins une bande (des hats discrets sont l'essentiel de l'aigu du
  bus), perte d'explication sans elle ≥ 0,1 % de la bande, dépassement du bus sur ≤ 15 % des trames,
  gain ≥ −14 dB (en dessous, elle ne fait que combler un reste).

Mesuré (`tests/TestComposition.cpp`) : 4 bus et 7 pistes dont un kick et une 808 qui tombent
ensemble, sans erreur ; bus saturé (tanh), décalé de 300 échantillons, avec une snare sans OMNIS :
les bonnes pistes ; 40 pistes et 6 bus : 40/40 en 43 ms. Limites : deux pistes jouant exactement le
même son aux mêmes instants ne se distinguent pas ; OMNIS BUS doit être EN PREMIER sur le bus (ce
qui compresse le bus avant lui fausse la somme), OMNIS PISTE EN DERNIER sur la piste.

## 14. (retiré le 08/11) Le chef d'orchestre et les chaînes de stems

Le master qui écoutait les stems et leur envoyait des ordres (gain, retouches), et la chaîne propre
à chaque stem, sont retirés avec le mixage. Leur historique est dans `docs/SUIVI.md`.

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
145 hats, 44 open hats ; sur un bus sans grave : pas de kick. Depuis le 09/11, il n'y a plus de
chaîne de bus : ce qui est entendu s'affiche dans la page COMPOSITION (« entendu dans le bus »).
