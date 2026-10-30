# Discographies (noms, titres, liens — aucun son, aucune image)

Produites par `omnis_refdb discographie` (API publique de Deezer) le 30/10/2026. Fichiers texte
séparés par des tabulations, à ouvrir dans Excel / LibreOffice.

| Dossier | Départ | Contenu |
|---|---|---|
| `scene/` | euhZ, Omyll (=MALADEUX), GRY (=BRR), La Fève | **complet** : 111 artistes (les 4, leurs proches Deezer, et les collaborateurs de GRY et Omyll : euhZ, NOLWIN, iNESS M, mïrï… avec leur scène), 5 180 titres |
| `playlist/` | les 159 artistes de la playlist + leurs proches Deezer | **partiel** : 1 178 artistes sur 1 391 (le calcul a atteint sa limite de temps), 177 956 titres ; `titres.tsv.gz` compressé (23 Mo décompressé) |

- `artistes.tsv` : identifiant Deezer, nom, fans, sorties, titres, d'où il vient (playlist,
  demandé, proche de X, collabore avec X), lien.
- `titres.tsv` : artiste, titre, interprète, sortie, type (album, ep, single, compile,
  featuring), date, durée, ISRC, lien.
- `introuvables.txt` : artistes absents de Deezer, homonymes gardés ou écartés, sorties d'un autre
  genre mises de côté (fiche GRy mêlée à un DJ), titres retirés (Matilda, Limanlar Şahit).

Pour finir `playlist/` (les 213 derniers artistes) sur le PC, avec l'outil de l'artefact
*OMNIS-Windows-outils* :

```
omnis_refdb discographie playlist.csv --artistes "euhZ|Omyll=MALADEUX|Gry=BRR|La Fève" --retirer "GRY=Matilda|GRY=Limanlar Şahit" --proches 1 --sortie discographies
```

Ces listes ne sont pas intégrées au plug-in : ce sont des noms et des liens, pour savoir quoi
mesurer (`omnis_refdb scan`) ; seules les fiches mesurées entrent dans la base de références.
