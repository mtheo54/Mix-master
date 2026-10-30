# OMNIS — Licences et mentions

OMNIS est développé par **Abrasion Labs**. Ce fichier recense le code et les données de tiers
utilisés, leur licence, ce qui est livré avec le plug-in et ce qui ne l'est pas. À joindre à toute
distribution (installateur, archive, dépôt).

## 1. Ce que contient le plug-in livré (`OMNIS.vst3`)

| Élément | Origine | Licence | Dans le binaire |
|---|---|---|---|
| Code d'OMNIS (DSP, analyse, IA, console) | Abrasion Labs | propriété d'Abrasion Labs | oui |
| VST 3 SDK v3.8.0 (`v3.8.0_build_66`) | Steinberg Media Technologies GmbH | MIT (texte en annexe A) | oui |
| VSTGUI 4.15 (livré avec le SDK) | Steinberg Media Technologies GmbH | licence VSTGUI, type BSD 3 clauses (annexe B) | oui |
| Lexiques, argot, éléments (`data/*.txt`) | Abrasion Labs | propriété d'Abrasion Labs | oui (texte) |
| Base de références (`data/references.txt`, `data/catalogue.txt`) | mesures d'Abrasion Labs | propriété d'Abrasion Labs | oui (texte) |

La base de références ne contient **que des noms d'artistes et de titres, des genres et des nombres**
(signatures spectrales, sonie, tempo…), mesurés sur des fichiers obtenus légalement. Aucun son,
aucune image, aucune parole.

**Aucune image** n'est livrée : ni dans le `.vst3`, ni dans l'installateur, ni dans les sources
(`data/`, `source/`). Le test `omnis_no_images` (ctest, CI) le vérifie à chaque compilation : il
refuse tout fichier image dans le paquet et toute signature PNG / JPEG / GIF dans ses octets,
binaire compris. Les polices sont celles du système (aucune n'est livrée). Sous Windows, le SDK
VST3 ajoute d'office au paquet une icône de dossier (`PlugIn.ico`, le logo VST de Steinberg) :
elle est retirée à la compilation (`cmake/OmnisRemoveFolderIcon.cmake`), le logo VST n'est donc
pas utilisé.

## 2. Outils hors plug-in (`omnis_refdb` et tests)

Ces bibliothèques ne servent qu'à l'outil de références et aux tests ; elles ne sont pas dans
`OMNIS.vst3`.

| Bibliothèque | Version | Licence | Rôle |
|---|---|---|---|
| stb_image (`third_party/stb`) | v2.30 | domaine public (Unlicense) **ou** MIT, au choix | lire les images JPG / PNG |
| stb_image_write (`third_party/stb`) | v1.16 | domaine public (Unlicense) **ou** MIT, au choix | écrire les avatars PNG |
| dr_flac (`third_party/dr_libs`) | v0.13.4 | domaine public (Unlicense) **ou** MIT-0, au choix | lire les FLAC (son, image intégrée) |
| dr_mp3 (`third_party/dr_libs`) | v0.7.4 | domaine public (Unlicense) **ou** MIT-0, au choix | lire les MP3 |

Ces licences n'imposent aucune attribution ; nous la donnons quand même. Sources, commits épinglés
et SHA-256 : `third_party/stb/README.md`, `third_party/dr_libs/README.md`.

## 3. Avatars, pochettes et données en ligne

- Les avatars d'artistes et les pochettes sont produits **sur le poste de l'utilisateur**, à partir
  de ses propres fichiers (image intégrée à ses FLAC) ou d'une requête de **son** poste à l'API
  publique de Deezer (`omnis_refdb deezer`). Ils sont rangés uniquement dans son cache local :
  - Windows : `%APPDATA%\Abrasion Labs\OMNIS\cache\avatars|covers\`
  - macOS : `~/Library/Application Support/Abrasion Labs/OMNIS/cache/…`
  - Linux : `~/.local/share/Abrasion Labs/OMNIS/cache/…`

  Comme la pochette affichée par un lecteur de musique, ces images restent la propriété de leurs
  ayants droit (artistes, labels) ; Abrasion Labs ne les distribue pas. Leur source est indiquée
  (« Avatar OMNIS d'après Deezer », « Pochette du fichier »).
- Le **plug-in ne fait aucune requête réseau**. Seul un clic sur le lien d'une carte ouvre le
  navigateur de l'utilisateur ; en mode promo, ce lien est désactivé.
- **Mode promo** (`/fbi`, ou Maj + Alt + 5 clics sur le logo, mémorisé dans `mode_promo.txt` du
  dossier de l'utilisateur) : aucune image du cache n'est ouverte ni affichée ; les avatars sont
  remplacés par des monogrammes vectoriels dessinés par OMNIS (initiales, courbe spectrale de la
  fiche). Crédits et liens sont masqués. Pour filmer des démonstrations.
- Données Deezer (noms, titres, liens, images) : utilisées selon les conditions de l'API Deezer,
  par l'outil, pour l'usage de la personne qui le lance. Les intégrer au produit vendu demanderait
  un accord avec Deezer et les ayants droit.
- Exports de playlists (CSV) : seuls les noms d'artistes et de titres sont lus ; aucune donnée
  calculée par la plate-forme d'origine n'est gardée.

## 4. Marques

« VST » est une marque déposée de Steinberg Media Technologies GmbH. Avec le SDK sous licence MIT,
l'usage du nom ou du logo VST est facultatif ; s'il est utilisé (fiche produit, installateur), il
doit respecter les règles d'usage des marques publiées par Steinberg. Les noms d'artistes, de titres
et de services cités (Deezer, FL Studio…) appartiennent à leurs propriétaires et ne sont employés
que pour désigner leurs œuvres ou produits.

## 5. Documentation de tiers

Les manuels d'autres logiciels consultés pendant le développement ne sont ni copiés ni livrés ;
seuls des principes, reformulés, ont servi.

---

## Annexe A — VST 3 SDK (MIT)

```
MIT License

Copyright (c) 2025, Steinberg Media Technologies GmbH

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Annexe B — VSTGUI

```
VSTGUI LICENSE
(c) 2022, Steinberg Media Technologies, All Rights Reserved

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
  * Neither the name of the Steinberg Media Technologies nor the names of its
    contributors may be used to endorse or promote products derived from this
    software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Annexe C — stb, dr_libs

Domaine public (Unlicense) ou MIT / MIT-0, au choix : le texte complet est à la fin de chaque
fichier (`third_party/stb/stb_image.h`, `stb_image_write.h`, `third_party/dr_libs/dr_flac.h`,
`dr_mp3.h`).
