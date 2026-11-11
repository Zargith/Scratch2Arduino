# Teacher and workshop kit

This kit helps you introduce programming and electronics with Scratch2Arduino, in class (ages 10–18),
in a club or in a workshop (makerspace, library). It follows the [8 tutorials](README.md#tutorials):
each session matches a tutorial, which students can follow on their own.

*Français : [../fr/teachers.md](../fr/teachers.md)*

## Audience and prerequisites

- **From age 10** (from 8 with an adult for the wiring). Having used Scratch helps but is not required.
- **No electronics knowledge**: each tutorial explains its wiring, and the *Wiring diagram* button draws
  it from the blocks.
- Students work **in pairs**: a *driver* on the mouse, a *navigator* who reads the tutorial and checks
  the wiring; they swap roles at every step.

## Getting the room ready

A week before:

1. **Install Scratch2Arduino** on every computer ([guide](guide.md#1-install-scratch2arduino)). On Linux,
   add every student account to the `dialout` group.
2. **Download the board tools while online**, so that the workshop then works offline: on each computer,
   with the account used during the workshop, run `s2a-cli setup` (installed next to the application; on
   Windows: `& "$env:ProgramFiles\Scratch2Arduino\bin\s2a-cli.exe" setup` in PowerShell). Or upload the
   *Blink* example once to each type of board used. The tools are stored in the user's folder: repeat it
   for each account.
3. **Test every kit**: plug the board in, `s2a-cli ports` (or the *Port* menu) must show it; open
   *New → Blink*, click *Upload*; the **L** LED blinks.
4. **Prepare numbered boxes** (one per pair) with the parts of the session, plus spare USB cables, LEDs
   and resistors.
5. Print the wiring diagrams of the examples (*Wiring diagram → Save the image*, or the pictures in
   [`examples/`](../../examples/README.md)).

## Safety rules to announce

- **Unplug the board** before changing a wire; have the circuit checked before plugging it back in.
- An LED always has **its resistor**.
- Never a wire straight between **5V and GND** (short circuit: the board heats up, the USB port shuts down).
- On an ESP32: sensors go to **3V3**, never 5 V on a pin.
- Robot batteries: never short them, take them out at the end of the session.

## Parts

Per pair, for tutorials 1 to 7 (a common "Arduino starter kit"):

| Part | Quantity | Tutorials |
|---|---|---|
| Arduino Uno (or Nano) + data USB cable | 1 | all |
| 830-point breadboard | 1 | 2 to 7 |
| Male-male / male-female wires | 20 / 10 | 2 to 7 |
| Red, orange, green LEDs | 2 of each | 2, 3 |
| 220 Ω resistors | 5 | 2, 3, 5 |
| Push button | 2 | 3, 7 |
| 10 kΩ potentiometer | 1 | 4 (and 6 in the 16-pin variant) |
| SG90 servo | 1 | 4 |
| HC-SR04 ultrasonic sensor | 1 | 5 |
| Common-cathode RGB LED | 1 | 5 |
| **Passive** buzzer | 1 | 5, 7 |
| DHT11 (or DHT22) sensor | 1 | 6 |
| 16×2 LCD screen with I2C module | 1 | 6 |
| 0.96" SSD1306 I2C OLED screen | 1 | 7 |
| MAX7219 8×8 LED matrix (module) | 1 | 7 |

For tutorial 8 (robot), per group: ESP32 DevKit + micro-USB cable, L298N H-bridge, 2 yellow gear motors
with wheels, a chassis with a caster (or cardboard), 4 × AA battery holder + batteries, USB power bank, an
**Android** phone with the "Serial Bluetooth Terminal" app.

## Formats

| Format | Time | Content |
|---|---|---|
| Discovery | 2 h | Tutorials 1 and 2 (up to functions if time allows), tutorial 1 challenges |
| One day | 6 h | Tutorials 1 to 5; in the afternoon each pair picks tutorial 6 or 7 and presents it |
| Course | 8 sessions of 1.5 to 2 h | One session per tutorial, challenges included; last session: free project or robot |

## Typical session (1 h 30)

| Time | Step | Facilitator's role |
|---|---|---|
| 10 min | Recap and goal | Show the expected result (a working circuit), ask a question: "how do you think the board knows that…?" |
| 10 min | Wiring | Pairs wire **with the board unplugged** from the diagram; the facilitator checks each circuit |
| 40 min | Program | Students follow the tutorial. Encourage **Live mode** to try things, then *Upload* |
| 20 min | Challenges | The tutorial's challenges, as they choose; the fastest help the others |
| 10 min | Wrap-up | Wrap-up questions (below), save the project (`.s2a`) |

## Sessions

### 1. Blink
- **Concepts**: program, instruction, sequence, endless loop, output, upload.
- **Misconception**: "without the second *wait*, the LED blinks" — let them see that it stays on.
- **Wrap-up**: what does the board do when plugged back in? Difference between *Live* and *Upload*?

### 2. Traffic light
- **Concepts**: LED (polarity), resistor, pin HIGH/LOW, sequence, function.
- **Misconception**: "an LED works both ways" — have them turn one round.
- **Wrap-up**: what is the resistor for? Why do functions make the program easier to read?

### 3. Button and LED
- **Concepts**: input, condition (true / false), if … then … else, polling loop, variable (switch).
- **Misconception**: "the program reacts to the button by itself" — remove *forever* to show that it
  reads the button only once.
- **Wrap-up**: what is the difference between "is pressed" and "was pressed"? Why wait for the release?

### 4. Potentiometer and servo
- **Concepts**: analog quantity, range of values, variable, proportionality (map).
- **Maths link**: the rule of three behind the *map* block.
- **Wrap-up**: which values does the potentiometer give? What does 25 become when mapped to 0 – 180?

### 5. Distance alarm
- **Concepts**: sensor, measurement, threshold, comparison, a value setting a duration.
- **Physics link**: speed of sound, echo (the sensor measures a round trip).
- **Wrap-up**: what happens when the threshold changes? How would you add a third zone?

### 6. Thermometer
- **Concepts**: digital sensor, I2C bus, display, text and numbers.
- **Science link**: temperature, humidity, readings (record values through the day).
- **Wrap-up**: why wait 2 seconds between readings? Why do accents disappear?

### 7. Reaction game
- **Concepts**: randomness, waiting for an event, score (an accumulating variable), complete project.
- **Misconception**: "random is truly unpredictable" — discuss pseudo-random numbers.
- **Wrap-up**: how do you prevent cheating? How would you measure the reaction time?

### 8. Remote robot
- **Concepts**: motor, H-bridge, separate power supply and common ground, wireless communication,
  organising with functions.
- **Watch out**: battery GND joined to the ESP32's GND; ENA/ENB jumpers removed.
- **Wrap-up**: why can the board not power a motor directly? What happens if the phone sends an unknown
  word?

## Assessment

- **Skills**: read a diagram and wire it; describe what a program does before running it; fix an error
  from the message shown; change a program to meet a challenge.
- **Evidence**: each pair saves its project (`.s2a`) and hands it in (USB stick, school platform); the
  file reopens identically on another computer.
- **Simple scale**: *done with help* / *done alone* / *improved (challenge met)* / *explained to another
  pair*.

## Facilitation tips

- The **Wiring diagram** button draws the circuit matching the student's blocks: when in doubt, compare
  the diagram with the breadboard.
- Loose blocks are **greyed out** and ignored: no need to delete everything to try something.
- Errors only appear on *Check*, *Upload* or *Live*, with an explanation and the block highlighted: let
  the student read the message before stepping in.
- **My blocks** lets you hand out a piece of program: the teacher exports it (`.s2ablocks`), students
  import it.
- The **serial monitor** and the **Live** panel make sensor values visible: ideal for "how much?"
  questions.
