# Suivi du projet : ce qui est attendu, ce qui reste à faire

## En attente de l'utilisateur

- **Provenance des références.** Une partie des sons mesurés le 22/10 semble venir de YouTube :
  des noms de chaîne à la place de l'artiste dans les tags, des titres « Music Video ». La règle
  reste celle des sources légales (titres achetés, prods personnelles, sons fournis par les
  artistes). Si l'utilisateur identifie des sons venus de YouTube, il faut les retirer de
  `data/references.txt` (un bloc `[Artiste - Titre]` par son).
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
- Genres : sa propre classification (8 familles de playlists, `data/genres_classement.txt`) ;
  2hollis et SLAYR : rage, hyperpop.

## Fait récemment

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

**Cap du produit : [docs/VISION.md](VISION.md)** (vendu ~150 €, parfait, propre comme un mix
pro, intuitif ; drums d'un bus travaillés élément par élément).

**Prochaine séance (dans l'ordre) :**
1. **Plantage de FL Studio en désactivant OMNIS avec le bouton rond du slot du mixer**
   (signalé le 24/10 ; c'est LE geste quotidien des producteurs sous FL, il doit être sûr ;
   une instance Master et cinq Stems liés). Pistes : setActive pendant que FL appelle encore
   process() (stemChain_.prepare, réallocation de l'anneau sec, link_.start / stop),
   arrêt du lien master / stems, minuterie de l'interface après fermeture. Écrire un test
   qui active / désactive en boucle pendant le traitement, sous TSan / ASan.
2. **Refaire l'interface proprement** d'après la maquette console v11 (artefact, « juste
   magnifique » selon l'utilisateur) : modules usinés, colonne Master, courbe de l'Inflator.
   L'interface actuelle reste la version mini. PAS la simplicité de LeMaster (jugé trop
   pauvre en réglages, « ne fait pas toujours le taf ») : garder TOUS les réglages de la
   console, module par module, pour choisir précisément ce qu'on modifie. Objectif de
   l'utilisateur : ~98 % de la qualité d'un vrai mix / master pro. Reprendre de LeMaster
   seulement : bouton AUTO, A/B, Match, mesures lisibles en bas.
3. **La barre de demande à l'IA** dans le plug-in : texte libre (FR / EN / argot), le master
   écoute la prod, la demande devient des réglages (Match EQ, multibande, Inflator, limiteur)
   comme `omnis_match`.
4. **OMNIS seul sur le master, sans stems** : beaucoup le poseront juste en fin de chaîne.
   Il doit alors masteriser la prod entière tout seul (écoute de la prod, barre IA,
   références), sans rien exiger des stems. Le mode Stems reste un plus.
5. **Tutoriel à la première ouverture** : partie par partie (rôles et groupes, stems,
   master, chef d'orchestre, barre IA), passable, relançable depuis un bouton « ? ».

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
