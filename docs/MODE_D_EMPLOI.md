# OMNIS — Mode d'emploi

*Abrasion Labs · plug-in de mixage et de mastering VST3 · version 0.1*

OMNIS masterise ta prod : il l'écoute, comprend ce que tu lui écris en français ou en anglais,
règle sa chaîne de 8 modules et amène le volume là où tu le veux. Tout reste visible et réglable
à la main.

---

## Sommaire

1. [En 2 minutes](#1-en-2-minutes)
2. [Installation](#2-installation)
3. [Premier lancement](#3-premier-lancement)
4. [Où le mettre](#4-où-le-mettre)
5. [La console](#5-la-console)
6. [AUTO](#6-auto)
7. [La barre IA](#7-la-barre-ia)
8. [Les références](#8-les-références)
9. [Les 8 modules](#9-les-8-modules)
10. [Écouter honnêtement](#10-écouter-honnêtement)
11. [Les mesures](#11-les-mesures)
12. [La latence](#12-la-latence)
13. [Avant d'exporter](#13-avant-dexporter)
14. [Et les pistes ?](#14-et-les-pistes-)
15. [Diagnostic du mix](#15-diagnostic-du-mix)
16. [Recettes](#16-recettes)
17. [Questions fréquentes](#17-questions-fréquentes)
18. [Tes données](#18-tes-données)
19. [Caractéristiques](#19-caractéristiques)

---

## 1. En 2 minutes

1. Mets **OMNIS sur le master** (dernier slot du master dans FL Studio).
2. Clique **AUTO** et joue **le morceau en entier**. À la fin, arrête la lecture : OMNIS règle la
   chaîne, essaie plusieurs réglages de volume et garde le plus propre.
3. Corrige avec des mots dans la **barre IA** : « plus de punch », « garde ma 808 », « un peu
   moins fort », « comme de la new wave ».
4. Avant d'exporter, écris **« export »** dans la barre IA : OMNIS dit si tout est prêt.

---

## 2. Installation

**Windows 10 ou 11 (64 bits)** : lance l'installateur. OMNIS va dans le dossier VST3 commun
(`C:\Program Files\Common Files\VST3\OMNIS.vst3`).

**macOS 10.15 ou plus récent** (Intel et Apple Silicon) : copie le dossier `OMNIS.vst3` dans
`/Library/Audio/Plug-Ins/VST3/`.

**Dans FL Studio** : *Options › Manage plugins › Find more plugins* (ou *Start scan*). OMNIS
apparaît dans la catégorie *Mastering* d'Abrasion Labs. Les autres DAW compatibles VST3 (Ableton
Live, Reaper, Bitwig, Studio One, Cubase…) le trouvent après un nouveau scan.

**Désinstaller** : la désinstallation retire le plug-in et garde ton dossier personnel (tes
références, tes réglages appris). Voir [§ 18](#18-tes-données) pour le supprimer aussi.

> Format : VST3 seulement. Pas encore d'AU (Logic) ni d'AAX (Pro Tools).

---

## 3. Premier lancement

- **Le guide** s'ouvre tout seul la première fois : 12 étapes, environ 2 minutes. **SUIVANT**
  pour avancer, **PASSER** pour le fermer. Le bouton **?** en haut le rouvre, tout comme les mots
  **« tuto »** ou **« aide »** dans la barre IA. Après une mise à jour, seules les étapes nouvelles
  te sont montrées.
- **La langue** : écris **/en** dans la barre IA pour passer l'interface en anglais, **/fr** pour
  revenir au français. Le choix est gardé.

---

## 4. Où le mettre

OMNIS est un plug-in de **mastering** : il travaille sur la prod entière. Mets-le **en dernier sur
le master**, après tes autres effets. Laisse un peu de marge au master de FL (crêtes vers −6 dBFS,
sans limiteur ni clipper avant OMNIS) : il a besoin de cette place pour bien travailler.

---

## 5. La console

```
┌──────────────────────────────────────────────────────────────┬──────────────┐
│ AUTO · A · B · A>B · BYPASS · MATCH · DELTA · STÉRÉO/MID/SIDE │   MASTER     │
│ ? · LAT                                                       │ entrée/sortie│
├──────────────────────────────────────────────────────────────┤ crêtes, LUFS │
│ IA [ ta demande……………… ] ▼ RÉF [ référence ] APPLIQUER  réponse │ true peak,   │
├──────────────────────────────────────────────────────────────┤ PLR, LRA…    │
│ RACK : Sub · Match EQ · Inflator · Imager · Multiband ·       │              │
│        Clipper · Limiter                                      │ ÉCOUTE       │
├──────────────────────────────────────────────────────────────┤ DIAGNOSTIC   │
│ TIROIR : réglages du module ouvert  │  son écran              │ EXPORT       │
├──────────────────────────────────────────────────────────────┤ DIAGNOSTIC   │
│ ANALYSEUR (spectre de sortie, voyant d'export) · GONIOMÈTRE   │              │
└──────────────────────────────────────────────────────────────┴──────────────┘
```

- **Le rack** : chaque module a un gros bouton (son réglage principal), un voyant **ON** et un
  petit écran qui dit ce qu'il fait en ce moment (« GR −2.1 dB », « CLIP −0.4 dB »…).
  - **Clique** un module pour ouvrir tous ses réglages dans le tiroir.
  - **Glisse un module par son nom** pour changer l'ordre de la chaîne.
- **Le tiroir** : à gauche, tous les réglages du module ouvert ; à droite, son écran (courbe,
  réduction, nœuds à glisser).
- **Les boutons rotatifs** : glisse vers le haut ou le bas. **Maj** : réglage fin.
  **Double-clic** : valeur par défaut.
- **A / B** : deux réglages complets de toute la console. **A>B** (ou **B>A**) copie celui que tu
  écoutes dans l'autre.

---

## 6. AUTO

1. Clique **AUTO**, puis lance la lecture **du début du morceau**.
2. OMNIS écoute **tout le morceau** (la barre IA montre le temps écouté). À la fin, **arrête la
   lecture** : l'écoute se termine. Tu peux aussi recliquer AUTO pour finir plus tôt (au moins
   4 secondes entendues). Un clic sur AUTO avant que le son arrive annule.
3. Il règle toute la chaîne d'après le morceau entier :
   - vers la **référence** choisie dans le menu RÉF ;
   - sinon vers le **style** que tu as écrit (« genre rage ») ;
   - sinon vers la moyenne de toutes les références.
4. **Les essais** : sur le passage le plus fort qu'il a entendu (15 secondes, gardées en mémoire
   le temps des essais puis oubliées), il essaie trois répartitions du volume — tout au limiteur,
   clip 40 / limiteur 60, clip 70 / limiteur 30 —, mène chacune au **NIVEAU** choisi (colonne de
   droite), mesure la distorsion et l'équilibre tonal, et garde **la plus propre**
   (« AUTO affiné : … retenu « clip 70 / 30 » »). Quelques secondes, en arrière-plan : le son
   continue normalement.
5. Il ajuste enfin le volume **par la mesure** en écoutant la sortie.

| NIVEAU | Sonie visée (passage fort) | Pour |
|---|---|---|
| Streaming | −14 LUFS | Spotify, YouTube, Deezer : rien n'est baissé par la plateforme |
| **Équilibré** (défaut) | −11 LUFS | fort mais vivant : le contraste couplet / refrain reste |
| Fort | −9 LUFS | prod dense, déjà compressée dans le projet |
| Très fort | −7 LUFS | rage, hard clipping : écrêtage assumé |
| Style | celle du style ou de la référence choisie | suivre une référence |

**Le style de ta prod.** Si tu n'as nommé aucun style ni choisi de référence, AUTO **reconnaît le
style à l'écoute** (trap, drill, rage, plugg / new wave, boom bap, mélodique / pop, électro, rock,
hyperpop, B.O.) et le dit : « J'entends : trap (64 %) », ou, s'il hésite, ses trois candidats. Il
vise le premier. Écris le bon style (« c'est de la drill ») : **il s'en souvient** et reconnaît
mieux tes prochaines prods (mémoire sur ton poste, des mesures seulement). Demande « c'est quel
style ? » à tout moment.

**Le garde-fou** : OMNIS ne compresse jamais plus que ce que le niveau permet. Si ta prod est très
dynamique, il s'arrête avant de l'écraser et te le dit (« Volume arrêté à … »). Pour aller plus
fort, rends-la plus dense dans ton projet (808 et kick compressés ou saturés), puis relance AUTO.
Changer le NIVEAU pendant la lecture y ramène le volume.

Tout reste réglable après. AUTO est volontairement discret : il respecte un son déjà travaillé.

---

## 7. La barre IA

Écris ce que tu veux, comme tu le dirais à un ingé. Fautes, abréviations et argot sont
compris (« tro d bass », « + de punch stp », « c trop crade »). Appuie sur **Entrée** ou sur
**APPLIQUER**. OMNIS répond à droite : ce qu'il a compris et ce qu'il a fait.

Si la prod n'a pas encore été entendue, il te demande de lancer la lecture. Il garde ta demande
et l'applique dès qu'il a écouté quelques secondes.

### Exemples

| Tu écris | OMNIS fait |
|---|---|
| « plus de punch » | relief des attaques (Punch du limiteur), un peu de multibande |
| « plus fort », « -8 lufs », « niveau spotify » | une sonie à atteindre ; le limiteur y est mené pas à pas |
| « garde ma 808 », « touche pas au sub » | le grave est protégé (le Match EQ n'y touche presque plus) |
| « moins boueux », « c'est brouillon » | moins de bas-médium |
| « plus brillant », « plus d'air », « crispy » | présence et aigus |
| « plus chaud », « plus gras » | bas-médium, un peu de saturation douce |
| « plus large dans les aigus » | l'Imager ouvre le haut (le grave reste mono) |
| « ça siffle », « les s agressent », « de-esser » | une bande de l'**EQ dynamique** à 7 kHz, qui ne retient que les sifflantes |
| « agressif dans les aigus », « ça pique » | une bande dynamique à 3,5 kHz, plus une petite correction fixe |
| « plus sale », « plus propre » | plus ou moins de clipper et d'Inflator |
| « genre rage », « comme de la drill » | la chaîne vers le profil de ce style |
| « comme GRY », « comme 2hollis » | la chaîne vers le style de l'artiste (s'il est connu) |

Deux demandes s'additionnent : « plus de punch » deux fois donne encore plus de punch. Les
nuances comptent : « un poil plus fort », « beaucoup plus large ».

### Commandes

| Tu écris | Effet |
|---|---|
| **annule** | défait la dernière action d'OMNIS (20 pas) |
| **remets tout à zéro** | tous les réglages reviennent au départ |
| **export** | dit si l'export est prêt, sinon ce qu'il faut changer |
| **diagnostic**, « qu'est-ce qui va pas » | le diagnostic du mix ([§ 15](#15-diagnostic-du-mix)) |
| **tuto**, **aide** | rouvre le guide |
| **/en**, **/fr** | langue de l'interface |

---

## 8. Les références

### Le menu RÉF

À côté de la barre IA. Il te permet de viser un **style**, un **artiste** ou un **titre** :

- **Styles** : les profils de genre livrés avec OMNIS (par exemple *Rap Français & New Wave ›
  plugg fr*). Chaque profil est la moyenne d'au moins 3 titres mesurés. Aucun son n'est livré,
  seulement des mesures.
- **Tes titres** : ceux que tu as ajoutés (voir plus bas).
- **L'entonnoir** (à gauche de RÉF) filtre le menu : une famille, puis un sous-genre.
- **« Aucune »** rend la main aux mots.

Choisir une référence règle tout de suite la chaîne : courbe du Match EQ, punch, grave mono,
écart couplet / refrain, écrêtage, volume. AUTO la vise ensuite. Le choix est gardé avec le
projet.

### Ajouter un fichier de référence

1. **RÉF › « Ajouter un fichier de référence… »**, puis choisis un WAV, un MP3 ou un FLAC.
2. **Nomme le fichier « Artiste - Titre »** (ou « Titre - Artiste »). OMNIS lit d'abord les
   étiquettes du fichier (ID3, FLAC, WAV), puis son nom.
3. Il le mesure en arrière-plan. Le son n'est **jamais gardé** : seules des mesures sont
   ajoutées à ta base personnelle. Le titre apparaît ensuite dans le menu RÉF.

**MusicBrainz** : pour confirmer l'artiste et le titre, OMNIS peut interroger MusicBrainz, une
base musicale libre. C'est **la seule connexion à Internet** d'OMNIS : l'artiste et le titre sont
envoyés, rien d'autre. Tu peux la couper dans le menu RÉF (« Recherche MusicBrainz : coupée »).
Un petit artiste peut être absent de MusicBrainz : le fichier est quand même ajouté, sous le nom
que tu lui as donné.

> Utilise des fichiers que tu as le droit d'utiliser : un titre acheté, ta propre prod.

### Écouter une référence jouée dans le DAW (avancé)

Tu peux aussi faire écouter un son à OMNIS sans fichier :
1. Envoie la piste de la référence vers l'**entrée auxiliaire (sidechain)** d'OMNIS, sans la
   router vers le master.
2. Active le paramètre **Listen Reference** (liste des paramètres du plug-in dans ton DAW) et
   joue au moins **60 secondes**, puis désactive-le.
3. La référence est mesurée et ajoutée à ta base personnelle.

---

## 9. Les 8 modules

Ordre par défaut : **Sub → Match EQ → Comp → Inflator → Imager → Multiband → Clipper → Limiter**.
Chaque module a son **ON** ; éteint, il ne touche plus au son. Le Comp arrive **éteint** : allume-le
quand tu veux de la colle.

### 1. Sub — infra-grave, grave mono, sub

| Réglage | Plage | Défaut | Rôle |
|---|---|---|---|
| Low Cut | 5–60 Hz | 16 Hz | retire l'infra-grave inutile |
| Mono Below | 20–300 Hz | 120 Hz | sous cette fréquence, le grave passe en mono (club, téléphone) |
| Mono Amount | 0–100 % | 100 % | dosage du mono |
| Mono Slope | 24 / 48 dB/oct | 24 | raideur de la séparation |
| Sub Gain / Sub Freq | −12…+6 dB / 30–150 Hz | 0 dB / 70 Hz | plateau de grave |
| Sub Synth | 0–100 % | 0 % | une octave en dessous, pour une 808 trop maigre |
| Sub Phase | Linear / Analog | Linear | Linear garde la forme de la 808 (crêtes stables) |

### 2. Match EQ — l'équilibre tonal, fixe et dynamique

Le sélecteur **EQ | DYN**, en haut de la page, choisit la vue.

**Vue EQ** : une courbe à phase linéaire, en Mid et en Side séparément (21 nœuds par canal, une
demi-octave chacun, ±12 dB). La barre IA et les références l'écrivent ; tu la retouches en
glissant les nœuds sur l'écran.
- **Amount** (0–100 %) dose toute la courbe ; **Tilt** (±6 dB) penche l'ensemble.
- **Caractère** : **Propre** (phase linéaire, précis : le défaut) ou **Analogique** (cloches plus
  larges et plus douces, phase minimale comme un EQ analogique : aucun pré-écho devant le kick et
  la 808 quand on pousse le grave). Même latence. Barre IA : « EQ analogique », « phase linéaire ».
- **MID / SIDE** : le canal que tu édites (l'autre est affiché en bleu).
- **À PLAT** : remet le canal édité à 0 dB.
- La carte **RÉFÉRENCE VISÉE** rappelle ce que tu vises.

**Vue DYN : l'EQ dynamique.** 4 bandes qui **n'agissent que quand un son sort du lot** : la
sifflante, la note de 808 qui gonfle, le hi-hat qui pique. Le reste du temps, elles ne font rien.
- Chaque bande compare son niveau à **son équilibre habituel dans le mix**. Elle ne dépend donc
  pas du volume de ta prod : pas besoin de régler un seuil en dBFS.

| Réglage | Plage | Défaut | Rôle |
|---|---|---|---|
| ACTIVE | on / off | off | allume la bande |
| Canal | Stéréo / Mid / Side | selon la bande | ce que la bande écoute et corrige |
| Freq | 20 Hz – 20 kHz | 70 · 300 · 3500 · 7000 Hz | centre de la bande |
| Q | 0,3–8 | 1,4 · 1 · 1,2 · 2 | largeur (plus grand = plus étroit) |
| Range | −18…+12 dB | −4 dB | jusqu'où elle va : **négatif = retient**, **positif = fait ressortir** |
| Threshold | 0–18 dB | 4 dB | de combien la bande doit dépasser son habitude avant d'agir |
| Attack / Release | 0,5–50 ms / 20–1000 ms | 5 / 150 ms | vitesse, commune aux 4 bandes |

- **Sur l'écran** : glisse un nœud à gauche ou à droite pour la fréquence, en haut ou en bas pour
  la plage. **Double-clic** sur un nœud : allume ou éteint la bande. La courbe pâle montre la
  plage, la courbe pleine ce que la bande fait en ce moment.
- **EN CE MOMENT** affiche le gain appliqué en direct. Dans le rack, la carte Match EQ affiche
  « DYN −x dB ».
- **Aucune latence ajoutée.** Bande éteinte : le son est intact.

### 3. Inflator — densité et chaleur

Rend le son plus dense et plus plein **sans faire monter les crêtes**. La 808 (sous Split) et le
reste du mix sont réglés séparément.

| Réglage | Plage | Défaut | Rôle |
|---|---|---|---|
| Inflate 808 / Inflate Mix | 0–100 % | 0 % | dosage sous / au-dessus de Split |
| Split | 60–300 Hz | 120 Hz | séparation 808 / mix |
| Drive | ±12 dB | 0 dB | poussée dans la courbe |
| Curve | −50…+50 | 0 | −50 : très dense et doux ; +50 : presque linéaire puis franc |
| Warmth | 0–100 % | 0 % | harmoniques paires (le « chaud ») |
| Output | −12…+6 dB | 0 dB | niveau de sortie |
| Oversampling | 1x–16x | 8x | qualité contre le repliement |
| Caractère | Inflate · Bande · Lampe · Transistor | Inflate | la couleur de la saturation (ci-dessous) |

**Les caractères** (changer en lecture ne claque pas) :
- **Inflate** : densité, la crête reste à sa place. Le plus « fort » à l'oreille.
- **Bande** : tasse les crêtes en douceur, harmoniques impaires discrètes. Colle, adoucit.
- **Lampe** : harmoniques paires, plus rond et chaud. Bien sur une 808 ou des mélodies froides.
- **Transistor** : propre tant qu'on ne pousse pas, puis mordant (harmoniques hautes). Pour du grain.

Dans la barre IA : « saturation à lampe », « effet bande », « du transistor ».

### 4. Imager — largeur stéréo par bande

Largeur du Side dans 4 bandes (séparations 250 Hz, 2 kHz et 8 kHz par défaut). **0 % = mono,
100 % = inchangé, 200 % = très large.** Le centre (808, kick, lead) n'est jamais touché.

### 5. Multiband — tenir la 808 dans le temps

Compresseur à 4 bandes (sub, bas, médium, aigu ; séparations 120 Hz, 800 Hz, 5 kHz, à glisser sur
l'écran). Par bande : **Threshold** (−60–0 dBFS), **Ratio** (1–20:1), **Attack** (1–300 ms),
**Release** (10–3000 ms), **Makeup** (±12 dB). Neutre par défaut (ratio 1:1). Le gros bouton du
rack (**GLUE**) règle les quatre bandes ensemble.

### 6. Clipper — écrêter les pics avant le limiteur

| Réglage | Plage | Défaut | Rôle |
|---|---|---|---|
| Mode | Hard / Soft Knee / Diode | Soft Knee | forme de l'écrêtage (Diode : plus chaud) |
| Drive | 0–24 dB | 0 dB | poussée dans le clipper |
| Ceiling | −12–0 dBFS | 0 dBFS | niveau d'écrêtage |
| Knee | 0–100 % | 50 % | douceur du coude |
| Oversampling | 1x–16x | 8x | qualité contre le repliement |
| Split | 60–300 Hz | 120 Hz | la 808 s'écrête à part, sans salir les mélodies |
| Sub Drive | ±12 dB | 0 dB | plus (ou moins) d'écrêtage sur la 808 seule |
| Sub Make-Room | 0–100 % | 0 % | la 808 cède un peu de place quand tout tape ensemble |

### 7. Limiter — le plafond final

| Réglage | Plage | Défaut | Rôle |
|---|---|---|---|
| Gain | 0–24 dB | 0 dB | poussée dans le limiteur (le volume) |
| Ceiling | −6–0 dBTP | −1 dBTP | plafond **true peak**, toujours tenu |
| Release | 1–1000 ms | 60 ms | relâchement de base |
| Auto Release | 0–100 % | 100 % | relâchement adaptatif (plus lent quand ça limite longtemps) |
| Punch | 0–100 % | 0 % | accentue les attaques avant de limiter : plus de relief |
| Upward | 0–12 dB | 0 dB | fait monter les passages calmes |
| Unlink Transient / Sustain | 0–100 % | 0 % | gauche et droite limités séparément (attaques / soutenu) |

### Changer l'ordre

Glisse un module par son nom dans le rack. Le son passe brièvement au signal d'origine le temps
de réordonner, sans clic. Exemple :
le Clipper après le Limiter pour un son plus agressif. Dans ce cas, le plafond est tenu à
l'échantillon près, mais le true peak n'est plus garanti.

---

### 8. Comp — compresseur de bus (la colle)

Un compresseur pour tout le mix, sans latence, le même gain à gauche et à droite. Son détecteur
écoute la prod **sans le grave** (SC High-Pass) : la 808 ne le fait pas pomper.

| Réglage | Plage | Défaut | Rôle |
|---|---|---|---|
| Threshold | −40…0 dB | −18 dB | au-dessus, il compresse (lu en crête) |
| Ratio | 1–10 :1 | 2 :1 | force de la compression |
| Attack / Release | 0,1–100 ms / 20–2000 ms | 10 / 200 ms | vitesse d'action / de retour |
| Makeup | 0–12 dB | 0 dB | rend le volume perdu |
| Mix | 0–100 % | 100 % | < 100 % : compression parallèle (garde les attaques) |
| SC High-Pass | 20–300 Hz | 90 Hz | le grave n'entre pas dans le détecteur |
| Caractère | Opto · FET · VCA · Vari-mu | VCA | la façon de compresser (ci-dessous) |

- **Opto** : doux ; relâche vite après un coup bref, lentement quand ça compresse longtemps.
- **FET** : rapide et mordant (détection de crête) ; pour faire claquer les drums.
- **VCA** : la colle propre du bus, la plus neutre.
- **Vari-mu** : très arrondi, la compression augmente peu à peu ; pour lisser une prod.

Gros bouton **SQUEEZE** : de rien à fort (seuil 0 → −36 dB), le volume en partie rendu.

## 10. Écouter honnêtement

| Bouton | Usage |
|---|---|
| **MATCH** | écoute au **même volume** que l'original : juge le son, pas le volume. Il clignote : pense à l'enlever |
| **DELTA** | n'entends **que ce que le module ouvert change** |
| **STÉRÉO / MID / SIDE** | écoute le centre seul ou les côtés seuls |
| **BYPASS** | tout OMNIS contourné, sans clic et sans décalage (la latence reste la même) |
| **A / B** | compare deux réglages complets |
| **ÉCOUTE** (colonne de droite) | entends ton master comme sur un autre système |

**Les écoutes simulées** : AirPods (2/3), AirPods Pro 2, AirPods Max, Beats Studio Pro,
Sony WH-1000XM5, Galaxy Buds2 Pro, JBL Tune 510BT, Sennheiser HD 600, Audio-Technica M50x,
haut-parleur de téléphone, TikTok sur téléphone, voiture, sono de club, spatial (binaural), et la
normalisation de Spotify (−14 LUFS), Deezer (−15), YouTube (−14) et Apple Music (−16).
- Le menu passe en **rouge** tant qu'une écoute est active. Elle n'est **jamais exportée** ni
  gardée dans le projet.
- Ce sont des approximations d'après les caractéristiques publiées, pas des mesures des
  appareils. Les marques ne servent qu'à désigner les modèles.

---

## 11. Les mesures

Colonne MASTER, chaque mesure avec un verdict en mots (BON, FORT, DENSE, PUNCHY…).

| Mesure | Ce qu'elle dit |
|---|---|
| **LUFS-S / LUFS-I** | sonie sur 3 s / sur tout ce qui a été joué (norme BS.1770) |
| **TRUE PEAK** | la vraie crête après conversion ; reste **sous −1 dBTP** pour le streaming |
| **CRÊTE MAX** | la plus haute crête d'échantillon |
| **PLR** | écart crête / sonie : plus il est grand, plus ça respire |
| **LRA** | écart de sonie entre passages calmes et forts |
| **CORR** | corrélation : près de 1 = mono, sous 0 = problème de phase |
| **GR** | réduction du limiteur |

---

## 12. La latence

- **Qualité max** (défaut) : environ **710 ms** à 48 et 96 kHz (767 ms à 44,1 kHz). C'est le prix
  de la phase linéaire très précise dans le grave. Le DAW la compense automatiquement : le son
  reste calé.
- **Mode TRAVAIL** : clique sur **LAT** en haut. La latence tombe vers **90 ms** (67 ms à
  96 kHz) pour jouer en direct, mais la qualité est réduite (grave moins précis). La case
  **clignote en rouge** tant qu'il est actif. **Remets la qualité max avant d'exporter.**

En pratique : compose et enregistre sans OMNIS ou en mode Travail, masterise en Qualité max.

---

## 13. Avant d'exporter

Écris **« export »** dans la barre IA, ou regarde le bandeau **PRÊT À EXPORTER** au-dessus de
l'analyseur. OMNIS vérifie :

- écoute simulée sur **Off** ;
- **MATCH**, **DELTA** éteints, écoute **STÉRÉO** ;
- mode **TRAVAIL** retiré ;
- **BYPASS** éteint ;
- true peak sous 0 dBTP (au mieux sous −1 dBTP) ;
- sonie raisonnable (au-delà de −6 LUFS, il prévient).

> **Export 16 bits** (CD, certaines plateformes) : OMNIS n'ajoute pas encore de dither. Active
> le dither de ton DAW à l'export. En 24 bits ou en flottant, ce n'est pas utile.

---

## 14. Et les pistes ?

OMNIS ne se met que sur le master : il ne traite pas les pistes une par une. Ce qui se corrige
mieux piste par piste (une 808 trop forte, un bas-médium qui s'accumule), le **diagnostic du mix**
te le dit, pour que tu le règles dans ton projet.

---

## 15. Diagnostic du mix

Bouton **DIAGNOSTIC DU MIX** (en bas à droite), ou « diagnostic » dans la barre IA. OMNIS compare
ta prod (ce qu'il a entendu) à la référence choisie, ou à la moyenne des références. Il dit ce
que **le master ne peut pas rattraper**, à corriger dans ton projet :
- **Important** : mix qui touche déjà 0 dBFS, mix déjà écrasé, grave pas mono ;
- **Conseil** : perte en mono, 808 trop forte ou absente, bas-médium qui s'accumule, médiums,
  présence, attaque du grave ;
- **Info** : air, note tenue par la 808.

Il ne corrige rien de lui-même : certains « défauts » sont voulus, c'est toi qui tranches.

---

## 16. Recettes

**Premier master rapide.** RÉF › ton style. AUTO, puis le morceau en entier. Puis « un peu moins
fort » ou « plus fort » selon le goût. « export » avant de rendre.

**Un hi-hat ou un lead qui siffle.** « ça siffle » dans la barre IA. Si ce n'est pas assez, redis-le :
la bande se creuse de 2 dB à chaque fois (jusqu'à −12 dB). À la main : Match EQ › DYN, bande 4,
canal Mid, 6–8 kHz, Range −6 dB.

**La 808 qui gonfle sur certaines notes.** Match EQ › DYN, bande 1, Stéréo, Freq sur la note qui
déborde (50–80 Hz), Q 2, Range −3 dB, Threshold 3 dB.

**Faire claquer la caisse claire.** Une bande dynamique à 200 Hz (corps) ou 2–4 kHz (claquement),
**Range positif** (+3 dB), Attack 1 ms, Release 80 ms : elle ne ressort que sur les coups.

**Plus fort sans écraser.** « plus fort » plutôt que pousser le limiteur à fond. OMNIS répartit
entre Clipper (les pics) et Limiter (le reste), en surveillant le PLR. Pour un rendu très fort et
sale : « plus sale », Clipper en Hard, un peu de Sub Drive.

**Vérifier en mono et sur téléphone.** ÉCOUTE › *Phone speaker* puis *TikTok (phone)*. Si la 808
disparaît : un peu de **Sub Synth** dans le module Sub, ou un peu d'**Inflate 808**.

---

## 17. Questions fréquentes

**OMNIS dit « lance la lecture ».** Il a besoin d'entendre ta prod quelques secondes avant d'agir.
Ta demande est gardée et s'applique dès qu'il a écouté.

**La latence affichée est très grande.** C'est normal en Qualité max ([§ 12](#12-la-latence)). Le
DAW la compense. Pour jouer en direct : mode TRAVAIL.

**MusicBrainz ne trouve pas mon titre.** Les petits artistes y sont souvent absents. Le fichier
est quand même ajouté avec le nom que tu lui as donné (« Artiste - Titre »). Sans nom ni
étiquette, OMNIS te demande de renommer le fichier.

**Je n'entends aucune différence.** Vérifie le voyant **ON** des modules, **BYPASS**, et que
l'écoute simulée est sur **Off**. Un module neutre (réglages par défaut) laisse passer le son tel
quel.

**Le son change de volume quand je compare.** Active **MATCH** : tu compares au même volume.

**J'ai tout cassé.** « annule » (pas à pas), ou « remets tout à zéro ».

**Combien de processeur ?** Sur un cœur à 48 kHz : environ 13 % au repos, 16 % tous modules
actifs.

---

## 18. Tes données

- **Aucune télémétrie.** OMNIS n'envoie rien, sauf l'artiste et le titre d'un fichier que tu
  ajoutes, à MusicBrainz, si cette recherche est activée.
- **Ton dossier personnel** :
  - Windows : `%APPDATA%\Abrasion Labs\OMNIS`
  - macOS : `~/Library/Application Support/Abrasion Labs/OMNIS`
- Il contient tes références ajoutées (des mesures, jamais de son), les pochettes que tu as
  fournies, tes préférences et ce qu'OMNIS a appris de tes retours. Supprime-le pour tout remettre
  à neuf.
- Le projet du DAW garde tes réglages et la référence choisie.

---

## 19. Caractéristiques

| | |
|---|---|
| Format | VST3, Windows 64 bits et macOS 10.15+ (Intel, Apple Silicon) |
| Audio | stéréo, 32 et 64 bits, toutes les fréquences courantes (testé de 22,05 à 384 kHz) |
| Latence | Qualité max ≈ 710 ms · Travail ≈ 90 ms |
| Chaîne | 8 modules réordonnables (dont un compresseur de bus à 4 caractères), EQ dynamique à 4 bandes intégré au Match EQ |
| Mesures | sonie BS.1770 (LUFS M / S / I), LRA, true peak, PLR, corrélation |
| Écoutes simulées | 18, plus Off |
| Langues | français, anglais |
| Réseau | MusicBrainz seulement, à l'ajout d'un fichier, désactivable |
| Éditeur | Abrasion Labs · https://onirique.store |

Mentions légales et licences des composants : `LICENSE_NOTICES.md`, livré avec le plug-in.
