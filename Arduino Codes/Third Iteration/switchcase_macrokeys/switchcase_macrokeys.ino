/*
  Macro Keyboard — ESP32 WROOM-32D (BLE keyboard) + LittleFS config
  ------------------------------------------------------------------
  - BLE keyboard via T-vK's ESP32 BLE Keyboard (NimBLE backend)
  - Persistent config in LittleFS: /config.json
  - Serial CLI @115200:
        GET            -> prints JSON
        PUT <len>\n    -> send exactly <len> raw JSON bytes
        RESET          -> restore factory JSON
  - Matrix, 2 encoders, mode button, LEDs, pot/brightness
  - Actions:
      * Chords: CTRL/ALT/SHIFT/GUI + keys (A..Z, F1..F12, arrows, page up/down, enter)
      * TEXT:...        (types literal text)
      * SEQ:[ "...", "..." ]  (sequence of actions, supports TEXT / chords / single keys)
      * Consumer media keys (volume, next/prev, play/pause, browser back/forward)
*/

#include <Arduino.h>
#include <Keypad.h>
#include <Encoder.h>
#include <ArduinoJson.h>

#include <LittleFS.h>
#define FSYS LittleFS

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

// ============================ PIN CONFIG (EDIT IF NEEDED) ============================
// Matrix: rows are outputs, cols are inputs with pull-ups
#define PIN_ROW0   14
#define PIN_ROW1   27
#define PIN_ROW2   26
#define PIN_ROW3   25

#define PIN_COL0   4
#define PIN_COL1   16
#define PIN_COL2   17
#define PIN_COL3   5

// Encoders
#define ENC_A_PIN1 18
#define ENC_A_PIN2 19
#define ENC_B_PIN1 23
#define ENC_B_PIN2 22

// Mode button, LEDs, Pot
#define PIN_MODE_BTN  21
#define PIN_LED_MODE1 15
#define PIN_LED_MODE2 2
#define PIN_LED_ARD1  12
#define PIN_LED_ARD2  13
#define PIN_POT       36    // ADC1_CH0

// ============================ BLE Keyboard (NimBLE backend) ============================
// Order matters: force NimBLE and include NimBLEDevice BEFORE BleKeyboard
#define USE_NIMBLE
#include <NimBLEDevice.h>
#include <BleKeyboard.h>
BleKeyboard BLEKeyboard("MacroPad ESP32", "Demential", 100);

// Some library versions don’t ship these browser keys — define if missing
#ifndef KEY_MEDIA_WWW_BACK
  #define KEY_MEDIA_WWW_BACK    0x224
#endif
#ifndef KEY_MEDIA_WWW_FORWARD
  #define KEY_MEDIA_WWW_FORWARD 0x225
#endif

// ============================ Keypad & Encoders ============================
const byte ROWS = 4, COLS = 4;
char keymap[ROWS][COLS] = {
  {'0','1','2','3'},
  {'4','5','6','7'},
  {'8','9','A','B'},
  {'C','D','E','F'},
};
byte rowPins[ROWS] = {PIN_ROW0, PIN_ROW1, PIN_ROW2, PIN_ROW3};
byte colPins[COLS] = {PIN_COL0, PIN_COL1, PIN_COL2, PIN_COL3};
Keypad keypad = Keypad(makeKeymap(keymap), rowPins, colPins, ROWS, COLS);

Encoder RotaryEncoderA(ENC_A_PIN1, ENC_A_PIN2);
Encoder RotaryEncoderB(ENC_B_PIN1, ENC_B_PIN2);
long positionEncoderA = -999;
long positionEncoderB = -999;

int modePushCounter = 0;
int buttonState = 0, lastButtonState = 0;

int potValue = 0, lum = 0;

