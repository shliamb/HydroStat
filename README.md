# 🇷🇺 РУССКАЯ ВЕРСИЯ

# Hydrostatic Water Level Controller (STM32 + MPX5010DP)

Цифровая система контроля уровня жидкости и автоматического управления дренажем на основе гидростатического метода ("воздушного колокола"). 

## 📌 О проекте

Устройство создано как надежная цифровая альтернатива взамен вышедшей из строя штатной аналоговой системы управления канализационной насосной станции **AquaTIM AM-STP-600**. Проект решает проблему точного отслеживания уровня сточных вод в резервуаре объемом ~10 литров (рабочий ход воды до 25 см), обеспечивает защиту от ложных срабатываний из-за колебаний воды и предоставляет наглядную графическую индикацию состояния системы.

---

## 🛠 Технический стек и компоненты

### Электропитание и логика
* **Основное питание системы:** **5 Вольт** постоянного тока (DC). Подается на общую шину питания устройства, от нее же напрямую запитываются датчик давления и силовая часть управляющего реле.
* **Микроконтроллер (MCU):** STM32 (совместимый с экосистемой Arduino IDE/STM32duino). Плата принимает на вход 5В, но ядро и логика портов работают на **3.3В**. 

### Периферия и индикация
* **Дисплей:** OLED 128x32 на контроллере SSD1306 (Интерфейс I2C, адрес `0x3C`).
* **Индикация:** Активный пьезоизлучатель (Buzzer) 5В для звукового подтверждения триггеров.

### Аналоговая часть (Датчик)
* **Сенсор давления:** Freescale **MPX5010DP** (Дифференциальный, диапазон 0–10 кПа, питание 5В, аналоговый выход).
* **Метод измерения:** Гидростатический. Датчик измеряет давление воздуха, сжимаемого поднимающимся столбом воды в герметичной трубке, опущенной на дно бака.

### Силовая коммутация (Драйвер реле)
* **Транзистор (Ключ):** N-Channel MOSFET **IRLML0030TRPBF** (Корпус SOT-23, Logic-Level, полное открытие от $V_{GS} \ge 2.5\text{В}$).
* **Реле:** Твердотельное реле (SSR) **FOTEK SSR-40 DA** (Управление 3-32V DC, коммутация 24-380V AC).

---

## 🔌 Схема подключения и распиновка

### Распределение пинов микроконтроллера
* `PA1` — Аналоговый вход (ADC) от делителя напряжения датчика MPX5010DP.
* `PA3` — Цифровой выход (OUTPUT) на сигнальный пьезодинамик.
* `PA4` — Цифровой выход (OUTPUT) на затвор (Gate) полевого транзистора.
* `PB6 / PB7` (или стандартные аппаратные пины) — I2C линии (`SCL` / `SDA`) для дисплея OLED.

### Схема согласования уровней и силовой части

1.  **Защита пина АЦП (PA1):**
    Так как датчик MPX5010DP питается от 5В и может выдать до 4.7В на выходе, а толерантность аналоговых пинов STM32 ограничена 3.3В, применен резистивный делитель напряжения:
    * $R_1$ (к выходу датчика) = **10 кОм**
    * $R_2$ (на GND) = **15 кОм**
    * *Коэффициент затухания:* $0.6$. При максимальных аварийных 5.0В от датчика, на пин STM32 прилетит ровно 3.0В, что абсолютно безопасно для ядра.

2.  **Драйвер нижнего плеча для SSR (Low-Side Switch):**
    Для компенсации просадки напряжения китайских клонов SSR и разгрузки портов MCU по току, реле управляется через MOSFET `IRLML0030`:
    * **Gate (Затвор):** Подключен к `PA4` + стягивающий резистор **10 кОм на GND** (предотвращает случайное включение насоса при перезагрузке микроконтроллера).
    * **Source (Исток):** Напрямую на общую землю (`GND`).
    * **Drain (Сток):** К минусовому контакту управления SSR `(-)` (клемма №4).
    * Плюсовой контакт SSR `(+)` (клемма №3) подключен напрямую к силовой линии **`+5В`** блока питания.

