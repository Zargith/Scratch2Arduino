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
//   H <pin> <11|22> DHT11 / DHT22 reading, in tenths of °C and of %
//                   (-9999 when the sensor does not answer)              -> "h <pin> <temp> <humidity>"
//   L <row> <text>  text on line <row> (1-2) of an I2C 16x2 LCD (address 0x27 or 0x3F);
//                   row 0 clears the screen                              -> "ok"
//   C <rs> <e> <d4> <d5> <d6> <d7> <row> <text>  text on line <row> (1-2) of a 16x2 LCD with
//                   16 pins (no I2C module); row 0 clears it              -> "ok"
//   O <row> <text>  text on line <row> (1-8) of an I2C SSD1306 OLED; row 0 clears it -> "ok"
//   N <pin> <count> <index> <r> <g> <b>  colour of LED <index> (1-<count>, 0: all) of a
//                   NeoPixel strip                                       -> "ok"
//   G <din> <clk> <cs> <row> <bits>  row <row> (1-8) of a MAX7219 8x8 LED matrix -> "ok"
//   X               every output LOW, no sound, servos released, strip, matrix and
//                   screens cleared                                      -> "ok"
//
// Pins are Arduino pin numbers (A0 is 14 on an Uno). An unknown or malformed command answers
// "! <command>" so that the computer never waits forever.

#include <Adafruit_NeoPixel.h>
#include <DHT.h>
#include <LiquidCrystal.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <U8x8lib.h>
#include <Wire.h>

const long kBaud = 115200;
const char* const kHello = "S2A 4";
const int kMaxLine = 48;
const int kMaxPins = 70;
const int kMaxServos = 8;

char line[kMaxLine + 1];
int length = 0;
bool output[kMaxPins];
Servo servos[kMaxServos];
int servoPins[kMaxServos];
int tonePin = -1;

DHT* dht = nullptr;
int dhtPin = -1;
int dhtType = 0;

LiquidCrystal_I2C* lcd = nullptr;
LiquidCrystal* lcd16 = nullptr;
long lcd16Pins[6] = {-1, -1, -1, -1, -1, -1}; // RS, E, D4-D7
U8X8_SSD1306_128X64_NONAME_HW_I2C* oled = nullptr;

Adafruit_NeoPixel strip(0, 6, NEO_GRB + NEO_KHZ800);
int stripPin = -1;

int matrixPins[3] = {-1, -1, -1}; // DIN, CLK, CS

