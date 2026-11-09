# OMNIS 2 — Refonte : le mastering des beatmakers

*Plan du 08/11/2026, revu le 09/11 : OMNIS devient un plug-in de **mastering seulement**.*

## 1. Décisions de l'utilisateur

| Sujet | Choix |
|---|---|
| Produit | **Un seul plug-in de mastering**, pour les beatmakers. Le mixage (instances liées Stem / Master, chef d'orchestre, chaînes par piste, voix) est **retiré**. |
| Public | Beatmakers : prods instrumentales (808, drums, mélodies). Aucune voix à traiter. |
| Barre IA | **Gardée, discrète** : tout se fait sans elle. |
| Tutoriel | Gardé, raccourci ; l'interface doit se comprendre sans lui. |
| IA | **Refaite** avec un **modèle de langue local, hors ligne** (le lexique reste pour l'instantané et en secours). |
| Apprentissage | Un **système qui apprend** (« neuronal ») : une base propre de ce qui se passe, un petit réseau qui s'ajuste, et plus tard une mise en commun anonyme via le serveur d'un ami de l'utilisateur. |
| Visuels | **Originaux**, dessinés par OMNIS (aucune pochette ni photo de rappeur livrée : droits des labels). |
| Interface (09/11) | **Simplifiée au maximum**, dans le **style du site abrasion labs** (pas celui de Micro-Contact, écarté) : noir et grille de 48 px, halo et accent **ambre** #FFAA00 d'OMNIS, Space Mono (titres, étiquettes en capitales espacées) + Space Grotesk, texte « verre », aucun arrondi, boutons à coins coupés, crochets d'angle, étiquettes de suivi « 01 · OBJ · SUB », HUD, carrés qui suivent le pointeur, curseur ambre clignotant dans la barre IA. Chaîne **à la Guitar Rig** : bibliothèque de modules à glisser-déposer (ajouter, retirer, déplacer, plusieurs caractères), ou construite par l'IA avec une animation. Le détail d'un module n'apparaît qu'en le cliquant. Maquette validée avant le code : https://claude.ai/artifact/JMr9KJZghGe5a9m5n9GNH6 |
| Limiteur (09/11) | « Ça compresse trop, ça tue la vibe » : AUTO ne met plus de **Punch** d'office (mesuré : 6 dB de distorsion et 7× plus de continu en plus, aucun relief gagné). Le Punch ne bouge que sur demande. |
| Méthode | Étapes enchaînées en autonomie, **vérifiées à chaque fois** (tests, mesures sur les prods de l'utilisateur, validateur, CI) ; l'utilisateur teste quand une étape est prête. |

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

**Étape 0 faite (08/11) : causes mesurées** (`omnis_auto`, AUTO hors ligne, reproduit l'export à
0,1 dB près : calme +10,1 dB, LRA 7,3 LU) :
- **Match EQ vers le profil « rap fr »** : sub −2,5 dB, aigu −6 dB, côtés +3 à +4 dB à 160–320 Hz.
  Sans lui : LRA 10,3 LU, côtés deux fois moins élargis, limiteur +3,3 dB au lieu de +5,8.
  *Corrigé* : une référence seule ne monte plus 200–600 Hz de plus de +1 dB, n'élargit plus sous
  320 Hz (+1 dB au plus jusqu'à 600 Hz), n'assombrit plus de plus de 3 dB à partir de 2,5 kHz
  (test). Les mots de la personne passent au-delà.
- **Compression vers le haut automatique** (« la réf est plus dense », « ton calme est sous ton
  refrain ») : *retirée* ; OMNIS le signale et ne le fait que sur demande (« plus dense »).
- **Le volume visé** : −10,2 LUFS sur le refrain d'une prod à −13 (PLR 12,6 dB) demande ~8 dB de
  gain (clip +2, limiteur +5,8). Les passages calmes prennent tout ce gain, les forts ~3 dB de
  moins : c'est le fonctionnement normal d'un mastering, accentué par la poussée du clipper et la
  densité (Inflator, Punch). → étape 5 : le **NIVEAU** visé se choisit (Streaming · Équilibré ·
  Fort), et AUTO répartit clip / limiteur selon le contraste à garder.
- **Fondus « combattus »** : le compresseur des stems règle son seuil sur la crête récente et
  rend la réduction moyenne (le niveau de la piste est « gardé ») ; au master, un limiteur poussé
  de 6 dB relâche quand une piste est coupée et tout le reste remonte d'autant. → étapes 1 et 4.
- **DC** : créée par le **limiteur** quand il écrase des crêtes asymétriques (kick, 808) :
  +6 dB → 516 ppm (−66 dBFS, inaudible). Pas une saturation mal filtrée ; seuil du §6 relevé à
  1000 ppm, à revoir avec la refonte du limiteur.

## 3. Règles du son (non négociables, testées)

1. **Ne jamais combattre la personne.** Aucune compensation de niveau continue. Le volume se règle
   **une fois** (AUTO), puis il est figé. Couper une piste dans le projet ne doit pas faire
   « remonter » le reste plus que la réduction du limiteur ne l'impose (limiteur peu poussé par
   défaut, part du clip réglée par AUTO).
2. **Le contraste reste.** Par défaut, LRA de sortie ≥ 70 % de celui de l'entrée au niveau
   « Équilibré » ; plus compact seulement au niveau « Fort » ou sur demande.
3. **Le bas-médium se nettoie, il ne se gonfle pas.** Aucune correction automatique ne monte
   200–600 Hz de plus de +1 dB.
4. **Les graves au centre.** La largeur n'augmente jamais sous 320 Hz sans demande.
5. **Sortie propre.** DC < 1000 ppm (−60 dBFS), true peak tenu.
6. **Latence.** Inchangée (qualité max ~710 ms / travail ~90 ms).

## 4. Architecture cible

### Un plug-in, une fenêtre simple, un rack « Avancé »
- **Écran principal** : **STYLE** (presets par style, avec emblème), **NIVEAU** (Streaming −14 ·
  Équilibré −11 · Fort −9 · Très fort −7 LUFS), **AUTO**, **AVANT / APRÈS** (à volume égal),
  voyant **PRÊT À EXPORTER**, la barre IA en une ligne discrète, glisser-déposer d'un fichier.
- **AVANCÉ** : le rack modulaire.

### Rack modulaire, modules à caractère
- Les modules se **retirent, s'ajoutent et se déplacent** (glisser-déposer). Une chaîne = une liste
  ordonnée de modules ; l'état garde la liste.
- Chaque type de module a plusieurs **caractères**, choisis à la main ou par l'IA :
  - **Compresseur de bus** : *Opto* (lent, doux), *FET* (rapide, mordant), *VCA* (colle, propre),
    *Vari-mu* (lampes, arrondi) ;
  - **Saturation multibande** (remplace l'Inflator seul) : *Bande*, *Lampe*, *Transistor*,
    *Inflate* (l'actuel), par bande avec dosage ;
  - **EQ** : *Propre* (phase linéaire), *Analogique* (cloches larges), *Dynamique* (l'actuelle) ;
  - **Imager** : *Bandes* (actuel), *Profondeur*, *Mono du grave* ;
  - **Clipper**, **Limiteur**, **Transient**, **Sub / 808** : un ou deux caractères.
- Aucun nom de marque dans l'interface (« Opto », pas « LA-2A ») ; principes reformulés seulement.
- L'IA choisit modules et caractères selon le style et ce qu'elle entend, et dit pourquoi.

### AUTO précis
- **Tout le morceau** est écouté (ou le **fichier glissé**, analysé en entier sans le rejouer),
  découpé en **parties** (intro, couplets, refrains, passages à part).
- **Plusieurs essais** : des chaînes candidates sont rendues hors ligne sur l'audio entendu (gardé en
  mémoire le temps du calcul, jamais écrit), notées sur les critères du §6 et l'écart au style ; la
  meilleure est gardée, et OMNIS dit pourquoi.

### IA (après le son)
- **Modèle de langue local** (1,5 à 3 Go, licence commerciale vérifiée, CPU, thread à part, sortie
  JSON contrainte vers les réglages) : comprend toutes les façons de parler, l'argot, les fautes ;
  explique ce qu'il fait ; ne fait jamais l'inverse (banque de phrases réelles en test).
- Le lexique répond tout de suite et sert de secours.
- **Glisser un fichier audio sur la barre IA** : reconnaissance, mesure, style visé.

### Apprentissage (le « système neuronal »)
1. **Une base propre, sur le PC** (`apprentissage/` dans le dossier de la personne) : un journal
   versionné d'événements — AUTO lancé (mesures de la prod, style, réglages proposés), retouches
   faites ensuite (écarts), demandes en mots et ce qui a été fait, « annule » (= mauvais choix),
   A / B choisi, réglages au moment de l'export (= bon choix). Jamais d'audio, jamais de nom de
   fichier ; des nombres.
2. **Un petit réseau de neurones local** (quelques milliers de poids) qui apprend de ce journal
   les écarts que la personne applique d'habitude selon la prod et le style, et corrige AUTO en
   conséquence — toujours dans les bornes du §3. Entraîné sur le PC, en tâche de fond.
3. **Plus tard, la mise en commun** : avec l'accord explicite de la personne, un export anonyme
   (mesures + écarts, aucun audio ni nom) vers le serveur de l'ami de l'utilisateur ; le serveur
   entraîne un modèle commun, redistribué comme mise à jour de données. Côté serveur : un service
   séparé, déployé par l'utilisateur (accès SSH à son ami, jamais dans le plug-in ni dans le chat).

### Interface et visuels (en dernier)
- Fenêtre propre et lisible ; presets par style avec emblème ; aperçu avant / après.
- Visuels originaux dessinés par le code, dans l'esprit rap FR.

## 5. Étapes

| # | Étape | Fin quand |
|---|---|---|
| 0 ✅ | **Reproduire** et mesurer les causes (`omnis_auto`). | causes chiffrées |
| 1 ✅ | **Mastering seul** : retirer instances liées, stems, chef d'orchestre, pages et réglages du mixage. | tests, validateur, CI verts |
| 2 ✅ | **Règles du son** : NIVEAU au choix (Streaming −14 · Équilibré −11 · Fort −9 · Très fort −7 · Style), densité (clip, Inflator) liée au niveau, **garde-fou** (compression permise au-delà de la marge de crête : 4 · 6 · 8 · 10 dB), AUTO à 35 % de l'écart au profil (sub ±1,5 dB). Sur « à la millanaise 2 », Équilibré : LRA 10,6 LU (72 %), limiteur +3,1 dB ; Fort : arrêté à −10,3 LUFS au lieu d'écraser (LRA 3,3 → 7,8). | critères §6 sur les prods de l'utilisateur |
| 3 ✅ | **AUTO précis** : écoute du **morceau entier** jusqu'à l'arrêt de la lecture (ou second clic, ou 6 s de silence ; 8 min au plus), ou **fichier glissé** sur la barre IA (Maj : référence) ; contraste des parties mesuré sur tout le morceau ; **3 essais** clip / limiteur (limiteur seul, 40/60, 70/30) sur les 15 s les plus fortes, menés au NIVEAU dans le garde-fou, notés (distorsion pondérée, balance tonale, sonie) ; le meilleur appliqué, puis volume ajusté à la mesure (après 4 s, le temps que le compteur suive). Tests : `TestProdListener` (3), plug-in AUTO (3). | critères §6 |
| 4 (en cours) | **Rack modulaire** à caractères (compresseurs, saturations, EQ, imagers…). Fait : **Inflator** à 4 caractères (Inflate, Bande, Lampe, Transistor ; `infl_char`, id 308), fondu de 10 ms au changement, barre IA (« à lampe », « effet bande », « transistor »). **Comp** (compresseur de bus, 8e module, ids 700–799, éteint par défaut) à 4 caractères (Opto, FET, VCA, Vari-mu), sans latence, détecteur sans le grave ; ordre du rack sur 8 (`rack_order`, id 14 ; l'ancien `chain_order` id 8 converti à la lecture) ; barre IA (« colle », « opto », « comp FET »…). **Match EQ Analogique** (`match_char`, id 202) : phase minimale (cepstre) à latence identique, cloches lissées ±1/3 d'octave ; pré-écho 4·10⁻³² contre 1·10⁻³ en Propre. Reste : caractères de l'Imager, ajouter / retirer des modules. | chaque caractère testé (réponse, distorsion, null-test neutre) |
| 5 | **Apprentissage** : journal, réseau local, tests ; export anonyme prêt pour le serveur. | tests, base relue à l'identique |
| 6 | **IA locale** : modèle de langue, banque de phrases réelles, glisser-déposer. | 0 contresens sur la banque |
| 7 | **Interface** : refonte dans l'esprit Abrasion d'après la maquette (chaîne glisser-déposer, bibliothèque, caractères, IA animée, STYLE / NIVEAU / AUTO, réglage au clic) ; modules en plusieurs exemplaires possibles. | maquette validée par l'utilisateur ; rendus `guishot` relus |
| 8 | **Guide et mode d'emploi** raccourcis. | |
| 9 | **Vérification** finale : tests, ASan / TSan, validateur, CI Windows / macOS, critères mesurés. | tout vert |

## 6. Critères d'acceptation (mesurés sur les fichiers de l'utilisateur)

- 200–600 Hz : écart de courbe automatique ≤ +1 dB (Mid), sauf demande.
- LRA de sortie ≥ 70 % de l'entrée ; passages calmes relevés ≤ +4 dB de plus que les forts.
- Side / Mid sous 300 Hz : jamais plus large que l'original.
- DC < 1000 ppm ; true peak ≤ plafond.
- Aucune allocation dans `process()` ; validateur Steinberg vert.

## 7. Ce qu'il faut de l'utilisateur

- Des **prods de beatmaker** (instrumentales), entières, et si possible une version masterisée
  à son goût d'une ou deux d'entre elles (la cible).
- Une **banque de phrases** : comment lui et ses potes parlent à OMNIS, avec ce qu'ils
  attendaient ; c'est le test de l'IA.
- Un essai dans FL Studio à la fin des étapes 2, 3, 4 et 7.

## 8. À part : le carnet des genres (pour l'utilisateur seulement)

Page privée : chaque style, ses rappeurs, leurs albums (pochettes en lien, Cover Art Archive),
les titres mesurés. Jamais livrée dans le plug-in.
