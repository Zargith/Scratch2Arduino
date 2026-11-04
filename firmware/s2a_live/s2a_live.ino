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
//   X               every output LOW (Live mode stopped)                 -> "ok"
//
// Pins are Arduino pin numbers (A0 is 14 on an Uno). An unknown or malformed command answers
// "! <command>" so that the computer never waits forever.

const long kBaud = 115200;
const char* const kHello = "S2A 1";
const int kMaxLine = 32;
const int kMaxPins = 70;

char line[kMaxLine + 1];
int length = 0;
bool output[kMaxPins];

void reply(const char* text) {
  Serial.println(text);
}

void reply(char kind, int pin, int value) {
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
