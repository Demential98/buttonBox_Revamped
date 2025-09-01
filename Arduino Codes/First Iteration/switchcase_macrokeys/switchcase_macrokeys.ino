/*******************************************************************
 * A multi-mode Macro keyboard with Arduino Pro Micro using      
 * row column matrix. 
 * (c) 2021 Demential
 *******************************************************************/
/*
  0-1   2-3   4-5   6-7   8-9             leve
      A     B     C    D                bottoni
  14                  15          bottoni encoder rotativi
*/


//
#include "HID-Project.h"
#include <Keypad.h>
#include <Encoder.h> 




//Encoder definizione pin
Encoder RotaryEncoderA(18, 15); //the LEFT encoder (encoder A)
Encoder RotaryEncoderB(14, 16);  //the RIGHT encoder (encoder B)

//Encoder Startup
long positionEncoderA  = -999; //encoderA LEFT position variable
long positionEncoderB  = -999; //encoderB RIGHT position variable



//Definizione Matrice Bottoni
const byte ROWS = 4; //four rows
const byte COLS = 4; //four columns

char keys[ROWS][COLS] = {  //organizzazione da sx a dx
  {'0', '1', '2', '3'},  // 0-1 leva n1     2-3 leva n2
  {'4', '5', '6', '7'},  // 4-5 leva n3    6-7 leva n4
  {'8', '9', 'A', 'B'},  // 8-9 leva n5     A / 10 bottone n1   B / 11 bottone n2
  {'C', 'D', 'E', 'F'},  // C / 12 bottone n3 D / 13 bottone n4   E / 14 encoder sx  F / 15 encoder dx
};


//pin da inserire
byte rowPins[ROWS] = {0, 2, 3, 4}; //righe matrice
byte colPins[COLS] = {5, 6, 7, 8 }; //colonne matrice
Keypad keypad = Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS );



//Inizializzazione variabili bottone Controllo Mode
int modePushCounter = 0;   // counter for the number of button presses
int buttonState = 0;         // current state of the button
int lastButtonState = 0;     // previous state of the button
const int ModeButton = A1; //19



//LED CONTROL
const int Mode1= 9;
const int Mode2= 10;
const int LedArd1 = 17;
const int LedArd2 = 30;
const int Potenziometro = A2; //20
int ValorePot = 0;
int lum = 0;



//Startup
void setup() {
  // comunicazione seriale
  Serial.begin(9600); //debug
  // inizializzazione bottone mode 
  pinMode(ModeButton, INPUT_PULLUP);  
  // inizializzazione Led's
  pinMode(LED_BUILTIN,OUTPUT);
  pinMode(LedArd1, OUTPUT); digitalWrite(LedArd1, HIGH);
  pinMode(LedArd2, OUTPUT); digitalWrite(LedArd2,HIGH);
  pinMode(Mode1,OUTPUT); digitalWrite(Mode1,LOW);
  pinMode(Mode2,OUTPUT); digitalWrite(Mode2,LOW);
  
  
  Consumer.begin();
  Mouse.begin();
}