// ============================ JSON Config ============================
static const char *factoryJson PROGMEM = R"json(
{"version":1,
 "keys":{
  "0":{"0":"CTRL+c","1":"CTRL+x","2":"CTRL+f","3":"F5","4":"CTRL+PAGE_UP","5":"CTRL+PAGE_DOWN","6":"CTRL+w","7":"CTRL+t","8":"CONSUMER:CONSUMER_BROWSER_BACK","9":"CONSUMER:CONSUMER_BROWSER_FORWARD","A":"CTRL+v","B":"CTRL+a","C":"TEXT:Alpha key12","D":"CTRL+s","E":"CONSUMER:MEDIA_VOLUME_MUTE","F":"CONSUMER:MEDIA_PLAY_PAUSE"},
  "1":{"0":"CTRL+c","1":"CTRL+V","A":"CTRL+ALT+SHIFT+v","B":"CTRL+ALT+SHIFT+ENTER","C":"SEQ:[\"CTRL+ALT+SHIFT+BACKSPACE\",\"RIGHT\",\"DOWN\",\"RIGHT\",\"DOWN\",\"ENTER\"]","E":"ENTER","F":"ENTER"},
  "2":{"9":"CTRL+ALT+i","A":"CTRL+ALT+SHIFT+w"},
  "3":{"0":"CTRL+SHIFT+F9","1":"TEXT:nice shot","2":"SEQ:[\"CTRL+k\",\"c\"]","3":"SEQ:[\"CTRL+k\",\"u\"]","4":"TEXT:thanks","5":"TEXT:i got it","6":"TEXT:take the shot","7":"TEXT:defending","8":"SEQ:[\"CTRL+k\",\"k\"]","9":"SEQ:[\"CTRL+k\",\"l\"]","A":"SEQ:[\"CTRL+k\",\"c\"]","B":"TEXT:my bad","C":"TEXT:nooooo!","D":"TEXT:close one","E":"TEXT:230998","F":"SEQ:[\"ENTER\",\"TEXT:XD\"]"}
 },
 "encoders":{
  "0":{"A+":"CONSUMER:MEDIA_VOLUME_DOWN","A-":"CONSUMER:MEDIA_VOLUME_UP","B+":"CONSUMER:MEDIA_PREVIOUS","B-":"CONSUMER:MEDIA_NEXT"},
  "1":{"A+":"LEFT","A-":"RIGHT","B+":"UP","B-":"DOWN"},
  "2":{"A+":"LEFT","A-":"RIGHT","B+":"LEFT","B-":"RIGHT"},
  "3":{"A+":"DOWN","A-":"UP","B+":"DOWN","B-":"UP"}
 }}
)json";

StaticJsonDocument<4096> cfg;

// ---------- FS helpers ----------
bool saveConfigText(const String &json) {
  File f = FSYS.open("/config.json", FILE_WRITE, true);
  if (!f) return false;
  bool ok = f.print(json) == (int)json.length();
  f.close();
  return ok;
}
bool loadConfigText(String &out) {
  File f = FSYS.open("/config.json", FILE_READ);
  if (!f) return false;
  out.reserve(f.size() + 1);
  while (f.available()) out += (char)f.read();
  f.close();
  return true;
}
bool parseConfig(const String &json) {
  cfg.clear();
  DeserializationError err = deserializeJson(cfg, json);
  return !err;
}
bool ensureConfigLoaded() {
  if (!FSYS.begin(true)) {
    // formatted if needed
  }
  String text;
  if (loadConfigText(text) && parseConfig(text)) return true;

  // write factory and parse
  String fac;
  for (const char* p = factoryJson; pgm_read_byte(p); ++p) fac += (char)pgm_read_byte(p);
  saveConfigText(fac);
  return parseConfig(fac);
}

// ============================ Serial Config CLI ============================
void configCLI() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n'); cmd.trim();

  if (cmd.startsWith("GET")) {
    String text;
    if (!loadConfigText(text)) { serializeJson(cfg, text); }
    Serial.println(text);
    return;
  }

  if (cmd.startsWith("PUT")) {
    int sp = cmd.indexOf(' ');
    if (sp < 0) { Serial.println(F("ERR len")); return; }
    int n = cmd.substring(sp+1).toInt();
    if (n <= 0 || n > 32768) { Serial.println(F("ERR badlen")); return; }

    String body; body.reserve(n+1);
    unsigned long to = millis()+10000;
    while ((int)body.length() < n && millis() < to) {
      if (Serial.available()) { body += (char)Serial.read(); to = millis()+10000; }
    }
    if ((int)body.length() != n) { Serial.println(F("ERR timeout")); return; }

    StaticJsonDocument<4096> tmp;
    if (deserializeJson(tmp, body)) { Serial.println(F("ERR json")); return; }

    if (!saveConfigText(body)) { Serial.println(F("ERR save")); return; }
    cfg.clear(); deserializeJson(cfg, body);
    Serial.println(F("SAVED"));
    return;
  }

  if (cmd.startsWith("RESET")) {
    String fac;
    for (const char* p = factoryJson; pgm_read_byte(p); ++p) fac += (char)pgm_read_byte(p);
    saveConfigText(fac);
    cfg.clear(); deserializeJson(cfg, fac);
    Serial.println(F("RESET OK"));
    return;
  }

  Serial.println(F("UNKNOWN"));
}

