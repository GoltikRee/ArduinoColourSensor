#include <Arduino.h>
#include <EEPROM.h>

#define ENABLE_SERIAL_DEBUG 0  // посылать ли отладочную информацию через Serial

#include <LCD_1602_RUS.h>
#include <Wire.h>

LCD_1602_RUS lcd(0x27, 16, 2);

const uint8_t RED_LED_PIN = 7;  // пин красного светодиода
const uint8_t GREEN_LED_PIN = 8;  // пин зелёного светодиода
const uint8_t BLUE_LED_PIN = 9;   // пин синего светодиода
const uint8_t SENSOR_PIN = A0;    // пин фоторезистора
const uint8_t MODE_BUTTON_PIN = 2;
// const uint8_t

const uint8_t CONSECUTIVE_READINGS_DELAY =
    10;  // задержка между считываниями одного цвета
const uint8_t COLOR_SWITCH_DELAY =
    90;  // задержка перед считыванием следующего цвета
const uint8_t CONSECUTIVE_READINGS_COUNT =
    7;  // количество последовательных считываний одного цвета, уменьшает шум

const String SERIAL_MESSAGE_START =
    "$#$";  // последовательность-индикатор начала пакета с цветом
const String SERIAL_MESSAGE_END =
    "@!@";  // последовательность-индикатор конца пакета с цветом
const String SERIAL_MESSAGE_VALUES_SEP = ",";

enum Color { Red = 0, Green, Blue, None };

enum Mode { Loading = 0, Paused, RunningAuto, RunningManual, Calibrating };

int current_R, current_G, current_B;
constexpr uint8_t ledPins[] = {
    RED_LED_PIN, GREEN_LED_PIN,
    BLUE_LED_PIN};  // пины для каждого цвета светодиода

// минимальные и максимальные значения напряжения для каждого из цветов
uint8_t rgbMin[] = {0, 0, 0};
uint8_t rgbMax[] = {0, 0, 0};

Mode currentMode;  // текущее состояние
Color currentLedColor;

bool refreshScreen = true;  // обновляем ли экран при считывании цвета,
                            // устанавливается в true при первом считывании

/**
 * Переводит ASCII-строку в "широкую" строку
 * */
wchar_t *stringToWideString(char *_str) {
    size_t size = 0, i = 0;

    while (_str[size++])
        ;
    wchar_t *_new_str = new wchar_t[size];
    while (i < size) _new_str[i] = (wchar_t)_str[i];
    return _new_str;
}

String toHex(uint8_t w) {
    String hex = String(w, HEX);
    hex.toUpperCase();
    return (w > 15) ? hex : "0" + hex;
}

inline void debug(String message) {
#if ENABLE_SERIAL_DEBUG
    Serial.println(message);
#endif
}

void switchAllLeds(bool state = LOW) {
    for (uint8_t ledPin : ledPins) digitalWrite(ledPin, state);
}

String rgbToHex(uint8_t r, uint8_t g, uint8_t b) {
    return String(r, HEX) + String(g, HEX) + String(b, HEX);
}

void lcd_printCenter(String _str, uint8_t row = lcd.getCursorRow()) {
    lcd.setCursor((16 - (_str.length())) / 2, row);
    lcd.print(_str);
}

void lcd_printCenter(const wchar_t *_str, uint8_t row = lcd.getCursorRow()) {
    uint8_t size = 0;
    while (_str[size++] != 0)
        ;
    lcd.setCursor((16 - (--size)) / 2, row);
    lcd.print(_str);
}

void lcd_init() {
    debug("Initializing LCD...");
    lcd.init();
    lcd.clear();
    lcd.backlight();
}

void lcd_displayLoadingScreen() {
    debug("Showing loading screen...");
    lcd_printCenter(L"ДАТЧИК ЦВЕТА");
    lcd.setCursor(0, 1);
    lcd_printCenter(L"Загрузка...");
}

uint8_t readColorLevel(Color color, uint8_t repeats) {
    uint32_t level_sum = 0;
    digitalWrite(ledPins[color], HIGH);
    while (repeats--) {
        level_sum += analogRead(SENSOR_PIN);
        delay(CONSECUTIVE_READINGS_DELAY);
        _delay_ms()
    }
    return level_sum / repeats;
}

