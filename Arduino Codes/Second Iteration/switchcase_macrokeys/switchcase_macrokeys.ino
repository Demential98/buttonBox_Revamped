/*
  Macro Keyboard (Leonardo/Pro Micro) — low-RAM build
  - JSON config lives in EEPROM; parsed on-demand (no big doc kept in RAM)
  - Hold Mode button (A1) on boot to enter CONFIG MODE (Serial 115200):
      GET           -> prints JSON from EEPROM
      PUT <len>\n   -> stream exactly <len> JSON bytes; device stores + CRC
      RESET         -> restore factory JSON
*/

#include <Arduino.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include "HID-Project.h"
#include <Keypad.h>
#include <Encoder.h>

// -------------------- Hardware (your original) --------------------
Encoder RotaryEncoderA(18, 15); // LEFT encoder (A)
Encoder RotaryEncoderB(14, 16); // RIGHT encoder (B)

long positionEncoderA  = -999;
long positionEncoderB  = -999;

const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
  {'0','1','2','3'},
  {'4','5','6','7'},
  {'8','9','A','B'},
  {'C','D','E','F'},
};
byte rowPins[ROWS] = {0, 2, 3, 4};
byte colPins[COLS] = {5, 6, 7, 8};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

int modePushCounter = 0;
int buttonState = 0, lastButtonState = 0;
const int ModeButton = A1;

const int Mode1 = 9;
const int Mode2 = 10;
const int LedArd1 = 17;
const int LedArd2 = 30;
const int Potenziometro = A2;
int ValorePot = 0;
int lum = 0;

// -------------------- Config storage (tiny) --------------------
struct EepromHeader {
  uint32_t magic;     // 0xDEADBEEF
  uint16_t version;   // 1
  uint16_t jsonLen;   // bytes of JSON payload
  uint32_t crc;       // CRC32 of JSON payload
};

static const uint32_t CFG_MAGIC   = 0xDEADBEEF;
static const uint16_t CFG_VERSION = 1;

// Derive EEPROM size when available
#ifndef EEPROM_SIZE
  #ifdef E2END
    #define EEPROM_SIZE (E2END + 1)
  #else
    #define EEPROM_SIZE 1024
  #endif
#endif

// We keep JSON modest so it fits 1KB EEPROM with header
static const uint16_t MAX_JSON = EEPROM_SIZE - sizeof(EepromHeader);

// CRC32 (table-less)
static uint32_t crc32_update(uint32_t crc, uint8_t data) {
  crc ^= data;
  for (uint8_t i=0;i<8;i++) crc = (crc >> 1) ^ (0xEDB88320 & (-(int32_t)(crc & 1)));
  return crc;
}
static uint32_t crc32_eeprom(uint16_t addr, uint16_t len) {
  uint32_t c = ~0U;
  for (uint16_t i=0;i<len;i++) c = crc32_update(c, EEPROM.read(addr + i));
  return ~c;
}
static uint32_t crc32_stream(Stream &s, uint16_t len) {
  uint32_t c = ~0U;
  for (uint16_t i=0;i<len;i++) {
    while (!s.available()) { /* wait */ }
    c = crc32_update(c, (uint8_t)s.read());
  }
  return ~c;
}

// Factory JSON kept in PROGMEM (flash), not SRAM
const char factoryJson[] PROGMEM = R"json(
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

// Length of factoryJson in flash
static uint16_t factoryLen() {
  // compute strlen_P
  uint16_t n=0; while (pgm_read_byte(factoryJson + n) != 0) n++; return n;
}

// Write factory to EEPROM (streamed, no RAM copy)
static bool installFactoryToEEPROM() {
  EepromHeader h;
  h.magic = CFG_MAGIC;
  h.version = CFG_VERSION;
  h.jsonLen = factoryLen();
  if (h.jsonLen == 0 || h.jsonLen > MAX_JSON) return false;

  // write payload first (after header)
  for (uint16_t i=0;i<h.jsonLen;i++) {
    uint8_t b = pgm_read_byte(factoryJson + i);
    EEPROM.update(sizeof(EepromHeader) + i, b);
  }
  h.crc = crc32_eeprom(sizeof(EepromHeader), h.jsonLen);

  // write header last
  const uint8_t* p = (const uint8_t*)&h;
  for (uint16_t i=0;i<sizeof(EepromHeader);i++) EEPROM.update(i, p[i]);
  return true;
}

// Validate header & CRC
static bool hasValidConfig(EepromHeader &out) {
  EepromHeader h;
  for (uint16_t i=0;i<sizeof(EepromHeader);i++) ((uint8_t*)&h)[i] = EEPROM.read(i);
  if (h.magic != CFG_MAGIC || h.version != CFG_VERSION) return false;
  if (h.jsonLen == 0 || h.jsonLen > MAX_JSON) return false;
  uint32_t crc = crc32_eeprom(sizeof(EepromHeader), h.jsonLen);
  if (crc != h.crc) return false;
  out = h;
  return true;
}

