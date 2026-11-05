# OMNIS dans l'installateur ONIRIQUE — notice d'intégration

Pour la personne (ou l'agent) qui gère l'installateur ONIRIQUE. OMNIS est un plug-in VST3
d'**Abrasion Labs**, vendu sur https://onirique.store. Tout ce qu'il faut est dans le paquet
**`OMNIS-pour-ONIRIQUE`** produit par la CI du dépôt (onglet Actions → dernière exécution verte
de la branche → Artifacts), à décompresser tel quel :

```
OMNIS-pour-ONIRIQUE/
├── Windows/OMNIS.vst3/          ← dossier (paquet VST3), à copier ENTIER
├── macOS/OMNIS.vst3/            ← paquet macOS universel (Apple Silicon + Intel), à copier ENTIER
├── outils/omnis_refdb.exe       ← facultatif : outil de mesure des références (Windows)
├── LICENSE_NOTICES.md           ← mentions de licence : à livrer avec le plug-in (obligatoire)
├── REFERENCES.md                ← mode d'emploi de l'outil (si l'outil est installé)
├── INTEGRATION_ONIRIQUE.md      ← ce fichier
└── MANIFEST.txt                 ← version, empreintes SHA-256 de chaque fichier
```

## 1. Où installer

| Système | Fichier | Destination | Droits |
|---|---|---|---|
| Windows 10 / 11 (64 bits) | `Windows/OMNIS.vst3` (dossier) | `C:\Program Files\Common Files\VST3\OMNIS.vst3` (`{commoncf64}\VST3` en Inno Setup, `$COMMONPROGRAMFILES64\VST3` en NSIS) | administrateur |
| macOS 10.15 et plus | `macOS/OMNIS.vst3` (paquet) | `/Library/Audio/Plug-Ins/VST3/OMNIS.vst3` (tous les utilisateurs) ou `~/Library/Audio/Plug-Ins/VST3/` | administrateur pour `/Library` |
| Windows (facultatif) | `outils/omnis_refdb.exe` + `REFERENCES.md` | le dossier des outils ONIRIQUE / Abrasion, par ex. `C:\Program Files\Abrasion Labs\OMNIS\` | administrateur |
| toujours | `LICENSE_NOTICES.md` | à côté des autres mentions ONIRIQUE (ou dans `C:\Program Files\Abrasion Labs\OMNIS\`), et/ou affiché par l'installateur | — |
| conseillé | `MODE_D_EMPLOI.md` (français), `USER_MANUAL.md` (anglais) | à côté des mentions, ou en lien depuis la fiche du produit | — |

- Copier **le dossier `OMNIS.vst3` entier**, avec son arborescence (`Contents/…`, `Resources/moduleinfo.json`) :
  ce n'est pas un fichier unique. Écraser une version précédente au même endroit.
- C'est le dossier VST3 standard : FL Studio, Ableton, Cubase, Studio One, Reaper le lisent sans
  réglage. Dans FL Studio, l'utilisateur fait une fois *Options → Manage plugins → Find plugins*.
- Rien d'autre : **aucune clé de registre, aucun service, aucune bibliothèque partagée, aucun
  redistribuable** (le plug-in est autonome). **Réseau** : une seule requête, vers `musicbrainz.org`,
  quand l'utilisateur ajoute un fichier de référence (via le `curl` du système ; désactivable dans le
  menu RÉF). Aucune autre connexion, aucune télémétrie.

## 2. Désinstallation

- Supprimer seulement `…\VST3\OMNIS.vst3` (et l'outil s'il a été installé).
- **Ne pas supprimer** le dossier de l'utilisateur, qui garde son apprentissage, ses références
  mesurées, son cache d'avatars et sa langue d'interface :
  - Windows : `%APPDATA%\Abrasion Labs\OMNIS`
  - macOS : `~/Library/Application Support/Abrasion Labs/OMNIS`

  (Le plug-in le crée lui-même au premier lancement ; l'installateur n'a rien à y mettre.)

## 3. Signature

- **Windows** : `OMNIS.vst3\Contents\x86_64-win\OMNIS.vst3` est une DLL. Si l'installateur
  ONIRIQUE signe ses binaires, signer aussi celle-ci (avant de la copier dans l'installateur).
- **macOS** : le paquet n'est **pas signé** par la CI. Avant distribution : signer le paquet
  avec le Developer ID de l'éditeur (`codesign --force --deep --options runtime --timestamp
  --sign "Developer ID Application: …" OMNIS.vst3`), puis notariser le `.pkg` / `.dmg` ONIRIQUE
  qui le contient (`xcrun notarytool submit … --wait`, puis `xcrun stapler staple`). Sans cela,
  macOS bloque le plug-in à l'ouverture.

## 4. Identité du plug-in (pour un inventaire ou une mise à jour)

| Élément | Valeur |
|---|---|
| Nom | OMNIS |
| Éditeur | Abrasion Labs — https://onirique.store |
| Version | dans `MANIFEST.txt` (ex. `0.1.0`) |
| Format | VST3 (effet stéréo, catégorie *Fx\|Mastering*) |
| Identifiant macOS | `com.abrasionlabs.omnis` |
| UID VST3 (processeur) | `2E5FA3CD-D9AA49CF-933A79CD-1EAA3F2B` |
| Langue | français ou anglais selon le système (rien à régler) |

## 5. À vérifier après intégration

1. Après installation : le dossier `OMNIS.vst3` est bien dans le dossier VST3 ci-dessus.
2. Dans FL Studio : *Find plugins*, puis OMNIS sur le master ; la console s'ouvre et le son passe.
3. Les empreintes des fichiers installés correspondent à `MANIFEST.txt`.
4. Désinstallation : `OMNIS.vst3` a disparu, le dossier de l'utilisateur est resté.