uint8_t adjustColorLevel(Color color, uint16_t raw_level) {
    raw_level = constrain(raw_level, rgbMin[color], rgbMax[color]);
    return map(raw_level, rgbMin[color], rgbMax[color], 255, 0);
}

void sendColorToSerial(uint8_t r, uint8_t g, uint8_t b) {
    Serial.println(SERIAL_MESSAGE_START + String(r) +
                   SERIAL_MESSAGE_VALUES_SEP + String(g) +
                   SERIAL_MESSAGE_VALUES_SEP + String(b) + SERIAL_MESSAGE_END);
}

void sendColorToLCD(uint8_t r, uint8_t g, uint8_t b) {
    lcd_printCenter(toHex(r) + toHex(g) + toHex(b), 1);
}

void displayColor(uint8_t r, uint8_t g, uint8_t b) {
    sendColorToSerial(r, g, b);
    sendColorToLCD(r, g, b);
}

void printModeInfo() {
    if (!refreshScreen) return;
    refreshScreen = false;
    lcd.home();
    lcd.print(currentMode == RunningAuto
                  ? "A"
                  : currentMode == RunningManual ? "P" : "");
    lcd_printCenter(currentMode == RunningAuto || currentMode == RunningManual
                        ? L"Цвет (RGB):"
                        : L"Пауза",
                    0);

    lcd.setCursor(0, 1);
}

void switchToAuto() {
    refreshScreen = true;
    debug("Entering AUTO mode...");
    currentMode = Mode::RunningAuto;
    lcd.clear();
    lcd.print("A");
    lcd_printCenter(L"Считываем", 0);
    lcd_printCenter(L"цвет...", 1);
    debug("The device is now in AUTO mode.");
}

void switchToManual() {
    refreshScreen = true;
    debug("Entering MANUAL mode...");
    switchAllLeds();
    currentMode = Mode::RunningManual;
    lcd.clear();
    lcd.print("P");
    lcd.println();
    lcd_printCenter(L"Жду команды!");
    lcd.println();
    debug("The device is now in MANUAL mode.");
}

void pause() {
    refreshScreen = true;
    debug("Entering PAUSED mode...");
    switchAllLeds();
    currentMode = Mode::Paused;
    lcd.clear();
    lcd.print(L"П");
    lcd.println();
    lcd_printCenter(L"ПАУЗА");
    lcd.println();
    debug("The device is now in PAUSED mode.");
}

void handleAutoIteration() { debug("Reading colour..."); }

void handleManualIteration() {}

void handlePausedIteration() {}

void handleCalibrationIteration() {}

void setup() {
    Serial.begin(19200);
    debug("INIT START");
    lcd_init();
    lcd_displayLoadingScreen();
    lcd.clear();
    currentMode = Mode::Loading;

    debug("Reading EEPROM & trying to receive calibration data...");

    for (uint8_t i = 0; i < 3; ++i) {
        EEPROM.get(i, rgbMin[i]);
        EEPROM.get(i + 3, rgbMax[i]);
    }

    debug("B values: " + String(rgbMin[0]) + ", " + String(rgbMin[1]) + ", " +
          String(rgbMin[2]));
    debug("W values: " + String(rgbMin[3]) + ", " + String(rgbMin[4]) + ", " +
          String(rgbMin[5]));

    debug("Setting LED pins to OUTPUT mode - pins " + String(ledPins[0]) +
          ", " + String(ledPins[1]) + ", " + String(ledPins[2]));

    for (uint8_t ledPin : ledPins) pinMode(ledPin, OUTPUT);

    currentMode = Mode::RunningAuto;
    currentLedColor = Color::None;
    switchToAuto();
}

void loop() {
    switch (currentMode) {
        case RunningAuto:
            handleAutoIteration();
            break;
        case RunningManual:
            handleManualIteration();
            break;
        case Paused:
            handlePausedIteration();
            break;
        case Calibrating:
            handleCalibrationIteration();
            break;
        default:
            currentMode = Mode::Loading;
            break;
    }
}