#include "HardwareSerial.h"
#include "esp32-hal-gpio.h"
#include "esp32-hal-timer.h"
#include "esp32-hal.h"
#include "esp_attr.h"
#include <Arduino.h>
#include <cstdint>
#include <sys/types.h>

const uint8_t buzzer = 14;

const uint8_t selectors[3] = {15, 22, 23};
const uint8_t segments[7] = {4, 33, 32, 21, 19, 18, 5};

const uint64_t displayFrequency = 120;
const uint64_t checkingInterval = 150;

hw_timer_t *dispTimer = NULL;

volatile uint8_t number[3] = {0, 0, 0};

struct Button {
    uint8_t pin;
    bool enabled;
};

volatile Button btnA     = {25, true};
volatile Button btnB     = {26, true};
volatile Button btnC     = {27, true};
volatile Button btnBenar = {34, true};
volatile Button btnSalah = {12, true};
volatile Button btnReset = {13, true};

volatile Button* buttons[6] = {&btnA, &btnB, &btnC, &btnBenar, &btnSalah, &btnReset};

enum State {
    READY,
    WAIT,
    SCORING,
    FINISH,
};

const char* states[] = {
    "READY",
    "WAIT",
    "SCORING",
    "SCORE_INPUT",
};

enum Team {
    TEAM_A,
    TEAM_B,
    TEAM_C,
};

volatile State currentState = WAIT;
volatile Team answeringTeam;
volatile bool wrongOnce = false;
volatile bool showScore = false;
volatile uint8_t resetButtonCount = 0;

volatile uint64_t lastTimeResetPressed = 0;
volatile uint64_t currentTimeResetPressed = 0;

volatile uint8_t score[3] = {0, 0, 0};

const uint8_t patterns[14][7] = {
    {0, 0, 0, 0, 0, 0, 1}, // 0
    {1, 0, 0, 1, 1, 1, 1}, // 1
    {0, 0, 1, 0, 0, 1, 0}, // 2
    {0, 0, 0, 0, 1, 1, 0}, // 3
    {1, 0, 0, 1, 1, 0, 0}, // 4
    {0, 1, 0, 0, 1, 0, 0}, // 5
    {0, 1, 0, 0, 0, 0, 0}, // 6
    {0, 0, 0, 1, 1, 1, 1}, // 7
    {0, 0, 0, 0, 0, 0, 0}, // 8
    {0, 0, 0, 0, 1, 0, 0}, // 9
    {0, 0, 0, 1, 0, 0, 0}, // A
    {1, 1, 0, 0, 0, 0, 0}, // B
    {0, 1, 1, 0, 0, 0, 1},  // C
    {1, 1, 1, 1, 1, 1, 1}  // C
};

void checkLed();
void checkPattern();
void checkDisplayNum();
void displayNumber(uint8_t num[3]);
void displayNumberWithTimer();
void setupDisplayTimer();

void btnACallback();
void btnBCallback();
void btnCCallback();
void btnBenarCallback();
void btnSalahCallback();
void btnResetCallback();

void setup() {
    Serial.begin(9600);
    for (auto pin : selectors) pinMode(pin, OUTPUT);
    for (auto pin : segments) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
    for (auto btn : buttons) pinMode(btn->pin, INPUT);

    checkLed();
    checkPattern();

    setupDisplayTimer();
    checkDisplayNum();

    number[0] = 0;
    number[1] = 0;
    number[2] = 0;

    attachInterrupt(btnA.pin, btnACallback, FALLING);
    attachInterrupt(btnB.pin, btnBCallback, FALLING);
    attachInterrupt(btnC.pin, btnCCallback, FALLING);
    attachInterrupt(btnBenar.pin, btnBenarCallback, FALLING);
    attachInterrupt(btnSalah.pin, btnSalahCallback, FALLING);
    attachInterrupt(btnReset.pin, btnResetCallback, FALLING);
}

void loop() {
    static uint64_t now = 0;
    static uint64_t prev = 0;
    static uint64_t count = 0;
    count++;

    if (resetButtonCount == 2) {
        showScore = true;
        resetButtonCount = 0;
    } else if (millis() - lastTimeResetPressed > 500) {
        if (resetButtonCount == 1) {
            if (showScore) showScore = false;
            else {
                switch (currentState) {
                case WAIT:
                    currentState = READY;
                    break;
                case FINISH:
                    currentState = WAIT;
                    score[0] = 0;
                    score[1] = 0;
                    score[2] = 0;
                    break;
                }
            }

        }
        resetButtonCount = 0;
    }
    // Serial.printf(
    //     "\nA %d B %d C %d Benar %d Salah %d Reset %d\n",
    //     digitalRead(btnA.pin),
    //     digitalRead(btnB.pin),
    //     digitalRead(btnC.pin),
    //     digitalRead(btnBenar.pin),
    //     digitalRead(btnSalah.pin),
    //     digitalRead(btnReset.pin)
    // );

    Serial.printf("showScore %d\n", showScore);

    if (showScore) {
        number[0] = score[0];
        number[1] = score[1];
        number[2] = score[2];
        return;
    }

    switch (currentState) {
    case READY:
        // Serial.printf("%ld READY", count);
        now = millis();
        if (now - prev > (wrongOnce ? 3000 : 15000)) {
            prev = now;
            currentState = WAIT;
        }
        number[0] = btnA.enabled ? 10 : 13;
        number[1] = btnB.enabled ? 11 : 13;
        number[2] = btnC.enabled ? 12 : 13;
        break;
    case WAIT:
        // Serial.printf("%ld WAIT", count);
        number[0] = score[0];
        number[1] = score[1];
        number[2] = score[2];
        btnA.enabled = true;
        btnB.enabled = true;
        btnC.enabled = true;
        wrongOnce = false;
        if (score[0] >= 9 || score[1] >= 9 || score[2] >= 9) currentState = FINISH;
        break;
    case SCORING:
        // Serial.printf("%ld SCORING", count);
        number[0] = answeringTeam == TEAM_A ? 10 : 13;
        number[1] = answeringTeam == TEAM_B ? 11 : 13;
        number[2] = answeringTeam == TEAM_C ? 12 : 13;
        prev = millis();
        break;
    case FINISH:
        // Serial.printf("%ld FINISH", count);
        number[0] = score[0] >= 9 ? 9 : 13;
        number[1] = score[1] >= 9 ? 9 : 13;
        number[2] = score[2] >= 9 ? 9 : 13;
        break;
    }
}

