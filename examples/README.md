# Exemples / Examples

Chaque dossier de [`projects/`](projects) contient un projet (`project.json`, à ouvrir dans
Scratch2Arduino) et son **schéma de câblage** (`wiring.png`) pour un Arduino Uno. Sur une Nano, les broches
portent les mêmes noms ; sur une Mega, l'I2C (SDA, SCL) est sur les broches 20 et 21.

Every folder of [`projects/`](projects) holds a project (`project.json`, to open in Scratch2Arduino) and its
**wiring diagram** (`wiring.png`) for an Arduino Uno. A Nano has the same pin names; on a Mega, I2C (SDA,
SCL) is on pins 20 and 21.

Sur les schémas, les fils rouges vont au 5V, les noirs au GND (par les rangées + et − de la platine
d'essai). Dans une platine, les 5 trous d'une même colonne (a à e, ou f à j) sont reliés entre eux.
On the diagrams, red wires go to 5V and black ones to GND (through the + and − rails of the breadboard).
On a breadboard, the 5 holes of a column (a to e, or f to j) are joined together.

| Projet / Project | Composants / Parts |
|---|---|
| [Clignotant / Blink](#clignotant--blink) | aucun / none |
| [Potentiomètre et servomoteur](#potentiomètre-et-servomoteur--potentiometer-and-servo) | potentiomètre, servo SG90 |
| [Alarme de distance](#alarme-de-distance--distance-alarm) | HC-SR04, LED RGB, 3 × 220 Ω, buzzer |
| [Thermomètre](#thermomètre--thermometer) | DHT11, écran LCD 16×2 I2C |
| [Jeu de réaction](#jeu-de-réaction--reaction-game) | matrice MAX7219, OLED SSD1306, bouton, buzzer |
| [Jauge lumineuse](#jauge-lumineuse--light-gauge) | potentiomètre, ruban NeoPixel ×8 |

`all-blocks` contient tous les blocs pour tester l'éditeur : ce n'est pas un montage à réaliser.
`all-blocks` uses every block to test the editor: it is not meant to be wired.

---

## Clignotant / Blink

![Clignotant](projects/blink/wiring.png)

La LED « L » soudée sur la carte clignote : rien à brancher. / The on-board "L" LED blinks: nothing to wire.

## Potentiomètre et servomoteur / Potentiometer and servo

![Potentiomètre et servomoteur](projects/servo-potentiometer/wiring.png)

Tourner le potentiomètre fait tourner le bras du servomoteur de 0 à 180°.
Turning the knob moves the servo arm from 0 to 180°.

| Composant / Part | Broche / Pin |
|---|---|
| Potentiomètre 10 kΩ : pattes extérieures / outer legs | GND et 5V |
| Potentiomètre : patte du milieu / middle leg | A0 |
| Servo SG90 : marron / brown | GND |
| Servo : rouge / red | 5V |
| Servo : orange | 9 |

## Alarme de distance / Distance alarm

![Alarme de distance](projects/distance-alarm/wiring.png)

La LED est verte quand rien n'est proche ; à moins de 20 cm elle devient rouge et le buzzer bipe de plus
en plus vite. / The LED is green when nothing is near; under 20 cm it turns red and the buzzer beeps
faster and faster.

| Composant / Part | Broche / Pin |
|---|---|
| HC-SR04 : VCC / Gnd | 5V / GND |
| HC-SR04 : Trig / Echo | 12 / 11 |
| LED RGB (cathode commune / common cathode) : R, G, B, chacune par une résistance de 220 Ω / each through a 220 Ω resistor | 9, 10, 6 |
| LED RGB : patte la plus longue / longest leg | GND |
| Buzzer : + / − | 8 / GND |

## Thermomètre / Thermometer

![Thermomètre](projects/thermometer/wiring.png)

L'écran affiche la température puis l'humidité, toutes les 2 secondes.
The screen shows the temperature, then the humidity, every 2 seconds.

| Composant / Part | Broche / Pin |
|---|---|
| DHT11 : VCC / GND | 5V / GND |
| DHT11 : DATA (out) | 3 |
| LCD 16×2 I2C : GND / VCC | GND / 5V |
| LCD : SDA / SCL | A4 / A5 |

Si l'écran reste vide, tourne la petite vis bleue derrière le module I2C (contraste).
If the screen stays blank, turn the small blue screw behind the I2C module (contrast).

## Jeu de réaction / Reaction game

![Jeu de réaction](projects/reaction-game/wiring.png)

Attends le cœur sur la matrice, puis appuie le plus vite possible : trop tôt, c'est perdu ! Le score
s'affiche sur l'écran OLED. / Wait for the heart on the matrix, then press as fast as you can: too early
and you lose! The score is shown on the OLED screen.

| Composant / Part | Broche / Pin |
|---|---|
| Matrice MAX7219 : VCC / GND | 5V / GND |
| Matrice : DIN / CS / CLK | 12 / 10 / 11 |
| OLED SSD1306 : GND / VCC | GND / 5V |
| OLED : SCL / SDA | A5 / A4 |
| Bouton / Button | 2 et GND |
| Buzzer : + / − | 8 / GND |

## Jauge lumineuse / Light gauge

![Jauge lumineuse](projects/light-gauge/wiring.png)

Plus on tourne le potentiomètre, plus il y a de LED allumées sur le ruban.
The more the knob is turned, the more LEDs light up on the strip.

| Composant / Part | Broche / Pin |
|---|---|
| Potentiomètre 10 kΩ : pattes extérieures / outer legs | GND et 5V |
| Potentiomètre : patte du milieu / middle leg | A0 |
| Ruban NeoPixel : 5V / GND | 5V / GND |
| Ruban NeoPixel : DIN | 5 |

---

Les schémas sont dessinés par [`tools/wiring/wiring.py`](../tools/wiring/wiring.py) (Python + Pillow) :
quand un exemple change de broches, mets à jour sa fonction dans ce script puis lance
`python3 tools/wiring/wiring.py <exemple>`.
The diagrams are drawn by [`tools/wiring/wiring.py`](../tools/wiring/wiring.py) (Python + Pillow): when an
example changes its pins, update its function in that script and run `python3 tools/wiring/wiring.py <example>`.
