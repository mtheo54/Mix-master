# Calibration du module 4 : Match EQ, adaptation à une référence, tenue multibande

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

Latence de l'étage : 8704 échantillons (197 ms à 44.1 kHz). Chaîne complète : voir §6.4. Piste d'économie : fusionner le Match EQ dans le noyau
du Sub Control. Les deux sont des modules M/S à phase linéaire, la fusion retirerait 197 ms.

## 2. Écouter le titre : la signature (`analysis/SpectralSignature`)

Seuls les éléments **persistants** sont mesurés :

- balance Mid et Side aux 21 nœuds, ramenée au « corps » du mix (moyenne 200 Hz – 5 kHz) ;
- **énergie totale du sub** (≤ 80 Hz) ;
- dynamique de chaque bande (P90 − P10 du niveau dans le temps) ;
- sonie intégrée, LRA, true peak, PLR.

Seul le **cœur du morceau** est mesuré : les trames à plus de 10 dB sous le 95e centile sont
écartées (intros, breaks, fins calmes, silences). La porte était d'abord à −20 dB : une intro sombre
à −17 dB entrait dans la mesure et décalait la balance jusqu'à 3.4 dB (test). Sur les références
denses, le passage à −10 dB ne change presque rien (sub +0.14 / +0.19 dB).

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

## 6. Tenue dans le temps : le multibande (`stages/MultibandStage`, paramètres 400–444)

### 6.1 L'étage

- 4 bandes (sub < 120 Hz < bas < 800 Hz < médium < 5 kHz < aigu) séparées à phase linéaire par
  trois FIR de 8192 taps. Chaque bande est la différence de deux passe-bas Linkwitz-Riley à phase
  nulle, la quatrième est le complément. La reconstruction est exacte : ratio 1 donne un retard
  pur (null-test < 1e-12).
- Par bande, liée en stéréo : seuil (dBFS RMS), ratio, attaque, relâchement, compensation, genou
  de 6 dB.
- Détecteur RMS sur une fenêtre glissante **tournée vers l'avant** : 50 ms pour le sub, 20 ms
  au-dessus. Le gain passe ensuite par `LookaheadGain` (anticipation de 10 ms, minimum glissant
  puis rampe), la même brique que le limiteur.

Trois versions ont été écartées au banc de test, chacune pour un défaut mesuré :

| Version | Défaut mesuré |
|---|---|
| RMS à constante de 30 ms, gain lissé en dB | l'attaque de la 808 passait : détecteur plus lent que l'anticipation |
| Fenêtre centrée de 25 ms | ondulait à 30 Hz (compression de 0.5 dB de trop) et était à moitié vide au début d'un coup |
| Fenêtre tournée vers l'avant (retenue) | le coup ne dépasse plus sa valeur tenue (< 1.5 dB) |

| Test | Résultat |
|---|---|
| Neutre | retard pur, < 1e-12 |
| Compensation +6 dB sur le médium | gain = 1 + (2 − 1)·\|B₂(f)\| à ±0.03 dB (chevauchement Linkwitz-Riley) |
| 30 Hz à −10 dBFS RMS, seuil −20, ratio 4 | −7.5 dB (±0.2) |
| 808 compressée de 9 dB | harmoniques < −50 dB, voix à 1 kHz intacte (±0.1 dB) |
| Coup de 808 sur silence | pas de pointe au-dessus de la valeur tenue |

### 6.2 Le réglage, d'après la référence (`ai/DynamicsIntent`)

1. L'écart de **variation** par bande (P90 − P10 dans le temps, signatures) est refermé à hauteur
   de la ressemblance, au plus 60 % de la variation de la prod.
2. Seuil = médiane de la bande (`analysis/BandLevels`). Ratio choisi pour que la moitié haute se
   réduise de l'écart voulu, au plus **4:1** (2:1 pour une région protégée).
3. Attaque : 10 ms dans le sub, 20 à 25 ms au-dessus pour laisser passer les transitoires
   (davantage si la demande parle de punch).
4. Compensation **recalée par mesure** en trois passes : chaque bande retrouve son énergie
   moyenne, donc l'équilibre tonal de l'intention ne bouge pas.

Exemple (prod `pour_claude`, référence SLAYR) :

```
Sub : varie de 48 dB (référence 34 dB) -> seuil -20.6 dBFS, ratio 4.0:1, attaque 10 ms    compensation +2.4 dB
Médium : varie de 16 dB (référence 9 dB) -> seuil -40.1 dBFS, ratio 2.0:1, attaque 25 ms  compensation +3.4 dB
```

### 6.3 Résultats

| Prod → référence | LRA prod → master (réf.) | Sonie intégrée | Distance tonale à la réf. | Sonie propre (passage fort) : témoin → avec tenue |
|---|---|---|---|---|
| pour_claude → SLAYR | 12.1 → **5.2 LU** (3.6) | −9.3 LUFS | 6.0 → 3.5 dB | −8.78 → −8.67 |
| pour_claude → poster boy | 12.1 → **5.2 LU** (7.7) | −9.2 LUFS | 8.2 → 5.5 dB | −8.78 → −8.60 |

**La tenue et la densité de la référence sont là, mais la sonie propre ne gagne que 0.1 à 0.2 LU.**

### 6.4 Pourquoi (mesuré, trois hypothèses écartées)

| Hypothèse | Test | Verdict |
|---|---|---|
| Le limiteur module la 808 cycle par cycle (anticipation trop courte) | anticipation 20 ms | aucun effet, revenu à 10 ms |
| Le sub remplit la marge | sub −4 puis −5 dB dans l'intention | +0.1 / +0.2 LU seulement |
| La tolérance est trop stricte | tolérance 3 dB (« sale ») | +0.6 LU seulement |

La vraie limite, ce sont les **crêtes courtes**. Le passage le plus fort de la prod a 12 dB entre
ses crêtes et sa sonie (PSR, sous les 50 ms). Viser −6 LUFS sous −0.3 dBTP demande moins de
5.7 dB, et poster boy est à 4.4 dB. Retirer 6 dB de crêtes courtes, c'est de la distorsion ou un
déplacement d'équilibre : les références font ce choix, leur son est saturé. Le multibande ne
travaille que sur la dynamique longue, ce qui est son rôle.

Pistes pour aller plus fort sans « détruire » : saturation dédiée de la 808, qui change une partie
de son énergie en harmoniques audibles (étage d'excitation, paramètres 300–399, déjà nommé
« saturation » dans le lexique) ; écrêtage par bande à crêtes courtes.

Latence de la chaîne complète : **29452 échantillons (668 ms) à 44.1 kHz**, compensés par le DAW.
Fusionner le Match EQ dans le noyau du Sub Control retirerait 197 ms.
