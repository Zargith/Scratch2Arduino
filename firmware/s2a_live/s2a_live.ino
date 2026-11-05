// S2A live - firmware for the Live mode of Scratch2Arduino.
//
// The computer runs the blocks and drives the pins of the board through this text protocol
// (115200 baud, one command per line, ended by '\n'):
//
//   ?               -> "S2A <protocol version>"     (also sent once at start-up)
//   M <pin> <mode>  pin mode: I input, U input with pull-up, O output   -> "ok"
//   D <pin> <0|1>   digital write                                        -> "ok"
//   P <pin> <0-255> PWM write (analogWrite)                              -> "ok"
//   R <pin>         digital read                                         -> "r <pin> <0|1>"
//   A <pin>         analog read                                          -> "a <pin> <0-1023>"
//   T <pin> <hz> <ms> buzzer sound for <ms> milliseconds (0: until stopped),
//                   a frequency of 0 stops it                            -> "ok"
//   S <pin> <0-180> servo angle (the pin is attached to a servo)         -> "ok"
//   U <trig> <echo> HC-SR04 ultrasonic distance in millimetres
//                   (4000 when nothing answers)                          -> "u <trig> <mm>"
//   X               every output LOW, no sound, servos released          -> "ok"
//
// Pins are Arduino pin numbers (A0 is 14 on an Uno). An unknown or malformed command answers
// "! <command>" so that the computer never waits forever.

#include <Servo.h>

const long kBaud = 115200;
const char* const kHello = "S2A 2";
const int kMaxLine = 32;
const int kMaxPins = 70;
const int kMaxServos = 8;

char line[kMaxLine + 1];
int length = 0;
bool output[kMaxPins];
Servo servos[kMaxServos];
int servoPins[kMaxServos];
int tonePin = -1;

// Gives the pin back to digitalWrite/analogWrite when a servo was using it.
void releaseServo(int pin) {
  for (int i = 0; i < kMaxServos; i++) {
    if (servoPins[i] == pin) {
      servos[i].detach();
      servoPins[i] = -1;
    }
  }
}

bool writeServo(int pin, int angle) {
  int free = -1;
  for (int i = 0; i < kMaxServos; i++) {
    if (servoPins[i] == pin) {
      servos[i].write(angle);
      return true;
    }
    if (servoPins[i] < 0 && free < 0) {
      free = i;
    }
  }
  if (free < 0) {
    return false;
  }
  servoPins[free] = pin;
  output[pin] = false;
  servos[free].attach(pin);
  servos[free].write(angle);
  return true;
}

void stopTone() {
  if (tonePin >= 0) {
    noTone(tonePin);
    tonePin = -1;
  }
}

long distanceMillimetres(int trig, int echo) {
  pinMode(trig, OUTPUT);
  pinMode(echo, INPUT);
  output[trig] = true;
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  const unsigned long duration = pulseIn(echo, HIGH, 25000UL);
  if (duration == 0) {
    return 4000;
  }
  return min((long)(duration * 10UL / 58UL), 4000L);
}

void reply(const char* text) {
  Serial.println(text);
}

void reply(char kind, int pin, long value) {
  Serial.print(kind);
  Serial.print(' ');
  Serial.print(pin);
  Serial.print(' ');
  Serial.println(value);
}

// Reads the next integer of `text` starting at `*position`; false when there is none.
bool readNumber(const char* text, int* position, long* value) {
  while (text[*position] == ' ') {
    (*position)++;
  }
  bool negative = false;
  if (text[*position] == '-') {
    negative = true;
    (*position)++;
  }
  if (text[*position] < '0' || text[*position] > '9') {
    return false;
  }
  long result = 0;
  while (text[*position] >= '0' && text[*position] <= '9') {
    result = result * 10 + (text[*position] - '0');
    (*position)++;
  }
  *value = negative ? -result : result;
  return true;
}