// ============================ HID helpers (BLE) ============================
uint8_t mapKeyToken(const String& t) {
  String s = t; s.toUpperCase();
  if (s.length()==1) { char c = s[0]; if (c>='A' && c<='Z') return (uint8_t)c; }
  if (s=="ENTER"||s=="RETURN") return KEY_RETURN;
  if (s=="LEFT") return KEY_LEFT_ARROW;
  if (s=="RIGHT") return KEY_RIGHT_ARROW;
  if (s=="UP") return KEY_UP_ARROW;
  if (s=="DOWN") return KEY_DOWN_ARROW;
  if (s=="PAGE_UP") return KEY_PAGE_UP;
  if (s=="PAGE_DOWN") return KEY_PAGE_DOWN;
  if (s=="BACKSPACE") return KEY_BACKSPACE;
  if (s=="F1") return KEY_F1; if (s=="F2") return KEY_F2; if (s=="F3") return KEY_F3; if (s=="F4") return KEY_F4;
  if (s=="F5") return KEY_F5; if (s=="F6") return KEY_F6; if (s=="F7") return KEY_F7; if (s=="F8") return KEY_F8;
  if (s=="F9") return KEY_F9; if (s=="F10") return KEY_F10; if (s=="F11") return KEY_F11; if (s=="F12") return KEY_F12;
  return 0;
}

void pressChord(const String& chord) {
  int start = 0;
  while (true){
    int plus = chord.indexOf('+', start);
    String part = (plus<0) ? chord.substring(start) : chord.substring(start, plus);
    String p = part; p.trim(); p.toUpperCase();
    if (p=="CTRL"||p=="CONTROL") BLEKeyboard.press(KEY_LEFT_CTRL);
    else if (p=="ALT")          BLEKeyboard.press(KEY_LEFT_ALT);
    else if (p=="SHIFT")        BLEKeyboard.press(KEY_LEFT_SHIFT);
    else if (p=="GUI"||p=="WIN"||p=="META") BLEKeyboard.press(KEY_LEFT_GUI);
    else { uint8_t k = mapKeyToken(part); if (k) BLEKeyboard.press(k); }
    if (plus<0) break; else start = plus+1;
  }
  delay(5);
  BLEKeyboard.releaseAll();
}

void runSingleAction(const String& a) {
  if (!a.length() || !BLEKeyboard.isConnected()) return;

  if (a.startsWith("CONSUMER:")) {
    String c = a.substring(9); c.toUpperCase();
    if      (c=="MEDIA_VOLUME_UP")          BLEKeyboard.write(KEY_MEDIA_VOLUME_UP);
    else if (c=="MEDIA_VOLUME_DOWN")        BLEKeyboard.write(KEY_MEDIA_VOLUME_DOWN);
    else if (c=="MEDIA_VOLUME_MUTE")        BLEKeyboard.write(KEY_MEDIA_MUTE);
    else if (c=="MEDIA_NEXT")               BLEKeyboard.write(KEY_MEDIA_NEXT_TRACK);
    else if (c=="MEDIA_PREVIOUS")           BLEKeyboard.write(KEY_MEDIA_PREVIOUS_TRACK);
    else if (c=="CONSUMER_BROWSER_BACK")    BLEKeyboard.write(KEY_MEDIA_WWW_BACK);
    else if (c=="CONSUMER_BROWSER_FORWARD") BLEKeyboard.write(KEY_MEDIA_WWW_FORWARD);
    else if (c=="MEDIA_PLAY_PAUSE")         BLEKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
    return;
  }

  if (a.startsWith("TEXT:")) {
    String t = a.substring(5);
    BLEKeyboard.print(t);
    return;
  }

  if (a.indexOf('+') >= 0) { pressChord(a); return; }

  uint8_t k = mapKeyToken(a);
  if (k) { BLEKeyboard.press(k); BLEKeyboard.release(k); }
}

