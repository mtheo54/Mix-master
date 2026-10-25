# Ce que font les pros, et ce qu'OMNIS doit en tirer (24/10)

Synthèse de recherches web (entretiens, séries « Mix With The Masters », guides de mastering
hip-hop) et de pratiques largement documentées. Les chiffres sont des **points de départ**
reconnus, pas des lois : OMNIS les ajuste à la prod. Sources en bas.

## 1. Ce qui revient chez les ingénieurs (rap, trap, pop urbaine)

**Gain staging d'abord.** Chaque piste à un niveau sain avant tout traitement (souvent
~−18 dBFS RMS, crêtes vers −6 dBFS) ; le master fader à 0 et le mix qui n'écrête pas
(laisser de la place au master). → La prod « sans OMNIS » de l'utilisateur écrêtait
(+1,6 dBTP) : un vrai cas.

**La 808 et le kick : la relation n°1.**
- 808 en mono dans le grave, kick en mono ;
- la 808 « multibande » : le sub tenu (compression / limitation du bas), le haut saturé
  pour qu'elle s'entende sur un téléphone (distorsion parallèle, multibande) ;
- kick et 808 qui ne se battent pas : place dans le temps (ducking de la 808 sous le kick,
  sidechain), place en fréquence (l'un tient le sub, l'autre le punch), et accord de la 808 ;
- traitement parallèle sur les drums (compression « New York »), gestion du sustain du kick
  pour garder les transitoires sans distorsion audible.

**La voix au centre de tout** (rap : sèche, en avant, intelligible) : passe-haut, EQ
soustractive (boue ~200–400 Hz, dureté ~2–5 kHz), de-esser, compression en série (une rapide
pour les crêtes, une lente pour la tenue), saturation légère, puis espace (delay / reverb en
envoi, filtrés) ; ad-libs et backs plus larges et plus bas.

**Drums** : kick mono, snare / clap avec un peu de largeur et d'espace, hats à leur place
dans l'aigu (souvent légèrement panoramiqués), transient shaping, compression de bus
(« glue »), saturation parallèle.

**Instruments / mélo** : passe-haut pour laisser le grave à la 808, EQ dynamique sur ce qui
gêne la voix, largeur (les nappes larges, le grave mono), automation.

