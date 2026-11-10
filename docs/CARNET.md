# Le carnet OMNIS (page web pour les utilisateurs)

Une page, `site/carnet/index.html`, à mettre sur la page d'OMNIS (onirique.store), par exemple
dans l'espace des clients connectés. Elle est **autonome** : un seul fichier, aucune police,
aucun script, aucune image chargés de l'extérieur, et sa règle de sécurité (`Content-Security-Policy`,
`connect-src 'none'`) lui interdit toute requête. Seuls les liens que la personne clique ouvrent
MusicBrainz ou Cover Art Archive.

## Ce qu'elle montre

- **Carnet** : familles, sous-genres et artistes que connaît OMNIS (`data/styles_artistes.txt`,
  déjà livré dans le plug-in), albums et EP (MusicBrainz, CC0). Aucun titre mesuré.
- **Graphe** : la base en réseau (familles, styles, artistes), les gros genres en gros.
- **IA · usages** : ce que la personne fait le plus après AUTO ou un preset, lu dans son
  `graphe_usages.json` (« exporte mon apprentissage ») ; un exemple marqué « exemple » sinon.
  Verdicts du juge : utile, inaudible, hésitation, à risque, annulé, choix artistique (DA).
- **Mes données** : la liste de tout ce qu'OMNIS garde sur le PC, de ce qui peut en sortir
  (seulement l'identification MusicBrainz d'une référence ajoutée, désactivable), et la lecture
  de ses fichiers (`graphe_usages.json`, `export_anonyme.tsv`, `references_utilisateur.txt`)
  **dans le navigateur** : rien n'est envoyé, pas même au compte.

## Compte et confidentialité

Le compte (géré par ONIRIQUE) **ouvre la page** ; il ne reçoit jamais les données : elles restent
sur le PC et la page les lit localement. Pas de serveur de données, pas de coût, rien à déclarer
de plus dans la politique de confidentialité que ce qu'OMNIS fait déjà (voir `LICENSE_NOTICES.md`).

## La mettre à jour

```bash
python3 tools/carnet/build_carnet.py          # régénère site/carnet/index.html
python3 tools/carnet/build_carnet.py --check  # (ctest omnis_carnet_a_jour) échoue si elle n'est pas à jour
```

Sources : `tools/carnet/template.html` (la page, sans données), `musicbrainz.json` (identifiants
et albums), `descriptions.json` (repères par style), `exemple_usages.json` (produit par le vrai
juge). Après une modification de `data/styles_artistes.txt`, régénérer la page.