// -------------------- EEPROM stream for ArduinoJson --------------------
class EEPROMRangeStream : public Stream {
public:
  EEPROMRangeStream(uint16_t start, uint16_t len) : _start(start), _len(len), _pos(0) {}
  int available() override { return (_pos < _len) ? 1 : 0; }
  int read() override {
    if (_pos >= _len) return -1;
    int v = EEPROM.read(_start + _pos);
    _pos++;
    return v;
  }
  int peek() override {
    if (_pos >= _len) return -1;
    return EEPROM.read(_start + _pos);
  }
  void flush() override {}
  size_t write(uint8_t) override { return 0; }
private:
  uint16_t _start, _len, _pos;
};

// -------------------- Action runner (compact) --------------------
static uint8_t mapKeyToken(const String& t) {
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

static void pressChord(const String& chord) {
  int start = 0;
  while (true){
    int plus = chord.indexOf('+', start);
    String part = (plus<0) ? chord.substring(start) : chord.substring(start, plus);
    String p = part; p.trim(); p.toUpperCase();
    if (p=="CTRL"||p=="CONTROL") Keyboard.press(KEY_LEFT_CTRL);
    else if (p=="ALT")   Keyboard.press(KEY_LEFT_ALT);
    else if (p=="SHIFT") Keyboard.press(KEY_LEFT_SHIFT);
    else if (p=="GUI"||p=="WIN"||p=="META") Keyboard.press(KEY_LEFT_GUI);
    else { uint8_t k = mapKeyToken(part); if (k) Keyboard.press(k); }
    if (plus<0) break; else start = plus+1;
  }
  delay(5);
  Keyboard.releaseAll();
}

static void runSingleAction(const String& a) {
  if (!a.length()) return;
  if (a.startsWith("CONSUMER:")) {
    String c = a.substring(9); c.toUpperCase();
    if (c=="MEDIA_VOLUME_UP") Consumer.write(MEDIA_VOLUME_UP);
    else if (c=="MEDIA_VOLUME_DOWN") Consumer.write(MEDIA_VOLUME_DOWN);
    else if (c=="MEDIA_VOLUME_MUTE") Consumer.write(MEDIA_VOLUME_MUTE);
    else if (c=="MEDIA_NEXT") Consumer.write(MEDIA_NEXT);
    else if (c=="MEDIA_PREVIOUS") Consumer.write(MEDIA_PREVIOUS);
    else if (c=="CONSUMER_BROWSER_BACK") Consumer.write(CONSUMER_BROWSER_BACK);
    else if (c=="CONSUMER_BROWSER_FORWARD") Consumer.write(CONSUMER_BROWSER_FORWARD);
    else if (c=="MEDIA_PLAY_PAUSE") Consumer.write(MEDIA_PLAY_PAUSE);
    return;
  }
  if (a.startsWith("TEXT:")) {
    String t = a.substring(5);
    for (size_t i=0;i<t.length();i++) Keyboard.write(t[i]);
    return;
  }
  if (a.indexOf('+') >= 0) { pressChord(a); return; }
  uint8_t k = mapKeyToken(a);
  if (k) { Keyboard.press(k); Keyboard.release(k); }
}

static void runAction(const String& spec) {
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

// -------------------- On-demand JSON lookup --------------------
// We parse only one string value at a time from EEPROM JSON using a filter,
// so the JsonDocument can be tiny (<= 128 bytes).
static bool readJsonStringPath(char* out, size_t outLen,
                               const char* path0, const char* path1,
                               const char* path2, const char* path3) {
  EepromHeader h;
  if (!hasValidConfig(h)) return false;
  EEPROMRangeStream js(sizeof(EepromHeader), h.jsonLen);

  // Build a filter that keeps only the requested leaf
  // Example for keys: filter["keys"][mode][key] = true
  StaticJsonDocument<96> filter;
  JsonVariant f = filter.to<JsonVariant>();
  if (path0) f = f[path0];
  if (path1) f = f[path1];
  if (path2) f = f[path2];
  if (path3) f = f[path3];
  f.set(true);

  // We only need a very small doc since only one value is kept
  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, js, DeserializationOption::Filter(filter));
  if (err) return false;

  JsonVariant v = doc;
  if (path0) v = v[path0];
  if (path1) v = v[path1];
  if (path2) v = v[path2];
  if (path3) v = v[path3];

  if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    strlcpy(out, s, outLen);
    return true;
  }
  return false;
}

static String getKeyAction(uint8_t mode, char keyLabel) {
  char buf[64]; // plenty for our action strings
  char modeStr[2] = { char('0' + mode), 0 };
  char keyStr[2]  = { keyLabel, 0 };
  if (readJsonStringPath(buf, sizeof(buf), "keys", modeStr, keyStr, nullptr)) return String(buf);
  return String("");
}

static String getEncoderAction(uint8_t mode, const char* which) {
  char buf[48];
  char modeStr[2] = { char('0' + mode), 0 };
  if (readJsonStringPath(buf, sizeof(buf), "encoders", modeStr, which, nullptr)) return String(buf);
  return String("");
}

// -------------------- Config-mode CLI (streaming) --------------------
static bool configMode = false;

static void enterConfigModeIfHeld() {
  pinMode(ModeButton, INPUT_PULLUP);
  delay(10);
  if (digitalRead(ModeButton) == LOW) configMode = true;
}