---

## 🧠 Ключевые инженерные решения

### 1. Программный гистерезис (Защита от дребезга)
Для исключения эффекта "микропереключений" реле на пограничных значениях (когда вода колышется при заполнении), в логику заложен регулируемый зазор между включением и выключением. Порог задается в физических сантиметрах:
* `Inside_ON_Threshold_CM = 20` (Включение откачки при достижении 20 см).
* `Inside_OFF_Threshold_CM = 5` (Отключение насоса только при падении уровня до 5 см).

### 2. Фильтрация "Скользящее среднее" (Moving Average)
Бурление стоков при работе насоса создает сильную турбулентность. Программа осуществляет постоянный циклический замер (выборка из 10 последних чтений АЦП), сглаживая пульсации. Это делает показания на экране стабильными, а движение прогресс-бара — плавным.

### 3. Оптимизация вычислений (Integer Only)
Вся математика перевода попугаев АЦП в милливольты, килопаскали и сантиметры реализована на целочисленных переменных (`int32_t`) со сдвигом разрядов (умножением на 10 и 100). Это экономит ресурсы процессора и исключает использование «тяжелой» для микроконтроллеров библиотеки `float`.

### 4. Эргономика интерфейса
Экран разделен на две информационные зоны:
1.  **Верхняя строчка (техническая, мелкий шрифт):** Вывод сырого значения АЦП, точного давления в кПа и текущего статуса системы (`IDL` — ожидание, `RUN` — откачка воды).
2.  **Основная зона (крупный шрифт):** Вывод уровня воды в сантиметрах с точностью до десятых долей и компактная графическая шкала заполнения (индикатор-"батарейка") в правой части дисплея, меняющаяся пропорционально объему.

---

## 📦 Зависимости и сборка
Для компиляции проекта в среде Arduino IDE требуются следующие библиотеки:
* `Adafruit_GFX_Library`
* `Adafruit_SSD1306`

**Важно перед запуском:** Первое подключение воздушной трубки к штуцеру датчика необходимо производить строго при **полностью пустом резервуаре**, чтобы запереть внутри системы воздух под базовым атмосферным давлением. В противном случае расчетные алгоритмы гидростатики сместятся в зону отрицательных значений.


---
---
---

# 🇺🇸 ENGLISH VERSION

# Hydrostatic Water Level Controller (STM32 + MPX5010DP)

A digital fluid level monitoring and automated drainage control system utilizing the hydrostatic "air trap" (air bell) method.

## 📌 Project Overview

This device was designed as a robust digital drop-in replacement for the failed OEM analog control system of an **AquaTIM AM-STP-600** sewage pumping station. The project addresses the challenge of precisely tracking wastewater levels in a ~10-liter reservoir (with a working water travel of up to 25 cm). It features built-in protection against false triggers caused by water sloshing and provides a clear, real-time graphical user interface.

---

## 🛠 Tech Stack & Components

### Power Supply & Logic
* **Main System Voltage:** **5V DC**. Supplied to the main power rail of the device, directly powering the pressure sensor and the control side of the solid-state relay.
* **Microcontroller (MCU):** STM32 (fully compatible with the Arduino IDE / STM32duino ecosystem). The board accepts 5V input, but the core logic and GPIO pins operate at **3.3V**.

### Peripherals & Indication
* **Display:** 128x32 OLED display driven by the SSD1306 controller (I2C interface, address `0x3C`).
* **Audio Indication:** 5V active piezo buzzer for audible trigger confirmations.

### Analog Section (Sensor)
* **Pressure Sensor:** Freescale **MPX5010DP** (Differential, 0–10 kPa range, 5V power supply, analog voltage output).
* **Measurement Principle:** Hydrostatic. The sensor measures the pressure of the air trapped inside a sealed tube submerged to the bottom of the tank, which is compressed as the water level rises.

