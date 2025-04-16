#include <Arduino.h>
#include <U8g2lib.h>
#include <CheapStepper.h>
#include <Wire.h>
#include <RtcDS1307.h>

// old display
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// rtc module
RtcDS1307<TwoWire> Rtc(Wire);

// stepper motor
CheapStepper stepper(8, 9, 10, 11);

// pins
const int button1Pin = 3;
const int button2Pin = 4;
const int button3Pin = 5;
const int stopPin = 6;

// constants
const int slideDistance = 8600;
const int MAX_TIMER_COUNT = 50;
const int SCREEN_BLANK_DELAY = 50;
const int SCREEN_BLANK_EFFECT_DELAY = 30;

// variables
bool moveClockwise = true;
unsigned long moveStartTime = 0;
bool button3Pressed = false;
bool button2Pressed = false;
bool button1Pressed = false;
bool positionKnown = false;
bool showScreen = true;
int screenBlankDelayCount = 0;
int screenBlankEffectDelay = 0;
int buttonStatus = 0;
int doorStatus = 0;
int mode = 0;
int timerCount = 0;
int alarmHr = 0;
int alarmMin = 0;
int clockHr = 0;
int clockMin = 0;
long reading = 0;

// enums for modes and button status
enum Mode {
    ModeDoNothing,
    ModeDisplayInit,
    ModeInitPos,
    ModeDisplayOpening,
    ModeRunForOpen,
    ModeDisplayClosing,
    ModeRunForClose,
    ModeInitPosAchieved,
    ModeTimeForFood,
    ModeEndOfTimeForFood,
    ModeError
};

enum ButtonStatus {
    ButtonStatusOpenClose,
    ButtonStatusSetTime,
    ButtonStatusSetAlarm
};

// function prototypes
void handleButtons(const RtcDateTime& now);
void handleModes();
void updateDisplay(const RtcDateTime& now);
void resetStepperPins();
void handleModeInitPos();
void handleModeRunForOpen();
void handleModeRunForClose();
void handleModeDisplayOpening();
void handleModeDisplayClosing();
void printTimeAndAlarm(const RtcDateTime& dt, const RtcDateTime& alrm, String statusStr, long weight, int mode);

void setup() {
    pinMode(button1Pin, INPUT_PULLUP);
    pinMode(button2Pin, INPUT_PULLUP);
    pinMode(button3Pin, INPUT_PULLUP);
    pinMode(stopPin, INPUT_PULLUP);
    pinMode(LED_BUILTIN, OUTPUT);

    Serial.begin(9600);
    Serial.println("start");

    // initialize oled
    u8g2.begin();

    // initialize rtc
    Rtc.Begin();
    RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
    if (!Rtc.IsDateTimeValid()) {
        Rtc.SetDateTime(compiled);
    }
    if (!Rtc.GetIsRunning()) {
        Rtc.SetIsRunning(true);
    }
    Rtc.SetSquareWavePin(DS1307SquareWaveOut_Low);

    // initialize stepper
    stepper.setRpm(48);
    moveStartTime = millis();
}

void loop() {
  // get current time
  if (!Rtc.IsDateTimeValid()) {
      Serial.println("RTC lost confidence in the DateTime!");
  }

  RtcDateTime now = Rtc.GetDateTime();

  // update current time variables before using them
  clockMin = now.Minute();
  clockHr = now.Hour();

  // alarm time check
  if ((mode == ModeDoNothing || mode == ModeError) &&
      clockHr == alarmHr && clockMin == alarmMin && now.Second() == 0) {
      mode = ModeTimeForFood;
      Serial.println("ALARM TRIGGERED");
  }

  // handle buttons and states
  handleButtons(now);

  // handle modes
  handleModes();

  // update display
  updateDisplay(now);
}

