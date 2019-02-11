#include "main.hpp"

#if ENABLE_SERIAL_DEBUG == 0
#define debug(s)
#else
inline void debug(String message) { Serial.println(message); }
#endif

#define _(s) String(s)

String toHex(uint8_t w) {
    String hex = String(w, HEX);
    hex.toUpperCase();
    return (w > 15) ? hex : "0" + hex;
}

void enable_led(Color color) { digitalWrite(ledPins[color], 1); }

void disable_led(Color color) { digitalWrite(ledPins[color], 0); }

void switchAllLeds(bool state = LOW) {
    for (uint8_t ledPin : ledPins)
        digitalWrite(ledPin, state);
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
    debug(F("Initializing LCD..."));
    lcd.init();
    lcd.clear();
    lcd.backlight();
}

void lcd_displayLoadingScreen() {
    debug(F("Showing loading screen..."));
    lcd_printCenter(L"ДАТЧИК ЦВЕТА");
    lcd.setCursor(0, 1);
    lcd_printCenter(L"Загрузка...");
}

void writeCalibrationData() {
    for (uint8_t i = 0; i < 3; ++i) {
        EEPROM.update(i, rgbMin[i]);
        EEPROM.update(i + 3, rgbMax[i]);
    }
}

void readColorLevel(Color color) {
    read_repeats_left--;
    debug("Reading #" + String(CONSECUTIVE_READINGS_COUNT - read_repeats_left));
    uint16_t c = analogRead(SENSOR_PIN);
    debug("Current reading: " + String(c));
    level_sum += c;
}

uint8_t adjustColorLevel(Color color, uint16_t raw_level) {
    debug("Raw: " + String(raw_level));
    raw_level = constrain(raw_level, rgbMin[color], rgbMax[color]);
    debug("Raw-C: " + String(raw_level));
    raw_level = map(raw_level, rgbMin[color], rgbMax[color], 255, 0);
    debug("Raw-Mapped: " + String(raw_level));
    return raw_level;
}

void sendColorToSerial(uint8_t r, uint8_t g, uint8_t b) {
    Serial.println(SERIAL_MESSAGE_START + String(r) +
                   SERIAL_MESSAGE_VALUES_SEP + String(g) +
                   SERIAL_MESSAGE_VALUES_SEP + String(b) + SERIAL_MESSAGE_END);
}

void sendColorToLCD(uint8_t r, uint8_t g, uint8_t b) {
    lcd_printCenter("#" + toHex(r) + toHex(g) + toHex(b), 1);
}

void displayColor(uint8_t r, uint8_t g, uint8_t b) {
    sendColorToSerial(r, g, b);
    sendColorToLCD(r, g, b);
}

void printModeInfo() {
    if (!refreshScreen)
        return;
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
    debug(F("Entering AUTO mode..."));
    reading_state = NotStarted;
    currentMode = Mode::RunningAuto;
    lcd.clear();
    lcd.print("A");
    lcd_printCenter(L"Считываем", 0);
    lcd_printCenter(L"цвет...", 1);
    debug(F("The device is now in AUTO mode."));
    Serial.println(SERIAL_MESSAGE_START + "AM" + SERIAL_MESSAGE_END);
    next_iteration_timer.start();
    multiple_readings_timer.start();
    color_switch_timer.start();
}

void switchToManual() {
    refreshScreen = true;
    debug(F("Entering MANUAL mode..."));
    switchAllLeds();
    reading_state = NotStarted;
    currentMode = Mode::RunningManual;
    lcd.clear();
    lcd.print("P");
    lcd_printCenter(L"Готов!", 0);
    debug(F("The device is now in MANUAL mode."));
    Serial.println(SERIAL_MESSAGE_START + "MM" + SERIAL_MESSAGE_END);
    multiple_readings_timer.start();
    color_switch_timer.start();
}

