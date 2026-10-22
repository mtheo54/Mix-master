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

- Appliquer la référence écoutée depuis le plug-in : le contrôleur calcule l'intention et écrit
  les paramètres `match_*` / `mb_*` via performEdit. Ça demande l'interface (module 6), pour
  saisir la demande et nommer la référence.