uint8_t selector = 0;
uint8_t prevSelector = 2;

void IRAM_ATTR displayNumberWithTimer() {
    digitalWrite(selectors[prevSelector], LOW);
    digitalWrite(selectors[selector], HIGH);
    for (int i = 0; i < 7; i++) {
        digitalWrite(segments[i], patterns[number[selector]][i]);
    }
    prevSelector = selector;
    selector = (selector + 1) % 3;
}

void checkLed() {
    Serial.println("Checking LED in 1s...\n");
    delay(1000);
    for (int i = 0; i < 3; i++) {
        digitalWrite(selectors[i], LOW);
    }
    for (int i = 0; i < 7; i++) {
        digitalWrite(segments[i], HIGH);
    }

    const char seg[7] = { 'a', 'b', 'c', 'd', 'e', 'f', 'g'};

    for (int i = 0; i < 3; i++) {
        Serial.printf("checking selector %d\n", i);
        digitalWrite(selectors[i], HIGH);
        for (int j = 0; j < 7; j++) {
            Serial.printf("checking led %c selector %d\n", seg[j], i);
            digitalWrite(segments[j], LOW);
            delay(checkingInterval);
            digitalWrite(segments[j], HIGH);
        }
        digitalWrite(selectors[i], LOW);
    }
    Serial.println("DONE...\n");
}

void checkPattern() {
    Serial.println("Checking Patterns in 1s...\n");
    delay(1000);

    for (int k = 0; k < 3; k++) {
        digitalWrite(selectors[k], HIGH);
        for (int i = 0; i < 13; i++) {
            Serial.printf("Checking pattern %d\n", i);
            for (int j = 0; j < 7; j++) {
                digitalWrite(segments[j], patterns[i][j]);
            }
            delay(checkingInterval);
        }
        digitalWrite(selectors[k], LOW);
    }

    Serial.println("DONE...\n");
}

void checkDisplayNum() {
    uint64_t prev = 0;
    uint64_t now = 0;
    uint8_t i = 0;

    Serial.println("Checking displayNumber in 1s...\n");
    delay(1000);

    while (i < 13) {
        now = millis();
        if (now - prev >= 300) {
            prev = now;
            number[0] = i;
            number[1] = (i + 1) % 13;
            number[2] = (i + 2) % 13;
            Serial.printf("Checking num %d\n", number[0]);
            i++;
        }
    }

    Serial.println("DONE...\n");
}

void setupDisplayTimer() {
    dispTimer = timerBegin(0, 80, true);
    timerAttachInterrupt(dispTimer, &displayNumberWithTimer, true);
    timerAlarmWrite(dispTimer, (1.0f / displayFrequency) * 1000000, true);
    timerAlarmEnable(dispTimer);
}

void buzz(int frequency = NOTE_A, int duration = 500) {
    tone(buzzer, frequency);
    delay(duration * 0.7);
    noTone(buzzer);
    delay(20); // brief pause for fade-out effect
    tone(buzzer, frequency);
    delay(duration * 0.3);
    noTone(buzzer);
}

void IRAM_ATTR btnACallback() {
    if (currentState != READY || !btnA.enabled) return;
    answeringTeam = TEAM_A;
    currentState = SCORING;
    buzz();
}
void IRAM_ATTR btnBCallback() {
    if (currentState != READY || !btnB.enabled) return;
    answeringTeam = TEAM_B;
    currentState = SCORING;
    buzz();
}
void IRAM_ATTR btnCCallback() {
    if (currentState != READY || !btnC.enabled) return;
    answeringTeam = TEAM_C;
    currentState = SCORING;
    buzz();
}
void IRAM_ATTR btnBenarCallback() {
    if (currentState != SCORING) return;
    score[answeringTeam]++;
    currentState = WAIT;
}
void IRAM_ATTR btnSalahCallback() {
    if (currentState != SCORING) return;
    if (score[answeringTeam] > 0) score[answeringTeam]--;
    buttons[answeringTeam]->enabled = false;
    currentState = wrongOnce ? WAIT : READY;
    wrongOnce = true;
}

void IRAM_ATTR btnResetCallback() {
    currentTimeResetPressed = millis();
    if (currentTimeResetPressed - lastTimeResetPressed <= 500) {
        resetButtonCount++;
    }
    lastTimeResetPressed = currentTimeResetPressed;
}
