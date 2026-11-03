// Blink: the built-in LED turns on for one second, then off for one second, forever.
// Used by `s2a-cli compile` in CI to check the toolchain on every board.

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(1000);
    digitalWrite(LED_BUILTIN, LOW);
    delay(1000);
}
