# Suivi du projet : ce qui est attendu, ce qui reste à faire

## En attente de l'utilisateur

- **Provenance des références (état réel au 03/11/2026).**
  - **Base perso de l'utilisateur** (`C:\OMNIS_mesure\references_utilisateur.txt`, sur son PC, hors
    dépôt) : 338 titres de `data/attendu_500.csv` mesurés le 03/11 à partir de **téléchargements
    YouTube** (choix de l'utilisateur ; le lien youtube.com est dans les tags de chaque fichier et
    `correspondance.csv` l'indique titre par titre). Aucun audio n'est gardé : seules les mesures.
  - **Base livrée** (`data/references.txt`, embarquée dans le plug-in vendu) : selon
    l'utilisateur (04/11), ses mesures viennent elles aussi de fichiers **non obtenus légalement**
    (téléchargements YouTube et autres). `LICENSE_NOTICES.md` a été corrigé le 04/11 pour le dire
    (il affirmait « fichiers obtenus légalement »). **04/11 : base perso fusionnée dans la base
    livrée avec l'accord explicite de l'utilisateur** (426 titres : 91 + 335 nouveaux, 3 remesurés ;
    familles reprises de `attendu_500.csv`, co-crédits GRY / Omyll rangés sous le nom de la liste).
    Liste : `docs/references_utilisees.csv`.
  - **Risque à trancher avant la vente** (pas un avis juridique) : vendre un produit dont la base
    est dérivée de téléchargements sans licence, et l'écrire dans sa notice, expose l'éditeur.
    Voie propre : remesurer la base livrée sur des fichiers achetés (FLAC / WAV de Bandcamp,
    Qobuz…) avant la sortie, puis remettre la notice en « achetés ». À voir avec un juriste.
- **Mesure de `data/attendu_500.csv` (03/11/2026)** : 338 mesurés, 80 refusés pour mauvaise
  durée, 82 sans fichier. 36 refus venaient d'une durée **par défaut de 185 s** écrite dans la
  liste pour les durées inconnues (erreur des scripts, corrigée : vraie durée Deezer, ou 0 =
  durée non contrôlée). Les 44 autres refus sont de vraies versions de clips, plus longues
  (Thriller 822 s au lieu de 358…) : à ne pas forcer. GRY / Omyll : 9 sur 10 mesurés
  (« Omyll - JSP » : durée inconnue, désormais non contrôlée). À remesurer : les 36.
- **Familles à confirmer** pour 22 artistes écoutés mais sans famille. Ils servent quand on les
  nomme, mais n'entrent dans aucun profil de genre :
  - Hades 972, La Goutte au Nez, FLY, Nidjo Sensei, Keezy, A-JEY, El Hindi, BloodyWraith Pee, ALP ;
  - lul bam, BBY GOYARD, Famous Freaky, G10KK, Mali, Tan the Terrible & GOON DES GARCONS, Nechie,
    Lil Shine, Long Beatz, Kyloaded ;
  - Harrison, Stains On A Road, Lila Hart.
- **Écoute** des masters « 808 tenue » (m6, SLAYR et poster boy), à comparer à la v3. Cette écoute
  décide du dosage de la tenue multibande et de l'étape suivante (saturation dédiée de la 808 ou
  non).
- **Onirique** (distribution + DRM) : savoir si leur outil de développeur permet d'envoyer des
  données personnalisées, d'héberger un fichier de données et de pousser les mises à jour.
  Ça détermine comment remontent les retours d'apprentissage (docs/APPRENTISSAGE.md §5).

## Décisions de l'utilisateur (à respecter)

- Tout en local : ni internet ni coût pour le son et l'IA. Le plug-in peut peser 1 à 2 Go
  (modèle de compréhension embarqué au module 5).
- Pas de mode figé Club / Safe : l'intensité vient de la demande.
- Ressembler à la référence sans copier ni détruire la prod. Le sub et la 808 font partie de
  l'identité des prods de l'utilisateur.
- (09/11) **Mastering seulement**, pour les beatmakers (pas de voix, pas de mixage piste par piste).
  Un système d'apprentissage (« neuronal ») : base locale propre, petit réseau local, puis mise en
  commun anonyme via le serveur d'un ami (accès SSH géré par l'utilisateur, jamais dans le chat).
