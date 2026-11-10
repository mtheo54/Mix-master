# Les usages sur le compte onirique.store, et la moyenne des utilisateurs

Décision de l'utilisateur (10/11) : le graphe des usages d'une personne est envoyé **seulement sur
son compte** onirique.store, **inaccessible au public et même au fondateur** ; seule la personne
le voit, depuis internet, sur son compte, si elle a OMNIS. Les données des utilisateurs servent,
comme les références son, à **améliorer le plug-in** : on en fait une moyenne, et avec le temps le
plug-in évolue de lui-même.

Les deux demandes semblent se contredire (personne ne peut lire, mais on en fait une moyenne). Elles
tiennent ensemble avec **deux envois séparés** :

## 1. L'espace personnel : chiffré sur le PC (à construire avec ONIRIQUE)

- La personne l'active (désactivé par défaut).
- Au premier envoi, OMNIS crée une **clé de lecture** (256 bits) sur le PC. Elle n'est **jamais
  envoyée** : OMNIS la montre une fois (12 mots à garder), et la redonne sur demande.
- OMNIS chiffre `graphe_usages.json` (et, si la personne le veut, ses références ajoutées) en
  **AES-256-GCM**, puis envoie le bloc chiffré à son compte (jeton de licence du compte).
- Sur le site, la page du carnet (`site/carnet/`, onglets « IA · usages » et « Mes données »)
  télécharge le bloc, demande la clé de lecture et **déchiffre dans le navigateur** (WebCrypto).
- Le serveur, ONIRIQUE et Abrasion Labs ne voient qu'un bloc illisible, sa taille et sa date.
  Clé perdue = copie illisible pour tout le monde (c'est le but) ; la copie du PC reste.
- « oublie mes habitudes » efface aussi la copie du compte ; la suppression du compte l'efface.

À faire côté plug-in quand l'API existera : chiffrement par les bibliothèques du système (Windows
CNG / macOS CommonCrypto, aucune bibliothèque tierce), envoi (déjà `HttpGet` pour MusicBrainz ; un
`HttpPost` à ajouter), réglage « copie sur mon compte ». À faire côté site : deux routes
authentifiées (déposer / reprendre le bloc chiffré, le supprimer) et la page du carnet derrière la
connexion.

## 2. La contribution à la moyenne : anonyme, séparée du compte (prêt côté plug-in)

- Un autre accord, distinct (désactivé par défaut) : « aider OMNIS à s'améliorer ».
- Fichier : `apprentissage/contribution_anonyme.tsv` (`ai::contributionText`), écrit par
  « exporte mon apprentissage ». Contenu : contextes **génériques** seulement (`auto:trap`,
  `style:rage fr` ; un preset « titre », nom d'une référence, est laissé de côté), gestes,
  nombres (fait, utile, choix artistique, écart moyen). Aucune date, aucun compte, aucun nom.
- Envoyé **sans le jeton du compte**, vers une adresse à part, sans journaliser l'adresse IP.
- Abrasion Labs en fait la moyenne : `omnis_refdb usages dossier --sortie
  data/usages_communaute.txt` (`ai::CommunityHabits::aggregate`). Un geste n'est publié que s'il
  est une **habitude chez au moins 5 personnes** dans ce contexte (sinon on pourrait reconnaître
  quelqu'un) et chez au moins 30 % de celles qui y ont travaillé. Les contributions brutes sont
  effacées après la moyenne.
- Le fichier moyen est livré avec la version suivante d'OMNIS (embarqué). **AUTO s'en sert** pour
  le style reconnu : « ce que font les autres en trap (12 personnes) : sub +0.4 dB ». Sa part vaut
  la moitié de la moyenne pour une personne sans habitudes, puis baisse à mesure que les siennes
  grandissent (× (1 − n / (n + 8))), et reste dans la moitié des bornes des habitudes. Le
  plug-in évolue ainsi de lui-même, version après version, comme avec les références son.

Le fondateur ne voit donc que des moyennes de 5 personnes au moins ; personne ne lit l'espace
personnel de quelqu'un d'autre.

## Avant la mise en ligne

Ces envois sont des données personnelles (même chiffrées ou anonymes) : accords séparés et
explicites, politique de confidentialité à jour (quoi, pourquoi, combien de temps, comment
effacer), et la page « Mes données » qui montre exactement ce qui part. À faire relire par
quelqu'un qui connaît le RGPD avant le lancement.
