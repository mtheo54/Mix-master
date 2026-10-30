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

**Encore à faire** : AU pour Logic ; anti-résonances spectral ; EQ dynamique par bande dans le
rack ; écoute simulée téléphone / voiture ; aperçu MP3 ; la voix et la prod ensemble ; Punch
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
