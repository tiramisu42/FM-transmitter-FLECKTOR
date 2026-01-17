#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Si4713.h>
#include <Preferences.h>
#include <cstring>
#include "bitmaps.h"  

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define BTN_UP     2
#define BTN_DOWN   1
#define BTN_OK     10
#define BTN_BACK   0


#define SI4713_RESET 3
Adafruit_Si4713 radio = Adafruit_Si4713(SI4713_RESET);

enum UiState {
  UI_SPLASH,
  UI_MENU,
  UI_FM_MENU,
  UI_FM_TX
};

UiState uiState = UI_SPLASH;
uint32_t splashStart;
bool splashDrawn = false;

const char* menuItems[] = {"FM","FUNC","FUNC","FUNC"};
const uint8_t MENU_COUNT = 4;
int8_t menuIndex = 0;
int8_t prevIndex = 0;
int animX = 0;
int animDir = 0;

const char* fmMenuItems[] = {"FM TX","FUNC","FUNC","FUNC"};
const uint8_t FM_MENU_COUNT = 4;
int8_t fmMenuIndex = 0;
int8_t fmPrevIndex = 0;
int fmAnimY = 0;
int fmAnimDir = 0;

Preferences prefs;
uint16_t fmFreq = 10230;
bool radioPaused = false;


int animY[4] = {0,0,0,0};
int animDirDigit[4] = {0,0,0,0};
int prevDigits[4] = {1,0,2,3};

// RDS
const char* rdsStation = "PizdaFM "; //8 символов
const char* rdsBuffer = "this frequency has been hacked by Tiramisu =)";

void splitFreq(uint16_t freq, int* d){
  d[0] = freq / 10000;
  d[1] = (freq / 1000) % 10;
  d[2] = (freq / 100) % 10;
  d[3] = (freq / 10) % 10;
}

void startDigitAnim(uint16_t oldF, uint16_t newF){
  int o[4], n[4];
  splitFreq(oldF, o);
  splitFreq(newF, n);

  for(int i=0;i<4;i++){
    if(o[i] != n[i]){
      prevDigits[i] = o[i];
      animY[i] = -32;
      animDirDigit[i] = 1;
    } else {
      animDirDigit[i] = 0;
      animY[i] = 0;
    }
  }
}


void drawSplash(){
  display.clearDisplay();
  display.drawBitmap(0, 7, bitmap_splash, 127, 20, SSD1306_WHITE);
  display.display();
}

void drawCenteredText(const char* t, int xOff){
  int w = strlen(t) * 12;
  int x = (SCREEN_WIDTH - w) / 2 + xOff;
  display.setTextSize(2);
  display.setCursor(x, 6);
  display.print(t);
}

void drawMenu(){
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);

  if(animDir == 0){
    drawCenteredText(menuItems[menuIndex], 0);
  } else {
    drawCenteredText(menuItems[prevIndex], animX);
    drawCenteredText(menuItems[menuIndex],
                     animX - animDir * SCREEN_WIDTH);
  }
  display.display();
}

void handleMenuButtons(){
  static bool lu=HIGH, ld=HIGH, lo=HIGH;
  bool u=digitalRead(BTN_UP);
  bool d=digitalRead(BTN_DOWN);
  bool o=digitalRead(BTN_OK);

  if(animDir == 0){
    if(lu && !u){
      prevIndex = menuIndex;
      menuIndex = (menuIndex + 1) % MENU_COUNT;
      animDir = 1; animX = 0;
    }
    if(ld && !d){
      prevIndex = menuIndex;
      menuIndex = (menuIndex + MENU_COUNT - 1) % MENU_COUNT;
      animDir = -1; animX = 0;
    }
    if(lo && !o){
      if(menuIndex == 0) uiState = UI_FM_MENU;
      delay(200);
    }
  }
  lu=u; ld=d; lo=o;
}

void updateMenuAnim(){
  if(animDir == 0) return;
  animX += animDir * 16;
  if(abs(animX) >= SCREEN_WIDTH){
    animDir = 0;
    animX = 0;
  }
}

void drawFMMenuItem(const char* t, int y){
  int w = strlen(t) * 12;
  int x = (SCREEN_WIDTH - w) / 2;
  display.setTextSize(2);
  display.setCursor(x, 6 + y);
  display.print(t);
}

void drawFMMenu(){
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);

  if(fmAnimDir == 0){
    drawFMMenuItem(fmMenuItems[fmMenuIndex], 0);
  } else {
    drawFMMenuItem(fmMenuItems[fmPrevIndex], fmAnimY);
    drawFMMenuItem(
      fmMenuItems[fmMenuIndex],
      fmAnimY - fmAnimDir * SCREEN_HEIGHT
    );
  }
  display.display();
}

