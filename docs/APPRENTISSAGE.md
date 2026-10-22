# Comprendre la demande, connaître les références, apprendre

Demande de l'utilisateur : *« faudrait que l'IA comprenne les fautes d'orthographe […], quelque
chose de subtil comme "je veux que ça soit propre" ou "ça manque de crispy", une grande base de mots
[…] et une base de références énorme ; si elle ne connaît pas, demander de faire le rapprochement
avec un autre artiste, ou d'envoyer un son […] ; enfin tout sera enregistré dans l'algorithme qui
s'auto-améliorera et mettra à jour le plug-in de tout le monde. »*

Ce document décrit ce qui est construit, ce qui ne l'est pas encore, et pourquoi.

```
phrase ──► TextMatch (fautes) ──► Lexicon (mots → axes) ──┐
                                  ReferenceLibrary ───────┼─► Understanding ─► question ?  ─► la personne répond
son de référence (optionnel) ─────────────────────────────┘          │
                                                                    ▼
                                    MatchIntent ─► Match EQ / Clipper / Limiteur ─► master
                                                                    │
                       la personne corrige / note ─► FeedbackRecord ─► apprentissage local
                                                                    └► export anonyme ─► serveur ─► nouvelle base pour tous
```

## 1. Les fautes (`ai/TextMatch`)

1. **Normalisation** :
   - minuscules ;
   - accents retirés ;
   - lettres répétées réduites (« fooort » → « fort »).
2. **Clé phonétique française** :
   - ph → f, qu / ck / c(a, o, u) → k, y → i, z → s, h muet ;
   - doubles lettres réduites ;
   - finales muettes (s, x, t, d, e) retirées.

   Ainsi « brillan », « brilants » et « brillant » ont la même clé.
3. **Distance de Damerau-Levenshtein** entre clés :
   - une faute tolérée jusqu'à 7 lettres, deux au-delà ;
   - le premier son doit être le même ;
   - les noms avec chiffres (« 2hollis », « 808 ») se comparent exactement.

Faux amis rencontrés en test, puis corrigés :

| Lu comme | Était | Correction |
|---|---|---|
| « poli » | « 2hollis » | premier son identique, chiffres exacts |
| « ça craque » | « ça claque » | une correspondance exacte bat une faute supposée |
| « less » | « les » | mots anglais très courts retirés de la grammaire |
| « aigre » | « aigus » | « aigus », « médiums », « bas-médium » ajoutés comme mots de bande |

## 2. Les nuances (`ai/Lexicon`, `data/lexique_fr.txt`)

La base de mots est une **donnée** : un fichier texte lisible, compilé dans le plug-in. On l'agrandit
sans toucher au code. Version 1 : 44 concepts, environ 420 expressions (français, anglais, argot).
Chaque concept se traduit en axes sonores :
- tonals : sub, bas, bas-médium, médium, présence, air ;
- largeur, sonie, propreté, ressemblance ;
- punch, densité, saturation : compris et mémorisés, appliqués quand leurs étages existeront.

Une petite grammaire lit chaque proposition :

| Phrase | Compris |
|---|---|
| « ça manque de crispy » | crispy + : présence +1.5, air +1 |
| « c'est trop brillant » | brillant − |
| « je veux que ce soit trop propre » | propre ×1.6 (« trop » = intensif après un souhait) |
| « c'est boueux », « trop boueux » | plainte : bas-médium −2 dans les deux cas |
| « c'est pas propre » / « pas de disto » | veut du propre / sans saturation |
| « un poil plus large » / « vraiment plus large » | largeur +1 / +3.2 |
| « pas trop fort » | sonie un peu moins |
| « garde ma 808 » | région protégée : ±1 dB au maximum, même si une autre phrase demande moins de basse |
| « comme 2holis », « genre slayer », « un son rage » | référence : titre, artiste, alias ou genre |
| « exactement comme » / « un peu comme » | ressemblance 80 % / 30 % |
| « -8 lufs », « pour spotify » | sonie −8 / −14 LUFS |
| « jveu ke sa sonne + crispi stp » | crispy + (langage SMS ignoré) |

