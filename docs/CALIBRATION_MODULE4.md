# Calibration du module 4 (partie 1) : Match EQ et adaptation à une référence

Point de départ, les mots de l'utilisateur : *« l'IA analyse la demande, écoute franchement le titre,
détermine les éléments persistants de pourquoi le mix est comme ça, et après elle s'adapte à la
prod. Le principe, ce n'est pas de faire un copier-coller, c'est de faire quelque chose qui s'y
ressemble sans détruire la prod. »*

Le module est construit comme cette phrase, dans l'ordre :

```
demande ──► ai::parseRequest ─────────────┐
référence ─► analysis::SpectralSignature ─┤
prod ──────► analysis::SpectralSignature ─┴─► ai::planMatch ─► courbe Mid/Side + sonie + explication
                                                     │
prod ─► Sub Control ─► Match EQ ─► Clipper ─► Limiteur ─► master
                                     ▲
              omnis_tune --max-clean : sonie la plus forte qui respecte l'INTENTION
```

## 1. Match EQ (`stages/MatchEqStage`, paramètres 200–270)

- 21 nœuds par canal (Mid et Side), une demi-octave d'écart, de 20 Hz à 20.5 kHz, ±12 dB, plus le
  dosage `Match Amount`. Ces 43 paramètres sont de vrais paramètres VST3 : ils sont sauvegardés
  dans la session et automatisables, et ce sont les sorties que le module 5 devra prédire.
- Entre deux nœuds, la courbe suit une spline de Catmull-Rom en log-fréquence (C1 ; une pente
  régulière est reproduite exactement).
- FIR à phase linéaire de 16384 taps, convolution UPOLS, noyaux recalculés en tâche de fond et
  fondus sur une trame, comme le Sub Control.

| Test | Résultat |
|---|---|
| Courbe nulle | retard pur, null-test < 1e-12 |
| Nœud Mid +6 dB à 1.28 kHz / Side +3 dB à 5.12 kHz | ±0.03 dB ; voisins à 6 nœuds : 0.00 dB |
| Match Amount 50 % de −6 dB à 40 Hz | −3.0 dB (±0.05) |
| Changement de courbe en lecture | aucune discontinuité |

Latence de l'étage : 8704 échantillons (197 ms à 44.1 kHz). Chaîne complète : 22454 échantillons
(509 ms) à 44.1 kHz, compensés par le DAW. Piste d'économie : fusionner le Match EQ dans le noyau
du Sub Control. Les deux sont des modules M/S à phase linéaire, la fusion retirerait 197 ms.

## 2. Écouter le titre : la signature (`analysis/SpectralSignature`)

Seuls les éléments **persistants** sont mesurés :

- balance Mid et Side aux 21 nœuds, ramenée au « corps » du mix (moyenne 200 Hz – 5 kHz) ;
- **énergie totale du sub** (≤ 80 Hz) ;
- dynamique de chaque bande (P90 − P10 du niveau dans le temps) ;
- sonie intégrée, LRA, true peak, PLR.

Les silences, intros et coupures sont exclus (porte à −20 dB du 95e centile).

**Pourquoi des bandes compactes et un bloc sub.** Le spectre fin dépend des notes : une 808 en Fa
et une 808 en La n'ont pas leur fondamentale au même endroit. Ce sont des choix d'arrangement, pas
de mix. Une première version lissait par gaussienne. Ses queues faisaient varier le nœud 113 Hz de
**5.8 dB** entre une 808 à 41 Hz et la même à 49 Hz, et l'EQ « corrigeait » une simple différence de
tonalité. Version retenue :

- bandes d'une demi-octave à support compact (poids cos², somme des poids = 1 par bin) ;
- sous 80 Hz, un seul écart, en énergie.

Même test : la correction proposée tombe à **0.05 dB**.

| Test | Résultat |
|---|---|
| Bruit rose | balance plate à ±0.6 dB (28 Hz – 14 kHz) |
| Pente de +1 dB/oct appliquée par le Match EQ | retrouvée par la signature : 5.0 dB sur 5 octaves (±0.3) |
| Moitié du signal muette | même balance (±0.8 dB) |
| 808 à 41 Hz contre 49 Hz | correction < 0.75 dB sur tous les nœuds (mesuré : 0.05) |

## 3. S'y ressembler sans détruire : l'intention (`ai/MatchIntent`)

