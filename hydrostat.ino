#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// НАСТРОЙКИ ЖЕЛЕЗА
#define SENSOR_PIN  PA1  // Датчик давления
#define RELAY_PIN   PA4  // Пин мосфета (управление реле)
#define BUZZER_PIN  PA3  // Пин пищалки

// ЭМПИРИЧЕСКИЕ КАЛИБРОВКИ АЦП (Для расчета шкалы 0-20 см) для шкалы
const int32_t ADC_EMPTY = 45;   // Пустой бак 50 - проверенно
const int32_t ADC_FULL  = 95;  // Максимум (перелив) 110 - проверенно

// НАСТРОЙКА ПОРОГОВ (С учетом твоих корректировок) для рабатывания
int32_t Inside_ON_Threshold_ADC  = 90; // Включение откачки
int32_t Inside_OFF_Threshold_ADC = 47;  // Выключение откачки (опускаем до 50)

// Переменные для фильтра (усреднения)
const int NUM_READINGS = 10;
int32_t readings[NUM_READINGS];
int readIndex = 0;
int32_t total = 0;

bool isRelayActive = false; 

void setup() {
  pinMode(SENSOR_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  
  for (int i = 0; i < NUM_READINGS; i++) readings[i] = 0;

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for(;;); 
  }
  display.clearDisplay();
}

void loop() {
  // 1. Фильтрация данных (Скользящее среднее)
  total = total - readings[readIndex];
  readings[readIndex] = analogRead(SENSOR_PIN);
  total = total + readings[readIndex];
  readIndex = readIndex + 1;
  if (readIndex >= NUM_READINGS) readIndex = 0;
  
  int32_t rawADC = total / NUM_READINGS; 

  // 2. Логика управления
  if (!isRelayActive && rawADC >= Inside_ON_Threshold_ADC) {
    isRelayActive = true;
    digitalWrite(RELAY_PIN, HIGH); 
    
    digitalWrite(BUZZER_PIN, HIGH);
    delay(150);
    digitalWrite(BUZZER_PIN, LOW);
  }
  else if (isRelayActive && rawADC <= Inside_OFF_Threshold_ADC) {
    isRelayActive = false;
    digitalWrite(RELAY_PIN, LOW); 
  }

  // 3. Масштабирование для интерфейса
  int32_t constrainedADC = constrain(rawADC, ADC_EMPTY, ADC_FULL);

  // Перевод АЦП в условные сантиметры (0.0 - 20.0 см)
  int32_t cm_x10 = map(constrainedADC, ADC_EMPTY, ADC_FULL, 0, 200); 
  int32_t current_cm_whole = cm_x10 / 10;

  // Ширина полосы прогресс-бара (макс 42 пикселя)
  int32_t barWidth = map(constrainedADC, ADC_EMPTY, ADC_FULL, 0, 42);

  // 4. Отрисовка экрана
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Верхняя строчка: Строго ADC и статус [OFF] / [ON]
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("ADC: "); display.print(rawADC);

  // Статус прижимаем к правому краю (координата 95)
  display.setCursor(95, 0);
  display.print(isRelayActive ? "[ON]" : "[OFF]");

  // Крупные условные сантиметры
  display.setTextSize(2);
  display.setCursor(4, 14);
  display.print(current_cm_whole);
  display.print(".");
  display.print(cm_x10 % 10);
  display.print(" cm");

  // Графический индикатор справа
  display.drawRect(80, 16, 46, 11, SSD1306_WHITE); 
  if (barWidth > 0) {
    display.fillRect(82, 18, barWidth, 7, SSD1306_WHITE); 
  }

  display.display();
  delay(50);
}