// Print JSON from EEPROM without copying into RAM
static void cmdGET() {
  EepromHeader h;
  if (!hasValidConfig(h)) { Serial.println(F("ERR no config")); return; }
  for (uint16_t i=0;i<h.jsonLen;i++) {
    Serial.write(EEPROM.read(sizeof(EepromHeader)+i));
  }
  Serial.println();
}

static void cmdPUT(uint16_t len) {
  if (len==0 || len > MAX_JSON) { Serial.println(F("ERR badlen")); return; }
  // Write payload first while computing CRC from Serial
  uint32_t c = ~0U;
  for (uint16_t i=0;i<len;i++) {
    // wait for data
    while (!Serial.available()) {}
    uint8_t b = (uint8_t)Serial.read();
    EEPROM.update(sizeof(EepromHeader) + i, b);
    c = crc32_update(c, b);
  }
  c = ~c;

  EepromHeader h;
  h.magic   = CFG_MAGIC;
  h.version = CFG_VERSION;
  h.jsonLen = len;
  h.crc     = c;

  const uint8_t* p = (const uint8_t*)&h;
  for (uint16_t i=0;i<sizeof(EepromHeader);i++) EEPROM.update(i, p[i]);

  Serial.println(F("SAVED"));
}

static void cmdRESET() {
  if (installFactoryToEEPROM()) Serial.println(F("RESET OK"));
  else Serial.println(F("ERR reset"));
}

static void configCLI() {
  Serial.begin(115200);
  unsigned long startWait = millis();
  while (!Serial && millis()-startWait < 3000) {}
  Serial.println(F("CONFIG MODE"));
  Serial.println(F("Commands: GET | PUT <len> | RESET"));
  for (;;) {
    if (Serial.available()) {
      String cmd = Serial.readStringUntil('\n'); cmd.trim();
      if (cmd.startsWith("GET"))       cmdGET();
      else if (cmd.startsWith("PUT"))  { int sp=cmd.indexOf(' '); if (sp<0){Serial.println(F("ERR len"));} else cmdPUT(cmd.substring(sp+1).toInt()); }
      else if (cmd.startsWith("RESET")) cmdRESET();
      else Serial.println(F("UNKNOWN"));
    }
  }
}

// -------------------- Your original helpers w/ config --------------------
static void checkModeButton(){
  buttonState = digitalRead(ModeButton);
  if (buttonState != lastButtonState) {
    if (buttonState == LOW) modePushCounter++;
    delay(50);
  }
  lastButtonState = buttonState;
  if (modePushCounter > 3) modePushCounter = 0;
}

static void encoderA(){
  long newPos = RotaryEncoderA.read()/4;
  if (newPos != positionEncoderA) {
    bool cw = newPos > positionEncoderA;
    positionEncoderA = newPos;
    String which = cw ? "A+" : "A-";
    String act = getEncoderAction((uint8_t)modePushCounter, which.c_str());
    if (act.length()) runAction(act);
  }
}

static void encoderB(){
  long newPos = RotaryEncoderB.read()/4;
  if (newPos != positionEncoderB) {
    bool cw = newPos > positionEncoderB;
    positionEncoderB = newPos;
    String which = cw ? "B+" : "B-";
    String act = getEncoderAction((uint8_t)modePushCounter, which.c_str());
    if (act.length()) runAction(act);
  }
}

static void handleKey(char key) {
  String act = getKeyAction((uint8_t)modePushCounter, key);
  if (act.length()) { runAction(act); delay(80); Keyboard.releaseAll(); }
}

// -------------------- Setup / Loop --------------------
void setup() {
  enterConfigModeIfHeld();
  if (configMode) { configCLI(); } // never returns

  Serial.begin(9600);
  pinMode(ModeButton, INPUT_PULLUP);
  pinMode(LED_BUILTIN,OUTPUT);
  pinMode(LedArd1, OUTPUT); digitalWrite(LedArd1, HIGH);
  pinMode(LedArd2, OUTPUT); digitalWrite(LedArd2,HIGH);
  pinMode(Mode1,OUTPUT); digitalWrite(Mode1,LOW);
  pinMode(Mode2,OUTPUT); digitalWrite(Mode2,LOW);

  Consumer.begin();
  Mouse.begin();

  // Ensure there's a valid config in EEPROM
  EepromHeader h;
  if (!hasValidConfig(h)) installFactoryToEEPROM();
}

void loop() {
  ValorePot = analogRead(Potenziometro);
  lum = ValorePot / 4;

  char key = keypad.getKey();
  if (key) handleKey(key);

  encoderA();
  encoderB();
  checkModeButton();

  switch (modePushCounter) {
    case 0: digitalWrite(Mode1,LOW);  digitalWrite(Mode2,LOW); break;
    case 1: analogWrite(Mode1,lum);   digitalWrite(Mode2,LOW); break;
    case 2: digitalWrite(Mode1,LOW);  analogWrite(Mode2,lum);  break;
    case 3: analogWrite(Mode1,lum);   analogWrite(Mode2,lum);  break;
  }
  delay(1);
}
