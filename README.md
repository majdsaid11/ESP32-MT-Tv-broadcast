# ESP32-MT-Tv-broadcast

مشروع تجريبي يحاكي فكرة محطة بث صغيرة باستخدام موديولات **nRF24L01**.

الفكرة ببساطة أن عندنا جهازين:

- **Transmitter / المرسل**  
  مبني على ESP8266، يتصل بالواي فاي، يفتح صفحة تحكم من المتصفح، يجلب الوقت والطقس، ثم يرسل البيانات لاسلكياً عبر nRF24L01.

- **Receiver / المستقبل**  
  مبني على ESP32، يستقبل البيانات من nRF24L01 ويعرضها على شاشة TFT ST7789 بشكل قريب من قناة تلفزيون، مع عنوان ووقت وطقس وشريط أخبار متحرك.

---

## Main Idea

المرسل يقوم بإرسال أوامر نصية قصيرة إلى المستقبل، مثل:

```cpp
TITLE:MT CH 108
L1:TIME 6:30 PM
L2:WX 25.0C H40%
L3:Clear W10km/h
```

والمستقبل يقرأ هذه الرسائل ويعرضها على الشاشة.

تم استخدام نمط إرسال يشبه البث:

```cpp
Broadcast / No ACK
```

يعني المرسل يرسل الرسائل فقط، ولا ينتظر تأكيد وصول من المستقبل.

---

## Hardware Used

### Transmitter

- ESP8266 NodeMCU أو Wemos D1 Mini
- nRF24L01 أو nRF24L01+
- شاشة OLED SSD1306 اختيارية
- مكثف 10uF إلى 100uF للـ nRF24L01

### Receiver

- ESP32 DevKit
- nRF24L01 أو nRF24L01+
- شاشة TFT ST7789 240x320
- ضع مكثف 10uF إلى 100uF للـ nRF24L01 بحالة ماعندك منظم جهد
---

## Required Libraries

ثبت المكتبات التالية من Arduino IDE:

- RF24 by TMRh20
- ArduinoJson
- Adafruit GFX Library
- Adafruit SSD1306
- Adafruit ST7789 and ST7735 Library
- ESP8266 Board Package
- ESP32 Board Package

---

## WiFi Settings

قبل رفع كود المرسل على ESP8266، افتح ملف:

```text
MTTransmitter/MTTransmitter.ino
```

وعدّل بيانات الواي فاي:

```cpp
const char* WIFI_SSID     = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
```

---

## Weather Location

المرسل يستخدم Open-Meteo لجلب حالة الطقس.

يمكنك تعديل إحداثيات المدينة من هنا:

```cpp
const float LATITUDE  = 33.5138;
const float LONGITUDE = 36.2765;
```

ضع إحداثيات مدينتك بدل القيم الموجودة.

---

## Important nRF24 Settings

يجب أن تكون هذه القيم متطابقة في كود المرسل والمستقبل:

```cpp
#define RF_CHANNEL 108
#define PAYLOAD_SIZE 32
const byte address[6] = "00001";
```

إذا كانت هذه القيم مختلفة، لن يستطيع المستقبل قراءة الرسائل القادمة من المرسل.

---

## ESP8266 Transmitter Wiring

### nRF24L01 with ESP8266

| nRF24L01 Pin | ESP8266 Pin | GPIO |
|---|---|---|
| VCC | 3.3V | لا تستخدم 5V |
| GND | GND | GND |
| CE | D1 | GPIO5 |
| CSN / CS | D2 | GPIO4 |
| SCK | D5 | GPIO14 |
| MISO | D6 | GPIO12 |
| MOSI | D7 | GPIO13 |

---

## ESP32 Receiver Wiring

### nRF24L01 with ESP32

| nRF24L01 Pin | ESP32 Pin | Code Name |
|---|---|---|
| VCC | 3.3V | لا تستخدم 5V |
| GND | GND | GND |
| CE | GPIO4 | NRF_CE_PIN = 4 |
| CSN / CS | GPIO5 | NRF_CSN_PIN = 5 |
| SCK | GPIO18 | SCK_PIN = 18 |
| MISO | GPIO19 | MISO_PIN = 19 |
| MOSI | GPIO23 | MOSI_PIN = 23 |

---

## ST7789 Display Wiring with ESP32