void runAction(const String& spec) {
  String s = spec; s.trim();
  if (s.startsWith("SEQ:[")) {
    int lb = s.indexOf('['), rb = s.lastIndexOf(']');
    if (lb<0 || rb<lb) return;
    String inner = s.substring(lb+1, rb);
    bool inQ=false; String cur;
    for (size_t i=0;i<inner.length();i++){
      char ch = inner[i];
      if (ch=='"'){ inQ=!inQ; continue; }
      if (ch==',' && !inQ) { cur.trim(); if (cur.length()) runSingleAction(cur); cur=""; continue; }
      if (ch!='\\') cur += ch;
    }
    cur.trim(); if (cur.length()) runSingleAction(cur);
    return;
  }
  runSingleAction(s);
}

// ============================ Config lookups ============================
String getKeyAction(uint8_t mode, char keyLabel) {
  const char* modeKey = (mode==0)?"0":(mode==1)?"1":(mode==2)?"2":"3";
  char keyStr[2] = { keyLabel, 0 };
  JsonVariant v = cfg["keys"][modeKey][keyStr];
  if (!v.isNull()) return String(v.as<const char*>());
  return String("");
}
String getEncoderAction(uint8_t mode, const char* which) {
  const char* modeKey = (mode==0)?"0":(mode==1)?"1":(mode==2)?"2":"3";
  JsonVariant v = cfg["encoders"][modeKey][which];
  if (!v.isNull()) return String(v.as<const char*>());
  return String("");
}

// ============================ Helpers ============================
void checkModeButton(){
  int bs = digitalRead(PIN_MODE_BTN);
  if (bs != lastButtonState) {
    if (bs == LOW) modePushCounter++;
    delay(50);
  }
  lastButtonState = bs;
  if (modePushCounter > 3) modePushCounter = 0;
}

void encoderA(){
  long newPos = RotaryEncoderA.read()/4;
  if (newPos != positionEncoderA) {
    bool cw = newPos > positionEncoderA;
    positionEncoderA = newPos;
    String which = cw ? "A+" : "A-";
    String act = getEncoderAction((uint8_t)modePushCounter, which.c_str());
    if (act.length()) runAction(act);
  }
}
void encoderB(){
  long newPos = RotaryEncoderB.read()/4;
  if (newPos != positionEncoderB) {
    bool cw = newPos > positionEncoderB;
    positionEncoderB = newPos;
    String which = cw ? "B+" : "B-";
    String act = getEncoderAction((uint8_t)modePushCounter, which.c_str());
    if (act.length()) runAction(act);
  }
}
void handleKey(char key) {
  String act = getKeyAction((uint8_t)modePushCounter, key);
  if (act.length()) { runAction(act); delay(80); BLEKeyboard.releaseAll(); }
}

// ============================ Setup / Loop ============================
void setup() {
  BLEKeyboard.begin();          // Uses NimBLE-Arduino backend
  Serial.begin(115200);

  pinMode(PIN_MODE_BTN, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(PIN_LED_ARD1, OUTPUT); digitalWrite(PIN_LED_ARD1, HIGH);
  pinMode(PIN_LED_ARD2, OUTPUT); digitalWrite(PIN_LED_ARD2, HIGH);
  pinMode(PIN_LED_MODE1, OUTPUT); digitalWrite(PIN_LED_MODE1, LOW);
  pinMode(PIN_LED_MODE2, OUTPUT); digitalWrite(PIN_LED_MODE2, LOW);

  ensureConfigLoaded();
}

void loop() {
  // Serial config CLI (non-blocking)
  configCLI();

  // Pot -> brightness
  potValue = analogRead(PIN_POT);     // 0..4095
  lum = potValue / 16;                // 0..255
  analogWrite(PIN_LED_MODE1, lum);
  analogWrite(PIN_LED_MODE2, (modePushCounter==3) ? lum : (modePushCounter==2 ? lum : 0));
  if (modePushCounter==1) analogWrite(PIN_LED_MODE2, 0);

  // Input handling
  char key = keypad.getKey();
  if (key) handleKey(key);

  encoderA();
  encoderB();
  checkModeButton();

  // Mode indicators
  switch (modePushCounter) {
    case 0: digitalWrite(PIN_LED_MODE1,LOW);  digitalWrite(PIN_LED_MODE2,LOW); break;
    case 1: /* PWM set above */ digitalWrite(PIN_LED_MODE2,LOW); break;
    case 2: digitalWrite(PIN_LED_MODE1,LOW); /* PWM set above */ break;
    case 3: /* both PWM set above */ break;
  }

  delay(1);
}
