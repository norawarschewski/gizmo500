#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <AccelStepper.h>
#include <DS1307.h>

// OLED setup
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
#define OLED_ADDR 0x3C

// RTC
DS1307 rtc(0x68, 4, 5, &Wire);

// Stepper
#define MotorInterfaceType 4
AccelStepper stepper(MotorInterfaceType, 8, 10, 9, 11);
const int HALF_TURN_STEPS = 1024;

// Buttons
const int SET_BTN = 2;
const int UP_BTN = 1;
const int DOWN_BTN = 0;

// Feeding time configuration
int feedHour = 0, feedMinute = 0;
bool settingHour = true;
bool inSettingMode = false;
bool waitingForClose = false;
bool doorIsOpen = false;

enum Mode { SetFeeding, ManualControl };
Mode currentMode = SetFeeding;

unsigned long lastDisplayUpdate = 0;

void setup() {
  Serial.begin(9600);
  Wire.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) while (1);
  rtc.begin();
  rtc.setFormat(24);
  rtc.setAMPM(0);

  // rtc.setTime("11:42:00");  // Set once if needed
  // rtc.setDate("18/6/25");

  pinMode(SET_BTN, INPUT_PULLUP);
  pinMode(UP_BTN, INPUT_PULLUP);
  pinMode(DOWN_BTN, INPUT_PULLUP);

  stepper.setMaxSpeed(500);
  stepper.setAcceleration(200);

  displayStatus("Set Feed Time");
}

void loop() {
  static bool lastSetState = HIGH;
  bool setPressed = (digitalRead(SET_BTN) == LOW);

  if (waitingForClose) {
    if (setPressed && lastSetState == HIGH) {
      displayStatus("Closing...");
      rotateStepper(false);
      displayStatus("Closed");
      doorIsOpen = false;
      waitingForClose = false;
    }
    lastSetState = setPressed;
    return;
  }

  if (setPressed && lastSetState == HIGH && !waitingForClose) {
    if (inSettingMode) {
      if (settingHour) settingHour = false;
      else {
        displayStatus("Feed Time Set");
        settingHour = true;
        inSettingMode = false;
      }
    } else {
      currentMode = (Mode)((currentMode + 1) % 2);
      if (currentMode == SetFeeding) {
        inSettingMode = true;
        settingHour = true;
        displayStatus("Set Feed Time");
      } else {
        displayStatus("Manual Mode");
      }
    }
    delay(300);
  }

  lastSetState = setPressed;

  switch (currentMode) {
    case SetFeeding:
      if (inSettingMode) handleFeedingTimeSetting();
      break;
    case ManualControl:
      handleManualControl();
      checkFeedingTime();
      break;
  }

  if (!waitingForClose && millis() - lastDisplayUpdate > 1000 && currentMode == ManualControl) {
    displayCurrentTime();
    lastDisplayUpdate = millis();
  }
}

void handleFeedingTimeSetting() {
  if (digitalRead(UP_BTN) == LOW) {
    if (settingHour && feedHour < 23) feedHour++;
    else if (!settingHour && feedMinute < 59) feedMinute++;
    delay(300);
  }
  if (digitalRead(DOWN_BTN) == LOW) {
    if (settingHour && feedHour > 0) feedHour--;
    else if (!settingHour && feedMinute > 0) feedMinute--;
    delay(300);
  }
  displaySetting("Set Feed", feedHour, feedMinute);
}

void handleManualControl() {
  bool upPressed = digitalRead(UP_BTN) == LOW;
  bool downPressed = digitalRead(DOWN_BTN) == LOW;

  if (upPressed && downPressed) {
    if (!doorIsOpen) {
      displayStatus("Opening...");
      rotateStepper(true);
      displayStatus("Opened");
      doorIsOpen = true;
    } else {
      displayStatus("Closing...");
      rotateStepper(false);
      displayStatus("Closed");
      doorIsOpen = false;
    }
    delay(500);
  }
}

void checkFeedingTime() {
  if (rtc.getTime()) {
    if (rtc.getHour() == feedHour && rtc.getMinute() == feedMinute && rtc.getSeconds() == 0) {
      displayStatus("Feeding...");
      rotateStepper(true);
      displayStatus("Press OK to close");
      doorIsOpen = true;
      waitingForClose = true;
      delay(1000);
    }
  }
}

void rotateStepper(bool forward) {
  stepper.setCurrentPosition(0);
  stepper.moveTo(forward ? HALF_TURN_STEPS : -HALF_TURN_STEPS);
  while (stepper.distanceToGo() != 0) stepper.run();
}

void displayCurrentTime() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.print("Time: ");
  display.printf("%02d:%02d\n", rtc.getHour(), rtc.getMinute());
  display.print("Food: ");
  display.printf("%02d:%02d", feedHour, feedMinute);
  display.display();
}

void displaySetting(String label, int hr, int min) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.print(label);
  display.println(settingHour ? " (H)" : " (M)");
  display.printf("%02d:%02d", hr, min);
  display.display();
}

void displayStatus(String msg) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.println(msg);
  display.display();
}
