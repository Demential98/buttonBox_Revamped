/*
  Macro Keyboard (Leonardo/Pro Micro) — Hybrid Host-Push Config
  --------------------------------------------------------------
  - On boot: prints "READY" at 115200 and waits for PC to push config.
    Protocol:
      "BINCFG <len>\n"  followed by <len> raw bytes
    Use the provided Python:  python send_packed.py COM13 config.json
    (It compacts your JSON and sends the binary.)

  - Config is kept in RAM (K/E/Dict). No EEPROM used, no size limits.
  - If nothing arrives within a timeout, a small built-in default is loaded.

  Requires libraries:
    - HID-Project
    - Keypad
    - Encoder

  Hardware (from your original):
    - Matrix rows: D0, D2, D3, D4
    - Matrix cols: D5, D6, D7, D8
    - Encoders: A: D18, D15   B: D14, D16
    - Mode button: A1 (D19)
    - LEDs: Mode1 D9, Mode2 D10, LedArd1 D17, LedArd2 D30
    - Potentiometer: A2

  Actions supported:
    - Chords: "CTRL+...", "ALT", "SHIFT", "GUI/WIN", letters A..Z, F1..F12, arrows, PAGE_UP/DOWN, ENTER/RETURN
    - Consumer: "CONSUMER:MEDIA_VOLUME_UP|DOWN|MUTE|NEXT|PREVIOUS|MEDIA_PLAY_PAUSE|CONSUMER_BROWSER_BACK|CONSUMER_BROWSER_FORWARD"
    - Text: "TEXT:hello world"
    - Sequences: 'SEQ:["CTRL+k","c"]'  or 'SEQ:["ENTER","TEXT:XD"]'
*/

#include <Arduino.h>
#include "HID-Project.h"
#include <Keypad.h>
#include <Encoder.h>

// -------------------- Hardware --------------------
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
const int ModeButton = A1;   // 19

const int Mode1 = 9;
const int Mode2 = 10;
const int LedArd1 = 17;
const int LedArd2 = 30;
const int Potenziometro = A2;
int ValorePot = 0;
int lum = 0;

// -------------------- RAM config (host-pushed) --------------------
#define CFG_MAGIC 0x42434647UL /* 'BCFG' */

// Binary header sent by PC
struct PackedHeader {
  uint32_t magic;     // 'BCFG'
  uint16_t version;   // 1
  uint16_t dictCount; // number of strings
};

// Key/encoder maps:
//   K: 4 modes × 16 keys (fixed order 0..9,A..F). Each cell is index into Dict, or 0xFF for none.
//   Emap: 4 modes × 4 encoder slots [A+, A-, B+, B-], same indexing.
static uint8_t K[4][16];
static uint8_t Emap[4][4];

// Action dictionary: pointers into a single string pool we own.
static const char* Dict[128];     // up to 128 unique strings; increase if needed
static char* strPool = nullptr;   // allocated buffer holding all strings (NUL-separated)
static uint16_t dictCount = 0;

static void freeConfigPool() {
  if (strPool) { free(strPool); strPool = nullptr; }
  for (uint16_t i=0;i<sizeof(Dict)/sizeof(Dict[0]);i++) Dict[i] = nullptr;
  dictCount = 0;
}

// Apply packed binary buffer (received from PC) into RAM structures
static bool applyPackedConfig(const uint8_t* buf, size_t len) {
  freeConfigPool();

  if (len < sizeof(PackedHeader) + sizeof(K) + sizeof(Emap)) return false;
  const PackedHeader* h = (const PackedHeader*)buf;
  if (h->magic != CFG_MAGIC || h->version != 1) return false;

  size_t off = sizeof(PackedHeader);
  memcpy(K, buf + off, sizeof(K));    off += sizeof(K);
  memcpy(Emap, buf + off, sizeof(Emap)); off += sizeof(Emap);

  dictCount = h->dictCount;
  if (dictCount > (uint16_t)(sizeof(Dict)/sizeof(Dict[0]))) return false;

  // remaining bytes = concatenated zero-terminated UTF-8 strings
  if (off > len) return false;
  size_t poolLen = len - off;
  strPool = (char*)malloc(poolLen + 1);
  if (!strPool) return false;
  memcpy(strPool, buf + off, poolLen);
  strPool[poolLen] = 0;

  // Build Dict[] pointers by walking the pool
  char* p = strPool;
  for (uint16_t i = 0; i < dictCount; i++) {
    Dict[i] = p;
    while (*p) p++;
    p++; // skip NUL
    if ((size_t)(p - strPool) > poolLen + 1) return false;
  }
  return true;
}

