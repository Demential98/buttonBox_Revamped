/*
  ESP32-S3 USB HID Macro Keyboard (USB TinyUSB) with LittleFS JSON config
  - USB HID keyboard + consumer control (media keys)
  - Config over Serial @115200: GET | PUT <len> | RESET
  - Config stored in LittleFS: /config.json
  - 4x4 Key matrix, 2 rotary encoders, a mode button, two mode LEDs, optional pot

  Board: ESP32-S3 (select CDC + HID in Tools)
  Requires:
    - ArduinoJson
    - Keypad
    - Encoder
    - LittleFS (ESP32 core built-in)
*/

#include <Arduino.h>
#include <Keypad.h>
#include <Encoder.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#define FSYS LittleFS

// -------- USB HID (TinyUSB: built-in for ESP32-S3) --------
#include "USB.h"
#include "USBHID.h"
#include "USBHIDKeyboard.h"
#include "USBHIDConsumerControl.h"

USBHID HID;
USBHIDKeyboard Keyboard;
USBHIDConsumerControl Consumer;

// -------- Hardware defaults (can be overridden by JSON) --------
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

const byte ROWS = 4, COLS = 4;
char keymapArr[ROWS][COLS] = {
  {'0','1','2','3'},
  {'4','5','6','7'},
  {'8','9','A','B'},
  {'C','D','E','F'},
};

// Pin placeholders, will be set from config (or fallback)
int pinRow[ROWS] = {-1,-1,-1,-1};
int pinCol[COLS] = {-1,-1,-1,-1};
int pinEncA1=-1, pinEncA2=-1, pinEncB1=-1, pinEncB2=-1;
int pinModeBtn=-1, pinLedMode1=-1, pinLedMode2=-1, pinLedArd1=-1, pinLedArd2=-1, pinPot=-1;

Keypad* keypad = nullptr;
Encoder* encA = nullptr;
Encoder* encB = nullptr;
long posA = -999, posB = -999;

int modePushCounter = 0;
int buttonState = 0, lastButtonState = 0;
int potValue = 0, lum = 255;

// -------- JSON config --------
StaticJsonDocument<6144> cfg;  // roomy on S3

// Default config embedded (edit pins to match your wiring)
static const char *factoryJson PROGMEM = R"json(
{"version":1,
 "pins":{
  "rows":[1,2,42,41],
  "cols":[45,48,47,21],
  "encA":[10,11],
  "encB":[12,13],
  "modeButton":9,
  "ledMode1":20,
  "ledMode2":14,
  "ledArd1":12,
  "ledArd2":13,
  "pot":46
 },
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
// ---- Config helpers (LittleFS + JSON) ----
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
  return deserializeJson(cfg, json) == DeserializationError::Ok;
}
bool ensureConfigLoaded() {
  FSYS.begin(true);
  String text;
  if (loadConfigText(text) && parseConfig(text)) return true;

  // fall back to factory
  String fac;
  for (const char* p = factoryJson; pgm_read_byte(p); ++p) fac += (char)pgm_read_byte(p);
  saveConfigText(fac);
  return parseConfig(fac);
}

void applyPinsFromConfig() {
  JsonObject pins = cfg["pins"].as<JsonObject>();
  if (pins.isNull()) return;

  JsonArray rows = pins["rows"].as<JsonArray>();
  JsonArray cols = pins["cols"].as<JsonArray>();
  for (int i=0;i<ROWS && i<(int)rows.size();++i) pinRow[i] = rows[i].as<int>();
  for (int i=0;i<COLS && i<(int)cols.size();++i) pinCol[i] = cols[i].as<int>();

  JsonArray encAarr = pins["encA"].as<JsonArray>();
  JsonArray encBarr = pins["encB"].as<JsonArray>();
  if (encAarr.size()>=2) { pinEncA1 = encAarr[0].as<int>(); pinEncA2 = encAarr[1].as<int>(); }
  if (encBarr.size()>=2) { pinEncB1 = encBarr[0].as<int>(); pinEncB2 = encBarr[1].as<int>(); }

  if (pins.containsKey("modeButton")) pinModeBtn  = pins["modeButton"].as<int>();
  if (pins.containsKey("ledMode1"))   pinLedMode1 = pins["ledMode1"].as<int>();
  if (pins.containsKey("ledMode2"))   pinLedMode2 = pins["ledMode2"].as<int>();
  if (pins.containsKey("ledArd1"))    pinLedArd1  = pins["ledArd1"].as<int>();
  if (pins.containsKey("ledArd2"))    pinLedArd2  = pins["ledArd2"].as<int>();
  if (pins.containsKey("pot"))        pinPot      = pins["pot"].as<int>();
}