**Master hip-hop** : EQ M/S (Side coupé sous ~50–60 Hz pour garder la 808 au centre),
couleur / saturation, compression de bus légère, **clipper avant le limiteur** (le clipper
prend l'essentiel des crêtes, ~2–5 dB, en dur pour mordre les transitoires), puis limiteur
true peak suréchantillonné (~1–3 dB de réduction, plafond ~−1 à −0,2 dBTP). Comparer à une
référence **au même volume**.

## 2. Ce qu'OMNIS a déjà, et ce qui manque

| Pratique pro | OMNIS aujourd'hui | À faire |
|---|---|---|
| Gain staging | Trim par stem ; sécurité au master | **Gain staging auto** : chaque stem ramené à un niveau sain, le master averti s'il écrête |
| 808 mono, sub tenu, haut saturé | Mono grave, multibande « tenue de la 808 », saturation Inflator | Saturation **du haut seulement** (parallèle, multibande) |
| Kick / 808 | Non comparés (volontaire) | **Ducking 808 sous le kick** (le master le commande, sans latence), conflit de fréquences |
| Voix lead | EQ dyn (boue, dureté, sifflantes), compression | **De-esser** dédié, compression en série, **envois delay / reverb**, ad-libs |
| Drums dans un bus | Traité comme un tout | **Éléments dans le bus** (kick mono, snare plus large, hats) — voir VISION.md |
| Transitoires | — | **Transient shaper** (attaque / sustain) |
| Compression parallèle | — | **Mix (wet / dry) sur chaque module** |
| Résonances | EQ dynamique par bandes fixes | **Suppresseur de résonances** (spectral, façon « soothe ») |
| Master : EQ M/S, couleur, clip, limiteur | Match EQ M/S, Inflator, clipper, limiteur TP, imager | Comparaison A/B **au même volume** dans le plug-in |
| Référence | Écoute d'une référence + base de tes références | Bouton **Match** dans l'interface |

## 3. Le rack OMNIS (la vision de l'utilisateur : « comme un rack de mixage dans FL »)

Comme les slots du mixer de FL Studio ou le Patcher : on **place** les modules qu'on veut,
dans l'ordre qu'on veut, on les **active / désactive**, on **dose** chacun.

- **Un rack par instance** : sur un stem (rack de piste) et sur le master (rack de master).
- **Modules** :
  - piste : Gate, Passe-haut, EQ (paramétrique + dynamique), De-esser, Compresseur
    (rapide / lent, styles), Transient, Saturation, Largeur / M/S, Envois (delay, reverb) ;
  - master : Match EQ, Sub & mono, Multibande, Inflator, Imager, Tilt, Clipper, Limiteur.
- **Chaque module** : bouton on / off, **mix wet / dry** (parallèle), solo d'écoute,
  « pourquoi ? » (ce qu'il fait et pourquoi OMNIS l'a réglé ainsi : la clé de l'intuitivité).
- **Ordre libre** par glisser-déposer, avec des ordres conseillés (ex. clipper avant
  limiteur ; EQ soustractive avant compression).
- **AUTO** : OMNIS construit le rack selon l'élément reconnu et la prod (modules choisis,
  ordre, doses), en respectant ce qui est déjà bien fait (comme aujourd'hui).
- **Barre IA** : « la voix plus en avant », « 808 plus sale », « enlève le reverb » →
  OMNIS active / désactive / dose les modules et explique ce qu'il a fait.
- **Racks enregistrables** par prod et par style (« rap fr », « rage », « ma voix »…),
  réactivables d'un clic.
- **Zéro latence sur les pistes** (règle actuelle) ; modules à latence (phase linéaire) au
  master seulement.

## 4. Priorités proposées (après le plantage du slot FL)

1. Le rack (structure, on / off, ordre, mix) sur les modules existants + l'interface console.
2. Barre IA + AUTO qui pilotent le rack ; OMNIS seul sur le master.
3. Kick / 808 (ducking commandé par le master), gain staging auto.
4. De-esser, transient shaper, compression parallèle, envois delay / reverb.
5. Éléments dans un bus de drums ; suppresseur de résonances.
6. Tutoriel de première ouverture (parcourt le rack module par module).

## Sources

- Jaycen Joshua, Mix With The Masters (808, kick, distorsion parallèle, multibande, gain
  staging) : https://mixwiththemasters.com/videos/jaycen-joshua-rosalia-yo-x-ti/part/2 ,
  https://mixwiththemasters.com/fr/videos/jaycen-joshua-lady-london-yea-yea/part/3 ,
  https://mixwiththemasters.com/videos/jaycen-joshua-advanced-mix-techniques ,
  https://producelikeapro.com/blog/jaycen-joshua-interview/
- Irko, voix hip-hop et 808 / kick : https://producelikeapro.com/blog/mixing-hiphop-rb-irko/
- Nk.F (Angèle, Damso), mix de voix : https://mixwiththemasters.com/videos/nk-f-angele-damso-demons
- Leslie Brathwaite (Cardi B), voix et 808 : https://mixwiththemasters.com/videos/leslie-brathwaite-cardi-b-money
- Chaîne de mastering hip-hop (EQ M/S, clipper, limiteur) : https://www.sageaudio.com/articles/creating-a-hip-hop-mastering-chain ,
  https://mastering.com/clipper-vs-limiter-know-the-difference/ ,
  https://masteringthemix.com/blogs/learn/how-to-use-a-clipper-when-mastering
- Préparer un mix pour le mastering (Lu Diaz) : https://www.waves.com/prepare-hip-hop-for-mastering

## 5. Banc d'essai OMNIS contre Ozone (24/10, kit de mesure, même volume ~−9,5 LUFS)

Ozone : réglage de l'utilisateur (exports 16 bits). OMNIS : réglages par défaut, limiteur
+2,8 dB (`omnis_render --set lim_gain=2.8`). Script : sur la boucle trap, le crête de bruit
rose, l'impulsion, le balayage aigu et le test inter-échantillons.

| Mesure | Ozone | OMNIS |
|---|---|---|
| True peak (test inter-échantillons) | **+1,73 dBTP** (dépasse 0) | −1,05 dBTP |
| True peak (boucle trap) | +0,11 à +0,25 dBTP | −0,99 dBTP |
| Sons parasites, balayage 1–20 kHz (moyenne) | −40,3 dB | **−53,9 dB** |
| Pré-écho (impulsion) | aucun | −58,5 dB (étages à phase linéaire) |
| Courbe sur bruit rose | EQ douce : −1 dB à 4 kHz, +0,9 dB d'air, −0,9 dB à 31 Hz | plate au-dessus de 250 Hz ; −2 à −3 dB par canal sous 125 Hz = mono du grave par défaut (Side retiré d'un bruit décorrélé ; une 808 déjà mono ne change pas) |
| PLR boucle trap | 9,6 dB (mais true peak > 0) | 8,4 dB |
| Crête/RMS par bande (sub / aigu) | 9,4 / 24,6 dB : plus de relief | 6,5 / 23,0 dB (comme l'original) |

À retenir : OMNIS gagne en propreté (true peak honnête, ~14 dB de parasites en moins).
Ozone garde plus de relief dans le sub et l'aigu : à écouter et à étudier (punch du
limiteur, transitoires). À faire : supprimer le pré-écho (option phase minimale sur le
master), comparer les choix tonaux sur de vraies prods (Master Assistant contre OMNIS).

Réglage Ozone confirmé (capture de l'utilisateur) : Ozone 12, preset « Modern Hip Hop » =
Equalizer → Vintage Comp → Maximizer (IRC 2, gain +4 dB, sortie −1,00 dB, **True Peak
désactivé**, Character 0,47 « very fast », **Transient Emphasis 50 %**, Stereo
Independence transitoire 20 %). Explique les deux écarts :
- true peak > 0 : option True Peak éteinte dans le preset (pas un défaut d'Ozone) ;
- plus de relief sub / aigu : **Transient Emphasis** (le maximizer rend du punch aux
  attaques après la limitation).
À ajouter à OMNIS : **« Punch » / accent des transitoires dans le limiteur** (dosable,
0–100 %), en gardant le true peak honnête ; et l'**indépendance stéréo** du limiteur
(réduction de gain liée / déliée, transitoires et maintien séparés).

## 6. Ce qu'on retient des manuels (Trackspacer, Decapitator, Pro-L 2, Pro-Q 4, schéma façon soothe) — 25/10

Lus pour comprendre les principes ; rien n'est recopié. Reformulé et adapté à OMNIS.

**Faire de la place entre deux sons (Trackspacer).** Un son « prioritaire » est analysé en
continu sur une trentaine de bandes ; l'autre est creusé en miroir, là où le prioritaire a
de l'énergie, au moment où il en a. Un seul dosage, une plage de fréquences (coupe-bas /
coupe-haut de l'analyse), attaque / relâche, L/R ou M/S, et un « figer » qui transforme le
creux du moment en EQ fixe.
→ Pour OMNIS : la **place de la voix dans la prod** (la prod se creuse sous la voix, sans
latence, le master la commande comme les retouches) et la **808 sous le kick** (même
principe, limité au grave). Notre lien master / stems fait déjà circuler l'audio : pas
besoin de side-chain à câbler dans le DAW, c'est un vrai avantage d'OMNIS.

**Saturation (Decapitator).** Plusieurs « styles » (bande, console, préampli à transistors,
lampe triode = harmoniques paires, lampe pentode = impaires), un drive, une tonalité AVANT
la saturation (ce qui sature change le caractère), un coupe-bas avant (évite le grave mou),
un petit relief de grave au coupe-bas (« bosse » de la bande), un coupe-aigu APRÈS (retire
le grésillement), un mélange sec / saturé, et une compensation automatique du volume.
→ Pour OMNIS : l'Inflator et la saturation des stems gagnent des **styles** (doux pair /
impair / bande / dur), un **filtre avant** et un **filtre après**, le **mélange** et le
**volume compensé** (pour comparer sans se faire piéger par le « plus fort = mieux »).

**Limiteur (Pro-L 2).** Plusieurs **styles** de limitation selon l'usage (transparent,
punchy, dynamique qui accentue les attaques avant de limiter, agressif proche du clip,
« bus » qui pompe exprès, sûr), un **look-ahead** réglable (court = plus fort mais
distorsion et crêtes entre échantillons), une étape rapide (transitoires) et une étape lente
(relâche), un **lien stéréo séparé** pour les transitoires et pour la relâche, le **true
peak** en deux temps (crêtes d'entrée, puis dépassement résiduel lissé), suréchantillonnage,
dither, et surtout « jamais au-dessus de 0 dBTP ». Gain et plafond liés pour comparer au même
volume.
→ Pour OMNIS : styles de limiteur, lien stéréo transitoires / relâche, **Punch** (déjà noté
§5), écoute au même volume, et des mesures claires (crêtes, réduction, sonie).

**EQ (Pro-Q 4).** Chaque bande peut devenir **dynamique** (elle ne coupe ou ne monte que
quand la bande dépasse un seuil, réglé automatiquement par défaut, déclenchement sur la
bande elle-même ou sur une autre piste) ; l'**EQ spectrale** ne touche, dans une bande, que
les fréquences précises qui dépassent (idéal pour la dureté des voix et des guitares) ; et
la **liste des instances** : toutes les EQ de la session dans une seule fenêtre, rangées par
piste avec leur nom, et les **collisions** (deux pistes qui occupent la même zone) montrées
par rapport à une piste de référence.
→ Pour OMNIS : c'est exactement notre **master chef d'orchestre en interface** : depuis le
master, voir toutes les pistes OMNIS (nom, élément reconnu, rack), régler chacune, et voir
les collisions que le chef d'orchestre a trouvées. Et un module **EQ dynamique / spectral**
dans le rack de piste.

**Suppresseur de résonances (schéma façon soothe).** Entrée (option side-chain), passage
éventuel en M/S, trim, lien entre canaux, **analyse** pilotée par quatre réglages
(profondeur = combien on retire, netteté = largeur des creux, sélectivité = ce qui compte
comme résonance, courbes d'EQ = où agir, avec un équilibre par bande), équilibre stéréo,
application, trim, **mélange** sec / traité, retour en L/R, bypass. Visualisation de ce qui
est retiré.
→ Pour OMNIS : le module **anti-résonances** du rack (piste et master), sur la même base que
notre EQ dynamique (habitude de la piste + seuil), mais en spectral fin, avec ces quatre
réglages et l'affichage de ce qui est retiré.

**Priorités ajustées** (après le plantage du slot FL) :
1. rack + interface console (avec la vue « toutes les pistes » depuis le master) ;
2. barre IA + AUTO ; OMNIS seul sur le master ;
3. limiteur : styles, Punch, lien stéréo ; saturation : styles, filtres avant / après ;
4. place entre les sons : voix / prod, 808 / kick (sans side-chain à câbler) ;
5. anti-résonances spectral ; EQ dynamique / spectral par bande ;
6. éléments d'un bus de drums ; tutoriel.

## 7. Ce qu'on retient des manuels (Gullfoss, KClip, LeMaster) — 25/10

Principes seulement, dits avec nos mots ; rien n'est repris de leur texte ni de leurs
algorithmes.

**Démasquage perceptif (Gullfoss).** Une EQ qui se recalcule en continu (des centaines de
fois par seconde) à partir d'un modèle d'audition : elle **remonte** ce qui est caché par
autre chose et **calme** ce qui prend trop de place, avec un dosage séparé pour chaque côté,
un curseur qui penche le jugement vers le grave ou l'aigu, des limites de fréquence, et
surtout une **compensation de sonie** : le résultat est jugé au même volume perçu, sinon
« plus fort » passe pour « mieux ». Version à faible latence (quelques ms) pour travailler,
version plus lourde pour l'export ; entrée side-chain pour le travail en stems.
→ Pour OMNIS : le module **anti-résonances / démasquage** du rack aura deux réglages
distincts (remonter ce qui est masqué, calmer ce qui domine) au lieu d'un seul « amount »,
une compensation de sonie intégrée, et le chef d'orchestre lui sert de side-chain sans câble
(il sait déjà qui gêne qui, §14 de l'architecture).

**Clipper (KClip).** Plusieurs **courbes** de clip (douce, nette, lampe, bande,
transistors, ampli…) qui changent les harmoniques produites ; un **seuil** et un « adoucir »
pour arrondir le coude ; **gain d'entrée / sortie liés à l'inverse** pour comparer à volume
égal ; **multibande** (1 ou 4 bandes, réglages et solo par bande) et **M/S** ; un **mix**
avec une écoute **delta** (n'entendre que ce que le clip retire) ; un plafond final dur à ne
pas pousser fort ; une **cible LUFS** qui règle le niveau ; suréchantillonnage plus élevé à
l'export qu'en temps réel.
→ Pour OMNIS : **écoute delta** sur chaque module (clip, limiteur, saturation, EQ
dynamique : « qu'est-ce qu'OMNIS enlève exactement ? ») — c'est la réponse directe à « on
voit ce qu'OMNIS change » de la vision. Courbes de clip au choix, clip multibande en option,
lien de gain inverse, cible LUFS, et une qualité d'export supérieure au temps réel.

**Mastering tout-en-un (LeMaster).** Concurrent direct de « OMNIS seul sur le master ». Ce
qui marche chez lui :
- **un ou deux gros boutons par module**, toute la complexité cachée (crossovers, temps,
  seuils) ; chaque module neutre à 0 et avec son interrupteur ;
- **AUTO** : écoute ~10 s du passage le plus fort (refrain, drop), règle toute la chaîne y
  compris le gain d'entrée vers une cible « commerciale » ou la signature d'un fichier de
  référence (sonie, dynamique, largeur, spectre), avec un caractère choisi avant (plus propre
  / plus percutant) ; tout reste modifiable après ;
- **calage d'entrée** automatique vers un niveau de travail fixe, avec une transition douce ;
- **Match** : gain compensé en bout de chaîne, lissé sur quelques secondes, qui clignote pour
  rappeler qu'il faut l'enlever avant l'export ; **A/B** avec copie A → B ; annuler /
  rétablir interne ; « enregistrer comme réglage par défaut » ;
- **écoute simulée** (mono, side seul, portable, téléphone, voiture, home-cinéma), jamais
  imprimée dans l'export ;
- **barre de mesures avec verdict coloré** : sonie court terme et intégrée, crête / true
  peak, dynamique (écart crête – sonie court terme), corrélation de phase, chacune avec un
  mot clair (calme, bon, fort, trop fort ; plat, dense, punchy ; phase, mono, ok) ;
- graves en mono sous ~120 Hz, compression « upward » qui remonte les passages calmes,
  « low end trick » (grave monté + bas-médium légèrement creusé), air en compression
  parallèle, clip suréchantillonné avant le limiteur, true peak en option, latence affichée
  en direct et constante.
→ Pour OMNIS (en gardant **tous** les réglages, que l'utilisateur veut, LeMaster étant jugé
trop pauvre) :
- le **rack** affiche par module un gros bouton principal, et les réglages fins se déplient
  (deux niveaux de lecture : rapide / complet) ;
- **AUTO** sur le master : capture du passage le plus fort, cible par style (nos signatures
  de genres) ou fichier de référence, choix propre / percutant ; la barre IA modifie ensuite
  le résultat avec des mots ;
- **Match + A/B + annuler** dès la première version de l'interface ; Match qui clignote ;
- **barre de mesures avec verdicts en mots** (en français, nos propres seuils, calés sur nos
  mesures de masters) ;
- **écoute simulée** : mono et side seul tout de suite ; téléphone / portable / voiture par
  des filtres dessinés par nous (pas de réponses de vrais appareils sans les avoir mesurées
  légalement nous-mêmes) ;
- ce que LeMaster n'a pas et qu'OMNIS a : les **stems qui se parlent** (chef d'orchestre),
  la reconnaissance des éléments, l'explication en mots de chaque retouche, la barre IA.

**Priorités** : inchangées (§6), avec en plus, dans le point « rack + interface » : écoute
delta par module, Match / A/B / annuler, barre de mesures à verdicts ; et dans « AUTO » :
capture du passage le plus fort + caractère propre / percutant.
