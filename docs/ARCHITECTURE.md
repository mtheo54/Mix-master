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
| Bus (thread du lien) → toutes les instances (contrôleur) | Mémoire partagée nommée : nom du bus et ce qu'il contient, sous seqlock (`link/LinkBus.h`) | ✅ |

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

## 11. Les bus (`link/`) : OMNIS écoute sans toucher au son

Réglage **RÔLE** (`role`, id 15) : **Master** (le mastering) ou **Bus**. En Bus, le son passe au
bit près, la latence annoncée est 0 (le contrôleur demande `kLatencyChanged`, le processeur change
de rôle tout de suite derrière sa porte, `switchRole`). Rien ne se pose sur les pistes (décision de
l'utilisateur, 09/11 : le mixage ne doit pas être touché). VST3 ne donne à un plug-in que le nom de
SA piste (`ChannelContext::IInfoListener`) : un bus ne connaît pas les noms de ses pistes ; il
entend ce qu'il contient (§12).

Les instances Bus partagent une région de mémoire nommée (`link/SharedRegion` : CreateFileMapping
« Local\ » sous Windows, shm_open ailleurs ; nommée parce que certains hôtes chargent chaque
plug-in dans son propre processus). Disposition `link/LinkBus.h`, version 5 dans le nom de la
région ; une mémoire neuve, à zéro, vaut « tout libre ». Un emplacement (64 en tout) : jeton et
battement de cœur (repris après 3 s de silence), nom du bus dans le DAW, secondes écoutées et
jusqu'à 16 éléments entendus (élément, confiance, nombre de coups ou de notes, durée tenue), sous
une séquence « seqlock » à un seul écrivain. Tout est atomique sans verrou.

**Threads :**
- **Thread audio** (Bus) : copie l'entrée vers la sortie et pousse le son (pendant la lecture) dans
  une file sans attente de ~11 s (vidée bien plus vite que le temps réel : rien ne se perd). Le
  morceau entier est écouté, **chaque seconde du projet une seule fois** (repérée par
  `projectTimeSamples` ; une seconde est « entendue » quand elle a été jouée depuis son début) :
  rejouer une boucle ne double pas les coups. Il ne touche jamais la région.
- **Thread du lien** (50 ms) : rejoint la région (réessaie chaque seconde), bat le cœur avant et
  après le travail lourd, écoute au plus ~4 s de son par tour (`stems/BusListener`, jusqu'à ~23 min
  de morceau gardées), publie le nom, le contenu et ce qui a été écouté du morceau chaque seconde
  (ou tout de suite si le bus est renommé). RÉÉCOUTER (n'importe quelle instance) incrémente un
  compteur de la région : chaque bus efface ses secondes entendues et repart de zéro.
- **Contrôleur** (toutes les instances, Master compris) : `link::Observer` lit la région sans
  prendre d'emplacement ; `stems::buildComposition` au plus une fois par seconde (page COMPOSITION,
  barre IA « composition »).

## 12. Ce qu'il y a dans un bus (`stems/BusListener`, `stems/DrumBus`)

Trames de ~43 ms (pas de ~11 ms), quatre bandes (sub 30–120, bas 120–300, médium 300–4000, aigu
4000–16 000 Hz). **Séparation tenu / frappé / bruit** (HPSS) : pour chaque bin, la médiane dans le
temps (21 trames, ~230 ms) contre la médiane dans les fréquences (17 bins) ; harmonique si la
première l'emporte d'un facteur 2, percussif dans l'autre sens, sinon bruit. Puis des règles
lisibles, chacune avec sa garde contre les faux :

- **DRUMS** : `stems/DrumBus` (coups dans quatre zones, calibré sur le DRUMS de l'utilisateur),
  confirmé par la forme des attaques : un **kick** a son énergie dans le sub et retombe de 12 dB en
  moins de 200 ms (ou, avec une 808, a un clic de batte audible) ; une **snare / clap** est un bruit
  large, qui saute dans le médium ET l'aigu et reste du bruit plus de 30 ms (l'attaque d'une note
  pincée redevient « tenue ») ; des **hats** sautent dans l'aigu. Et assez de coups pour la durée
  écoutée (un toutes les ~7 s ; un toutes les 20 s pour un open hat).
- **808 / BASSE** : du grave harmonique, fort, qui tient à 12 dB de son sommet au moins 250 ms par
  note (un kick, même tonal, retombe en moins de 150 ms) ; au moins 3 notes et 2,5 s tenues.
- **MÉLO** : de l'harmonique dans le médium, une fois retiré ce qui suit le grave (droite des
  moindres carrés médium ~ sub : les harmoniques d'une 808 saturée) ; **lead / synthé** si les
  notes s'attaquent (≥ 0,7 attaque / s), **nappe** si le son tient (20e centile fort).
- **EFX** : l'aigu lissé sur ~250 ms ; une **montée** = la courbe descend vraiment quand on remonte
  le temps (≥ 0,3 dB / 100 ms ; un plateau de hats n'en est pas une), ≥ 8 dB, ≥ 450 ms, forte dans
  le bus, et **continue** (près du sommet, l'aigu brut ne retombe presque jamais de 6 dB sous sa
  moyenne : une rentrée de hats ou un roulement de snare a des creux). **Riser** si elle dure
  ≥ 1,5 s, sinon **reverse cymbal** si elle est coupée net (−10 dB en ~120 ms). **Impact** : tout
  monte de 12 dB d'un coup, grave et aigu, avec du bruit large (un mélo qui démarre après un silence
  est tenu), puis tient à 15 dB près 0,7 s en retombant d'au moins 4 dB.

Vérifié sur les stems de l'utilisateur (`omnis_bus`) : DRUMS → kick 168, snare / clap 363, hats
145, open hats 44 (et 2 reverse plausibles avant des reprises) ; MELO → lead + nappe seulement ;
808 → 808 seulement. Coût : ~1/20 d'un cœur par bus écouté.

## 13. La page COMPOSITION (`stems/Composition`)

La liste des bus vivants, chacun avec son nom dans le DAW, son groupe (d'après son nom, `familyFromName` :
DRUMS, MELO, EFX, 808… ; sinon d'après ce qu'il contient) et ce qu'il contient ; rangés par
groupe. Texte en clair pour la barre IA (`describeComposition`).

## 13 bis. Ce que la personne fait le plus (`ai/UsageGraph`)

Une **séance** commence à une proposition d'OMNIS : AUTO ou un preset (référence / style du menu
RÉF, journal `PRESET`) ; elle finit à la proposition suivante, à l'export, à « annule » ou à la
fermeture. Le contrôleur garde l'état proposé (`UsageState` : les 6 réglages clés, les 8 modules,
les caractères, la largeur du grave) et le **chemin parcouru** par chaque réglage (allers-retours,
mesurés dans `setParamNormalized`) ; ce qu'OMNIS règle lui-même (volume mené, essais clip /
limiteur, garde `OmnisEdit`) fait partie de sa proposition, pas des retouches.