bool validPin(long pin) {
  return pin >= 0 && pin < kMaxPins && pin < NUM_DIGITAL_PINS;
}

void execute(const char* command) {
  int position = 1;
  long pin = 0;
  long value = 0;
  switch (command[0]) {
  case '?':
    reply(kHello);
    return;
  case 'X':
    stopTone();
    for (int i = 0; i < kMaxServos; i++) {
      if (servoPins[i] >= 0) {
        releaseServo(servoPins[i]);
      }
    }
    for (int i = 0; i < kMaxPins && i < NUM_DIGITAL_PINS; i++) {
      if (output[i]) {
        digitalWrite(i, LOW);
      }
    }
    reply("ok");
    return;
  case 'M':
    if (readNumber(command, &position, &pin) && validPin(pin)) {
      while (command[position] == ' ') {
        position++;
      }
      const char mode = command[position];
      if (mode == 'O' || mode == 'I' || mode == 'U') {
        releaseServo(pin);
        if (pin == tonePin) {
          stopTone();
        }
        pinMode(pin, mode == 'O' ? OUTPUT : (mode == 'U' ? INPUT_PULLUP : INPUT));
        output[pin] = mode == 'O';
        reply("ok");
        return;
      }
    }
    break;
  case 'D':
    if (readNumber(command, &position, &pin) && validPin(pin) && readNumber(command, &position, &value)) {
      digitalWrite(pin, value != 0 ? HIGH : LOW);
      reply("ok");
      return;
    }
    break;
  case 'P':
    if (readNumber(command, &position, &pin) && validPin(pin) && readNumber(command, &position, &value)) {
      analogWrite(pin, constrain(value, 0, 255));
      reply("ok");
      return;
    }
    break;
  case 'T': {
    long duration = 0;
    if (readNumber(command, &position, &pin) && validPin(pin) && readNumber(command, &position, &value) &&
        readNumber(command, &position, &duration)) {
      if (tonePin >= 0 && tonePin != pin) {
        stopTone(); // one sound at a time
      }
      if (value <= 0) {
        noTone(pin);
        tonePin = -1;
      } else {
        releaseServo(pin);
        output[pin] = true;
        tonePin = pin;
        if (duration > 0) {
          tone(pin, (unsigned int)constrain(value, 31L, 65535L), (unsigned long)duration);
        } else {
          tone(pin, (unsigned int)constrain(value, 31L, 65535L));
        }
      }
      reply("ok");
      return;
    }
    break;
  }
  case 'S':
    if (readNumber(command, &position, &pin) && validPin(pin) && readNumber(command, &position, &value) &&
        writeServo(pin, constrain(value, 0, 180))) {
      reply("ok");
      return;
    }
    break;
  case 'U': {
    long echo = 0;
    if (readNumber(command, &position, &pin) && validPin(pin) && readNumber(command, &position, &echo) &&
        validPin(echo)) {
      reply('u', pin, distanceMillimetres(pin, echo));
      return;
    }
    break;
  }
  case 'R':
    if (readNumber(command, &position, &pin) && validPin(pin)) {
      reply('r', pin, digitalRead(pin) == HIGH ? 1 : 0);
      return;
    }
    break;
  case 'A':
    if (readNumber(command, &position, &pin) && validPin(pin)) {
      reply('a', pin, analogRead(pin));
      return;
    }
    break;
  }
  Serial.print("! ");
  Serial.println(command);
}

void setup() {
  Serial.begin(kBaud);
  for (int i = 0; i < kMaxPins; i++) {
    output[i] = false;
  }
  for (int i = 0; i < kMaxServos; i++) {
    servoPins[i] = -1;
  }
  reply(kHello);
}

void loop() {
  while (Serial.available() > 0) {
    const char c = Serial.read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      line[length] = '\0';
      if (length > 0) {
        execute(line);
      }
      length = 0;
    } else if (length < kMaxLine) {
      line[length++] = c;
    }
  }
}
