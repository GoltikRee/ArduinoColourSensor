#include <Arduino.h>
#include <EEPROM.h>

// Посылать ли отладочную информацию через Serial
#define ENABLE_SERIAL_DEBUG 0

#include <GyverButton.h>
#include <GyverEncoder.h>
#include <GyverTimer.h>
#include <LCD_1602_RUS.h>
#include <Wire.h>

LCD_1602_RUS lcd(0x27, 16, 2);

// Пин красного светодиода
const uint8_t RED_LED_PIN = 6;
// Пин зелёного светодиода
const uint8_t GREEN_LED_PIN = 7;
// Пин синего светодиода
const uint8_t BLUE_LED_PIN = 8;
// Пин фоторезистора
const uint8_t SENSOR_PIN = A0;
// Пин кнопки смены режимов
const uint8_t MODE_BUTTON_PIN = 5;
// Пин CLK энкодера
const uint8_t ENCODER_CLK_PIN = 2;
// Пин DT энкодера
const uint8_t ENCODER_DT_PIN = 3;
// Пин кнопки энкодера (SW)
const uint8_t ENCODER_SW_PIN = 4;

// Задержка между считываниями одного цвета
const uint8_t CONSECUTIVE_READINGS_DELAY = 20;
// Задержка перед считыванием следующего цвета
const uint8_t COLOR_SWITCH_DELAY = 200;
// Количество последовательных считываний одного цвета, уменьшает шум
const uint8_t CONSECUTIVE_READINGS_COUNT = 7;

// Минимальная задержка (мс) между считываниями в автоматическом режиме
const uint32_t MIN_AUTO_DELAY = 100;
// Максимальная задержка (мс) между считываниями в автоматическом режиме
const uint32_t MAX_AUTO_DELAY = 10000;
// Изменение задержки (мс) при обычном повороте энкодера
const uint32_t USUAL_ROTATION_DELAY_STEP = 100;
// Изменение задержки (мс) при повороте энкодера с нажатием
const uint32_t PRESSED_ROTATION_DELAY_STEP = 500;

// Последовательность-индикатор начала пакета данных для программы
const String SERIAL_MESSAGE_START = "$#$";
// Последовательность-индикатор конца пакета данных для программы
const String SERIAL_MESSAGE_END = "@!@";
// Разделитель значений цветов в пакете
const String SERIAL_MESSAGE_VALUES_SEP = ",";

// Цвета светодиода
enum Color { Red = 0, Green, Blue, None };

// Режимы работы
enum Mode { Loading = 0, Paused, RunningAuto, RunningManual, Calibrating };

// Возможные состояния считывания цвета
enum ReadingColorState {
    NotStarted = 0,
    WaitingRed,
    ReadingRed,
    WaitingGreen,
    ReadingGreen,
    WaitingBlue,
    ReadingBlue
};

// Возможные состояния в ручном режиме
enum ManualState { Idle = 0, Reading };

// Возможные состоянияя при калибровке
enum CalibrationState { NotCalibrating = 0, ReadingWhite, ReadingBlack };

// Настройки, управляемые DIP-переключателем на плате
struct {
    bool enable_debug = 0, disable_lcd = 0, use_dummy_data = 0,
         disable_lcd_backlight = 0, calibrate_on_start = 0, dont_save_data = 0;
} DipSwitchParams;

// Текущие считанные цвета (0-255)
uint8_t current_R, current_G, current_B;

// Пины светодиодов (для удобства)
constexpr uint8_t ledPins[] = {RED_LED_PIN, GREEN_LED_PIN, BLUE_LED_PIN};

// Минимальные и максимальные значения напряжения для каждого из цветов,
// устанавливаются в результате калибровки
uint8_t rgbMin[] = {0, 0, 0};
uint8_t rgbMax[] = {255, 255, 255};

// Текущий режим работы
Mode currentMode;

// Режим работы до паузы
Mode modeBeforePause;

// Текущее состояние в ручном режиме
ManualState manual_state = Idle;

// Текущее состояние считывания цвета
ReadingColorState reading_state = NotStarted;

// Текущее состояние калибровки
CalibrationState calibration_state = NotCalibrating;

// Текущая задержка между считываниями цвета в
// автоматическом режиме
uint32_t current_auto_delay = MIN_AUTO_DELAY * 5;

// Оставшееся количество считываний одного и того же цвета
uint8_t read_repeats_left;

// Текущая сумма значений напряжения с фоторезистора
uint32_t level_sum;

// Таймер, задержка перед следующим считыванием в автоматическом режиме
GTimer_ms next_iteration_timer(current_auto_delay);

// Таймер, задержка перед следующим считыванием того же цвета
GTimer_ms multiple_readings_timer(CONSECUTIVE_READINGS_DELAY);

// Таймер, задержка между включением светодиода и началом считывания
GTimer_ms color_switch_timer(COLOR_SWITCH_DELAY);

GButton modeButton(MODE_BUTTON_PIN);
Encoder encoder(ENCODER_CLK_PIN, ENCODER_DT_PIN, ENCODER_SW_PIN, 1);

// Обновляем ли экран при считывании цвета, устанавливается в true при
// первом считывании
bool refreshScreen = true;
