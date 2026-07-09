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

// НАСТРОЙКИ ГЕОМЕТРИИ БАКА (в сантиметрах)
const int32_t TANK_MAX_HEIGHT_CM = 25; // Максимальная высота бака для шкалы (100% заполнения)

// ПОРОГИ СРАБАТЫВАНИЯ РЕЛЕ (в сантиметрах)
// Изменяй эти цифры под реальную высоту своего бака после тестов
int32_t Inside_ON_Threshold_CM  = 20;  // Включить сброс при 20 см воды
int32_t Inside_OFF_Threshold_CM = 5;   // Выключить сброс, когда осталось 5 см воды

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
  
  // Инициализация массива фильтра нулями
  for (int i = 0; i < NUM_READINGS; i++) readings[i] = 0;

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for(;;); 
  }
  display.clearDisplay();
}

void loop() {
  // 1. ФИЛЬТРАЦИЯ ДАННЫХ (Скользящее среднее)
  total = total - readings[readIndex];
  readings[readIndex] = analogRead(SENSOR_PIN);
  total = total + readings[readIndex];
  readIndex = readIndex + 1;
  if (readIndex >= NUM_READINGS) readIndex = 0;
  
  int32_t rawADC = total / NUM_READINGS; // Усредненное значение АЦП

  // 2. МАТЕМАТИКА (Перевод в мВ -> кПа -> см)
  int32_t vPin_mv = (rawADC * 3300) / 4095;
  int32_t vSensor_mv = (vPin_mv * 5) / 3;
  
  // Давление в сотых долях кПа (1 кПа = 100 единиц)
  int32_t kPa_x100 = (vSensor_mv - 200) * 10 / 45; 
  if (kPa_x100 < 0) kPa_x100 = 0;

  // Перевод в сантиметры (1 кПа = 10.2 см воды). Результат умножен на 10 для точности (205 = 20.5 см)
  int32_t cm_x10 = (kPa_x100 * 102) / 100; 
  int32_t current_cm_whole = cm_x10 / 10;

  // 3. ЛОГИКА ТРИГГЕРА С ГИСТЕРЕЗИСОМ
  if (!isRelayActive && current_cm_whole >= Inside_ON_Threshold_CM) {
    isRelayActive = true;
    digitalWrite(RELAY_PIN, HIGH); // Открываем мосфет -> включаем SSR
    
    // Короткий писк при включении
    digitalWrite(BUZZER_PIN, HIGH);
    delay(150);
    digitalWrite(BUZZER_PIN, LOW);
  }
  else if (isRelayActive && current_cm_whole <= Inside_OFF_Threshold_CM) {
    isRelayActive = false;
    digitalWrite(RELAY_PIN, LOW); // Закрываем мосфет -> выключаем SSR
  }

  // 4. ОТРИСОВКА ЭКРАНА (128х32)
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Верхняя техническая строчка (Шрифт 1)
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("ADC:"); display.print(rawADC);
  
  display.setCursor(55, 0);
  display.print(kPa_x100 / 100); display.print("."); 
  if((kPa_x100 % 100) < 10) display.print("0");
  display.print(kPa_x100 % 100); display.print("kPa");

  display.setCursor(100, 0);
  display.print(isRelayActive ? "RUN" : "IDL");

  // Центральная строчка: Сантиметры (Шрифт 2 - крупный)
  display.setTextSize(2);
  display.setCursor(4, 10);
  display.print(current_cm_whole);
  display.print(".");
  display.print(cm_x10 % 10);
  display.print(" cm");

  // Нижняя часть: Графический прогресс-бар (Координаты: Y от 26 до 32)
  // Рисуем рамку индикатора (внешний контур)
  display.drawRect(80, 14, 46, 11, SSD1306_WHITE); // Компактно разместим рамку справа от цифр
  
  // Рассчитываем ширину полосы заполнения внутри рамки (макс ширина 42 пикселя)
  int32_t barWidth = (current_cm_whole * 42) / TANK_MAX_HEIGHT_CM;
  if (barWidth > 42) barWidth = 42;
  if (barWidth < 0)  barWidth = 0;
  
  // Заливаем прогресс внутри рамки
  if (barWidth > 0) {
    display.fillRect(82, 16, barWidth, 7, SSD1306_WHITE);
  }

  display.display();
  delay(50); // Частота обновления системы
}