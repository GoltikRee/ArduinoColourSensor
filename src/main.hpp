#include <Arduino.h>
#include <EEPROM.h>

#define ENABLE_SERIAL_DEBUG 1  // посылать ли отладочную информацию через Serial

#include <LCD_1602_RUS.h>
#include <Wire.h>

LCD_1602_RUS lcd(0x27, 16, 2);

const uint8_t RED_LED_PIN = 6;  // пин красного светодиода
const uint8_t GREEN_LED_PIN = 7;  // пин зелёного светодиода
const uint8_t BLUE_LED_PIN = 8;   // пин синего светодиода
const uint8_t SENSOR_PIN = A0;    // пин фоторезистора
const uint8_t MODE_BUTTON_PIN = 2;

const uint8_t CONSECUTIVE_READINGS_DELAY =
    20;  // задержка между считываниями одного цвета
const uint8_t COLOR_SWITCH_DELAY =
    200;  // задержка перед считыванием следующего цвета
const uint8_t CONSECUTIVE_READINGS_COUNT =
    7;  // количество последовательных считываний одного цвета, уменьшает шум

const String SERIAL_MESSAGE_START =
    "$#$";  // последовательность-индикатор начала пакета с цветом
const String SERIAL_MESSAGE_END =
    "@!@";  // последовательность-индикатор конца пакета с цветом
const String SERIAL_MESSAGE_VALUES_SEP = ",";

enum Color { Red = 0, Green, Blue, None };

enum Mode { Loading = 0, Paused, RunningAuto, RunningManual, Calibrating };

enum ReadingColorState {
    NotStarted = 0,
    WaitingRed,
    ReadingRed,
    WaitingGreen,
    ReadingGreen,
    WaitingBlue,
    ReadingBlue,
    WaitingForNextIteration
};

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

#include "utils.cpp"