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
// ESP32 and ESP8266 only (Wi-Fi):
//   W <name>\t<password>  joins this Wi-Fi network, now and at every start (saved on the board);
//                   an empty name forgets it                             -> "ok"
//   I               network state -> "i <access point> <its address> <address on the joined
//                   network, or ->"
//   B               last message of the phone connected over Bluetooth, once -> "b <text>", or
//                   "b" when it sent nothing new
//   Y <text>        sends a line to that phone                           -> "ok"
// Arduino boards (Uno, Nano, Mega), with an HC-05 / HC-06 Bluetooth module:
//   J <rx> <tx> <baud>  the module is wired to these pins (<rx>: the pin that receives, wired to TXD of
//                   the module; <tx>: the pin that sends, wired to RXD), at this speed; saved on the board
//                   and used at every start. "J" alone forgets it                    -> "ok"
//   K               module settings -> "k <rx> <tx> <baud>", or "k -" when there is none
// The module takes the same commands as USB; it is a plain serial link, so the board cannot know when
// the computer goes away. The Mega uses its serial ports for 19/18, 17/16 and 15/14, other pins and the
// other boards use SoftwareSerial.
// The ESP32 and the ESP8266 also create their own Wi-Fi network "S2A-xxxx" (password
// "scratch2arduino") and accept the same commands on TCP port 3333 (one computer at a time; it
// answers "S2A <version>" when it connects). ESP32 only: B and Y, and a Bluetooth serial device
// (SPP) named "S2A-xxxx", taking the same commands. A Bluetooth device that starts with "?" is a computer; any other one is a phone (a
// Bluetooth terminal app): its lines are kept for the "B" command. When the computer of a Wi-Fi
// or Bluetooth connection goes away, every output is switched off.
//
// Pins are Arduino pin numbers (A0 is 14 on an Uno). An unknown or malformed command answers
// "! <command>" so that the computer never waits forever. Each answer goes back to where the
// command came from (USB, Wi-Fi or Bluetooth).

#include <Adafruit_NeoPixel.h>
#include <DHT.h>
#include <LiquidCrystal.h>
#include <LiquidCrystal_I2C.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <ESP32Servo.h> // same Servo class, for the ESP32
#else
#include <Servo.h>
#endif
#include <U8x8lib.h>
#include <Wire.h>
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
#define S2A_WIFI 1
#endif
#if defined(ARDUINO_ARCH_ESP32)
#define S2A_BLUETOOTH 1
#include <BluetoothSerial.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#elif defined(ARDUINO_ARCH_ESP8266)
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#elif defined(ARDUINO_ARCH_AVR)
#define S2A_MODULE 1 // HC-05 / HC-06 Bluetooth module on two pins
#include <EEPROM.h>
#include <SoftwareSerial.h>
#endif

const long kBaud = 115200;
const char* const kHello = "S2A 8";
#if defined(S2A_WIFI)
const int kMaxLine = 120; // room for a Wi-Fi name and password
#else
const int kMaxLine = 48;
#endif
const int kMaxPins = 70;
const int kMaxServos = 8;

// A line being received from the computer, over USB, Wi-Fi or Bluetooth.
struct Input {
  Stream* stream;
  char text[kMaxLine + 1];
  int length;
};

Input usb = {&Serial, {0}, 0};
Print* out = &Serial; // where the answer to the command being executed goes

#if defined(S2A_WIFI)
const char* const kAccessPointPassword = "scratch2arduino";
const uint16_t kTcpPort = 3333;
WiFiServer server(kTcpPort);
WiFiClient client;
Input network = {&client, {0}, 0};
bool clientConnected = false;
char accessPoint[16] = "S2A"; // also the Bluetooth name
#endif
#if defined(S2A_MODULE)
// The module's serial link: a serial port of the Mega, or SoftwareSerial (created once its pins are known).
Stream* modulePort = nullptr;
Input module = {nullptr, {0}, 0};
long moduleRx = -1;
long moduleTx = -1;
long moduleBaud = 0;
#endif
#if defined(S2A_BLUETOOTH)
BluetoothSerial bluetoothPort;
Input bluetooth = {&bluetoothPort, {0}, 0};
bool bluetoothConnected = false;
bool bluetoothComputer = false; // false: a phone (or nothing yet)
char phoneMessage[kMaxLine + 1] = "";
#endif
bool output[kMaxPins];
Servo servos[kMaxServos];
int servoPins[kMaxServos];
int tonePin = -1;
unsigned long toneEnd = 0; // when a timed sound ends (0: until stopped)

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
  out->print("h ");
  out->print(pin);
  out->print(' ');
  out->print(isnan(temperature) ? -9999L : (long)lround(temperature * 10));
  out->print(' ');
  out->println(isnan(humidity) ? -9999L : (long)lround(humidity * 10));
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