bool i2cDevice(byte address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void readDht(int pin, int type) {
  if (dht == nullptr || pin != dhtPin || type != dhtType) {
    delete dht;
    dht = new DHT(pin, type == 22 ? DHT22 : DHT11);
    dht->begin();
    dhtPin = pin;
    dhtType = type;
    output[pin] = false;
    delay(type == 22 ? 2000 : 1000); // the first reading needs the sensor to wake up
  }
  const float temperature = dht->readTemperature();
  const float humidity = dht->readHumidity();
  Serial.print("h ");
  Serial.print(pin);
  Serial.print(' ');
  Serial.print(isnan(temperature) ? -9999L : (long)lround(temperature * 10));
  Serial.print(' ');
  Serial.println(isnan(humidity) ? -9999L : (long)lround(humidity * 10));
}

void showOnLcd(int row, const char* text) {
  if (lcd == nullptr) {
    Wire.begin();
    lcd = new LiquidCrystal_I2C(i2cDevice(0x3F) && !i2cDevice(0x27) ? 0x3F : 0x27, 16, 2);
    lcd->init();
    lcd->backlight();
  }
  if (row <= 0) {
    lcd->clear();
    return;
  }
  lcd->setCursor(0, row > 2 ? 1 : row - 1);
  int i = 0;
  for (; i < 16 && text[i] != '\0'; i++) {
    lcd->print(text[i]);
  }
  for (; i < 16; i++) {
    lcd->print(' ');
  }
}

void showOnLcd16(const long* pins, int row, const char* text) {
  bool same = lcd16 != nullptr;
  for (int i = 0; i < 6; i++) {
    same = same && lcd16Pins[i] == pins[i];
  }
  if (!same) {
    delete lcd16;
    lcd16 = new LiquidCrystal(pins[0], pins[1], pins[2], pins[3], pins[4], pins[5]);
    for (int i = 0; i < 6; i++) {
      lcd16Pins[i] = pins[i];
      output[pins[i]] = false; // the screen owns its pins
    }
    lcd16->begin(16, 2);
  }
  if (row <= 0) {
    lcd16->clear();
    return;
  }
  lcd16->setCursor(0, row > 2 ? 1 : row - 1);
  int i = 0;
  for (; i < 16 && text[i] != '\0'; i++) {
    lcd16->print(text[i]);
  }
  for (; i < 16; i++) {
    lcd16->print(' ');
  }
}

void showOnOled(int row, const char* text) {
  if (oled == nullptr) {
    oled = new U8X8_SSD1306_128X64_NONAME_HW_I2C(U8X8_PIN_NONE);
    oled->begin();
    oled->setFont(u8x8_font_chroma48medium8_r);
    oled->clear();
  }
  if (row <= 0) {
    oled->clear();
    return;
  }
  char padded[17];
  int i = 0;
  for (; i < 16 && text[i] != '\0'; i++) {
    padded[i] = text[i];
  }
  for (; i < 16; i++) {
    padded[i] = ' ';
  }
  padded[16] = '\0';
  oled->drawString(0, row > 8 ? 7 : row - 1, padded);
}

void setPixels(int pin, int count, int index, int red, int green, int blue) {
  if (pin != stripPin || count != (int)strip.numPixels()) {
    strip.clear();
    strip.show();
    strip.updateLength(count);
    strip.setPin(pin);
    strip.begin();
    strip.setBrightness(64); // full brightness dazzles and draws too much current
    stripPin = pin;
    output[pin] = false;
  }
  const uint32_t colour = strip.Color(red, green, blue);
  if (index <= 0) {
    strip.fill(colour);
  } else if (index <= count) {
    strip.setPixelColor(index - 1, colour);
  }
  strip.show();
}

void matrixSend(byte reg, byte data) {
  digitalWrite(matrixPins[2], LOW);
  shiftOut(matrixPins[0], matrixPins[1], MSBFIRST, reg);
  shiftOut(matrixPins[0], matrixPins[1], MSBFIRST, data);
  digitalWrite(matrixPins[2], HIGH);
}

void matrixRow(int din, int clk, int cs, int row, int bits) {
  if (din != matrixPins[0] || clk != matrixPins[1] || cs != matrixPins[2]) {
    matrixPins[0] = din;
    matrixPins[1] = clk;
    matrixPins[2] = cs;
    for (int i = 0; i < 3; i++) {
      pinMode(matrixPins[i], OUTPUT);
      output[matrixPins[i]] = false;
    }
    digitalWrite(cs, HIGH);
    matrixSend(0x0F, 0); // no test mode
    matrixSend(0x09, 0); // no digit decoding
    matrixSend(0x0A, 4); // intensity
    matrixSend(0x0B, 7); // all 8 rows
    matrixSend(0x0C, 1); // on
    for (int i = 1; i <= 8; i++) {
      matrixSend(i, 0);
    }
  }
  matrixSend(row, bits);
}

// Text after the first number of a command ("L 1 Hello" -> "Hello").
const char* textAfter(const char* command, int position) {
  if (command[position] == ' ') {
    position++;
  }
  return command + position;
}

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
    if (stripPin >= 0) {
      strip.clear();
      strip.show();
    }
    if (matrixPins[0] >= 0) {
      for (int i = 1; i <= 8; i++) {
        matrixSend(i, 0);
      }
    }
    if (lcd != nullptr) {
      lcd->clear();
    }
    if (lcd16 != nullptr) {
      lcd16->clear();
    }
    if (oled != nullptr) {
      oled->clear();
    }
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
  case 'H':
    if (readNumber(command, &position, &pin) && validPin(pin) && readNumber(command, &position, &value)) {
      readDht(pin, value);
      return;
    }
    break;
  case 'L':
  case 'O':
    if (readNumber(command, &position, &value)) {
      if (command[0] == 'L') {
        showOnLcd(value, textAfter(command, position));
      } else {
        showOnOled(value, textAfter(command, position));
      }
      reply("ok");
      return;
    }
    break;
  case 'C': {
    long wiring[6];
    bool ok = true;
    for (int i = 0; ok && i < 6; i++) {
      ok = readNumber(command, &position, &wiring[i]) && validPin(wiring[i]);
    }
    if (ok && readNumber(command, &position, &value)) {
      showOnLcd16(wiring, value, textAfter(command, position));
      reply("ok");
      return;
    }
    break;
  }
  case 'N': {
    long numbers[5];
    bool ok = readNumber(command, &position, &pin) && validPin(pin);
    for (int i = 0; ok && i < 5; i++) {
      ok = readNumber(command, &position, &numbers[i]);
    }
    if (ok && numbers[0] >= 1 && numbers[0] <= 300) {
      setPixels(pin, numbers[0], numbers[1], constrain(numbers[2], 0, 255), constrain(numbers[3], 0, 255),
                constrain(numbers[4], 0, 255));
      reply("ok");
      return;
    }
    break;
  }
  case 'G': {
    long numbers[4];
    bool ok = readNumber(command, &position, &pin) && validPin(pin);
    for (int i = 0; ok && i < 4; i++) {
      ok = readNumber(command, &position, &numbers[i]);
    }
    if (ok && validPin(numbers[0]) && validPin(numbers[1]) && numbers[2] >= 1 && numbers[2] <= 8) {
      matrixRow(pin, numbers[0], numbers[1], numbers[2], numbers[3] & 0xFF);
      reply("ok");
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