// ---- Serial config CLI: GET | PUT <len> | RESET ----
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
    if (n <= 0 || n > 65536) { Serial.println(F("ERR badlen")); return; }

    String body; body.reserve(n+1);
    unsigned long to = millis()+20000;
    while ((int)body.length() < n && millis() < to) {
      if (Serial.available()) { body += (char)Serial.read(); to = millis()+20000; }
    }
    if ((int)body.length() != n) { Serial.println(F("ERR timeout")); return; }

    StaticJsonDocument<6144> tmp;
    if (deserializeJson(tmp, body)) { Serial.println(F("ERR json")); return; }
    if (!saveConfigText(body)) { Serial.println(F("ERR save")); return; }

    cfg.clear(); deserializeJson(cfg, body);
    Serial.println(F("SAVED"));

    // Re-apply pins/peripherals live
    applyPinsFromConfig();
    if (keypad) { delete keypad; keypad = nullptr; }
    if (encA) { delete encA; encA = nullptr; }
    if (encB) { delete encB; encB = nullptr; }
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

// ---- HID helpers ----
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
  if (s=="TAB") return KEY_TAB;
  if (s=="ESC"||s=="ESCAPE") return KEY_ESC;
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
    if      (p=="CTRL"||p=="CONTROL") Keyboard.press(KEY_LEFT_CTRL);
    else if (p=="ALT")                 Keyboard.press(KEY_LEFT_ALT);
    else if (p=="SHIFT")               Keyboard.press(KEY_LEFT_SHIFT);
    else if (p=="GUI"||p=="WIN"||p=="META") Keyboard.press(KEY_LEFT_GUI);
    else { uint8_t k = mapKeyToken(part); if (k) Keyboard.press(k); }
    if (plus<0) break; else start = plus+1;
  }
  delay(5);
  Keyboard.releaseAll();
}

void consumerWrite(const String& c) {
  String u = c; u.toUpperCase();

  uint16_t usage = 0;
  if      (u == "MEDIA_VOLUME_UP")           usage = HID_USAGE_CONSUMER_VOLUME_INCREMENT;
  else if (u == "MEDIA_VOLUME_DOWN")         usage = HID_USAGE_CONSUMER_VOLUME_DECREMENT;
  else if (u == "MEDIA_VOLUME_MUTE")         usage = HID_USAGE_CONSUMER_MUTE;
  else if (u == "MEDIA_NEXT")                usage = HID_USAGE_CONSUMER_SCAN_NEXT;        // note: no _TRACK
  else if (u == "MEDIA_PREVIOUS")            usage = HID_USAGE_CONSUMER_SCAN_PREVIOUS;    // note: no _TRACK
  else if (u == "MEDIA_PLAY_PAUSE")          usage = HID_USAGE_CONSUMER_PLAY_PAUSE;
  else if (u == "CONSUMER_BROWSER_BACK")     usage = HID_USAGE_CONSUMER_AC_BACK;
  else if (u == "CONSUMER_BROWSER_FORWARD")  usage = HID_USAGE_CONSUMER_AC_FORWARD;

  if (usage) {
    Consumer.press(usage);
    delay(2);
    Consumer.release();       // or Consumer.releaseAll();
  }
}

void runSingleAction(const String& a) {
  if (!a.length()) return;

  if (a.startsWith("CONSUMER:")) { consumerWrite(a.substring(9)); return; }

  if (a.startsWith("TEXT:")) { Keyboard.print(a.substring(5)); return; }

  if (a.startsWith("SEQ:")) {
    String payload = a.substring(4);
    StaticJsonDocument<1024> seq;
    if (deserializeJson(seq, payload) == DeserializationError::Ok && seq.is<JsonArray>()) {
      for (JsonVariant v : seq.as<JsonArray>()) {
        if (v.is<const char*>()) runSingleAction(String(v.as<const char*>())); // recurse
      }
    }
    return;
  }

  if (a.indexOf('+') >= 0) { pressChord(a); return; }

  uint8_t k = mapKeyToken(a);
  if (k) Keyboard.write(k);
}