void matrixSettings() {
  matrixSend(0x0F, 0); // no test mode
  matrixSend(0x09, 0); // no digit decoding
  matrixSend(0x0A, 4); // intensity
  matrixSend(0x0B, 7); // all 8 rows
  matrixSend(0x0C, 1); // on
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
    matrixSettings();
    for (int i = 1; i <= 8; i++) {
      matrixSend(i, 0);
    }
  } else if (row == 1) {
    matrixSettings(); // a voltage dip (buzzer, motor) can reset the chip: set it again with each picture
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

// A silent buzzer is let go: otherwise the flicker of LEDs on the same GND hums through it.
void stopTone() {
  if (tonePin >= 0) {
    noTone(tonePin);
    pinMode(tonePin, INPUT);
    output[tonePin] = false;
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
  out->println(text);
}

void reply(char kind, int pin, long value) {
  out->print(kind);
  out->print(' ');
  out->print(pin);
  out->print(' ');
  out->println(value);
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
#if defined(ARDUINO_ARCH_ESP8266)
  if (pin == A0) {
    return true; // the only analog input, after the digital pins
  }
#endif
#if defined(S2A_MODULE)
  if (pin >= 0 && (pin == moduleRx || pin == moduleTx)) {
    return false; // the Bluetooth module's link: a block must not take it over
  }
#endif
  return pin >= 0 && pin < kMaxPins && pin < NUM_DIGITAL_PINS;
}

#if defined(S2A_WIFI)
// The home network, kept on the board: Preferences (ESP32) or EEPROM (ESP8266).
void saveNetwork(const String& name, const String& password) {
#if defined(ARDUINO_ARCH_ESP32)
  Preferences settings;
  settings.begin("s2a", false);
  settings.putString("ssid", name);
  settings.putString("password", password);
  settings.end();
#else
  EEPROM.begin(128);
  EEPROM.write(0, 'S'); // written by this firmware
  for (int i = 0; i < 32; i++) {
    EEPROM.write(1 + i, i < (int)name.length() ? name[i] : 0);
  }
  for (int i = 0; i < 64; i++) {
    EEPROM.write(33 + i, i < (int)password.length() ? password[i] : 0);
  }
  EEPROM.end();
#endif
}

void loadNetwork(String& name, String& password) {
#if defined(ARDUINO_ARCH_ESP32)
  Preferences settings;
  settings.begin("s2a", true);
  name = settings.getString("ssid", "");
  password = settings.getString("password", "");
  settings.end();
#else
  name = "";
  password = "";
  EEPROM.begin(128);
  if (EEPROM.read(0) == 'S') {
    for (int i = 0; i < 32 && EEPROM.read(1 + i) != 0; i++) {
      name += (char)EEPROM.read(1 + i);
    }
    for (int i = 0; i < 64 && EEPROM.read(33 + i) != 0; i++) {
      password += (char)EEPROM.read(33 + i);
    }
  }
  EEPROM.end();
#endif
}

// "name<TAB>password": saved on the board, joined now and at every start.
void joinNetwork(const char* text) {
  String name = text;
  String password;
  const int tab = name.indexOf('\t');
  if (tab >= 0) {
    password = name.substring(tab + 1);
    name = name.substring(0, tab);
  }
  saveNetwork(name, password);
  WiFi.disconnect();
  if (name.length() > 0) {
    WiFi.begin(name.c_str(), password.c_str());
  }
}

void networkState() {
  out->print("i ");
  out->print(accessPoint);
  out->print(' ');
  out->print(WiFi.softAPIP());
  out->print(' ');
  if (WiFi.status() == WL_CONNECTED) {
    out->println(WiFi.localIP());
  } else {
    out->println('-');
  }
}

void startWifi() {
  WiFi.mode(WIFI_AP_STA);
  uint8_t mac[6];
  WiFi.softAPmacAddress(mac);
  snprintf(accessPoint, sizeof(accessPoint), "S2A-%02X%02X", mac[4], mac[5]);
  WiFi.softAP(accessPoint, kAccessPointPassword);
  String name;
  String password;
  loadNetwork(name, password);
  if (name.length() > 0) {
    WiFi.begin(name.c_str(), password.c_str());
  }
  String host = accessPoint;
  host.toLowerCase(); // s2a-xxxx.local
  if (MDNS.begin(host.c_str())) {
    MDNS.addService("s2a", "tcp", kTcpPort);
  }
  server.begin();
  server.setNoDelay(true);
}

// One computer at a time: a new one replaces the previous one.
void serveNetwork() {
  if (server.hasClient()) {
    if (client) {
      client.stop();
    }
#if defined(ARDUINO_ARCH_ESP8266)
    client = server.available(); // the new connection
#else
    client = server.accept();
#endif
    client.setNoDelay(true);
    network.length = 0;
    clientConnected = true;
    client.println(kHello);
  }
  if (clientConnected && !client.connected()) {
    clientConnected = false;
    resetAll(); // the computer went away: nothing keeps running on its own
  }
}
#endif

#if defined(S2A_BLUETOOTH)
// Bluetooth serial device with the name of the Wi-Fi network (Wi-Fi must start first).
// Pairing without a code ("just works"): nothing to type on the computer or the phone.
void startBluetooth() {
  bluetoothPort.enableSSP(false, false);
  bluetoothPort.begin(accessPoint);
}

// No greeting: a phone would show it. A computer asks "?" first.
void serveBluetooth() {
  const bool connected = bluetoothPort.hasClient();
  if (connected && !bluetoothConnected) {
    bluetooth.length = 0;
    bluetoothComputer = false;
  } else if (!connected && bluetoothConnected) {
    if (bluetoothComputer) {
      resetAll(); // the computer went away: nothing keeps running on its own
    }
    bluetoothComputer = false;
  }
  bluetoothConnected = connected;
}
#endif

#if defined(S2A_MODULE)
const int kModuleSaved = 0x4A; // first EEPROM byte when the settings were written by this firmware

bool moduleBaudOk(long baud) {
  return baud == 9600 || baud == 19200 || baud == 38400 || baud == 57600 || baud == 115200;
}

// Pins 0 and 1 are the USB link.
bool modulePinsOk(long rx, long tx, long baud) {
  return rx > 1 && tx > 1 && rx != tx && rx < NUM_DIGITAL_PINS && tx < NUM_DIGITAL_PINS && rx < kMaxPins &&
         tx < kMaxPins && moduleBaudOk(baud);
}

// Starts the link with the module; false when these pins cannot be used.
bool startModule(long rx, long tx, long baud) {
  if (!modulePinsOk(rx, tx, baud) || modulePort != nullptr) {
    return false; // pins 0 and 1 are the USB link; a running link cannot move (restart the board)
  }
#if defined(HAVE_HWSERIAL1)
  // The Mega's own serial ports, when the module is on them.
  HardwareSerial* hardware = nullptr;
  if (rx == 19 && tx == 18) {
    hardware = &Serial1;
  }
#if defined(HAVE_HWSERIAL2)
  if (rx == 17 && tx == 16) {
    hardware = &Serial2;
  }
#endif
#if defined(HAVE_HWSERIAL3)
  if (rx == 15 && tx == 14) {
    hardware = &Serial3;
  }
#endif
  if (hardware != nullptr) {
    hardware->begin(baud);
    modulePort = hardware;
  }
#endif
  if (modulePort == nullptr) {
    if (digitalPinToPCICR((uint8_t)rx) == nullptr) {
      return false; // SoftwareSerial needs a pin with a "pin change" interrupt to receive
    }
    SoftwareSerial* software = new SoftwareSerial(rx, tx);
    software->begin(baud);
    software->listen();
    modulePort = software;
  }
  module.stream = modulePort;
  module.length = 0;
  output[rx] = false;
  output[tx] = false;
  moduleRx = rx;
  moduleTx = tx;
  moduleBaud = baud;
  return true;
}

// Settings kept in the EEPROM: marker, rx, tx, baud / 100 (two bytes).
void saveModule(long rx, long tx, long baud) {
  EEPROM.update(0, rx < 0 ? 0xFF : kModuleSaved);
  EEPROM.update(1, (byte)rx);
  EEPROM.update(2, (byte)tx);
  EEPROM.update(3, (byte)((baud / 100) >> 8));
  EEPROM.update(4, (byte)((baud / 100) & 0xFF));
}

void loadModule() {
  if (EEPROM.read(0) == kModuleSaved) {
    startModule(EEPROM.read(1), EEPROM.read(2), ((long)EEPROM.read(3) << 8 | EEPROM.read(4)) * 100L);
  }
}

// "J <rx> <tx> <baud>": saved and started; "J" alone forgets it (the link stops at the next start).
bool setupModule(const char* command, int position) {
  long rx = 0;
  long tx = 0;
  long baud = 0;
  if (!readNumber(command, &position, &rx)) {
    saveModule(-1, -1, 0);
    return true;
  }
  if (!readNumber(command, &position, &tx) || !readNumber(command, &position, &baud)) {
    return false;
  }
  if (rx == moduleRx && tx == moduleTx && baud == moduleBaud) {
    saveModule(rx, tx, baud);
    return true;
  }
  if (!modulePinsOk(rx, tx, baud)) {
    return false;
  }
  saveModule(rx, tx, baud);
  if (modulePort == nullptr) {
    startModule(rx, tx, baud);
  }
  return true; // other pins than the running ones: used from the next start
}
#endif

// Every output LOW, no sound, servos released, strip, matrix and screens cleared.
void resetAll() {
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
}

void execute(const char* command) {
  int position = 1;
  long pin = 0;
  long value = 0;
  switch (command[0]) {
  case '?':
    reply(kHello);
    return;
#if defined(S2A_WIFI)
  case 'W':
    joinNetwork(textAfter(command, position));
    reply("ok");
    return;
  case 'I':
    networkState();
    return;
#endif
#if defined(S2A_MODULE)
  case 'J':
    if (setupModule(command, position)) {
      reply("ok");
      return;
    }
    break;
  case 'K':
    if (moduleRx < 0) {
      reply("k -");
    } else {
      out->print("k ");
      out->print(moduleRx);
      out->print(' ');
      out->print(moduleTx);
      out->print(' ');
      out->println(moduleBaud);
    }
    return;
#endif
#if defined(S2A_BLUETOOTH)
  case 'B':
    out->print('b');
    if (phoneMessage[0] != '\0') {
      out->print(' ');
      out->print(phoneMessage);
      phoneMessage[0] = '\0';
    }
    out->println();
    return;
  case 'Y':
    if (bluetoothConnected && !bluetoothComputer) {
      bluetoothPort.println(textAfter(command, position));
    }
    reply("ok");
    return;
#endif
  case 'X':
    resetAll();
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
        if (tonePin == pin) {
          stopTone();
        } else {
          noTone(pin);
        }
      } else {
        releaseServo(pin);
        output[pin] = true;
        tonePin = pin;
        toneEnd = duration > 0 ? max(millis() + (unsigned long)duration, 1UL) : 0;
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
  out->print("! ");
  out->println(command);
}

void setup() {
  Serial.begin(kBaud);
#if defined(ARDUINO_ARCH_ESP32)
  analogReadResolution(10); // 0-1023, as on the Arduino boards
#endif
  for (int i = 0; i < kMaxPins; i++) {
    output[i] = false;
  }
  for (int i = 0; i < kMaxServos; i++) {
    servoPins[i] = -1;
  }
#if defined(S2A_WIFI)
  startWifi();
#endif
#if defined(S2A_BLUETOOTH)
  startBluetooth();
#endif
#if defined(S2A_MODULE)
  loadModule();
#endif
  reply(kHello);
}

// Executes the complete lines received on `input`; the answers go back to it.
void readFrom(Input& input) {
  while (input.stream->available() > 0) {
    const char c = input.stream->read();
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      input.text[input.length] = '\0';
#if defined(S2A_BLUETOOTH)
      if (&input == &bluetooth && !bluetoothComputer) {
        if (strcmp(input.text, "?") == 0) {
          bluetoothComputer = true; // the Live mode of a computer
        } else {
          if (input.length > 0) {
            strcpy(phoneMessage, input.text); // a phone: kept for the "B" command
          }
          input.length = 0;
          continue;
        }
      }
#endif
      if (input.length > 0) {
        out = input.stream;
        execute(input.text);
        out = &Serial;
      }
      input.length = 0;
    } else if (input.length < kMaxLine) {
      input.text[input.length++] = c;
    }
  }
}

void loop() {
  if (tonePin >= 0 && toneEnd != 0 && (long)(millis() - toneEnd) >= 0) {
    stopTone();
  }
  readFrom(usb);
#if defined(S2A_WIFI)
  serveNetwork();
  if (clientConnected) {
    readFrom(network);
  }
#endif
#if defined(ARDUINO_ARCH_ESP8266)
  MDNS.update();
#endif
#if defined(S2A_BLUETOOTH)
  serveBluetooth();
  if (bluetoothConnected) {
    readFrom(bluetooth);
  }
#endif
#if defined(S2A_MODULE)
  if (modulePort != nullptr) {
    readFrom(module);
  }
#endif
}
