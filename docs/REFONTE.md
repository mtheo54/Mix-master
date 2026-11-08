# OMNIS 2 — Refonte : un mix, puis un master

*Plan du 08/11/2026. Décisions de l'utilisateur, après ses essais dans FL Studio.*

## 1. Décisions

| Sujet | Choix |
|---|---|
| Structure | **Deux plug-ins** dans le même paquet `OMNIS.vst3` : **OMNIS Mix** (pistes / bus) et **OMNIS Master** (master). Plus de menu « Rôle ». |
| Mixage | **OMNIS mixe tout seul**, la personne ajuste (un curseur par piste, ou en mots) ; « annule » reste possible. |
| Barre IA | **Gardée, discrète** : tout se fait sans elle. |
| Tutoriel | Gardé, raccourci ; l'interface doit se comprendre sans lui. |
| IA | **Refaite** avec un **modèle de langue local, hors ligne** (le lexique reste pour l'instantané et en secours). |
| Visuels | **Originaux**, dessinés par OMNIS (aucune pochette ni photo de rappeur livrée : droits des labels). |
| Ordre | **Le son d'abord** (règles, deux plug-ins, rack modulaire, AUTO précis), puis l'IA, puis l'interface finale. |

## 2. Constat mesuré (« à la millanaise 2 », sans / avec OMNIS, preset rap FR)

| Mesure | Original | Avec OMNIS | Lecture |
|---|---|---|---|
| Passages calmes | — | **+10 dB** | l'intro et les breaks montent presque au niveau du refrain |
| Passages forts | — | +2 à +4 dB | |
| LRA | 14,7 LU | **7,2 LU** | contraste divisé par deux |
| 250 Hz – 1 kHz (Mid) | — | **+8 dB** (sub +2 à +3, aigu +0,7) | son boueux / « carton » : l'inverse d'un mix propre |
| Side 120–500 Hz | — | **+10 à +12 dB** | bas-médium étalé sur les côtés, centre creusé |
| Composante continue | 67 ppm | 630 ppm | une saturation n'est pas suivie d'un filtre DC |
| Décalage temporel | — | 1 échantillon | aucun dans cet export |

**Problème signalé en plus** : avec des automations de fader (couper une basse, une mélodie), le
mixage « bascule », pas à chaque fois.

**Causes probables (à confirmer à l'étape 0)** :
- le master et les stems **compensent le niveau en continu** : compression vers le haut
  (`lim_upward`, « densité » et glue), chaîne de stem qui « garde le niveau de la piste », course au
  volume du contrôleur. Un fondu de l'utilisateur est alors combattu, selon le moment ;
- l'intention tonale du profil « rap FR » et l'Imager (bande bas-médium) **ajoutent** du bas-médium
  et de la largeur là où un mix propre en retire ;
- Warmth de l'Inflator / mode Diode du clipper / saturation de stem : DC non retiré à la sortie.

## 3. Règles du son (non négociables, testées)

1. **Ne jamais combattre la personne.** Aucune compensation de niveau continue, ni sur une piste
   ni au master. Le volume se règle **une fois** (AUTO), puis il est figé. Une automation de fader
   donne exactement le fondu demandé (test : fondu de −20 dB d'une piste = −20 dB à la sortie de
   cette piste, ±0,5 dB).
2. **Le contraste reste.** LRA de sortie ≥ 70 % de celui de l'entrée par défaut (plus compact
   seulement si la personne le demande).
3. **Le bas-médium se nettoie, il ne se gonfle pas.** Aucune correction automatique ne monte
   200–600 Hz de plus de +1 dB ; la boue est retirée en priorité (EQ dynamique).
4. **Les graves au centre.** La largeur n'augmente jamais sous 300 Hz ; le bas-médium jamais au-delà
   de 100 % sans demande.
5. **Sortie propre.** DC < 100 ppm (filtre après chaque saturation), true peak tenu.
6. **Latence.** OMNIS Mix : 0. OMNIS Master : inchangée (qualité max / travail).

## 4. Architecture cible

### Un paquet, deux plug-ins
- `OMNIS.vst3` déclare deux classes : **OMNIS Master** (UID actuel, sessions existantes relues) et
  **OMNIS Mix** (nouvel UID). Moteur, mesures et IA partagés.
- Le lien entre instances (`link/`) reste : chaque OMNIS Mix rejoint le groupe du projet ; le
  groupe se règle dans « Avancé » (utile seulement avec deux projets ouverts).

### OMNIS Mix (sur chaque piste ou bus)
- **0 latence**, bâti sur `stems/StemChain`, refait selon les règles du §3.
- **Une fenêtre**, petite : l'élément reconnu (menu pour corriger), un gros bouton **DOSE**
  (0–100 %), trois curseurs **NIVEAU** (dans le mix, ±6 dB), **PRÉSENCE**, **LARGEUR**, et une ligne
  « ce que je fais » (« voix : boue −2 dB, sifflantes retenues, compression 3 dB »).
- **Mixage dans les règles**, par élément : gain d'entrée (niveau de travail), coupe-bas selon
  l'élément, EQ soustractive dynamique (boue, résonances), compression adaptée, de-esser (voix),
  saturation légère (808), graves en mono, largeur au-dessus du grave.
- **Équilibre entre pistes** (avec le Master) : niveaux relatifs par rôle (voix devant, 808 et kick
  qui se partagent le bas : la 808 s'efface sur les coups de kick), masquages corrigés
  automatiquement (chef d'orchestre, appliqué au lieu de proposé).

### OMNIS Master (sur le master) — deux fenêtres
- **MIX** : toutes les pistes OMNIS Mix du projet sur un seul écran (nom, élément, niveau, ce qui
  est fait, curseur par piste), bouton **MIXER** (équilibre automatique), conflits corrigés. Sans
  aucune piste OMNIS Mix : un message clair (« Mets OMNIS Mix sur tes pistes pour le mixage ; le
  mastering marche déjà seul »).
- **MASTER** : **STYLE** (menu RÉF simplifié), **NIVEAU** (Streaming −14 · Équilibré −11 ·
  Fort −9 · Très fort −7 LUFS), **AUTO**, **AVANT / APRÈS** (à volume égal), voyant **PRÊT À
  EXPORTER**. Le rack de 7 modules passe dans **AVANCÉ**.
- La barre IA : une ligne discrète en haut des deux fenêtres.

### Rack modulaire, modules à caractère
- Les modules se **retirent, s'ajoutent et se déplacent** (glisser-déposer), sur le Master comme
  sur chaque piste. Une chaîne = une liste ordonnée de modules ; l'état garde la liste.
- Chaque type de module a plusieurs **caractères**, choisis à la main ou par l'IA :
  - **Compresseur** : *Opto* (lent, doux, programme), *FET* (rapide, mordant), *VCA bus*
    (colle, propre), *Vari-mu* (lampes, arrondi) ;
  - **Saturation multibande** (remplace l'Inflator seul) : *Bande*, *Lampe*, *Transistor*,
    *Inflate* (l'actuel), par bande avec dosage ;
  - **EQ** : *Propre* (phase linéaire), *Analogique* (cloches larges, à pentes douces), *Dynamique* ;
  - **Imager** : *Bandes* (actuel), *Haas / profondeur*, *Mono-maker* ;
  - **Clipper**, **Limiteur**, **De-esser**, **Transient**, **Sub / 808** : un ou deux caractères.
- Aucun nom de marque dans l'interface (« Opto », pas « LA-2A ») ; les principes seulement,
  reformulés (règle du projet).
- L'IA choisit les modules et leur caractère selon l'élément, le style et ce qu'elle entend, et
  dit pourquoi (« voix : compresseur Opto, la dynamique est large et le style est doux »).

### AUTO précis
- **Tout le morceau** est écouté (ou le **fichier glissé** : analyse complète sans le rejouer),
  découpé en **parties** (intro, couplets, refrains, passages à part) ; aucune partie n'est oubliée.
- **Plusieurs essais** : des chaînes candidates sont rendues hors ligne sur l'audio entendu
  (gardé en mémoire le temps du calcul, jamais écrit), notées sur les critères du §6 et sur l'écart
  au style, et la meilleure est gardée. La personne voit le résultat et pourquoi.

### IA (après le son)
- **Modèle de langue local** (1,5 à 3 Go, licence commerciale vérifiée, CPU, thread à part,
  sortie en JSON contraint vers les réglages) : comprend toutes les façons de parler, l'argot, les
  fautes, les phrases longues ; explique ce qu'il fait ; ne fait jamais l'inverse (test : chaque
  demande d'une banque de phrases réelles, avec le sens attendu).
- Le lexique actuel répond tout de suite et sert de secours si le modèle n'est pas là.
- **Glisser un fichier audio sur la barre IA** : reconnaissance (étiquettes, nom, MusicBrainz si
  activé), mesure, et le style de la prod visé.

### Interface et visuels (en dernier)
- Fenêtres propres et lisibles ; **presets par style** (avec emblème), aperçu avant / après.
- **Visuels originaux** dessinés par le code, dans l'esprit rap FR (emblèmes de styles,
  textures, petites illustrations). Aucune image livrée sous droits de tiers.

## 5. Étapes

| # | Étape | Fin quand |
|---|---|---|
| 0 | **Reproduire** : rendu hors ligne des stems de l'utilisateur (DRUMS, 808, MELO du 24/10) et de « à la millanaise 2 » avec le code actuel ; confirmer les causes du §2. Les sons restent hors dépôt. | causes chiffrées |
| 1 | **Règles du §3 dans le moteur actuel** : plus de compensation continue, upward éteint par défaut, bas-médium et largeur bornés, filtre DC. | critères §6 tenus sur les fichiers |
| 2 | **Deux plug-ins** : fabrique, UID, processeur / contrôleur par variante, état, validateur pour les deux. | validateur 2 × vert, sessions anciennes relues |
| 3 | **Rack modulaire** : chaîne = liste de modules ; modules à caractère (compresseurs Opto / FET / VCA / Vari-mu, saturation multibande, EQ, imagers…). | chaque caractère testé (réponse, distorsion, null-test neutre) |
| 4 | **Chaîne OMNIS Mix** refaite par élément + équilibre entre pistes + 808 / kick ; l'IA choisit les modules. | critères §6 sur les stems |
| 5 | **AUTO précis** : morceau entier ou fichier glissé, parties, essais multiples notés. | critères §6 sur « à la millanaise 2 » |
| 6 | **IA locale** : modèle de langue, banque de phrases réelles, glisser-déposer de fichiers. | 0 contresens sur la banque |
| 7 | **Interfaces** : fenêtre Mix ; Master MIX / MASTER / AVANCÉ ; presets ; visuels originaux. | rendus `guishot` relus |
| 8 | **Guide et mode d'emploi** raccourcis. | |
| 9 | **Vérification** : tests, ASan / TSan, validateur, CI Windows / macOS, critères mesurés. | tout vert |

## 6. Critères d'acceptation (mesurés sur les fichiers de l'utilisateur)

- 250 Hz – 1 kHz : écart à l'original ≤ +1 dB (Mid), sauf demande.
- LRA de sortie ≥ 70 % de l'entrée ; passages calmes relevés ≤ +4 dB de plus que les forts.
- Side / Mid sous 300 Hz : jamais plus large que l'original.
- DC < 100 ppm ; true peak ≤ plafond.
- Fondu de −20 dB d'une piste (automation) : −20 dB ±0,5 dB à la sortie, avec OMNIS Mix et Master.
- OMNIS Mix : 0 échantillon de latence, aucune allocation dans `process()`.

## 7. Ce qu'il faut de l'utilisateur

- **Des stems complets d'un morceau avec la voix** (voix lead, backs, 808, drums, mélo) : sans voix,
  l'équilibre « voix devant » ne peut pas être réglé ni vérifié.
- Un essai dans FL Studio à la fin des étapes 1, 4, 5 et 7.
- Une **banque de phrases** : comment toi et ton testeur parlez à OMNIS (copier-coller de vraies
  demandes, avec ce que vous attendiez) ; c'est le test de l'IA.

## 8. À part : le carnet des genres (pour l'utilisateur seulement)

Une page privée : chaque genre et sous-genre, ses rappeurs, leurs albums et titres marquants, les
pochettes affichées par lien (Cover Art Archive, sans copie). Pour se repérer ; jamais livrée dans
le plug-in.
