#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ============================================================================
// 1. НАСТРОЙКИ ЖЕЛЕЗА И ПИНОВ
// ============================================================================
#define SENSOR_PIN  PA1  // Аналоговый вход (датчик давления MPX5010DP)
#define RELAY_PIN   PA4  // Цифровой выход на затвор MOSFET (управление SSR)
#define BUZZER_PIN  PA3  // Цифровой выход на пищалку (Buzzer)
#define BUTTON_PIN  PA2  // Цифровой вход кнопки (к GND, подтяжка INPUT_PULLUP)

// ============================================================================
// 2. КАЛИБРОВКИ И ПОРОГИ (Проверенные значения)
// ============================================================================
const int32_t ADC_EMPTY = 45;   // Пустой бак (45 АЦП = 0.0 см)
const int32_t ADC_FULL  = 95;   // Максимум / перелив (95 АЦП = 20.0 см)

int32_t Inside_ON_Threshold_ADC  = 90; // Порог включения стандартной откачки
int32_t Inside_OFF_Threshold_ADC = 47; // Порог выключения откачки

// ============================================================================
// 3. КОНСТАНТЫ ТАЙМЕРОВ И АВТОМАТИКИ
// ============================================================================
const unsigned long SAFETY_TIMEOUT_MS = 10000;      // 10 секунд задержки перед сливом застоя
const unsigned long THREE_DAYS_MS     = 259200000UL; // 3 суток в миллисекундах (72 часа)
const unsigned long EXTRA_PURGE_MS    = 3000;        // 3 секунды дополнительной откачки при продувке
const unsigned long UI_AUTO_RETURN_MS = 12000;       // 12 секунд показа экрана статистики

// ============================================================================
// 4. СЛУЖЕБНЫЕ ПЕРЕМЕННЫЕ
// ============================================================================
// Переменные скользящего среднего (фильтр шумов АЦП)
const int NUM_READINGS = 10;
int32_t readings[NUM_READINGS];
int readIndex = 0;
int32_t total = 0;

// Состояния системы
bool isRelayActive     = false; // Включена ли помпа сейчас
bool isDeepPurgeActive = false; // Идет ли прямо сейчас 3-дневная продувка
bool showStatsScreen   = false; // Показывать ли информационный экран

// Таймеры и счетчики
unsigned long standbyWaterTimer = 0; // Время появления стоячей воды
unsigned long lastDeepFlushTime = 0; // Время последней 3-дневной продувки
unsigned long purgeExtraTimer   = 0; // Таймер удержания помпы для продувки
unsigned long statsScreenTimer  = 0; // Таймер автовозврата экрана
uint32_t deepFlushCount         = 0; // Счетчик успешных 3-дневных продувок

// Кнопка
unsigned long buttonPressTime   = 0;
bool buttonStatePrevious        = HIGH;

// ============================================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ЗВУКА
// ============================================================================
// Короткий одиночный пик
void soundBeep(int durationMs) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(durationMs);
  digitalWrite(BUZZER_PIN, LOW);
}

// Предупредительный каскадный звук перед 3-дневной продувкой (чтобы не пугать)
void soundWarningCascade() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(70);
    digitalWrite(BUZZER_PIN, LOW);
    delay(180);
  }
}

// ============================================================================
// SETUP: ИНИЦИАЛИЗАЦИЯ
// ============================================================================
void setup() {
  pinMode(SENSOR_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  for (int i = 0; i < NUM_READINGS; i++) readings[i] = 0;

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for(;;); 
  }
  
  display.clearDisplay();
  lastDeepFlushTime = millis(); // Старт отсчета 3 суток с момента включения
}

