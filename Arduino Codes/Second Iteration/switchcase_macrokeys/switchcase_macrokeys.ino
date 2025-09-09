/*
  Macro Keyboard (Leonardo/Pro Micro) — Host-Push Config + Built-in Defaults
  --------------------------------------------------------------------------
  - On boot: prints "READY" @115200 and waits briefly for host:
        "BINCFG <len>\n"  then <len> packed bytes (header + maps + string pool)
  - If received & valid, uses RAM config. Otherwise, uses PROGMEM defaults.
  - No EEPROM/Flash writes; config only in RAM (host-pushed) or PROGMEM (fallback).
  - Libraries: HID-Project, Keypad, Encoder

  Wiring (as your original):
    Rows: D0, D2, D3, D4
    Cols: D5, D6, D7, D8
    Enc A: D18, D15   Enc B: D14, D16
    Mode btn: A1 (D19)
    LEDs: Mode1 D9, Mode2 D10, LedArd1 D17, LedArd2 D30
    Pot: A2

  Supported actions (strings in dictionary / JSON / packed):
    - Chords: "CTRL+...", "ALT", "SHIFT", "GUI/WIN", A..Z, F1..F12, arrows, PAGE_UP/DOWN, ENTER/RETURN
    - Consumer: "CONSUMER:MEDIA_VOLUME_UP|DOWN|MUTE|NEXT|PREVIOUS|MEDIA_PLAY_PAUSE|CONSUMER_BROWSER_BACK|CONSUMER_BROWSER_FORWARD"
    - Text: "TEXT:hello world"
    - Sequences: 'SEQ:["CTRL+k","c"]'  or 'SEQ:["ENTER","TEXT:XD"]'
*/

#include <Arduino.h>
#include "HID-Project.h"
#include <Keypad.h>
#include <Encoder.h>

// ============================= Hardware =============================
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

// ===================== Packed config format (host) =====================
#define CFG_MAGIC 0x42434647UL /* 'BCFG' */
struct PackedHeader {
  uint32_t magic;     // 'BCFG'
  uint16_t version;   // 1
  uint16_t dictCount; // number of strings
};

// Runtime maps (RAM). Values are indices into dictionary (0..dictCount-1) or 0xFF for none.
static uint8_t K[4][16];
static uint8_t Emap[4][4];

// --------- Host (RAM) dictionary state ---------
static const char* DictRAM[128];     // pointers into strPool (set only when host config arrives)
static char* strPool = nullptr;      // malloc'ed pool for host config strings
static uint16_t dictCount = 0;
static bool hostConfigLoaded = false;

static void freeHostPool() {
  if (strPool) { free(strPool); strPool = nullptr; }
  for (uint8_t i=0;i<128;i++) DictRAM[i] = nullptr;
  dictCount = 0;
  hostConfigLoaded = false;
}

