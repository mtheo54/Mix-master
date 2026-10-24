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