Les mots non compris sont gardés : ils servent à l'apprentissage (§4).

**Limite honnête.** Un lexique, même grand, ne comprendra jamais *tout* comme le ferait un modèle de
langage. Décision de l'utilisateur : **tout en local, sans internet ni coût**, avec un plug-in qui
peut peser 1 à 2 Go. Le module 5 ajoutera donc un **modèle de compréhension embarqué**. La piste
privilégiée est un modèle d'*embeddings* multilingue (quelques centaines de Mo) exécuté sur la
machine. Il rapproche une expression inconnue (« ça manque de mordant ») des expressions du
lexique par le sens, et rend le même vecteur d'axes. Le lexique reste le vocabulaire de sortie et
la solution de repli. Toute correction de la personne nourrit les deux.

## 3. Les références (`ai/ReferenceLibrary`, `data/references.txt`)

- Une entrée = artiste, titre, alias, genres et **signature mesurée**. La signature contient les
  nombres de `SpectralSignature` : balance M/S, énergie du sub, dynamique par bande, sonie, LRA,
  PLR. Jamais d'audio : la base se partage sans redistribuer de musique.
- Recherche tolérante aux fautes : titre, puis artiste ou alias, puis genre. Plusieurs titres
  donnent une signature **moyenne** : ce qui est commun à tous est justement ce qui persiste.
- **Inconnu** → question, et rien n'est rendu :
  > Je ne connais pas encore « yeat ». Pour viser juste : cite-moi un artiste proche que je connais
  > (2hollis, SLAYR), ou glisse-moi un son de cet artiste : je l'écouterai et je l'ajouterai à la
  > base.
- **Son fourni pour un artiste inconnu** → sa signature entre dans la base utilisateur
  (`omnis_match … --db`).
- **Artiste connu de nom seulement** (`omnis_refdb nom` / `noms`, sans son) : il est reconnu dans la
  demande et le plug-in vise le **profil moyen de ses genres**, en suggérant d'envoyer un son :
  > Je connais Ken Carson de nom : en attendant, je vise le profil moyen rage. Glisse-moi un son de
  > Ken Carson pour viser juste.

  Si aucun titre de ses genres n'a encore été mesuré, la question est posée. Une entrée de nom ne
  remplace jamais des mesures : elle ne fait qu'ajouter des genres et des alias.
- **Les noms passent avant les mots** : les noms de la base (artiste, alias, titre, fautes
  comprises) sont repérés avant la lecture du lexique. Sans ça, « ken carsonn » devenait
  « carton » (mesuré). Les noms de moins de 5 caractères se comparent exactement : « trop » n'est
  pas le genre « trap ».