| ST7789 Pin | ESP32 Pin | Code Name |
|---|---|---|
| VCC | 3.3V أو 5V حسب نوع الشاشة | VCC |
| GND | GND | GND |
| SCL / SCK | GPIO18 | SCK_PIN = 18 |
| SDA / MOSI | GPIO23 | MOSI_PIN = 23 |
| CS | GPIO13 | TFT_CS_PIN = 13 |
| DC / A0 | GPIO21 | TFT_DC_PIN = 21 |
| RST / RES | GPIO22 | TFT_RST_PIN = 22 |
| BL / LED | GPIO32 | TFT_BL_PIN = 32 |

> ملاحظة مهمة:  
> حتى لو كانت الشاشة تقبل 5V على VCC، خطوط الإشارة مع ESP32 يجب أن تبقى 3.3V.

---

## Important Note About nRF24L01 Power

موديول nRF24L01 حساس جداً للتغذية.

لأفضل استقرار:

- لا توصله على 5V أبداً.
- استخدم 3.3V ثابت.
- ضع مكثف بين VCC و GND قريب من الموديول.
- يفضل مكثف بقيمة بين 10uF و 100uF.
- إذا كان الاتصال يقطع، غالباً المشكلة من التغذية.

---

## OLED Note on the Transmitter

في جهاز المرسل تم استخدام شاشة OLED صغيرة لعرض معلومات البداية مثل:

- اسم المشروع
- عنوان IP
- قناة nRF24
- حالة التشغيل

لكن شاشة OLED في هذا التوصيل تستخدم نفس خطوط SPI أو نفس دبابيس الباص المطلوبة لتشغيل nRF24L01.

لذلك تم استخدام خدعة بسيطة:

1. عند تشغيل الجهاز، يتم تشغيل شاشة OLED.
2. يتم رسم معلومات البداية عليها مرة واحدة فقط.
3. بعد أول تحديث، تبقى الصورة ثابتة ومعلقة على الشاشة.
4. بعدها يترك الكود الباص للـ nRF24L01 حتى يرسل البيانات بدون تشويش.
5. لذلك لا يتم تحديث شاشة OLED بشكل مستمر أثناء عمل المحطة.

هذه الطريقة تمنع شاشة OLED من التأثير على إرسال nRF24L01، لأن تحديث الشاشة باستمرار على نفس الخطوط قد يسبب تقطيع أو مشاكل في الإرسال.

---

## How to Use

1. وصل القطع حسب الجداول الموجودة فوق.
2. افتح كود المرسل وعدل بيانات الواي فاي.
3. ارفع كود `MTTransmitter.ino` على ESP8266.
4. افتح Serial Monitor بسرعة:

```text
115200
```

5. بعد اتصال ESP8266 بالواي فاي، سيظهر عنوان IP.
6. افتح عنوان IP من المتصفح على نفس شبكة الواي فاي.
7. ارفع كود `MTReceiver.ino` على ESP32.
8. إذا كل شيء صحيح، ستظهر البيانات على شاشة المستقبل.

---

## Web Control Page

بعد تشغيل المرسل وفتح عنوان IP من المتصفح، يمكنك:

- إرسال شريط أخبار متحرك.
- إرسال رسالة مباشرة.
- تحديث الطقس.
- إرسال Ping.
- مسح شاشة المستقبل.
- إعادة إرسال بيانات القناة.

---

## Troubleshooting

إذا لم يعمل الاتصال بين المرسل والمستقبل:

- تأكد أن nRF24L01 موصول على 3.3V وليس 5V.
- (فقط بحالة لايوجد منظم جهد)ضع مكثف بين VCC و GND على nRF24L01.
- تأكد من توصيل CE و CSN بشكل صحيح.
- تأكد أن RF_CHANNEL نفسه في المرسل والمستقبل.
- تأكد أن address نفسه في المرسل والمستقبل.
- تأكد أن PAYLOAD_SIZE نفسه في الكودين.
- قرّب المرسل والمستقبل من بعض أثناء التجربة.
- افتح Serial Monitor وشاهد رسائل الخطأ.
- جرب تغيير RF_CHANNEL إذا كان هناك تشويش.

---

## Notes

هذا المشروع تعليمي وتجريبي، والهدف منه شرح فكرة إرسال البيانات لاسلكياً باستخدام nRF24L01 بين ESP8266 و ESP32، مع عرض المعلومات على شاشة بطريقة تشبه محطة بث صغيرة.

المشروع ليس نظام بث حقيقي، لكنه تجربة ممتعة لتعلم:

- ESP8266 WiFi
- ESP32 Display
- nRF24L01 Wireless Communication
- Web Server Control
- Arduino Projects
- SPI Devices
- TFT UI Design

---

## Author

Made by MajdTech