// ===================== Built-in default (PROGMEM) =====================
// Dict entries (46). Stored as separate PROGMEM strings to avoid big RAM usage.
#define DSTR(n, s) static const char d_##n[] PROGMEM = s;
DSTR(0,  "CONSUMER:CONSUMER_BROWSER_BACK")
DSTR(1,  "CONSUMER:CONSUMER_BROWSER_FORWARD")
DSTR(2,  "CONSUMER:MEDIA_NEXT")
DSTR(3,  "CONSUMER:MEDIA_PLAY_PAUSE")
DSTR(4,  "CONSUMER:MEDIA_PREVIOUS")
DSTR(5,  "CONSUMER:MEDIA_VOLUME_DOWN")
DSTR(6,  "CONSUMER:MEDIA_VOLUME_MUTE")
DSTR(7,  "CONSUMER:MEDIA_VOLUME_UP")
DSTR(8,  "CTRL+ALT+SHIFT+ENTER")
DSTR(9,  "CTRL+ALT+SHIFT+v")
DSTR(10, "CTRL+ALT+SHIFT+w")
DSTR(11, "CTRL+ALT+i")
DSTR(12, "CTRL+PAGE_DOWN")
DSTR(13, "CTRL+PAGE_UP")
DSTR(14, "CTRL+SHIFT+F9")
DSTR(15, "CTRL+V")
DSTR(16, "CTRL+a")
DSTR(17, "CTRL+c")
DSTR(18, "CTRL+f")
DSTR(19, "CTRL+s")
DSTR(20, "CTRL+t")
DSTR(21, "CTRL+v")
DSTR(22, "CTRL+w")
DSTR(23, "CTRL+x")
DSTR(24, "DOWN")
DSTR(25, "ENTER")
DSTR(26, "F5")
DSTR(27, "LEFT")
DSTR(28, "RIGHT")
DSTR(29, "SEQ:[\"CTRL+ALT+SHIFT+BACKSPACE\",\"RIGHT\",\"DOWN\",\"RIGHT\",\"DOWN\",\"ENTER\"]")
DSTR(30, "SEQ:[\"CTRL+k\",\"c\"]")
DSTR(31, "SEQ:[\"CTRL+k\",\"k\"]")
DSTR(32, "SEQ:[\"CTRL+k\",\"l\"]")
DSTR(33, "SEQ:[\"CTRL+k\",\"u\"]")
DSTR(34, "SEQ:[\"ENTER\",\"TEXT:XD\"]")
DSTR(35, "TEXT:230998")
DSTR(36, "TEXT:Alpha key12")
DSTR(37, "TEXT:close one")
DSTR(38, "TEXT:defending")
DSTR(39, "TEXT:i got it")
DSTR(40, "TEXT:my bad")
DSTR(41, "TEXT:nice shot")
DSTR(42, "TEXT:nooooo!")
DSTR(43, "TEXT:take the shot")
DSTR(44, "TEXT:thanks")
DSTR(45, "UP")
#undef DSTR

// Array of PROGMEM pointers
static const char* const DictPROGMEM[] PROGMEM = {
  d_0,d_1,d_2,d_3,d_4,d_5,d_6,d_7,d_8,d_9,d_10,d_11,d_12,d_13,d_14,d_15,d_16,d_17,d_18,d_19,d_20,d_21,d_22,d_23,
  d_24,d_25,d_26,d_27,d_28,d_29,d_30,d_31,d_32,d_33,d_34,d_35,d_36,d_37,d_38,d_39,d_40,d_41,d_42,d_43,d_44,d_45
};
static const uint16_t DictPROGMEM_Count = sizeof(DictPROGMEM)/sizeof(DictPROGMEM[0]);

// Built-in maps (same as your packed.json numbers). 0xFF means "no action".
static const uint8_t K_Default[4][16] PROGMEM = {
/* mode 0 */ {17,23,18,26,13,12,22,20,  0,1,21,16,36,19,6,3},
/* mode 1 */ {17,15,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,9,8,29,0xFF,0xFF,25},
/* mode 2 */ {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, 0xFF,11,10,0xFF,0xFF,0xFF,0xFF,0xFF},
/* mode 3 */ {14,41,30,33,44,39,43,38, 31,32,30,40,42,37,35,34}
};

static const uint8_t E_Default[4][4] PROGMEM = {
/* mode 0 */ {5,7,4,2},   // A+,A-,B+,B-
/* mode 1 */ {27,28,45,24},
/* mode 2 */ {27,28,27,28},
/* mode 3 */ {24,45,24,45}
};

// Copy defaults from PROGMEM to RAM maps; use PROGMEM dict by flag.
static bool useProgmemDict = false;
static void loadBuiltInDefaultsToRAM() {
  memcpy_P(K, K_Default, sizeof(K));
  memcpy_P(Emap, E_Default, sizeof(Emap));
  dictCount = DictPROGMEM_Count;
  useProgmemDict = true;
  hostConfigLoaded = false;
}

