# OMNIS — la vision du produit (mots de l'utilisateur, 24/10)

Ce document fixe le cap. Chaque décision technique s'y mesure.

## Le produit

- **Vendu environ 150 €.** Un plug-in qui fait **ton** mixage et **ton** master : selon tes
  goûts, ce dont la prod a besoin, une idée, ou un audio de référence.
- **Parfait.** Pas de petites erreurs par-ci par-là : aucun plantage (le bouton de slot de
  FL Studio doit être sûr), aucun clic, aucun réglage absurde, aucune fausse alerte.
- **Le mix le plus propre possible**, au niveau d'un mix / master pro (~98 %), même à partir
  de stems pas beaux ni propres : « tout ressort incroyable ».
- **Magique et compréhensible** : on voit ce qu'OMNIS utilise, ce qu'il change et pourquoi
  c'est utile. Hyper intuitif ; un tutoriel à la première ouverture.
- **Tous les réglages accessibles**, module par module (pas la simplicité de LeMaster, jugée
  trop pauvre) : on choisit précisément quel élément régler et ce qui ne va pas.
- **Deux usages** : OMNIS seul sur le master (masterise la prod entière, beaucoup le
  poseront juste en fin de chaîne) ; ou OMNIS sur les stems + le master chef d'orchestre.

## Ce que « parfait » veut dire sur les stems

Exemple donné : OMNIS sur un **bus de drums** doit comprendre ce qu'il contient — kick,
snare, clap, hi-hats, open hats — et travailler **chaque élément** à sa façon, à
l'intérieur du bus :
- le kick bien mono ;
- la snare un peu plus large, si le style de la prod le demande ;
- les hats à leur place dans l'aigu et dans le stéréo ;
… et tout doit communiquer dans FL Studio avec l'OMNIS Master pour que tout joue bien
ensemble : le mix parfait.

(Aujourd'hui, un bus de drums est traité comme un tout — « Drums (boucle) ». À venir :
repérer les éléments dans le bus (coups, bandes, transitoires) et appliquer à chacun son
traitement : mono sous ~150 Hz pour le kick, largeur dans la bande de la snare, etc.)

## Les éléments d'un bus de drums, nommés

OMNIS doit **détecter chaque élément** d'un bus (kick, snare, clap, hi-hat, open hat,
percus…) et **afficher leur nom exact** : la personne voit sur quoi il a travaillé et
comment (« Kick : mono sous 150 Hz », « Snare : +10 % de largeur », « Open hat : dureté à
7 kHz calmée »). Ne jamais laisser un traitement invisible.

## Dans tous les logiciels

Pas seulement FL Studio : Ableton Live, Logic Pro, Cubase, Reaper (et Studio One, Bitwig…).
- VST3 (aujourd'hui) : FL Studio, Ableton Live (11 et plus), Cubase, Reaper, Studio One,
  Bitwig. À vérifier dans chacun : le lien master / stems (mémoire partagée, processus
  séparés de Bitwig), le nom de piste (IInfoListener), le changement de rôle en route.
- Logic Pro : il faut une version **Audio Unit** (enveloppe AU du SDK VST3, à construire
  sous macOS avec Xcode).
- Pro Tools : **AAX** (SDK Avid et signature PACE) : plus tard.

## Le langage des producteurs

La barre IA doit comprendre le vocabulaire réel du studio, en français, en anglais et en
argot : boueux / muddy, boxy, harsh, sizzle, silky, gritty, dirty, clipped / éclaté, glue,
punchy, headroom, wet / mouillé / trop de reverb, ça perce / ça ressort pas, mono
compatible… (`data/lexique_fr.txt`, `data/argot.txt`). À enrichir à chaque mot
nouveau entendu.

## Plus tard : la voix et la prod ensemble

Que la voix s'intègre à la prod **sans que l'une prenne le dessus sur l'autre** : propre,
impactant, sans qu'elles se détruisent. Gros chantier (place de la voix dans la prod, prod
qui s'efface sous la voix aux bons moments, espace commun) : pour un futur.

## L'ambition

Un plug-in si juste qu'il « devrait être illégal » : qu'il change vraiment le mixage et le
mastering. On le construit en cherchant ensemble.