void loop() {
ValorePot = analogRead(Potenziometro);
lum = ValorePot/4;
char key = keypad.getKey();     
encoderA();
encoderB();
  
checkModeButton();

  switch (modePushCounter) { // switch between keyboard configurations:
    case 0:    //  Application Alpha or MODE 0    //Due LED Spenti                                                  //General Purpose                    

      digitalWrite(Mode1,LOW); digitalWrite(Mode2,LOW); //indicate what mode is loaded
       if (key) {
    Serial.println(key);
    switch (key) {                                                        
      case '0': //copy                                      leva 1A
       Keyboard.press(KEY_LEFT_CTRL);
       Keyboard.press('c'); break;
      case '1':  //cut                                      leva 1B 
       Keyboard.press(KEY_LEFT_CTRL);
       Keyboard.press('x'); break;
      case '2': //trova                                     leva 2A
       Keyboard.press(KEY_LEFT_CTRL);
       Keyboard.press('f'); break;
      case '3': //aggiorna                                  leva 2B
        Keyboard.press(KEY_F5); break;
      case '4'://                                           leva 3A
        Keyboard.press(KEY_LEFT_CTRL);
       Keyboard.press(KEY_PAGE_UP); break;
      case '5'://                                           leva 3B
        Keyboard.press(KEY_LEFT_CTRL);
       Keyboard.press(KEY_PAGE_DOWN); break;
      case '6'://                                           leva 4A
        Keyboard.press(KEY_LEFT_CTRL);
        Keyboard.press('w'); break;
      case '7'://                                           leva 4B
       Keyboard.press(KEY_LEFT_CTRL);
        Keyboard.press('t'); break;
      case '8'://                                           leva 5A
        Consumer.write(CONSUMER_BROWSER_BACK); break;
      case '9'://                                           leva 5B
        Consumer.write(CONSUMER_BROWSER_FORWARD); break;
      case 'A'://                                           botttone 1
        Keyboard.press(KEY_LEFT_CTRL);
        Keyboard.press('v'); break;
      case 'B'://                                           bottone 2
        Keyboard.press(KEY_LEFT_CTRL);
        Keyboard.press('a'); break;
      case 'C'://                                           bottone 3                                  
        Keyboard.println("Alpha key12"); break;
      case 'D'://                                           bottone 4
        Keyboard.press(KEY_LEFT_CTRL); 
        Keyboard.press('s'); break;
      case 'E'://                                           encoder sx
        Consumer.write(MEDIA_VOLUME_MUTE); break;
      case 'F'://                                           encoder dx
        Consumer.write(MEDIA_PLAY_PAUSE); break;
    }
    delay(100); Keyboard.releaseAll(); // this releases the buttons 
  }
      break;
      
    case 1:    //  Application Beta or MODE 1   //LED Destro Acceso Led Sinistro Spento                             //Excel                                 
      
      analogWrite(Mode1,lum); digitalWrite(Mode2,LOW);
      if (key) {
    Serial.println(key);
    switch (key) {
      case '0'://copia                                      leva 1A
       Keyboard.press(KEY_LEFT_CTRL);
       Keyboard.press('c'); break;
      case '1'://incolla                                    leva 1B  
       Keyboard.press(KEY_LEFT_CTRL);
       Keyboard.press('V'); break;
      case '2'://                                           leva 2A
        Keyboard.println("Beta key2"); break;
      case '3'://                                           leva 2B
        Keyboard.println("Beta key3"); break;
      case '4'://                                           leva 3A
        Keyboard.println("Beta key4"); break;
      case '5'://                                           leva 3B
        Keyboard.println("Beta key5"); break;
      case '6'://                                           leva 4A
        Keyboard.println("Beta key6"); break;
      case '7'://                                           leva 4B
        Keyboard.println("Beta key7"); break;
      case '8'://                                           leva 5A
        Keyboard.println("Beta key8"); break;
      case '9'://                                           leva 5B
        Keyboard.println("Beta key9"); break;
      case 'A'://incolla senza formattazione                bottone 1
        Keyboard.press(KEY_LEFT_CTRL);
        Keyboard.press(KEY_LEFT_ALT); 
        Keyboard.press(KEY_LEFT_SHIFT);
        Keyboard.press('v'); break; 
      case 'B'://dividi celle                               bottone 2
        Keyboard.press(KEY_LEFT_CTRL);
        Keyboard.press(KEY_LEFT_ALT); 
        Keyboard.press(KEY_LEFT_SHIFT);
        Keyboard.press(KEY_RETURN); break;
      case 'C'://esegui macro                               bottone 3
        Keyboard.press(KEY_LEFT_CTRL);
        Keyboard.press(KEY_LEFT_ALT); 
        Keyboard.press(KEY_LEFT_SHIFT);
        Keyboard.press(KEY_BACKSPACE); 
        Keyboard.releaseAll();
        delay(100);
        Keyboard.write(KEY_RIGHT_ARROW);
        Keyboard.write(KEY_DOWN_ARROW);
        Keyboard.write(KEY_RIGHT_ARROW);
        Keyboard.write(KEY_DOWN_ARROW);
        Keyboard.write(KEY_RETURN);break;
      case 'D'://                                           bottone 4
        Keyboard.println("Beta key13"); break;
      case 'E'://                                           encoder sx 
        Keyboard.press(KEY_RETURN); break;
      case 'F'://                                           encoder dx
        Keyboard.press(KEY_RETURN); break;
    }
    delay(100); Keyboard.releaseAll(); // this releases the buttons 
  }
      break;
      
    case 2:    // Application Delta or MODE 2   //Led Sinistro Acceso Led Destro Spento                             //Photoshop
      digitalWrite(Mode1,LOW); analogWrite(Mode2,lum);
    if (key) {
    Serial.println(key);
    switch (key) {
      case '0'://                                           leva 1A
        Keyboard.println("Delta key0"); break;
      case '1'://                                           leva 1B  
        Keyboard.println("Delta key1"); break;
      case '2'://                                           leva 2A 
        Keyboard.println("Delta key2"); break;
      case '3'://                                           leva 2B
        Keyboard.println("Delta key3"); break;
      case '4'://                                           leva 3A
        Keyboard.println("Delta key4"); break;
      case '5'://                                           leva 3B
        Keyboard.println("Delta key5"); break;
      case '6'://                                           leva 4A
        Keyboard.println("Delta key6"); break;
      case '7'://                                           leva 4B
        Keyboard.println("Delta key7"); break;
      case '8'://                                           leva 5A
        Keyboard.println("Delta key8"); break;
      case '9'://dimensione immagine                        leva 5B
        Keyboard.press(KEY_LEFT_ALT); 
        Keyboard.press(KEY_LEFT_CTRL);
        Keyboard.press('i');  break;
      case 'A'://export as jpg                              bottone 1
        Keyboard.press(KEY_LEFT_ALT);
        Keyboard.press(KEY_LEFT_SHIFT); 
        Keyboard.press(KEY_LEFT_CTRL);
        Keyboard.press('w');break;
      case 'B'://                                           bottone 2
        Keyboard.println("Delta key11"); break;
      case 'C'://                                           bottone 3
        Keyboard.println("Delta key12"); break;
      case 'D'://                                           bottone 4
        Keyboard.println("Delta key13"); break;
      case 'E'://                                           encoder sx
        Keyboard.println("Delta key14"); break;
      case 'F'://                                           encoder dx
        Keyboard.println("Delta key15"); break;
    }
    delay(100); Keyboard.releaseAll(); // this releases the buttons 
  }
      break;
      
    case 3:    // Application Gamma or MODE 3   //Due Led Accesi
      analogWrite(Mode1,lum); analogWrite(Mode2,lum);
    if (key) {
    Serial.println(key);
    switch (key) {
      case '0'://                                           leva 1A
        Keyboard.press(KEY_LEFT_CTRL);
Keyboard.press(KEY_LEFT_SHIFT);
Keyboard.press(KEY_F9);
break;
      case '1'://                                           leva 1B 
        Keyboard.println("nice shot"); break;
      case '2':
Keyboard.press(KEY_LEFT_CTRL);
Keyboard.press('k');
Keyboard.release('k');
Keyboard.press('c');
break;
case '3':
Keyboard.press(KEY_LEFT_CTRL);
Keyboard.press('k');
Keyboard.release('k');
Keyboard.press('u');
break;

      case '4'://                                           leva 3A
        Keyboard.println("thanks"); break;
      case '5'://                                           leva 3B
        Keyboard.println("i got it"); break;
      case '6'://                                           leva 4A
        Keyboard.println("take the shot"); break;
      case '7'://                                           leva 4B
        Keyboard.println("defending"); break;
      case '8':
Keyboard.press(KEY_LEFT_CTRL);
Keyboard.press('k');
Keyboard.release('k');
Keyboard.press('k');
break;
case '9':
Keyboard.press(KEY_LEFT_CTRL);
Keyboard.press('k');
Keyboard.release('k');
Keyboard.press('l');
break;

      case 'A'://                                           bottone 1
        Keyboard.press(KEY_LEFT_CTRL);
        Keyboard.press('k');
        Keyboard.release('k');
        Keyboard.press('c');break;
      case 'B'://                                           bottone 2
        Keyboard.println("my bad"); break;
      case 'C'://                                           bottone 3
        Keyboard.println("nooooo!"); break;
      case 'D'://                                           bottone 4
        Keyboard.println("close one"); break;
      case 'E'://                                           encoder sx
        Keyboard.println("230998"); break;
      case 'F'://                                           encoder dx         
        Keyboard.write(KEY_RETURN);
        Keyboard.println("XD"); break;
    }
    delay(100); Keyboard.releaseAll(); // this releases the buttons 
  }
      break;
  }
  delay(1);  // delay in between reads for stability

}