1. **Écart** : balance relative de la référence moins celle de la prod, lissée une fois.
2. **Ressemblance** r, 50 % par défaut : on ne referme qu'une fraction de l'écart. Elle est bornée
   à **85 %**, une copie n'est jamais proposée.
3. **Bornes d'identité** :
   - sub ±3 dB ;
   - ailleurs −6 / +4 dB (couper est plus sûr que booster) ;
   - aucun boost d'une bande où la prod n'a presque rien (plus de 40 dB sous son corps).
4. **Largeur** : même principe sur Side − Mid, au-dessus de 120 Hz, ±4 dB, sans élargir une bande
   quasi mono.
5. **Demande** : appliquée après les bornes, la personne a le dernier mot. Quelques mots-clés
   suffisent :
   - « garde ma 808 » → sub ±1 dB ;
   - « un peu » → 30 % ;
   - « exactement » → 80 % ;
   - « −8 LUFS », « propre », « sale », « brillant », « large »…

Chaque intention est **expliquée**. Exemple réel (prod `pour_claude`, référence SLAYR) :

```
Pourquoi la référence sonne comme ça :
  - Sub (20–80 Hz) : 12.2 dB plus discret que dans ta prod
  - Bas (110–230 Hz) : 5.8 dB plus discret que dans ta prod
  - Image stéréo plus serrée au-dessus de 1 kHz (Side -5.7 dB)
  - Master à -6.0 LUFS, LRA 3.6 LU, PLR 8.2 dB
  - Sub beaucoup plus tenu dans le temps (variation 36 dB contre 52 dB) : compression / saturation
    de la 808, pas une question d'EQ
Ce que l'on garde de ta prod :
  - Sub (20–80 Hz) : la référence est à -12.2 dB, on ne bouge que de -3.0 dB (identité de ta prod)
```

## 4. Résultats sur les fichiers de l'utilisateur (`omnis_match`)

Plafond −0.3 dBTP, tolérance de 1.5 dB par rapport à l'intention. Le témoin est la même recherche,
sans Match EQ.

| Prod | Référence | Distance tonale prod → master | Sonie (passage fort) | Témoin | Sonie intégrée |
|---|---|---|---|---|---|
| pour_claude | SLAYR | 6.0 → **3.6 dB** | −8.75 | −8.78 | −9.7 LUFS |
| pour_claude | poster boy | 8.2 → **5.5 dB** | −8.75 | −8.78 | −9.6 LUFS |
| pour_claude_plugin | SLAYR | 5.7 → **3.3 dB** | −8.88 | −9.06 | −9.2 LUFS |

**La ressemblance progresse nettement, la sonie ne bouge pas.** Retirer 3 dB de sub n'a libéré
aucune marge. Avec une tolérance de 3 dB (« sale »), on n'atteint que −8.05 LUFS.

## 5. Ce qui bloque vraiment (mesuré)

Hypothèse testée : la 808 écrêtée remplit les médiums d'harmoniques. **Fausse.** Rendue seule avec
les mêmes réglages, elle ne produit que 40.8 dB dans 250–500 Hz, contre 73 dB dans le mix.

La vraie cause apparaît en mesurant la réduction de gain par bande au passage le plus fort (clip
11 dB + limiteur 4.7 dB) :

| Bande | 20–120 | 120–250 | 250–500 | 0.5–1 k | 1–2 k | 2–4 k | 4–8 k |
|---|---|---|---|---|---|---|---|
| Réduction appliquée | **−6.4** | −5.4 | −3.9 | −3.8 | −4.0 | −5.5 | −5.6 dB |

Les crêtes sont celles de la 808, et le clipper et le limiteur se comportent comme un compresseur
qui la cible. Ce ne sont pas les médiums qui montent, c'est le sub qui est écrasé dans le temps.

La signature le dit aussi : la 808 de SLAYR varie de **36 dB** dans le temps, celle de la prod de
**52 dB**. La sonie de la référence vient de la **tenue de sa 808** (compression lente, saturation),
pas de son EQ.

**Étape suivante** : un étage dynamique multibande (paramètres 400–499) qui tient la 808 dans le
temps, avec un gain de compensation. On garde ainsi l'énergie du sub tout en baissant ses crêtes,
et le limiteur n'a plus à l'écraser. La cible de tenue vient de la signature de la référence (écart
P90 − P10 par bande), dosée et bornée comme l'EQ.