// handle button presses
void handleButtons(const RtcDateTime& now) {
    int buttonState1 = digitalRead(button1Pin);
    int buttonState2 = digitalRead(button2Pin);
    int buttonState3 = digitalRead(button3Pin);
    int stopPinState = digitalRead(stopPin);

    if (buttonState3 == LOW) button3Pressed = false;
    if (buttonState3 == HIGH && !button3Pressed) {
        screenBlankDelayCount = 0;
        buttonStatus = (buttonStatus + 1) % 3; // cycle through button statuses
        button3Pressed = true;
    }

    if (buttonState2 == LOW) button2Pressed = false;
    if (buttonState2 == HIGH && !button2Pressed) {
        screenBlankDelayCount = 0;
        if (buttonStatus == ButtonStatusSetAlarm) {
            alarmMin = (alarmMin + 10) % 60;
        } else if (buttonStatus == ButtonStatusSetTime) {
            clockMin = (clockMin + 1) % 60;
            Rtc.SetDateTime(RtcDateTime(2019, 1, 21, now.Hour(), clockMin, 0));
        }
        button2Pressed = true;
    }

    if (buttonState1 == LOW) button1Pressed = false;
    if (buttonState1 == HIGH && !button1Pressed) {
        screenBlankDelayCount = 0;
        if (buttonStatus == ButtonStatusSetTime) {
            clockHr = (clockHr + 1) % 24;
            Rtc.SetDateTime(RtcDateTime(2019, 1, 21, clockHr, now.Minute(), 0));
        } else if (buttonStatus == ButtonStatusSetAlarm) {
            alarmHr = (alarmHr + 1) % 24;
        } else if (buttonStatus == ButtonStatusOpenClose) {
            if (mode == ModeDoNothing || mode == ModeError) {
                mode = positionKnown ? (doorStatus == DoorStatusClosed ? ModeDisplayOpening : ModeDisplayClosing) : ModeDisplayInit;
            }
        }
        button1Pressed = true;
    }

    if (stopPinState == HIGH && mode == ModeInitPos) {
        mode = ModeInitPosAchieved;
    }
}

// handle modes
void handleModes() {
    switch (mode) {
        case ModeInitPos:
            handleModeInitPos();
            break;
        case ModeRunForOpen:
            handleModeRunForOpen();
            break;
        case ModeRunForClose:
            handleModeRunForClose();
            break;
        case ModeDisplayOpening:
            handleModeDisplayOpening();
            break;
        case ModeDisplayClosing:
            handleModeDisplayClosing();
            break;
        case ModeInitPosAchieved:
            positionKnown = true;
            doorStatus = DoorStatusClosed;
            mode = ModeDisplayOpening;
            timerCount = 0;
            break;
        case ModeTimeForFood:
            mode = ModeDisplayOpening;
            delay(2000);
            buttonStatus = ButtonStatusOpenClose;
            break;
        default:
            break;
    }

    if (timerCount > MAX_TIMER_COUNT) {
        mode = ModeError;
    }
}

// update display
void updateDisplay(const RtcDateTime& now) {
    String info = "---";
    String status = "";

    switch (buttonStatus) {
        case ButtonStatusOpenClose: status = "Open/Close"; break;
        case ButtonStatusSetAlarm: status = "Set Alarm"; break;
        case ButtonStatusSetTime: status = "Set Time"; break;
    }

    switch (mode) {
        case ModeDoNothing: info = "..."; break;
        case ModeDisplayInit: info = "Initializing..."; break;
        case ModeInitPos: info = "Initializing..."; break;
        case ModeDisplayOpening: info = "Opening..."; break;
        case ModeRunForOpen: info = "Open"; break;
        case ModeDisplayClosing: info = "Closing..."; break;
        case ModeRunForClose: info = "Closed"; break;
        case ModeInitPosAchieved: info = "Ready"; break;
        case ModeTimeForFood: info = "Time for food"; break;
        case ModeError: info = "Error"; break;
    }

    if (screenBlankDelayCount < SCREEN_BLANK_DELAY) {
        screenBlankDelayCount++;
        showScreen = true;
    } else {
        showScreen = false;
    }

    if (showScreen) {
        printTimeAndAlarm(now, RtcDateTime(2000, 1, 1, alarmHr, alarmMin, 0), status, reading, mode);
    } else {
        screenBlankEffectDelay++;
        if (screenBlankEffectDelay == SCREEN_BLANK_EFFECT_DELAY) {
            printRandom();
            screenBlankEffectDelay = 0;
        }
    }
}