- **Catalogue de l'utilisateur** (`data/catalogue.txt`, embarqué) : 1911 titres et 1247 artistes de
  ses playlists, importés par `omnis_refdb csv` avec sa propre classification en 8 familles
  (`data/genres_classement.txt` : « Rap Français & New Wave » → rap fr, new wave, rap…). Seuls les
  noms, les titres et ce classement sont gardés. Les colonnes calculées par Spotify (genres,
  sonie, energy, danceability…) sont **ignorées**, conformément à sa politique.
  - Garde-fous pour 3000 noms : un mot du lexique ou un mot outil n'est jamais un nom. Un nom avec
    faute ne commence ni ne finit par un mot outil (« comme la » n'était plus « CALME »). Un titre
    d'un seul mot (« Intro ») n'est reconnu qu'après « comme », « genre », « titre »… Avec faute,
    l'artiste passe avant « artiste + titre » (« ken carsonn » n'est pas « Ken Carson - Pots »).
  - « fein de travis scott » : un titre est cherché plus largement parmi ceux de l'artiste reconnu
    (FE!N).
  - Un titre mesuré hérite des genres de son artiste (SLAYR, classé hyperpop par l'utilisateur).
  - Réponses honnêtes : « Je n'ai encore écouté aucun son rap fr » plutôt qu'un profil
    approximatif. La question ne cite que les artistes **réellement écoutés**.
  - Recherche par clés précalculées : 60 à 90 ms par demande, chargement de la base compris
    (4 s avant l'optimisation).
- **Formats** : WAV, MP3, FLAC (décodeurs dr_mp3 / dr_flac, domaine public, `third_party/`), tout
  en local. `omnis_refdb scan dossier/` mesure tout un dossier de fichiers nommés
  « Artiste - Titre.ext ».
- Base livrée aujourd'hui : 2 titres (poster boy, Sloppy Joe), les seules références fournies,
  genres confirmés par l'utilisateur (rage, hyperpop). La base « énorme » se construit avec
  `omnis_refdb add` sur des fichiers audio possédés légalement, et avec les sons que les personnes
  fournissent (§4).
- **Pas de Spotify.** Depuis le 27 novembre 2024, une nouvelle application n'a plus accès aux
  analyses audio, aux caractéristiques audio ni aux extraits de 30 s. Et la politique développeur
  de Spotify interdit d'utiliser son contenu pour entraîner ou alimenter un modèle d'IA. Les
  signatures doivent donc venir de fichiers audio réels, analysés sur la machine.

## 4. Apprendre (`ai/Feedback`)

1. **Retour de séance** : les concepts compris, la **correction** que la personne a faite ensuite,
   sa note (−1 / 0 / +1), et les mots et références inconnus. La correction est calculée par
   `correctionFromCurves` : courbe proposée contre courbe gardée, région par région, plus la
   largeur et la sonie.
2. **Apprentissage local** (`learnFromFeedback`) :
   - la correction est partagée entre les concepts de la phrase au prorata de leur poids ;
   - taux 0.3, effet borné à ±6 dB ;
   - exemple : après trois « ça manque de crispy » suivis de +1.5 dB de présence, « crispy » vaut
     2.85 dB de présence au lieu de 1.5 ;
   - un mot inconnu suivi **deux fois** de corrections cohérentes devient un concept (« zinzolin »
     → présence +1.8). Avec des corrections contradictoires, rien n'est appris.
3. **Export anonyme** (`exportAggregate`) : des nombres par concept, des mots isolés, des noms de
   références demandées. **Jamais la phrase, jamais l'audio.**
4. **Fusion communautaire** (`mergeAggregates`, `applyCommunity`) : un changement n'entre dans la
   base de tout le monde que s'il est confirmé par au moins **10 personnes différentes** et
   **20 observations** (réglable). Les références réclamées par beaucoup de monde sont listées pour
   être ajoutées.

```bash
omnis_ask "ça manque de crispy" --log retours.tsv --correction "presence=+1.5" --note 1
omnis_learn local retours.tsv --lexique lexique_utilisateur.txt
omnis_learn export retours.tsv --sortie agrege.txt
omnis_learn fusion agrege_*.txt --sortie fusion.txt                   # serveur
omnis_learn communaute fusion.txt --sortie lexique_v2.txt             # serveur
```

## 5. Ce qui reste à construire pour la mise à jour de tout le monde

Toute la logique ci-dessus est écrite et testée hors ligne. Pour en faire un vrai service, il
manque :

| Brique | Pourquoi c'est indispensable |
|---|---|
| **Consentement explicite** (opt-in, désactivable) | On envoie des données d'usage : RGPD, confiance. Rien ne part sans accord. |
| Serveur de collecte et de fusion | Reçoit les exports, applique `applyCommunity`, garde l'historique. |
| **Validation avant diffusion** | Une base mise à jour doit repasser un banc de masters de référence (garde tonale, distorsion, sonie) : une mauvaise mise à jour toucherait tout le monde d'un coup. |
| Paquets de données **versionnés et signés** | Le plug-in ne charge qu'un paquet signé par Abrasion Labs ; retour arrière possible. |
| Vérification au lancement du plug-in (jamais pendant le rendu audio) | Thread de fond, sans réseau dans le thread audio ; échec silencieux, la base embarquée reste valable. Désactivable : le plug-in reste entièrement utilisable hors ligne. |
| Protection contre les abus | Le seuil de personnes différentes empêche une seule machine de « voter » 1000 fois. |

Seules les **données** se mettent à jour (lexique et références), jamais le code DSP. C'est plus
sûr, plus léger, et ça ne demande pas de réinstaller le plug-in.