void handleFMMenuButtons(){
  static bool lu=HIGH, ld=HIGH, lo=HIGH;
  bool u=digitalRead(BTN_UP);
  bool d=digitalRead(BTN_DOWN);
  bool o=digitalRead(BTN_OK);

  if(fmAnimDir == 0){
    if(lu && !u){
      fmPrevIndex = fmMenuIndex;
      fmMenuIndex = (fmMenuIndex + 1) % FM_MENU_COUNT;
      fmAnimDir = 1; fmAnimY = 0;
    }
    if(ld && !d){
      fmPrevIndex = fmMenuIndex;
      fmMenuIndex = (fmMenuIndex + FM_MENU_COUNT - 1) % FM_MENU_COUNT;
      fmAnimDir = -1; fmAnimY = 0;
    }
    if(lo && !o){
      if(fmMenuIndex == 0) uiState = UI_FM_TX;
      delay(200);
    }
  }

  if(!digitalRead(BTN_BACK)){
    uiState = UI_MENU;
    delay(200);
  }

  lu=u; ld=d; lo=o;
}

void updateFMMenuAnim(){
  if(fmAnimDir == 0) return;
  fmAnimY += fmAnimDir * 8;
  if(abs(fmAnimY) >= SCREEN_HEIGHT){
    fmAnimDir = 0;
    fmAnimY = 0;
  }
}

void startRadio(){
  pinMode(SI4713_RESET, OUTPUT);
  digitalWrite(SI4713_RESET, HIGH);
  delay(100);

  if(!radio.begin()){
    display.clearDisplay();
    display.setCursor(0,0);
    display.print("SI4713 ERROR");
    display.display();
    while(1);
  }

  radio.setTXpower(115);
  radio.tuneFM(fmFreq);

  
  radio.beginRDS();
  radio.setRDSstation(rdsStation);
  
}

void handleFMButtons(){
  static bool lastUp=HIGH, lastDown=HIGH, lastOk=HIGH;

  bool up = digitalRead(BTN_UP);
  bool down = digitalRead(BTN_DOWN);
  bool ok = digitalRead(BTN_OK);

  if(lastUp == HIGH && up == LOW){
    uint16_t oldFreq = fmFreq;
    fmFreq += 10;
    if(fmFreq > 10800) fmFreq = 8750;
    radio.tuneFM(fmFreq);
    startDigitAnim(oldFreq, fmFreq);
  }

  if(lastDown == HIGH && down == LOW){
    uint16_t oldFreq = fmFreq;
    if(fmFreq <= 8750) fmFreq = 10800;
    else fmFreq -= 10;
    radio.tuneFM(fmFreq);
    startDigitAnim(oldFreq, fmFreq);
  }

  if(lastOk == HIGH && ok == LOW){
    radioPaused = !radioPaused;
    radio.setTXpower(radioPaused ? 0 : 110);
  }

  if(!digitalRead(BTN_BACK)){
    radio.setTXpower(0);
    radioPaused = true;
    uiState = UI_FM_MENU;
    delay(200);
  }

  lastUp = up; lastDown = down; lastOk = ok;
}

void drawFMScreen(){
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);

  display.setTextSize(1);
  display.setCursor(0,0);
  display.print("FM TX");

  int digits[4];
  splitFreq(fmFreq, digits);
  int xPos = 40;

  for(int i=0;i<4;i++){
    display.setTextSize(2);
    if(animDirDigit[i]){
      display.setCursor(xPos, 8 + animY[i]);
      display.print(prevDigits[i]);
      display.setCursor(xPos, 8 + animY[i] - 32);
      display.print(digits[i]);
      animY[i] += 4;
      if(animY[i] >= 0){
        animY[i] = 0;
        animDirDigit[i] = 0;
        prevDigits[i] = digits[i];
      }
    } else {
      display.setCursor(xPos,8);
      display.print(digits[i]);
      prevDigits[i] = digits[i];
    }
    xPos += 12;
    if(i==2){
      display.print(".");
      xPos += 12;
    }
  }

  if(radioPaused){
    display.drawBitmap(0, 24, icon_play, 8, 8, SSD1306_WHITE);
  } else {
    display.drawBitmap(0, 24, icon_pause, 8, 8, SSD1306_WHITE);
  }

  display.display();
}

void setup(){
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);

  Wire.begin(8,9);
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);

  prefs.begin("fm", false);
  fmFreq = prefs.getUShort("freq", 10230);

  splashStart = millis();
  startRadio();
}

void loop(){
  if(uiState == UI_SPLASH){
    if(!splashDrawn){
      drawSplash();
      splashDrawn = true;
    }
    if(millis() - splashStart > 3000){
      display.clearDisplay();
      display.display();
      uiState = UI_MENU;
    }
    return;
  }

  static uint32_t lastRDS = 0;
  if(millis() - lastRDS > 1000){
    lastRDS = millis();
    radio.setRDSstation(rdsStation);
    radio.setRDSbuffer(rdsBuffer);
  }

  switch(uiState){
    case UI_MENU:
      handleMenuButtons();
      updateMenuAnim();
      drawMenu();
      break;

    case UI_FM_MENU:
      handleFMMenuButtons();
      updateFMMenuAnim();
      drawFMMenu();
      break;

    case UI_FM_TX:
      handleFMButtons();
      drawFMScreen();
      break;
  }
}