// Wait briefly at boot for host to push a config
static bool waitForHostConfig(unsigned long ms_timeout = 6000) {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0) < 1500) { /* wait for USB */ }
  Serial.println(F("READY")); // host looks for this banner

  // Expect a line: BINCFG <len>\n  then <len> raw bytes
  String line;
  while (millis() - t0 < ms_timeout) {
    while (Serial.available()) {
      char c = (char)Serial.read();
      if (c == '\n') {
        line.trim();
        if (line.startsWith("BINCFG")) {
          int sp = line.indexOf(' ');
          if (sp < 0) { Serial.println(F("ERR len")); return false; }
          int n = line.substring(sp+1).toInt();
          if (n <= 0 || n > 8192) { Serial.println(F("ERR badlen")); return false; }
          uint8_t* buf = (uint8_t*)malloc(n);
          if (!buf) { Serial.println(F("ERR mem")); return false; }
          int got = 0; unsigned long to = millis()+4000;
          while (got < n && millis() < to) {
            if (Serial.available()) { buf[got++] = (uint8_t)Serial.read(); to = millis()+4000; }
          }
          if (got != n) { Serial.println(F("ERR timeout")); free(buf); return false; }

          bool ok = applyPackedConfig(buf, (size_t)n);
          free(buf);
          Serial.println(ok ? F("SAVED") : F("ERR parse"));
          return ok;
        } else {
          Serial.println(F("UNKNOWN"));
          line = "";
        }
      } else {
        line += c;
      }
    }
    if (millis() - t0 > ms_timeout) break;
  }
  return false; // nothing received
}

// -------------------- Action runner --------------------
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

// -------------------- Resolve actions from RAM maps --------------------
static int keyLabelToIndex(char label) {
  if (label >= '0' && label <= '9') return label - '0';
  if (label >= 'A' && label <= 'F') return 10 + (label - 'A');
  if (label >= 'a' && label <= 'f') return 10 + (label - 'a');
  return -1;
}
static const char* dictAt(uint8_t idx) {
  return (idx != 0xFF && idx < dictCount) ? Dict[idx] : nullptr;
}
static String getKeyAction(uint8_t mode, char keyLabel) {
  int ix = keyLabelToIndex(keyLabel);
  if (ix < 0) return String("");
  const char* s = dictAt(K[mode][ix]);
  return s ? String(s) : String("");
}
static String getEncoderAction(uint8_t mode, const char* which) {
  int slot = (which[0]=='A' && which[1]=='+') ? 0 :
             (which[0]=='A' && which[1]=='-') ? 1 :
             (which[0]=='B' && which[1]=='+') ? 2 :
             (which[0]=='B' && which[1]=='-') ? 3 : -1;
  if (slot < 0) return String("");
  const char* s = dictAt(Emap[mode][slot]);
  return s ? String(s) : String("");
}

// -------------------- Original helpers --------------------
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

// -------------------- Tiny fallback (if host didn't send) --------------------
// This is intentionally tiny so RAM stays low. Adjust if you want defaults.
static void loadFallbackDefaults() {
  // Clear maps
  for (uint8_t m=0;m<4;m++){ for(uint8_t i=0;i<16;i++) K[m][i]=0xFF; for(uint8_t s=0;s<4;s++) Emap[m][s]=0xFF; }

  // Very small dict (expand if you like)
  static const char* d[] = {
    "CTRL+c","CTRL+x","CTRL+f","F5","CTRL+v","ENTER",
    "CONSUMER:MEDIA_VOLUME_UP","CONSUMER:MEDIA_VOLUME_DOWN"
  };
  for (uint8_t i=0;i<sizeof(d)/sizeof(d[0]);i++) Dict[i]=d[i];
  dictCount = sizeof(d)/sizeof(d[0]);

  // Mode 0 a few examples
  K[0][0] = 0; // '0' -> CTRL+c
  K[0][1] = 1; // '1' -> CTRL+x
  K[0][2] = 2; // '2' -> CTRL+f
  K[0][3] = 3; // '3' -> F5
  K[0][10] = 4; // 'A' -> CTRL+v
  K[1][15] = 5; // mode1 'F' -> ENTER

  Emap[0][0] = 7; // mode0 A+ -> VOL DOWN
  Emap[0][1] = 6; // mode0 A- -> VOL UP
}

// -------------------- Setup / Loop --------------------
void setup() {
  // Option: require the host only when holding the Mode button:
  pinMode(ModeButton, INPUT_PULLUP);
  bool got = false;
  if (digitalRead(ModeButton) == LOW) {
    got = waitForHostConfig(10000); // wait longer if button held
  } else {
    got = waitForHostConfig(3000);  // short wait otherwise
  }

  // Init hardware
  pinMode(LED_BUILTIN,OUTPUT);
  pinMode(LedArd1, OUTPUT); digitalWrite(LedArd1, HIGH);
  pinMode(LedArd2, OUTPUT); digitalWrite(LedArd2,HIGH);
  pinMode(Mode1,OUTPUT); digitalWrite(Mode1,LOW);
  pinMode(Mode2,OUTPUT); digitalWrite(Mode2,LOW);

  Consumer.begin();
  Mouse.begin();

  if (!got || dictCount == 0) {
    // No host config received -> minimal defaults
    loadFallbackDefaults();
  }
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