// node-specific handlers
void handleModeInitPos() {
    for (int s = 0; s < 8000; s++) {
        stepper.step(moveClockwise);
        if (digitalRead(stopPin) == HIGH) {
            mode = ModeInitPosAchieved;
            break;
        }
    }
    timerCount++;
}

void handleModeRunForOpen() {
    stepper.move(!moveClockwise, slideDistance);
    resetStepperPins();
    mode = ModeDoNothing;
    doorStatus = DoorStatusopen;
}

void handleModeRunForClose() {
    stepper.move(moveClockwise, slideDistance);
    resetStepperPins();
    mode = ModeDoNothing;
    doorStatus = DoorStatusClosed;
}

void handleModeDisplayOpening() {
    if (doorStatus == DoorStatusClosed) {
        mode = ModeRunForOpen;
        doorStatus = DoorStatusUnkonwn;
    }
}

void handleModeDisplayClosing() {
    mode = ModeRunForClose;
    doorStatus = DoorStatusUnkonwn;
}

// reset stepper pins
void resetStepperPins() {
    digitalWrite(8, LOW);
    digitalWrite(9, LOW);
    digitalWrite(10, LOW);
    digitalWrite(11, LOW);
}

// draw to oled 
void DrawToOled(int x, int y, const char *s)
{
  u8g2.firstPage();
  do {
    u8g2.drawStr(x,y,s);
    u8g2.drawStr(x,y+9,s);
  } while ( u8g2.nextPage() );
}

#define countof(a) (sizeof(a) / sizeof(a[0]))

void printDateTime(const RtcDateTime& dt)
{
    char datestring[20];
    snprintf_P(datestring, 
            countof(datestring),
            PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
            dt.Month(),
            dt.Day(),
            dt.Year(),
            dt.Hour(),
            dt.Minute(),
            dt.Second() );
    Serial.print(datestring);
    DrawToOled(2, 30, datestring);
}

void printRandom()
{
  int mode = 0;
  char modeString[2];
  int x,y;
    x = random(12, 120);
    y = random(16, 58);
    snprintf_P(modeString, 
            countof(modeString),
            PSTR("%01u"),
            mode);             
              u8g2.firstPage();
  do {   
      u8g2.drawStr(x,y,modeString);
      } while ( u8g2.nextPage() );   
}

void printTimeAndAlarm(const RtcDateTime& dt, const RtcDateTime& alrm, String statusStr, long weight, int mode)
{
    char datestring[10];
    char alarmstring[7];
    char weightString[4];
    char modeString[2];
    int x,y;
    x = 12;
    y = 23;

    snprintf_P(datestring, 
            countof(datestring),
            PSTR("%02u:%02u:%02u"),
            dt.Hour(),
            dt.Minute(),
            dt.Second() );
    Serial.print(datestring);

    snprintf_P(alarmstring, 
            countof(alarmstring),
            PSTR("%02u:%02u"),
            alrm.Hour(),
            alrm.Minute());


  int lenStatusString = statusStr.length();
  char statusstring[lenStatusString+1];
    
  statusStr.toCharArray(statusstring, lenStatusString+1);

    snprintf_P(modeString, 
            countof(modeString),
            PSTR("%01u"),
            mode);            


  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_9x15B_mf);
    u8g2.drawStr(x, y, statusstring);
    u8g2.drawStr(x,y+20,datestring);
    u8g2.setFont(u8g2_font_10x20_mf);
    u8g2.drawStr(x,y+40,alarmstring);
    u8g2.setFont(u8g2_font_10x20_mf);
    u8g2.setFont(u8g2_font_9x15_mf);
  } while ( u8g2.nextPage() );            
}

void ScreenBlank()
{
  u8g2.clear();
}