// =============== Host apply (BINCFG payload) ===============
static bool applyPackedConfig(const uint8_t* buf, size_t len) {
  freeHostPool();

  if (len < sizeof(PackedHeader) + sizeof(K) + sizeof(Emap)) return false;
  const PackedHeader* h = (const PackedHeader*)buf;
  if (h->magic != CFG_MAGIC || h->version != 1) return false;

  size_t off = sizeof(PackedHeader);
  memcpy(K, buf + off, sizeof(K));    off += sizeof(K);
  memcpy(Emap, buf + off, sizeof(Emap)); off += sizeof(Emap);

  dictCount = h->dictCount;
  if (dictCount > 128) return false;
  if (off > len) return false;

  // Remaining bytes = NUL-terminated strings pool
  size_t poolLen = len - off;
  strPool = (char*)malloc(poolLen + 1);
  if (!strPool) return false;
  memcpy(strPool, buf + off, poolLen);
  strPool[poolLen] = 0;

  // Build DictRAM[] pointers
  char* p = strPool;
  for (uint16_t i = 0; i < dictCount; i++) {
    DictRAM[i] = p;
    while (*p) p++;
    p++; // skip NUL
    if ((size_t)(p - strPool) > poolLen + 1) { freeHostPool(); return false; }
  }

  hostConfigLoaded = true;
  useProgmemDict = false;
  return true;
}

// =============== Boot wait for host push ===============
static bool waitForHostConfig(unsigned long ms_timeout = 6000) {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0) < 1500) { /* wait for USB */ }
  Serial.println(F("READY")); // host listens for this

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
          bool ok = (got == n) && applyPackedConfig(buf, (size_t)n);
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
  }
  return false; // nothing received
}

// ============================= Actions =============================
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

// ===== Dict lookup (handles PROGMEM or host RAM) =====
static String dictString(uint8_t idx) {
  if (idx==0xFF || idx>=dictCount) return String("");
  if (hostConfigLoaded) {
    const char* p = DictRAM[idx];
    return p ? String(p) : String("");
  } else {
    // Fetch PROGMEM pointer then copy to temp buffer
    PGM_P p = (PGM_P)pgm_read_ptr(&DictPROGMEM[idx]);
    static char buf[192]; // long enough for longest sequence/text above
    strcpy_P(buf, p);
    return String(buf);
  }
}

// ===== Resolve actions from maps =====
static int keyLabelToIndex(char label) {
  if (label >= '0' && label <= '9') return label - '0';
  if (label >= 'A' && label <= 'F') return 10 + (label - 'A');
  if (label >= 'a' && label <= 'f') return 10 + (label - 'a');
  return -1;
}
static String getKeyAction(uint8_t mode, char keyLabel) {
  int ix = keyLabelToIndex(keyLabel);
  if (ix < 0) return String("");
  uint8_t di = K[mode][ix];
  return dictString(di);
}
static String getEncoderAction(uint8_t mode, const char* which) {
  int slot = (which[0]=='A' && which[1]=='+') ? 0 :
             (which[0]=='A' && which[1]=='-') ? 1 :
             (which[0]=='B' && which[1]=='+') ? 2 :
             (which[0]=='B' && which[1]=='-') ? 3 : -1;
  if (slot < 0) return String("");
  uint8_t di = Emap[mode][slot];
  return dictString(di);
}

// ============================= Helpers =============================
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

// ============================= Setup / Loop =============================
void setup() {
  pinMode(ModeButton, INPUT_PULLUP);

  // If mode button held at boot, wait longer for host push
  bool got = false;
  if (digitalRead(ModeButton) == LOW) got = waitForHostConfig(10000);
  else                                got = waitForHostConfig(3000);

  // Init hardware
  pinMode(LED_BUILTIN,OUTPUT);
  pinMode(LedArd1, OUTPUT); digitalWrite(LedArd1, HIGH);
  pinMode(LedArd2, OUTPUT); digitalWrite(LedArd2,HIGH);
  pinMode(Mode1,OUTPUT); digitalWrite(Mode1,LOW);
  pinMode(Mode2,OUTPUT); digitalWrite(Mode2,LOW);

  Consumer.begin();
  Mouse.begin();

  if (!got) {
    loadBuiltInDefaultsToRAM(); // use PROGMEM dict + RAM maps
  } else {
    // Host config loaded to RAM already
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
