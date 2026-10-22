# Suivi du projet : ce qui est attendu, ce qui reste à faire

## En attente de l'utilisateur

- **Références audio** (annoncées pour le soir du 22/10/2026) : « le plus de références possible,
  venant des artistes », provenant de sources légales (titres achetés, prods personnelles, sons
  fournis par les artistes). Rien n'est téléchargé de notre côté : pas de Spotify ni de YouTube
  (conditions d'utilisation).
  - À la réception, rangées dans un dossier de travail (jamais dans le dépôt : aucun audio n'est
    versionné). Renommées « Artiste - Titre.ext » si besoin, à partir des indications de
    l'utilisateur.
  - Mesure : `omnis_refdb scan dossier/ --db data/references.txt`. Seules les signatures (nombres)
    entrent dans `data/references.txt`. Les genres viennent du catalogue de l'utilisateur
    (`data/catalogue.txt`), un titre mesuré héritant des genres de son artiste.
  - Après l'ajout : vérifier les profils par famille (variation, balance, sonie, PLR) et les
    décrire à l'utilisateur.
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

- **Écoute d'une référence dans le DAW** (bus auxiliaire « Reference », paramètre « Listen
  Reference »). Au moins 60 s, écoutes cumulables, cœur du morceau seulement. Voir README,
  « Écouter une référence dans FL Studio ».
- **Analyse d'un fichier en parallèle** (identique au bit près) ; `omnis_refdb scan` mesure un
  fichier par cœur : prêt pour les références de ce soir.

## À venir

- Appliquer la référence écoutée depuis le plug-in : le contrôleur calcule l'intention et écrit
  les paramètres `match_*` / `mb_*` via performEdit. Ça demande l'interface (module 6), pour
  saisir la demande et nommer la référence.