À la fin, `judgeSession` découpe la différence en **gestes** et les juge avec des règles de base du
métier : inaudible (sous 0,3 dB de pente, 0,5 dB de sub, 4 % d'Inflator, 0,5 dB de volume, 5 % de
largeur, 1 dB de seuil), hésitation (chemin ≥ 4 × l'écart pour un petit écart, ou beaucoup bougé
pour revenir au départ), à risque (sub > +6 dB, pente > 4 dB, Inflator > 85 %, gain clip +
limiteur > 14 dB, médium > 170 %, seuil du Comp < −30 dB, grave élargi au-delà de 100 %,
limiteur éteint), annulé, sinon utile. Seuls les écarts **utiles** vont au réseau
(`usefulDeltas` → journal `RETOUCHE`).

Le **graphe** (`apprentissage/graphe.tsv`, versionné) : nœuds contexte (« AUTO · trap »,
« Preset · style drill fr ») et geste (vus, utiles, gardés jusqu'à l'export, à risque, écart
moyen), liens contexte → geste et geste ↔ geste (utiles dans la même séance). Habitude : utile
≥ 3 fois et ≥ 60 % du temps (l'export compte double pour le rang) ; douteux : vu ≥ 3 fois, utile
< 40 %. Un nœud vu une seule fois et pas revu depuis 30 séances est oublié ; 300 nœuds au plus.
Export JSON anonyme (`graphe_usages.json`) pour la page de données ; barre IA « mes habitudes ».

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