void pause() {
    if (currentMode == Calibrating) {
        debug(F("Device is calibrating, won't pause"));
        return;
    }
    refreshScreen = true;
    if (currentMode == Paused) {
        debug(F("Leaving PAUSED mode..."));
        if (modeBeforePause == RunningAuto) {
            switchToAuto();
        } else {
            switchToManual();
        }
        return;
    }
    debug(F("Entering PAUSED mode..."));
    switchAllLeds();
    reading_state = NotStarted;
    modeBeforePause = currentMode;
    currentMode = Mode::Paused;
    lcd.clear();
    lcd.print(L"П");
    lcd_printCenter(L"ПАУЗА", 0);
    debug(F("The device is now in PAUSED mode."));
    Serial.println(SERIAL_MESSAGE_START + "PM" + SERIAL_MESSAGE_END);
}

bool readColor() {
    switch (reading_state) {
        case NotStarted:
            if (currentMode == RunningManual ||
                next_iteration_timer.isReady()) {
                debug(F("Enabled RED led. Waiting."));
                reading_state = WaitingRed;
                enable_led(Red);
                color_switch_timer.reset();
            }
            return false;
            break;
        case WaitingRed:
            if (color_switch_timer.isReady()) {
                debug(F("Started reading level of RED."));
                reading_state = ReadingRed;
                level_sum = 0;
                read_repeats_left = CONSECUTIVE_READINGS_COUNT;
            }
            return false;
            break;
        case ReadingRed:
            readColorLevel(Red);
            if (!read_repeats_left) {
                current_R = adjustColorLevel(
                    Color::Red, level_sum / CONSECUTIVE_READINGS_COUNT);
                debug(F("Ended reading level of RED."));
                debug("RSUM: " + _(level_sum) +
                      ", C: " + _(CONSECUTIVE_READINGS_COUNT));
                debug("RED: " + _(current_R));
                reading_state = WaitingGreen;
                debug(F("Disabled RED led & enabled GREEN led. Waiting."));
                disable_led(Red);
                enable_led(Green);
                color_switch_timer.reset();
            }
            return false;
            break;
        case WaitingGreen:
            if (color_switch_timer.isReady()) {
                debug(F("Started reading level of GREEN."));
                reading_state = ReadingGreen;
                level_sum = 0;
                read_repeats_left = CONSECUTIVE_READINGS_COUNT;
            }
            return false;
            break;
        case ReadingGreen:
            readColorLevel(Green);
            if (!read_repeats_left) {
                current_G = adjustColorLevel(
                    Color::Green, level_sum / CONSECUTIVE_READINGS_COUNT);
                debug(F("Ended reading level of GREEN."));
                debug("GSUM: " + _(level_sum) +
                      ", C: " + _(CONSECUTIVE_READINGS_COUNT));
                debug("GREEN: " + _(current_G));
                reading_state = WaitingGreen;
                debug(F("Disabled GREEN led & enabled BLUE led. Waiting."));
                reading_state = WaitingBlue;
                disable_led(Green);
                enable_led(Blue);
                color_switch_timer.reset();
            }
            return false;
            break;
        case WaitingBlue:
            if (color_switch_timer.isReady()) {
                debug(F("Started reading level of BLUE."));
                reading_state = ReadingBlue;
                level_sum = 0;
                read_repeats_left = CONSECUTIVE_READINGS_COUNT;
            }
            return false;
            break;
        case ReadingBlue:
            readColorLevel(Blue);
            if (!read_repeats_left) {
                current_B = adjustColorLevel(
                    Color::Blue, level_sum / CONSECUTIVE_READINGS_COUNT);
                debug("BSUM: " + _(level_sum) +
                      ", C: " + _(CONSECUTIVE_READINGS_COUNT));
                debug("BLUE: " + _(current_B));
                debug(F("Disabled BLUE led. Broadcasting results."));
                disable_led(Blue);
                color_switch_timer.reset();
                displayColor(current_R, current_G, current_B);
                reading_state = NotStarted;
            } else {
                return false;
            }
            break;
    }
    return true;
}

void handleAutoIteration() {
    if (!readColor()) {
        return;
    }
    // обновляем интервал до сл. итерации
    debug(F("Waiting for the next iteration."));
    next_iteration_timer.setInterval(current_auto_delay);
    debug(F("-----"));
}