### Power Switching (Relay Driver)
* **Transistor (Switch):** **IRLML0030TRPBF** N-Channel MOSFET (SOT-23 package, Logic-Level, fully turns on at $V_{GS} \ge 2.5\text{V}$).
* **Relay:** **FOTEK SSR-40 DA** Solid-State Relay (3-32V DC control input, 24-380V AC load switching).

---

## 🔌 Wiring & Pinout

### MCU Pin Mapping
* `PA1` — Analog Input (ADC) from the MPX5010DP sensor's voltage divider.
* `PA3` — Digital Output to the 5V active buzzer.
* `PA4` — Digital Output to the Gate of the N-channel MOSFET.
* `PB6 / PB7` (or standard hardware pins) — I2C lines (`SCL` / `SDA`) for the OLED display.

### Voltage Matching & Power Schematics

1.  **ADC Pin Protection (PA1):**
    Since the MPX5010DP sensor is powered by 5V and can output up to 4.7V, while the STM32 analog pins are limited to a 3.3V tolerance, a resistive voltage divider is implemented:
    * $R_1$ (connected to the sensor output) = **10 kΩ**
    * $R_2$ (connected to GND) = **15 kΩ**
    * *Attenuation Coefficient:* $0.6$. Even at an emergency maximum of 5.0V from the sensor, the STM32 pin receives exactly 3.0V, ensuring core safety.

2.  **Low-Side Driver for the SSR:**
    To compensate for the voltage drops common in aftermarket SSR clones and to isolate the MCU ports from high current draw, the relay is driven via the `IRLML0030` MOSFET:
    * **Gate:** Connected to `PA4` + a **10 kΩ pull-down resistor to GND** (prevents accidental pump activation during MCU bootup/reset).
    * **Source:** Connected directly to the common ground (`GND`).
    * **Drain:** Connected to the negative control input `(-)` of the SSR (Terminal #4).
    * The positive control input `(+)` of the SSR (Terminal #3) is hooked up directly to the main **`+5V`** power line.

---

## 🧠 Key Engineering Features

### 1. Software Hysteresis (Anti-Chatter Protection)
To prevent rapid relay "chattering" or micro-switching at boundary values due to surface ripples, the firmware implements a configurable buffer gap. The thresholds are defined directly in physical centimeters:
* `Inside_ON_Threshold_CM = 20` (Starts drainage when water hits 20 cm).
* `Inside_OFF_Threshold_CM = 5` (Shuts down the pump only when water drops to 5 cm).

### 2. Moving Average Filtering
The incoming wastewater creates significant turbulence during pump operation. The firmware performs continuous cyclic sampling (averaging the last 10 ADC readings) to smooth out fluctuations. This keeps the display readouts stable and the progress bar fluid.

### 3. Integer-Only Math Optimization
All conversions from raw ADC digits to millivolts, kilopascals, and centimeters are handled using integer math (`int32_t`) with bit/digit shifts (multiplying by 10 and 100). This saves CPU clock cycles and completely eliminates the need for resource-heavy floating-point (`float`) libraries.

### 4. Ergonomic UI Design
The compact 128x32 screen layout is divided into two operational zones:
1.  **Top Row (Technical Info, Small Font):** Displays raw ADC counts, exact pressure in kPa, and system status (`IDL` for standby, `RUN` for active pumping).
2.  **Main Area (Large Font):** Features a high-visibility readout of the water level in centimeters (down to tenths) paired with a compact graphical progress bar (battery-style indicator) on the right side that dynamically fills as the volume rises.

---

## 📦 Dependencies & Flashing
Compiling this project via the Arduino IDE requires the following libraries:
* `Adafruit_GFX_Library`
* `Adafruit_SSD1306`

**Crucial Setup Notice:** The air tube must be connected to the pressure sensor nozzle **only when the tank is completely empty**. This traps the ambient atmospheric pressure inside the system as a true baseline. Connecting it while water is present will shift the hydrostatic calculations into incorrect negative values.