//Controllo Bottone Mode 
void checkModeButton(){
  buttonState = digitalRead(ModeButton);
  if (buttonState != lastButtonState) {
    if (buttonState == LOW) { // se lo stato é cambiato il bottone é stato cliccato quindi aumenta il counter 
      modePushCounter++; 
     // Serial.println("pressed"); Serial.print("number of button pushes: "); Serial.println(modePushCounter); //debug
    } 
    delay(50);
  }
  lastButtonState = buttonState; // save the current state as the last state, for next time through the loop
   if (modePushCounter >3){ //reset the counter after 4 presses (remember we start counting at 0)
      modePushCounter = 0;}
}

//Controllo e Comandi Encoder
void encoderA(){
  long newPos = RotaryEncoderA.read()/4; //When the encoder lands on a valley, this is an increment of 4.
  
  if (newPos != positionEncoderA && newPos > positionEncoderA) {
    positionEncoderA = newPos;
    //Serial.println(positionEncoderA);
    
    switch(modePushCounter){
    case 0:                           //mode 0    Volume giú 
      Consumer.write(MEDIA_VOLUME_DOWN);break;
    case 1:                           //mode 1    Freccia SX
      Keyboard.press(KEY_LEFT_ARROW);
      Keyboard.release(KEY_LEFT_ARROW);break;     
    case 2:                           //mode 2    Freccia SX
      Keyboard.press(KEY_LEFT_ARROW);
      Keyboard.release(KEY_LEFT_ARROW);break; 
    case 3:                           //mode 3    Freccia Giú
      Keyboard.press(KEY_DOWN_ARROW);
      Keyboard.release(KEY_DOWN_ARROW);    break;
   }
 }
  if (newPos != positionEncoderA && newPos < positionEncoderA) {
    positionEncoderA = newPos;
  //Serial.println(positionEncoderA);
  switch(modePushCounter){
    case 0:                           //mode 0    Volume su
      Consumer.write(MEDIA_VOLUME_UP); break;
    case 1:                           //mode 1    Freccia DX
      Keyboard.press(KEY_RIGHT_ARROW);
      Keyboard.release(KEY_RIGHT_ARROW); break;
    case 2:                           //mode 2    Freccia DX
      Keyboard.press(KEY_RIGHT_ARROW);
      Keyboard.release(KEY_RIGHT_ARROW); break;
    case 3:                           //mode 3    Freccia Su 
      Keyboard.press(KEY_UP_ARROW);
      Keyboard.release(KEY_UP_ARROW); break;
  }
}
}