// ============================================================================
// MAIN LOOP: ОСНОВНОЙ ЦИКЛ
// ============================================================================
void loop() {
  unsigned long currentMillis = millis();

  // --------------------------------------------------------------------------
  // ШАГ 1: ФИЛЬТРАЦИЯ ДАННЫХ (Скользящее среднее по 10 замерам)
  // --------------------------------------------------------------------------
  total = total - readings[readIndex];
  readings[readIndex] = analogRead(SENSOR_PIN);
  total = total + readings[readIndex];
  readIndex = (readIndex + 1) % NUM_READINGS;
  
  int32_t rawADC = total / NUM_READINGS; 

  // --------------------------------------------------------------------------
  // ШАГ 2: ОБРАБОТКА КНОПКИ (Переключение экрана / Сброс таймера)
  // --------------------------------------------------------------------------
  bool buttonCurrent = digitalRead(BUTTON_PIN);
  
  if (buttonStatePrevious == HIGH && buttonCurrent == LOW) {
    buttonPressTime = currentMillis; // Зафиксировали время нажатия
  } 
  else if (buttonStatePrevious == LOW && buttonCurrent == HIGH) {
    unsigned long pressDuration = currentMillis - buttonPressTime;
    
    if (pressDuration >= 1500) {
      // Удержание > 1.5 сек: Сброс 3-дневного таймера на текущую секунду (если переключали свет)
      lastDeepFlushTime = currentMillis;
      soundBeep(300); // Длинный сигнал подтверждения
    } else if (pressDuration > 40) {
      // Короткий клик: Переключение на экран статистики
      showStatsScreen = !showStatsScreen;
      statsScreenTimer = currentMillis;
      soundBeep(50);
    }
  }
  buttonStatePrevious = buttonCurrent;

  // --------------------------------------------------------------------------
  // ШАГ 3: ЛОГИКА УПРАВЛЕНИЯ И АВТОМАТИКИ
  // --------------------------------------------------------------------------
  
  // А) АВТОМАТИЧЕСКАЯ ПРОДУВКА ВЗДУХОМ РАЗ В 3 ДНЯ (72 ЧАСА)
  if (!isRelayActive && (currentMillis - lastDeepFlushTime >= THREE_DAYS_MS)) {
    soundWarningCascade(); // Мягкое предупреждение "пик... пик... пик..."
    delay(400);
    
    isRelayActive     = true;
    isDeepPurgeActive = true;
    digitalWrite(RELAY_PIN, HIGH); // Старт глубокой откачки
    standbyWaterTimer = 0;
  }

  // Б) СТАНДАРТНОЕ ВКТЮЧЕНИЕ ПО ВЕРХНЕМУ ПОРОГУ (90 АЦП)
  else if (!isRelayActive && rawADC >= Inside_ON_Threshold_ADC) {
    isRelayActive     = true;
    isDeepPurgeActive = false;
    digitalWrite(RELAY_PIN, HIGH);
    soundBeep(150);
    standbyWaterTimer = 0;
  }

  // В) ТАЙМЕР БЕЗОПАСНОСТИ ЗАСТОЯ (10 Секунд)
  // Если есть вода (> 57 ADC), помпа молчит, и уровень не меняется 10 секунд
  else if (!isRelayActive && rawADC > (Inside_OFF_Threshold_ADC + 10)) {
    if (standbyWaterTimer == 0) {
      standbyWaterTimer = currentMillis; // Засекаем 10 секунд
    } else if (currentMillis - standbyWaterTimer >= SAFETY_TIMEOUT_MS) {
      // Срабатывает обычный сброс воды
      isRelayActive     = true;
      isDeepPurgeActive = false;
      digitalWrite(RELAY_PIN, HIGH);
      soundBeep(150);
      standbyWaterTimer = 0;
    }
  } else if (!isRelayActive && rawADC <= (Inside_OFF_Threshold_ADC + 10)) {
    standbyWaterTimer = 0; // Вода ушла, сбрасываем таймер застоя
  }

  // Г) ЛОГИКА ВЫКЛЮЧЕНИЯ НАСОСА
  if (isRelayActive) {
    // Когда уровень упал до нижнего порога (47 АЦП)
    if (rawADC <= Inside_OFF_Threshold_ADC) {
      
      if (isDeepPurgeActive) {
        // Если идет 3-дневная продувка — держим помпу еще 3 секунды "насухо"
        if (purgeExtraTimer == 0) {
          purgeExtraTimer = currentMillis;
        } else if (currentMillis - purgeExtraTimer >= EXTRA_PURGE_MS) {
          // Завершаем продувку
          isRelayActive     = false;
          isDeepPurgeActive = false;
          digitalWrite(RELAY_PIN, LOW);
          
          purgeExtraTimer   = 0;
          lastDeepFlushTime = currentMillis; // Перезапуск 3-дневного таймера
          deepFlushCount++;                 // Прибавляем +1 к счетчику продувок
        }
      } else {
        // Стандартное выключение сразу при достижении 47 АЦП
        isRelayActive     = false;
        digitalWrite(RELAY_PIN, LOW);
        standbyWaterTimer = 0;
      }
    } else {
      purgeExtraTimer = 0; // Корректировка, если АЦП еще колеблется около дна
    }
  }

  // --------------------------------------------------------------------------
  // ШАГ 4: РАСЧЕТЫ ДЛЯ ИНТЕРФЕЙСА
  // --------------------------------------------------------------------------
  int32_t constrainedADC   = constrain(rawADC, ADC_EMPTY, ADC_FULL);
  int32_t cm_x10           = map(constrainedADC, ADC_EMPTY, ADC_FULL, 0, 200); 
  int32_t current_cm_whole = cm_x10 / 10;
  int32_t current_cm_fract = cm_x10 % 10; // Строго 1 знак после запятой
  int32_t barWidth         = map(constrainedADC, ADC_EMPTY, ADC_FULL, 0, 42);

  // --------------------------------------------------------------------------
  // ШАГ 5: ОТРИСОВКА НА OLED ЭКРАНЕ (128х32)
  // --------------------------------------------------------------------------
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Таймаут автовозврата экрана статистики на главный рабочий экран через 12 сек
  if (showStatsScreen && (currentMillis - statsScreenTimer >= UI_AUTO_RETURN_MS)) {
    showStatsScreen = false;
  }

  if (showStatsScreen) {
    // === ЭКРАН 2: СТАТИСТИКА ПРОФИЛАКТИК (Вызывается кнопкой) ===
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("[ SYSTEM STATS ]");

    display.setCursor(0, 12);
    display.print("Purges Done: "); display.print(deepFlushCount);

    // Расчет оставшихся часов до следующей продувки
    unsigned long elapsed = currentMillis - lastDeepFlushTime;
    long remainingMs = THREE_DAYS_MS - elapsed;
    if (remainingMs < 0) remainingMs = 0;
    int remainingHours = remainingMs / 3600000UL;

    display.setCursor(0, 23);
    display.print("Next Purge:  "); display.print(remainingHours); display.print("h");

  } else {
    // === ЭКРАН 1: ОСНОВНОЙ РАБОЧИЙ ИНТЕРФЕЙС ===
    
    // Верхняя строчка: АЦП и Статус
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("ADC: "); display.print(rawADC);

    display.setCursor(85, 0);
    if (isDeepPurgeActive) {
      display.print("[PURGE]"); // Режим глубокой продувки
    } else {
      display.print(isRelayActive ? "[ON]" : "[OFF]");
    }

    // Крупные сантиметры (Формат X.X или XX.X)
    display.setTextSize(2);
    display.setCursor(0, 14);
    display.print(current_cm_whole);
    display.print(".");
    display.print(current_cm_fract);

    // Подпись "cm" мелким шрифтом правее цифр (не залезает на рамку)
    display.setTextSize(1);
    display.setCursor(52, 21);
    display.print("cm");

    // Графический индикатор заполнения справа
    display.drawRect(80, 16, 46, 11, SSD1306_WHITE); 
    if (barWidth > 0) {
      display.fillRect(82, 18, barWidth, 7, SSD1306_WHITE); 
    }
  }

  display.display();
  delay(50); // Частота обновления системы
}