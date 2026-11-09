# OMNIS — guide pour l'agent (Claude Code)

Plug-in VST3 de mastering pour beatmakers (C++20, VSTGUI) d'**Abrasion Labs**, vendu ~150 €. Le
propriétaire est français, travaille sous **FL Studio** : répondre en français, commentaires et
textes d'interface en français.

## Économiser les tokens

1. **Carte du code d'abord** : `docs/CODEMAP.md` (une ligne par module : rôle, types, fonctions,
   tests). La chercher (`grep -n mot docs/CODEMAP.md`) avant d'ouvrir des sources, puis lire
   seulement les fichiers et les plages de lignes utiles (`sed -n a,bp`, `grep -n`).
   Après avoir ajouté / renommé des fichiers : `python3 tools/codemap/codemap.py`.
2. **Ne jamais lire en entier** : `data/catalogue.txt` (17 000 lignes), `data/references.txt`,
   `data/profils_genres.txt`, `third_party/`, `build*/`. Utiliser `grep`.
3. Sorties longues : `ninja … 2>&1 | grep -E "error|warning" -A3`,
   `omnis_tests 2>&1 | grep -E "FAIL|ECHEC|echec\(s\)"`, `tail`.
4. Fichiers temporaires (rendus, kits de test) : dans le scratchpad de la session, jamais à la
   racine du dépôt (le hook de fin refuse les fichiers non suivis).

## Règles du projet (non négociables)

- **Aucun son dans le dépôt** (`.gitignore` exclut `audio/` et les formats audio).
- **Aucune image livrée** : ni dans `OMNIS.vst3`, ni dans `data/` / `source/` (test ctest
  `omnis_no_images`). Avatars / pochettes = cache local de l'utilisateur (`gfx/AvatarCache.h`).
- La **classification des genres de l'utilisateur prime** (`data/genres_classement.txt`).
- Manuels de logiciels tiers : principes seulement, reformulés ; jamais copiés.
- Tout en local, sans coût ; pas de Spotify. Ne **jamais** demander une clé / un mot de passe
  dans le chat (secrets d'environnement ou Remote Control).
- Pas de PR sans demande explicite. Licences et mentions : `LICENSE_NOTICES.md`.
- **Deux langues** : le code écrit les textes en français ; l'anglais vient de `data/i18n_en.txt`
  (texte français<TAB>anglais), appliqué à l'affichage par `i18n::tr` (`core/I18n.h`). Tout
  nouveau texte visible (interface, réponses de l'IA, diagnostic) : ajouter sa ligne anglaise.
  Les chaînes de format passent par `tr` avant d'être remplies (helpers `fmt` / `format`).

## Compiler, tester

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release        # GUI désactivée ici (OMNIS_BUILD_GUI=OFF)
ninja -C build                                                   # moteur, outils, plug-in
build/bin/Release/omnis_tests            # ~190 tests unitaires (DSP, IA, données)
build/bin/Release/omnis_plugin_tests     # plug-in (état, barre IA, lien, menus)
build/bin/Release/validator build/VST3/Release/OMNIS.vst3        # validateur Steinberg
ctest --test-dir build                   # dont omnis_no_images
# Interface : build-gui (-DOMNIS_BUILD_GUI=ON), cible omnis_guishot -> PNG de chaque page
```

Outils (`build/bin/Release/`) : `omnis_render in.wav out.wav --set cle=valeur --report`
(clés : `omnis_render --params`), `omnis_testgen kit dossier --sr 48000` (signaux de test),
`omnis_analyze`, `omnis_compare`, `omnis_ask "phrase"` (barre IA hors plug-in),
`omnis_refdb` (base de références, profils de genre, emblèmes ; plus de Deezer).

## Architecture en bref (détails : `docs/ARCHITECTURE.md`)

- `source/engine/` : `Engine` (rack de 8 étages réordonnable, PDC, bypass / Delta, garde-fou de
  crête `Stage::limitsPeaks` / `setPeakGuard`), `MasteringChain` (construit la chaîne).
- `source/stages/` : Sub → Match EQ → Comp → Inflator → Imager → Multiband → Clipper → Limiter
  (`params::kDefaultChain`, ordre = `rack_order` ; l'ancien `chain_order` à 7 est converti). Phase linéaire : latence totale ~710 ms.
- `source/dsp/` : FIR / FFT / convolution, suréchantillonnage HIIR, `LookaheadGain`, ADAA.
- `source/analysis/` : sonie BS.1770, true peak, signature spectrale, descripteurs musicaux.
- `source/ai/` : lexique (`data/lexique_fr.txt`), compréhension des demandes, références,
  actions console (`ConsoleActions`), apprentissage.
- **Base livrée = profils de genre seulement** (`data/profils_genres.txt`, généré par
  `omnis_refdb profils data/references.txt --sortie data/profils_genres.txt`, `ai/GenreProfiles`) :
  aucune fiche de titre dans le plug-in. `data/references.txt` = mesures brutes par titre (source,
  non intégrée) ; après l'avoir modifiée, régénérer les profils.
- `source/params/` : tous les paramètres (`Parameters.cpp`, ids stables).
- `source/plugin/vst3/` : processeur, contrôleur (`ask()` : barre IA, `/fbi` mode promo,
  « remets tout à zéro »), éditeur VSTGUI (`OmnisEditor.cpp`).
- **Mastering seulement** (09/11) : le mixage (chaînes de stems, chef d'orchestre) est retiré ;
  plan de la refonte : `docs/REFONTE.md`. `omnis_auto` rejoue l'AUTO du plug-in sur un fichier.
  Mais OMNIS peut **écouter les bus** (RÔLE Bus, rien sur les pistes ; `link/`, `stems/BusListener`) :
  page COMPOSITION, son inchangé, latence 0 ; `omnis_bus fichier.wav` rejoue l'écoute d'un bus.
- `tools/` : outils en ligne de commande ; `tests/` : un fichier par domaine.

## Docs utiles

`docs/PROTOCOLE_DE_TEST.md` (critères d'acceptation DSP), `docs/REFERENCES.md` (base de
références, profils, emblèmes, sons à mesurer), `docs/SUIVI.md` (historique), `docs/CALIBRATION_*`.