void encoderB(){
  long newPos = RotaryEncoderB.read()/4; //When the encoder lands on a valley, this is an increment of 4.
  if (newPos != positionEncoderB && newPos > positionEncoderB) {
    positionEncoderB = newPos;
    //Serial.println(positionEncoderB);
   switch(modePushCounter){
   case 0:                           //mode 0    Indietro tra tracce musicali
      Consumer.write(MEDIA_PREVIOUS); break;
    case 1:                           //mode 1    Freccia Su 
      Keyboard.press(KEY_UP_ARROW);
      Keyboard.release(KEY_UP_ARROW); break; 
    case 2:                           //mode 2    Freccia DX e SX
      Keyboard.press(KEY_LEFT_ARROW);
      Keyboard.release(KEY_LEFT_ARROW); break;
    case 3:                           //mode 3    Freccia Giú
      Keyboard.press(KEY_DOWN_ARROW);
      Keyboard.release(KEY_DOWN_ARROW);   break; 
   }
 }
  if (newPos != positionEncoderB && newPos < positionEncoderB) {
    positionEncoderB = newPos;
  //Serial.println(positionEncoderB);
  switch(modePushCounter){
    case 0:                           //mode 0    Avanti tra tracce musicali
      Consumer.write(MEDIA_NEXT); break;
    case 1:                           //mode 1    Freccia Giú
      Keyboard.press(KEY_DOWN_ARROW);
      Keyboard.release(KEY_DOWN_ARROW);   break;   
    case 2:                           //mode 2    Freccia DX
      Keyboard.press(KEY_RIGHT_ARROW);
      Keyboard.release(KEY_RIGHT_ARROW); break;
    case 3:                           //mode 3    Freccia Su 
      Keyboard.press(KEY_UP_ARROW);
      Keyboard.release(KEY_UP_ARROW); break;
  }
}
}