// ---- Setup/Loop helpers ----
void buildPeripheralsIfNeeded() {
  if (!keypad && pinRow[0]>=0 && pinRow[1]>=0 && pinRow[2]>=0 && pinRow[3]>=0 &&
      pinCol[0]>=0 && pinCol[1]>=0 && pinCol[2]>=0 && pinCol[3]>=0) {
    for (int i=0;i<ROWS;i++) pinMode(pinRow[i], OUTPUT);
    for (int i=0;i<COLS;i++) pinMode(pinCol[i], INPUT_PULLUP);
    keypad = new Keypad(makeKeymap(keymapArr), (byte*)pinRow, (byte*)pinCol, ROWS, COLS);
  }
  if (!encA && pinEncA1>=0 && pinEncA2>=0) encA = new Encoder(pinEncA1, pinEncA2);
  if (!encB && pinEncB1>=0 && pinEncB2>=0) encB = new Encoder(pinEncB1, pinEncB2);
}

void applyModeLeds() {
  if (pinLedMode1>=0) pinMode(pinLedMode1, OUTPUT);
  if (pinLedMode2>=0) pinMode(pinLedMode2, OUTPUT);
  auto setL = [&](int pin, int val){ if (pin>=0) analogWrite(pin, val); };
  int b0=0,b1=0;
  switch (modePushCounter) {
    case 0: b0=0;   b1=0;   break;
    case 1: b0=lum; b1=0;   break;
    case 2: b0=0;   b1=lum; break;
    case 3: b0=lum; b1=lum; break;
  }
  setL(pinLedMode1, b0);
  setL(pinLedMode2, b1);
}

void setupPinsDefaultsIfUnset() {
  if (pinRow[0]==-1 && pinCol[0]==-1) {
    int dRows[4] = {14,27,26,25}; for (int i=0;i<4;i++) pinRow[i]=dRows[i];
    int dCols[4] = {4,16,17,5};   for (int i=0;i<4;i++) pinCol[i]=dCols[i];
    pinEncA1=18; pinEncA2=19; pinEncB1=23; pinEncB2=22;
    pinModeBtn=21; pinLedMode1=15; pinLedMode2=2; pinLedArd1=12; pinLedArd2=13; pinPot=-1;
  }
}

void handleModeButton() {
  if (pinModeBtn<0) return;
  int s = digitalRead(pinModeBtn);
  if (s != lastButtonState) {
    if (s == LOW) modePushCounter = (modePushCounter + 1) & 0x03;
    delay(20);
  }
  lastButtonState = s;
}

String keyActionFor(char key, int mode) {
  char id[2] = { key, 0 };
  String m = String(mode);
  JsonVariant v = cfg["keys"][m][id];
  if (v.is<const char*>()) return String(v.as<const char*>());
  return String();
}

String encActionFor(bool A, bool plus, int mode) {
  String m = String(mode);
  const char* token = A ? (plus?"A+":"A-") : (plus?"B+":"B-");
  JsonVariant v = cfg["encoders"][m][token];
  if (v.is<const char*>()) return String(v.as<const char*>());
  return String();
}

// ---- Arduino entry points ----
void setup() {
  Serial.begin(115200);
  delay(200);

  // Start USB HID first
  HID.begin();
  Keyboard.begin();
  Consumer.begin();
  USB.begin();

  FSYS.begin(true);
  ensureConfigLoaded();
  applyPinsFromConfig();
  setupPinsDefaultsIfUnset();

  if (pinLedArd1>=0) { pinMode(pinLedArd1, OUTPUT); digitalWrite(pinLedArd1, HIGH); }
  if (pinLedArd2>=0) { pinMode(pinLedArd2, OUTPUT); digitalWrite(pinLedArd2, HIGH); }
  if (pinModeBtn>=0) pinMode(pinModeBtn, INPUT_PULLUP);
}

void loop() {
  configCLI();
  buildPeripheralsIfNeeded();

  if (pinPot>=0) {
    potValue = analogRead(pinPot);
    lum = constrain(potValue/4, 0, 255);
  } else {
    lum = 200;
  }

  handleModeButton();
  applyModeLeds();

  if (keypad) {
    char key = keypad->getKey();
    if (key) {
      String act = keyActionFor(key, modePushCounter);
      if (act.length()) runSingleAction(act);
      delay(50);
    }
  }

  if (encA) {
    long p = encA->read();
    if (p != posA && posA!=-999) {
      bool plus = (p > posA);
      String act = encActionFor(true, plus, modePushCounter);
      if (act.length()) runSingleAction(act);
    }
    posA = p;
  }
  if (encB) {
    long p = encB->read();
    if (p != posB && posB!=-999) {
      bool plus = (p > posB);
      String act = encActionFor(false, plus, modePushCounter);
      if (act.length()) runSingleAction(act);
    }
    posB = p;
  }

  delay(5);
}