- (10/11) **Usages sur le compte** : le graphe des usages va seulement sur le compte onirique.store
  de la personne, illisible par le public et même par le fondateur ; les données servent, comme
  les références son, à une moyenne qui fait évoluer le plug-in. Mise en œuvre :
  `docs/DONNEES_COMPTE.md` (espace chiffré sur le PC + contribution anonyme séparée, ≥ 5 personnes).
- Genres : sa propre classification (8 familles de playlists, `data/genres_classement.txt`) ;
  2hollis et SLAYR : rage, hyperpop.
- (05/11) Licence et signature Mac : gérées par ONIRIQUE. Windows : pas de certificat (SmartScreen
  se calme avec les téléchargements). Base de références livrée gardée telle quelle ; titres
  rachetés et remesurés plus tard, avec les ventes. Écoute : un ingé / producteur testera.
  Lancement : 89 €, puis 150 € après environ un mois ; mise en ligne d'ici 2 à 4 semaines.

## Fait récemment

- **10/11 — Fenêtre RÉF : tous les genres, une illustration chacun** (demande de l'utilisateur, avant
  d'envoyer le plug-in à des collègues). Le menu déroulant et l'entonnoir sont remplacés par une fenêtre
  (`plugin/vst3/RefBrowser.inc`, `OmnisController::genreCatalog`) : familles en onglets, une tuile par
  sous-genre (192 styles de `styles_artistes.txt` + ceux de la base), illustration dessinée par le code
  (couleur de la famille, motif du caractère du genre, silhouette de la courbe de son profil : aucune
  image livrée), « profil · N titres » ou « repère : la famille », fiche (artistes, tes références),
  UTILISER CE GENRE. Captures `guishot` : `omnis-master-ref-*.png`.

- **10/11 — La moyenne des utilisateurs** (`ai/CommunityHabits`, `omnis_refdb usages`,
  `data/usages_communaute.txt` embarqué, vide pour l'instant). « exporte mon apprentissage » écrit
  aussi `contribution_anonyme.tsv` ; AUTO applique la moyenne du style reconnu, de moins en moins à
  mesure que la personne a ses habitudes. Reste avec ONIRIQUE : l'API du compte (espace chiffré,
  envoi anonyme) — spécification dans `docs/DONNEES_COMPTE.md`.

- **10/11 — Carnet OMNIS pour tous les utilisateurs** (demande : « disponible à tout utilisateur sur la
  page d'OMNIS, connectés à leur compte, ils pourront surveiller leurs données »). `site/carnet/index.html`
  (généré par `tools/carnet/build_carnet.py`, ctest `omnis_carnet_a_jour`) : carnet, graphe, IA · usages,
  Mes données. Autonome, aucune requête (CSP). Le compte ouvre la page ; les données restent sur le PC
  et sont lues dans le navigateur (`docs/CARNET.md`). À faire côté ONIRIQUE : la mettre en ligne.

- **10/11 — Juge : verdict « choix artistique » (DA)** (demande de l'utilisateur : « une partie
  créative / artistique / DA dans le verdict du juge »). Un geste contre la règle générale est une DA
  quand le style visé l'assume (table `daRules` : écraser en rage / hard clipping / hyperpop / phonk,
  sombre en lo-fi / boom bap / plugg, très large en ambient…) ou quand il est gardé jusqu'à l'export une
  2e fois dans le même contexte (`reviewArtistic`). Appris (le réseau tient compte du style) ; limiteur
  éteint jamais. Graphe v2 (v1 se relit), « mes habitudes » cite les choix artistiques.

- **10/11 — Sous-genres des artistes vérifiés avec MusicBrainz** (demande de l'utilisateur). Genres votés
  des 440 artistes de `data/styles_artistes.txt` comparés aux sous-genres d'OMNIS : 244 sous-genres ajoutés
  (au moins 2 votes, 4 sous-genres au plus par artiste, version « fr » pour les artistes français), rien de
  retiré (MusicBrainz est muet sur la new wave fr, l'underground…, et son silence ne contredit pas un style).
  Familles : celle de l'utilisateur prime ; déplacés seulement là où ses données ET MusicBrainz s'accordent
  (Travis Scott, Drake → rap US ; The Weeknd, Pharrell → pop / R&B ; SOPHIE → hyperpop ; Soundgarden →
  rock). 21 Savage, Chief Keef, Pop Smoke, Post Malone restent en rap fr (ses playlists). Doublons fusionnés
  (JAŸ-Z = JAY-Z, JPEGMAFIA). Homonymes sur MusicBrainz : GRY, Luther, MISCHA. Profils régénérés (40) ;
  « drill fr » et « rap hardcore » (fr) passent sous 3 titres, d'autres apparaissent (gangsta rap, pop rap,
  rap mélodique US, rap abstrait). Carnet : genres MusicBrainz sur chaque fiche, 393 identités confirmées.

- **10/11 — Graphe de ce que les personnes font le plus, et juge d'utilité** (demande de
  l'utilisateur : « répertorier ce que les utilisateurs font le plus, seulement après AUTO ou un
  preset ; les modifications les plus répétées enregistrées, les inutiles ignorées ; l'IA doit avoir
  une idée de base de ce qui est utile »). `ai/UsageGraph` : séances (AUTO / preset), gestes jugés
  (utile, inaudible, hésitation, à risque, annulé) par des règles de mastering, graphe local
  (contextes, gestes, liens), habitudes (3 fois, 60 % utile), oubli de ce qui ne revient pas ; seuls
  les gestes utiles nourrissent le réseau ; le volume mené par OMNIS n'est plus pris pour une
  retouche. Barre IA « mes habitudes », export `graphe_usages.json`. Tests : 2 unitaires, plug-in
  (3 AUTO + sub à la main → « Plus de sub (3 fois) », rien sur le volume).
- **09/11 — Écouter toute la prod** (demande de l'utilisateur, « pour que ce soit hyper précis ») :
  chaque bus écoute le morceau entier (plus de limite à 3 min : ~23 min gardées, trames en float),
  sans perte (file de ~11 s, vidée 4 s par tour), et chaque seconde du projet une seule fois (une
  boucle rejouée ne double pas les coups). La page dit « morceau écouté : 2:27 » (ou les passages
  manquants) ; RÉÉCOUTER (page ou barre IA) remet tous les bus à zéro après un changement du mix.
  Test : 8 s jouées puis rejouées deux fois → 19 kicks avant comme après ; réécoute → 0 puis 19.
- **09/11 — OMNIS sur les bus : page COMPOSITION, groupe EFX** (demande de l'utilisateur : voir
  ce qu'il y a dans chaque bus, un groupe EFX pour les reverse cymbals ; puis, décision : **rien sur
  les pistes**, « sinon ça pose problème au mixage », et pas de script FL : « on garde uniquement
  l'option 1 », l'écoute). VST3 ne donne à un plug-in que le nom de SA piste : le bus ne connaît pas
  les noms de ses pistes, il entend ce qu'il contient. RÔLE (`role`, id 15) : Master / Bus ; en Bus
  le son passe bit à bit, latence 0. `stems/BusListener` : séparation tenu / frappé / bruit (HPSS,
  médianes 230 ms × 17 bins, marge 2), puis règles lisibles : DRUMS (`stems/DrumBus`, confirmé par
  la forme des attaques : kick dans le sub, snare / clap = bruit large qui tient > 30 ms et monte
  dans l'aigu, assez de coups pour la durée), 808 (grave tenu ≥ 250 ms à 12 dB de son sommet), MÉLO
  (harmonique du médium hors harmoniques de la 808 ; nappe / lead), EFX (montées continues de
  l'aigu, sans creux : reverse < 1,5 s coupée net, riser ≥ 1,5 s ; impact : coup large qui retombe).
  Vérifié sur les stems de l'utilisateur (`omnis_bus`) : DRUMS = kick 168, snare / clap 363, hats
  145, open hats 44 (+ 2 reverse plausibles à 60,9 et 105,6 s) ; MELO et « sans omnis melo » =
  lead + nappe seulement ; 808 = 808 seulement. Les fausses détections trouvées en route (rentrées
  de hats, roulements de snare, attaques de notes, mélo qui démarre après un silence) ont chacune
  leur garde. Région partagée v4 (`link/LinkBus`) : nom du bus et ce qu'il contient. Page
  COMPOSITION, barre IA « composition », « rôle bus / master ». Tests : 6 écouteur de bus, 3
  composition / lien (dont TSan), 1 plug-in (3 bus + master).
- **09/11 — Étape 5 : apprendre les habitudes** (`ai/HabitLearning`, contrôleur). Journal sur le
  poste (`apprentissage/journal.tsv`, versionné, nombres seulement : grandeurs de la prod, groupe,
  réglages proposés, écarts retouchés, « annule » après AUTO, réglages à l'export). Petit réseau
  (16 neurones tanh + chemin linéaire direct, 6 sorties bornées) réentraîné à chaque retouche,
  sauvegardé (`reseau.txt`) ; il corrige AUTO avec une part n/(n+8) et le dit (« ton habitude :
  sub +0.6 dB »). Export anonyme (« exporte mon apprentissage »), « oublie mes habitudes ». Les
  tests écrivent dans un dossier temporaire (`OMNIS_TEST_USER_DIR`), plus jamais dans le vrai
  dossier. Tests : 3 unitaires (journal relu à l'identique, courbe d'apprentissage, bornes et
  hasard), 1 plug-in (3 séances « sub +2 » → 4e AUTO propose +0,55 dB et l'annonce).
- **09/11 — Reconnaissance du style à l'écoute** (`ai/StyleRecognizer`, `data/groupes_sonores.txt`
  via `omnis_refdb groupes`). 10 groupes sonores (trap, drill, rage, plugg / new wave, boom bap,
  mélodique / pop, rock, électro, hyperpop, B.O.) ; moyenne et dispersion par groupe (agrégées,
  aucune fiche de titre). Mesuré honnêtement (`omnis_refdb style-eval --groupes`, validation
  croisée en 5 plis par artiste) : bon groupe en tête 34 %, dans les 3 premiers 75 % (10 groupes ;
  le plus gros fait 29 %) ; probabilités calibrées (« 34 % » juste une fois sur trois). Limite : la
  base = morceaux masterisés avec voix, pas des instrumentales ; sur les prods de l'utilisateur,
  biais vers « rage ». D'où la **mémoire personnelle** (`styles_appris.txt`) : nommer le style
  d'une prod l'apprend ; elle pèse jusqu'à 80 %. AUTO reconnaît quand aucun style n'est nommé,
  « c'est quel style ? » répond. Tests : 4 unitaires, 2 plug-in.
- **09/11 — Limiteur : « ça compresse trop »** (retour de l'utilisateur). Cause mesurée sur « à la
  millanaise 2 » : AUTO mettait le **Punch** à 32 % (base 15 % + écart d'attaque avec la réf) ; il
  remonte chaque attaque, que le limiteur réécrase. Sans lui, à volume égal (−11 LUFS) :
  distorsion −27,2 dB au lieu de −21,5, écart tonal 0,6 au lieu de 1,2 dB, continu 111 ppm au lieu
  de 796, même relief (PLR 13,0). AUTO ne pousse plus le Punch : il le propose (« dis plus de
  punch »). **Maquette de la nouvelle interface** (style du site abrasion labs — le style Micro-Contact est écarté —, chaîne à la Guitar Rig) :
  https://claude.ai/artifact/JMr9KJZghGe5a9m5n9GNH6 — à valider avant la refonte (étape 7).
- **09/11 — Barre IA : audit des inversions** (tolérance aux fautes trop large) et **banque de
  60 phrases réelles** (`tests/TestPhraseBank.cpp` : parlé, SMS, argot, anglais ; axe et sens
  vérifiés). Corrigé : « **mon** kick est trop fort » montait le kick (« mon » lu « monte ») ;
  « mon 808 est trop forte » montait le sub ; « c'est écrasé » lu « éclaté » ; « faible »,
  « bave », « piquent » non compris. Règles : un mot court ne vaut pas un mot plus long de 2
  lettres ; un mot connu tel quel du lexique n'est jamais la faute d'un autre ; « sauf », « less »
  exacts. **Ajouter à la banque chaque phrase réelle mal comprise.**
- **09/11 — Match EQ « Analogique »** : même courbe, phase minimale (méthode du cepstre, réponse
  placée à N/2 : latence et PDC inchangées) et cloches lissées sur ±1/3 d'octave ; aucun pré-écho
  devant les transitoires (mesuré). Sélecteur CARACTÈRE, écran lissé, barre IA (« EQ analogique »).
  **Corrigé** : « passe » était lu « pas » (négation) et « son » « sans » — « passe le grave en
  mono » élargissait le grave, « passe le sub plus fort » le baissait. Test de non-régression.
- **09/11 — Compresseur de bus « Comp »** (refonte, étape 4) : 8e module du rack, après le Match
  EQ, **éteint par défaut** (les sessions sonnent pareil). Caractères Opto (relâchement selon le
  programme), FET (crête), VCA (colle propre), Vari-mu (genou de 18 dB) ; sans latence, stéréo lié,
  passe-haut du détecteur (90 Hz : la 808 ne pompe pas), Mix parallèle. Ordre du rack sur 8
  modules (`rack_order`, id 14) ; les sessions et mémoires A/B à 7 modules (id 8) sont converties
  à la lecture. Interface : rack à 8 (titres resserrés), page COMP, gros bouton SQUEEZE, DELTA.
  Barre IA : « colle », « opto », « comp FET », « VCA », « vari-mu », « moins de colle » ;
  « comp » n'est plus lu « coupe », une expression du lexique n'est plus prise pour un titre
  approchant (« comp fet » ≠ « Compresses »). Tests : compresseur (7), rack, état, barre IA.
- **09/11 — Caractères de saturation** (refonte, étape 4) : l'Inflator a 4 caractères — Inflate
  (densité), Bande (crêtes tassées, impaires), Lampe (asymétrique, paires), Transistor (coude
  franc, impaires hautes) —, même structure parallèle (exacte au repos), fondu de 10 ms au
  changement. Paramètre `infl_char` (id 308, 159 au total). Barre IA : « à lampe », « effet
  bande », « transistor » (« tape » seul reste « ça frappe »). Tests : courbes, harmoniques
  mesurées, pas de clic, barre IA.
- **09/11 — AUTO précis** (refonte, étape 3) : AUTO écoute le **morceau entier** (fin : arrêt de la
  lecture, second clic, 6 s de silence ou 8 min), ou un **son glissé** sur la barre IA (Maj : il
  rejoint les références). Puis 3 essais de répartition clip / limiteur sur les 15 s les plus
  fortes (en arrière-plan, `tools/common/LoudnessTuner` `searchMaster`, aussi dans le plug-in), le
  plus propre gardé, volume ajusté ensuite à la mesure. Corrigé au passage : la poursuite du volume
  attend 4 s que le compteur suive un nouveau réglage (dépassait de 2,4 dB), « 1 titres ».
- **09/11 — OMNIS devient un plug-in de mastering seul** (décision de l'utilisateur, beatmaker) :
  instances liées, stems, chef d'orchestre, bus de drums, pages STEM / CHEF, menus RÔLE / GR,
  13 paramètres (ids 5–7, 900–906, 1008–1010 réservés, jamais réutilisés) et `data/elements.txt`
  retirés. Plan : `docs/REFONTE.md`. Étape 0 (causes mesurées avec `omnis_auto`) faite.
- **05/11 — EQ dynamique** dans le Match EQ (vue DYN, `dsp/DynamicEq`) : 4 bandes Mid / Side après
  la courbe à phase linéaire, sans latence, qui n'agissent que quand la bande sort de son
  équilibre habituel dans le mix (seuil en dB au-dessus de l'habitude, indépendant du volume).
  26 paramètres (272–297), 170 au total. « ça siffle » / « agressif » en allument une (barre IA).
  Tests : `TestDynamicEq` (6), barre IA. CPU : non mesurable.
- **05/11 — Guide** : 14 étapes (RÉF / import, EQ dynamique, latence, écoutes et export) ;
  « tuto » / « aide » dans la barre IA ; après une mise à jour, seules les étapes nouvelles.
- **05/11 — Bilan de robustesse** (rapport §15.1) : ASan / UBSan / TSan / fuzz / validateur
  étendu / clang-tidy. Corrigés : lecture de session (malloc / delete[], chaîne sans zéro),
  allocations de l'écoute simulée sur le thread audio, hôte aberrant (fréquence hors 1 kHz – 2 MHz, bloc), sûreté de
  l'import. Nouveaux tests permanents : `TestRobustness`, `TestPluginRealtime`, sessions
  corrompues, hôte aberrant.
- **04/11 — Ajouter un fichier de référence** (menu RÉF) : étiquettes, nom « Artiste - Titre » /
  « Titre - Artiste », MusicBrainz (une requête par fichier, désactivable), pochette du fichier ;
  `omnis_refdb reconnaitre`. Deezer retiré (API non commerciale). Notice mise à jour (seule
  connexion : MusicBrainz).
- **04/11 — Base livrée = profils de genre seulement** : `data/profils_genres.txt` (39 profils :
  7 familles, 32 sous-genres, ≥ 3 titres chacun), généré par `omnis_refdb profils` depuis
  `data/references.txt` (426 titres, non intégré). Chaque titre est rangé dans UN sous-genre par
  la mesure (`docs/classement_titres.csv`). Notice de licence mise à jour. 20 titres sans
  famille (artistes à classer) restent hors profils.
- **03/11 — Filtre RÉF famille › sous-genre** (`data/styles_artistes.txt`, ~440 artistes, co-crédits
  reconnus) et **rapport technique complet** pour un ingé : [docs/RAPPORT_TECHNIQUE.md](RAPPORT_TECHNIQUE.md)
  (127 artistes encore « à classer » en annexe B).
- **Interface, première version** (VSTGUI du SDK, dessinée en code, Windows et macOS) : rôle,
  groupe, bypass ; la piste (mode, élément, élément reconnu, doses du mode Manuel) ; le master
  (LUFS, true peak, LRA, corrélation, stems liés ; entrée, Inflator, clip, limiteur, plafond,
  sortie) ; le chef d'orchestre (propositions une à une, Oui / Non, retouches en place, Annuler).
  Le plug-in prêt à installer est publié par la CI (artefact OMNIS-Windows-VST3).
- **Stems, étape 3 : le master chef d'orchestre.** Il écoute les stems alignés sur le temps du
  projet, repère qu'un élément en masque un autre au même moment (grave d'un instrument sur la
  808 / la basse / le kick, instrument sur la présence de la voix) et propose une retouche en
  français (« Couper le grave de « Piano » sous 150 Hz ? »). Rien n'est appliqué sans accord ;
  acceptée, la retouche part au stem et s'ajoute à sa chaîne, sans latence. Sur la prod de
  l'utilisateur : aucune fausse alerte (la mélo est 28 dB sous la 808 quand elles jouent ensemble).
- **Stems calibrés sur une vraie prod (808, DRUMS, MELO, 2 min 49).** Trois défauts que les sons
  synthétiques cachaient, corrigés :
  - 808 : le passe-haut à 25 Hz faisait passer la crête de −0,2 à +3,7 dBFS (808 écrêtée, continu
    dans chaque attaque ; même à 10 Hz : +3 dB). Plus de passe-haut automatique sur kick, 808,
    basse. Crête après la chaîne : −0,13 dBFS ;
  - DRUMS (bus sans kick) entendu comme « hi-hat » et coupé à 350 Hz : le nom d'un groupe
    (« drums », « loop ») l'emporte maintenant ;
  - MELO (63 % de l'énergie à 150–400 Hz) : l'EQ dynamique coupait son corps 60 % du temps. Il ne
    coupe plus que ce qui dépasse l'habitude de la piste de 3 dB : 6 % du temps.
  Outil `omnis_stems` pour refaire ces mesures. L'audio reste chez l'utilisateur.
- **Stems, étape 2 : chaque stem a sa chaîne, sans latence.** « Stem Mode » Off (défaut) / Auto /
  Manuel. Drums : compression, saturation par la courbe de l'Inflator, espace (mono dans le grave,
  largeur des hats). 808 : saturation et EQ dynamique. Instruments et voix : EQ dynamique (boue,
  carton, dureté, sifflantes). Un son déjà saturé, déjà large ou qui joue dans le grave est
  respecté. Bornes fixes. La reconnaissance écoute le son sec (avant la chaîne). Modèles à
  calibrer sur de vrais stems.
- **Stems, étape 1 : chaque stem reconnaît son élément.** 21 éléments (kick, snare, clap, hi-hat,
  open hat, cymbale, percu, boucle, 808, basse, piano, guitare, cuivres, cordes, nappe, lead, pluck,
  voix, backs, FX, autre). Par l'écoute d'abord, parce que les sons de packs ont des noms illisibles ;
  le nom de la piste (donné par le DAW, norme VST3) précise. Le master voit l'élément et le nom de
  chaque stem. À calibrer sur de vrais stems.

- **Compréhension v2** : anglais courant, SMS et argot (`data/argot.txt`), modificateurs placés
  après (« propre de ouf », « clean af »), sujet + niveau : « la 808 est trop forte » baisse la
  808 au lieu d'ajouter du sub (défaut corrigé), « the vocals are too quiet » monte la voix.
- **Optimisation** : environ 25 % d'instructions en moins sur la chaîne complète, sortie identique
  au bit près (convolution vectorisée, FFT, demi-bandes, true peak du limiteur, exp/pow évités).
- **Robustesse** : un NaN ou un infini venu de l'hôte rendait la sortie invalide puis muette
  jusqu'à la remise à zéro. L'entrée est maintenant nettoyée ; si un étage produit malgré tout un
  résultat non fini, la sortie est mise à zéro le temps du bloc et les états sont vidés. Même
  protection sur l'audio qu'un stem envoie au master.
- **Inflator en 8x par défaut** (au lieu de 4x) : avec du drive, le repliement passe de −48 à
  −76 dB sous la fondamentale (sinus de 13 kHz, drive +12 dB). Coût : environ +17 % sur la
  chaîne. Les projets déjà enregistrés gardent leur réglage.

- **Références de l'utilisateur (22/10).**
  - 97 sons ont été mesurés sur son PC (`omnis_refdb scan`, outils compilés sous Windows) : les
    sons restent sur son disque, seules les signatures sont versionnées.
  - 89 sons ont été versés dans `data/references.txt`, qui compte 91 entrées avec 2hollis et SLAYR.
  - 8 sons ont été écartés :
    - deux type beats : MORDWEY, Floaty Beats ;
    - deux compilations : Lungy, Lofi Holic ;
    - deux versions sped up / slowed : Kashpaint, nandooxz ;
    - Sad Music, à −25.8 LUFS ;
    - RUM3BAER - Luther, dont l'artiste est invérifiable.
  - Familles : 37 sons classés par les playlists de l'utilisateur, plus 30 artistes classés sans
    ambiguïté (`data/artistes_proches.txt`, section « Familles fixées »).
- **Aigu coupé par un encodeur.** Sur 33 des 91 références, le nœud 20 kHz est plus de 20 dB
  sous celui de 10 kHz : c'est un fichier compressé (YouTube, MP3 faible), même converti en FLAC.
  `SpectralSignature::detectCutoff` repère cette coupure. Le nœud coupé est alors exclu :
  - des profils (moyenne sur les seuls titres qui le mesurent) ;
  - de l'intention (aucune correction) ;
  - de la distance tonale.

  Sans ça, OMNIS aurait retiré de l'air aux prods pour imiter un encodeur.
- **Profils par famille** (provisoires, sur les 37 sons classés par les playlists).

  | | Sonie | PLR | LRA | Sub / corps | 320 Hz | 2.6 kHz | Side − Mid médiums |
  |---|---|---|---|---|---|---|---|
  | rap fr (26 sons) | −9.2 LUFS | 10.0 dB | 7.0 LU | +15 dB | +3.6 dB | −4.4 dB | −7 à −9 dB |
  | rap us (11 sons) | −8.3 LUFS | 9.2 dB | 5.2 LU | +14 dB | +0.8 dB | −1.9 dB | −14 dB |
  | rage (2 sons) | −4.8 LUFS | 6.3 dB | 5.7 LU | +11 dB | +0.8 dB | −2.7 dB | −8 dB |

  - Les masters propres V3 à V6 (−9.3 à −9.8 LUFS) sont dans la norme du rap fr et du rap us.
    Seul le rage est 4 à 5 dB plus fort.
  - Les profils sont fiables dans les médiums (1.3 à 2.6 dB d'écart d'un titre à l'autre). Le sub
    varie de 5 à 7 dB à 40 Hz d'un titre à l'autre : la limite de ±3 dB sur le sub reste justifiée.

- **Écoute d'une référence dans le DAW** (bus auxiliaire « Reference », paramètre « Listen
  Reference »). Au moins 60 s, écoutes cumulables, cœur du morceau seulement. Voir README,
  « Écouter une référence dans FL Studio ».
- **Analyse d'un fichier en parallèle** (identique au bit près) ; `omnis_refdb scan` mesure un
  fichier par cœur : prêt pour les références de ce soir.

## À venir

**Pratiques des pros et plan du rack : [docs/RECHERCHE_MIX_PRO.md](RECHERCHE_MIX_PRO.md).**
**Cap du produit : [docs/VISION.md](VISION.md)** (vendu ~150 €, parfait, propre comme un mix
pro, intuitif ; drums d'un bus travaillés élément par élément).

**Prochaine séance (dans l'ordre) :**
1. ~~**Plantage de FL Studio en désactivant OMNIS avec le bouton rond du slot du mixer**~~
   **Réglé le 25/10.** Cause reproduite : l'hôte réactive / reprépare l'instance
   (setActive, setupProcessing) pendant que son thread audio est encore dans process() ;
   le moteur était réalloué sous ses pieds (use-after-free relevé par ASan dans le
   clipper). Correctif : une porte entre les threads (`OmnisProcessor::GateHold`) :
   avant toute réallocation on ferme la porte et on attend la sortie de process() ;
   porte fermée ou instance inactive, le son passe tel quel et les automations sont
   gardées. Tampons nuls de l'hôte refusés proprement ; activation sans setupProcessing
   tolérée ; plus de redémarrage demandé à l'hôte au milieu de setComponentState.
   Test `tests/TestPluginSlotToggle.cpp` (Seul, master + stem liés, changements de rôle
   en route) : plante l'ancien code sous ASan, propre avec le correctif sous ASan et TSan.
2. ~~Refaire l'interface~~ **Fait le 25/10** : console v11 complète (rack, tiroir avec TOUS
   les réglages, écrans, colonne Master, mesures à verdict, A / B, Match, Delta, guide).
   Moteur : modules ON / OFF, ordre du rack, Delta, Gain Match ; limiteur Punch / upward /
   lien stéréo (le relief qui manquait face à Ozone : +1 dB de PLR à Punch 100 %).
3. ~~Barre IA~~ **Fait** : demande -> modules, volume mené par la mesure.
4. ~~OMNIS seul sur le master~~ **Fait** : AUTO (passage le plus fort, style de la base).
5. ~~Tutoriel~~ **Fait** : 10 étapes, « ? ».
6. **Bus de drums** : kick / snare / hats / open hat reconnus, travaillés, nommés (fait).

**À vérifier par l'utilisateur dans FL Studio** (rien de tout ça n'a pu être écouté ici) :
le bouton du slot (plus de plantage attendu), la console sous Windows (polices Segoe UI /
Consolas, rendu Direct2D), glisser un module, la barre IA sur une vraie prod, AUTO, le guide,
et ce qu'OMNIS dit de son bus de drums (est-ce juste ?).

**CI** : en échec depuis le 24/10 au soir en 2 s, sans journal, même pour des commits de
documentation : minutes GitHub Actions épuisées (pas le code). Elle reviendra avec le
nouveau mois ; d'ici là, construire sous Windows avec Visual Studio Build Tools (README).

**Encore à faire** : AU pour Logic ; anti-résonances spectral ; écoute simulée téléphone / voiture ; aperçu MP3 ; la voix et la prod ensemble ; Punch
réglé à l'oreille sur de vraies prods.

Compréhension corrigée (25/10) : dans « la 808 est trop dirty », la 808 dit seulement OÙ
agir (« là où agir »), elle ne reçoit plus de sub en plus ; « la 808 est trop forte » la
baisse toujours, « plus de 808 » la monte toujours (test).

Retours d'écoute (24/10, prod 808 / basse / 2 drums / mélo) : Auto jugé bien comme ça
(discret, respecte les sons déjà travaillés) ; pas de potard d'intensité demandé.

- **Chef d'orchestre, suite.**
  - L'interface complète de la console (tous les étages, la demande en texte, les courbes).
  - Garder les retouches acceptées quand le projet est rouvert (les stems changent d'identifiant
    à chaque activation : les rattacher au nom de la piste dans l'état du master).
  - Kick / 808 : la place de chacun dans le temps (ducking).
  - L'équilibre de chaque piste face à la référence.
  - Calibrer les seuils sur une prod qui a vraiment un masquage.

- Appliquer la référence écoutée depuis le plug-in : le contrôleur calcule l'intention et écrit
  les paramètres `match_*` / `mb_*` via performEdit. Ça demande l'interface (module 6), pour
  saisir la demande et nommer la référence.

## Le VPS de données (prévu par l'utilisateur, 30/10)

Pas d'audio. Il garde ce qu'OMNIS apprend et ce qu'on mesure :
- `references/` : la base des signatures (`references.txt` et suivants), qui peut grandir
  sans alourdir le dépôt du code (91 titres = 48 Ko, ~0,5 Ko par titre : 10 000 titres ≈ 5 Mo) ;
- `bancs/` : les résultats des bancs d'essai (OMNIS / Ozone, distorsion, true peak, sonie)
  et des tests, datés, pour suivre chaque version ;
- `retours/` : les retours d'écoute et les exports anonymes d'apprentissage
  (docs/APPRENTISSAGE.md), pour améliorer le lexique et les cibles ;
- `journal/` : les informations générales sur le plug-in (versions livrées, ce qui a été
  testé dans quel DAW).
Un dépôt Git privé à part (ex. `omnis-data`) sur le VPS : historique de chaque changement,
et accès par Remote Control (dossier racine d'un dépôt Git). Au moment d'une version, la base
de références est recopiée dans `data/` et embarquée dans le plug-in, qui reste 100 % hors
ligne. Accès : Remote Control sur le VPS (de préférence) ou clé SSH en secret de
l'environnement — jamais de clé collée dans la conversation.