void handleManualIteration() {
    if (encoder.isClick()) {
        if (manual_state == Idle) {
            lcd_printCenter(L"Считываем", 0);
            manual_state = Reading;
        } else {
            switchAllLeds();
            manual_state = Idle;
            reading_state = NotStarted;
            lcd_printCenter(L"  Готов! ", 0);
        }
    }
    if (manual_state == Reading)
        if (readColor()) {
            manual_state = Idle;
            reading_state = NotStarted;
            lcd_printCenter(L"  Готов! ", 0);
        }
}

void handlePausedIteration() {
    if (encoder.isClick() || modeButton.isClick() || modeButton.isHolded()) {
        pause();
        return;
    }
}

void handleCalibrationIteration() {
    Serial.println("White");
    delay(10000);
    // rgbMin[Color::Red] = readColorLevel(Color::Red, 7);
    // rgbMin[Color::Green] = readColorLevel(Color::Green, 7);
    // rgbMin[Color::Blue] = readColorLevel(Color::Blue, 7);
    Serial.println("WR: " + String(rgbMin[Color::Red]));
    Serial.println("WG: " + String(rgbMin[Color::Green]));
    Serial.println("WB: " + String(rgbMin[Color::Blue]));

    Serial.println("Black");
    delay(10000);
    // rgbMax[Color::Red] = readColorLevel(Color::Red, 7);
    // rgbMax[Color::Green] = readColorLevel(Color::Green, 7);
    // rgbMax[Color::Blue] = readColorLevel(Color::Blue, 7);
    Serial.println("BR: " + String(rgbMax[Color::Red]));
    Serial.println("BG: " + String(rgbMax[Color::Green]));
    Serial.println("BB: " + String(rgbMax[Color::Blue]));
    delay(5000);
}

void setup() {
    Serial.begin(19200);
    debug(F("INIT START"));
    lcd_init();
    lcd_displayLoadingScreen();
    lcd.clear();
    currentMode = Mode::Loading;

    debug(F("Reading EEPROM & trying to receive calibration data..."));

    for (uint8_t i = 0; i < 3; ++i) {
        // EEPROM.get(i, rgbMin[i]);
        // EEPROM.get(i + 3, rgbMax[i]);
        rgbMin[i] = 0;
        rgbMax[i] = 255;
    }

    debug("B values: " + String(rgbMin[0]) + ", " + String(rgbMin[1]) + ", " +
          String(rgbMin[2]));
    debug("W values: " + String(rgbMin[3]) + ", " + String(rgbMin[4]) + ", " +
          String(rgbMin[5]));

    debug(F("Setting LED pins to OUTPUT mode"));

    for (uint8_t ledPin : ledPins)
        pinMode(ledPin, OUTPUT);

    currentMode = Mode::RunningAuto;
    switchToAuto();
}

void loop() {
    modeButton.tick();
    encoder.tick();

    if (modeButton.isHolded())
        pause();
    else if (modeButton.isClick()) {
        switch (currentMode) {
            case Paused:
                pause();
                break;
            case RunningAuto:
                switchToManual();
                break;
            case RunningManual:
                switchToAuto();
                break;
            default:
                break;
        }
    }

    if (encoder.isLeftH())
        current_auto_delay -= PRESSED_ROTATION_DELAY_STEP;
    else if (encoder.isRightH())
        current_auto_delay += PRESSED_ROTATION_DELAY_STEP;
    else if (encoder.isLeft())
        current_auto_delay -= USUAL_ROTATION_DELAY_STEP;
    else if (encoder.isRight())
        current_auto_delay += USUAL_ROTATION_DELAY_STEP;

    current_auto_delay =
        constrain(current_auto_delay, MIN_AUTO_DELAY, MAX_AUTO_DELAY);

    switch (currentMode) {
        case Loading:
            switchToAuto();
            break;
        case Paused:
            handlePausedIteration();
            break;
        case RunningAuto:
            handleAutoIteration();
            break;
        case RunningManual:
            handleManualIteration();
            break;
        case Calibrating:
            handleCalibrationIteration();
            break;
        default:
            break;
    }
}