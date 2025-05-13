#include <Wire.h>                       // подключаем библиотеку для работы с I²C
#include <SPI.h>                        // подключаем библиотеку для работы с SPI
#include <SD.h>                         // библиотека для работы с SD-картой
//#include <AsyncTCP.h>                   // (удалено) асинхронный TCP-стек для ESP32 — не нужен
#include <ESPAsyncWebServer.h>          // библиотека асинхронного веб-сервера
#include <ArduinoJson.h>                // библиотека для работы с JSON
#include <TFT_eSPI.h>                   // драйвер для TFT-дисплеев (ILI9341 и др.)
#include <XPT2046_Touchscreen.h>        // библиотека для контроллера сенсора XPT2046
#include <RTClib.h>                     // Adafruit RTClib: класс RTC_DS3231
#include <TCA9548.h>                    // библиотека robtillaart/TCA9548 для I²C-мультиплексора
#include <Adafruit_MCP23X17.h>          // драйвер для расширителя портов MCP23017

// ────────────── PIN & CONST DEFINITIONS ──────────────
#define SD_CS        13                // номер CS-пина для SD-карты
#define TFT_CS       15                // номер CS-пина для TFT-дисплея
#define TFT_DC       2                 // пин DC (Data/Command) для TFT
#define TFT_RST      4                 // пин RESET для TFT
#define TOUCH_CS     5                 // CS-пин сенсорного контроллера
#define TOUCH_IRQ    34                // IRQ-пин сенсора (необязательно)
#define I2C_SDA      21                // пин SDA для I²C
#define I2C_SCL      22                // пин SCL для I²C
#define SPI_SCLK     18                // пин SCLK для SPI
#define SPI_MISO     19                // пин MISO для SPI
#define SPI_MOSI     23                // пин MOSI для SPI

#define NUM_CHANNELS 8                 // число каналов у TCA9548A
#define DEVS_PER_CH  8                 // число MCP23017 на каждом канале
#define TOTAL_EXPS   (NUM_CHANNELS*DEVS_PER_CH)  // всего расширителей = 64
#define TOTAL_PINS   (TOTAL_EXPS*16)   // всего GPIO = 64×16 = 1024
#define PAIR_COUNT   (TOTAL_PINS*(TOTAL_PINS-1)/2) // число пар соединений

// ────────────── ГЛОБАЛЬНЫЕ ОБЪЕКТЫ ──────────────
TFT_eSPI            tft = TFT_eSPI();          // объект для управления TFT
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);   // объект для тача
RTC_DS3231          rtc;                       // объект для часов DS3231
TCA9548             tca(0x70, &Wire);          // мультиплексор на I²C-адресе 0x70
Adafruit_MCP23X17   mcp[TOTAL_EXPS];            // массив из 64 расширителя MCP23017
AsyncWebServer      server(80);                 // веб-сервер на порту 80

enum UIState { ST_MENU, ST_REF, ST_LIST, ST_TEST }; // варианты экранов интерфейса
UIState ui = ST_MENU;                           // текущее состояние — меню

// ────────────── ПРОТОТИПЫ ФУНКЦИЙ ──────────────
void initHardware();                            // инициализация «железа»
void initWebServer();                           // настройка веб-сервера
void drawMenu();                                // отрисовка главного меню
void handleTouch();                             // обработка касаний
void createReference(const String &name);       // создание эталона
void showFileList();                            // показ списка эталонов
void startTest(const String &refFile);          // запуск теста по эталону
void selectPinAsOutput(int idx, uint8_t level); // перевести пин в OUTPUT
void selectPinAsInput(int idx);                 // перевести пин в INPUT
bool readPin(int idx);                          // прочитать состояние пина

// ────────────── SETUP ──────────────
void setup() {
  Serial.begin(115200);                         // инициализация Serial для отладки
  Wire.begin(I2C_SDA, I2C_SCL);                 // запуск шины I²C
  SPI.begin(SPI_SCLK, SPI_MISO, SPI_MOSI);      // запуск шины SPI

  initHardware();                               // инициализация SD, RTC, TCA, MCP

  // TFT
  tft.init();                                   // инициализация дисплея
  tft.setRotation(1);                           // установка ориентации
  tft.fillScreen(TFT_BLACK);                    // очистка экрана
  tft.setTextColor(TFT_WHITE, TFT_BLACK);       // настройка цвета текста
  tft.setTextSize(2);                           // размер шрифта

  // Touch
  ts.begin();                                   // инициализация сенсора
  ts.setRotation(1);                            // ориентация тача

  initWebServer();                              // настройка веб-сервера
  drawMenu();                                   // отрисовка меню
}

// ────────────── MAIN LOOP ──────────────
void loop() {
  handleTouch();                                // проверяем касания
  // веб-сервер работает в фоне автоматически
}

// ────────────── ИНИЦИАЛИЗАЦИЯ «ЖЕЛЕЗА» ──────────────
void initHardware() {
  // SD
  if (!SD.begin(SD_CS)) {                       // пробуем инициализировать SD
    Serial.println("SD init failed");           // ошибка — сообщаем
    while (1) delay(100);                       // ждем в бесконечном цикле
  }
  // RTC
  rtc.begin();                                  // запускаем модуль DS3231

  // TCA9548A + MCP23017
  tca.begin();                                  // инициализируем мультиплексор
  for (int ch = 0; ch < NUM_CHANNELS; ch++) {   // по каждому каналу
    tca.enableChannel(ch);                      // открываем канал
    for (int dev = 0; dev < DEVS_PER_CH; dev++) { // по каждому MCP на канале
      int idx = ch * DEVS_PER_CH + dev;         // вычисляем индекс в массиве
      if (!mcp[idx].begin_I2C(0x20 + dev)) {    // инициализируем MCP по I²C
        Serial.println("Ошибка инициализации MCP");
      }
      for (int p = 0; p < 16; p++) {            // по всем 16 портам чипа
        mcp[idx].pinMode(p, INPUT);             // ставим режим «вход»
        mcp[idx].pinMode(p, INPUT_PULLUP);      // включаем внутренний pull-up
      }
    }
    tca.disableChannel(ch);                     // закрываем канал
  }
}

// ────────────── ИНИЦИАЛИЗАЦИЯ ВЕБ-СЕРВЕРА ──────────────
void initWebServer() {
  server.serveStatic("/", SD, "/www/")          // раздаём статику из /www на SD
        .setDefaultFile("index.html");          // по умолчанию — index.html

  // POST /api/reference — сохранение нового эталона
  server.on("/api/reference", HTTP_POST, [](AsyncWebServerRequest *req){
    if (!req->hasParam("name", true)) {         // проверяем параметр name
      req->send(400, "text/plain", "missing name");
      return;
    }
    String name = req->getParam("name", true)->value(); // получаем имя
    createReference(name);                      // создаём эталон
    req->send(200, "application/json", "{\"status\":\"ok\"}");
  });

  // GET /api/list — список эталонов
  server.on("/api/list", HTTP_GET, [](AsyncWebServerRequest *req){
    File root = SD.open("/refs");                // открываем папку /refs
    DynamicJsonDocument doc(2048);               // JSON-док
    JsonArray arr = doc.to<JsonArray>();         // JSON массив
    while (File f = root.openNextFile()) {       // перебираем файлы
      if (!f.isDirectory() && String(f.name()).endsWith(".bin")) {
        JsonObject o = arr.createNestedObject(); // добавляем объект
        o["file"] = String(f.name());           // имя файла
        o["size"] = f.size();                   // размер
      }
      f.close();                                 // закрываем файл
    }
    String out; serializeJson(doc, out);         // сериализуем JSON
    req->send(200, "application/json", out);     // отвечаем клиенту
  });

  // POST /api/upload — загрузка .bin
  server.on("/api/upload", HTTP_POST, [](AsyncWebServerRequest *req){
    req->send(200, "text/plain", "uploaded");   // базовый ответ
  }, [](AsyncWebServerRequest *req, String filename, size_t index,
        uint8_t *data, size_t len, bool final){
    static File uploadFile;                     // файл приёмник
    if (index == 0) {                           // начало загрузки
      String path = "/refs/" + filename;        // путь на SD
      uploadFile = SD.open(path, FILE_WRITE);   // открываем
    }
    if (uploadFile) uploadFile.write(data, len); // пишем chunk
    if (final) uploadFile.close();               // конец — закрываем
  });

  // GET /api/download — отдача JSON из .bin
  server.on("/api/download", HTTP_GET, [](AsyncWebServerRequest *req){
    if (!req->hasArg("file")) {
      req->send(400, "text/plain", "no file");
      return;
    }
    String fn = req->arg("file");                // имя файла
    File f = SD.open("/refs/" + fn, FILE_READ);   // открываем
    if (!f) {
      req->send(404, "text/plain", "not found");
      return;
    }
    uint32_t ts = 0, cnt = 0;
    f.read((uint8_t*)&ts, 4);                    // читаем timestamp
    f.read((uint8_t*)&cnt, 4);                   // читаем count
    DynamicJsonDocument doc(cnt + 256);
    doc["file"] = fn; doc["timestamp"] = ts;     // записываем метаданные
    JsonArray data = doc.createNestedArray("data");
    for (uint32_t i = 0; i < cnt; i++) {
      uint8_t b; f.read(&b,1);                   // читаем каждый байт
      data.add(b != 0);                          // булево в JSON
    }
    f.close();                                   // закрываем файл
    String out; serializeJson(doc, out);         // сериализуем
    req->send(200, "application/json", out);     // отдаем клиенту
  });

  // POST /api/test — запуск теста
  server.on("/api/test", HTTP_POST, [](AsyncWebServerRequest *req){
    if (!req->hasParam("file", true)) {
      req->send(400, "text/plain", "no ref");
      return;
    }
    String fn = req->getParam("file", true)->value();
    startTest(fn);                               // выполняем сравнение
    req->send(200, "application/json", "{\"status\":\"ok\"}");
  });

  server.begin();                               // запускаем сервер
}

// ────────────── TOUCH & MENU ──────────────
void drawMenu() {
  tft.fillScreen(TFT_BLACK);                    // очищаем экран
  tft.setCursor(20, 40);  tft.println("1. Новый эталон"); // пункт 1
  tft.setCursor(20, 80);  tft.println("2. Список эталонов"); // пункт 2
  tft.setCursor(20,120);  tft.println("3. Тест");       // пункт 3
}

void handleTouch() {
  if (!ts.tirqTouched()) return;                // если нет касания — выход
  TS_Point p = ts.getPoint();                   // получаем координаты
  int y = map(p.y, 240, 3800, 0, tft.height());  // нормируем Y
  if      (y < 80)  ui = ST_REF;                 // первая строка
  else if (y < 120) ui = ST_LIST;                // вторая
  else if (y < 160) ui = ST_TEST;                // третья
  else return;                                   // вне кнопок
  tft.fillScreen(TFT_BLACK);                     // очистка экрана
  switch (ui) {
    case ST_REF: {
      tft.setCursor(10,10); tft.println("Введите имя в Serial");
      Serial.println("REF_NAME?");               // запрашиваем имя
      while (!Serial.available()) delay(10);     // ждём ввода
      String name = Serial.readStringUntil('\n');
      createReference(name);                     // создаём эталон
      ui = ST_MENU; drawMenu();                  // возвращаем меню
      break;
    }
    case ST_LIST:
      showFileList();                            // показываем список
      ui = ST_MENU; drawMenu();                  // возвращаем меню
      break;
    case ST_TEST: {
      File root = SD.open("/refs"); String fn;
      while (File f = root.openNextFile()) {     // выбираем первый файл
        if (!f.isDirectory() && fn=="") fn = f.name();
        f.close();
      }
      root.close();
      startTest(fn);                             // запускаем тест
      ui = ST_MENU; drawMenu();                  // возвращаем меню
      break;
    }
    default: break;
  }
}

// ────────────── CREATE REFERENCE ──────────────
void createReference(const String &name) {
  String path = "/refs/" + name + ".bin";        // путь на SD
  File f = SD.open(path, FILE_WRITE);            // открываем файл
  uint32_t ts = rtc.now().unixtime();            // текущее Unix-время
  f.write((uint8_t*)&ts, 4);                     // записываем 4 байта
  uint32_t cnt = PAIR_COUNT;                     // число пар
  f.write((uint8_t*)&cnt, 4);                    // записываем 4 байта
  size_t k = 0;                                  // счётчик
  for (int i = 0; i < TOTAL_PINS - 1; i++) {     // по всем GPIO
    selectPinAsOutput(i, HIGH);                  // включаем i-й порт
    for (int j = i + 1; j < TOTAL_PINS; j++) {   // по j>i
      bool v = readPin(j);                       // читаем j-й порт
      uint8_t b = v ? 1 : 0;                     // 0/1
      f.write(&b, 1);                            // в файл
      ++k;                                       // +1 к счётчику
      float p = float(k) / cnt;                  // прогресс 0..1
      int w = (tft.width() - 20) * p;            // ширина бара
      tft.fillRect(10, tft.height() - 20, w, 10, TFT_GREEN); // рисуем
    }
    selectPinAsInput(i);                         // возвращаем вход
  }
  f.close();                                     // закрываем файл
  tft.fillScreen(TFT_BLACK);                     // очистка экрана
  tft.setCursor(10,10); tft.println("Эталон сохранён"); // сообщение
}

// ────────────── SHOW FILE LIST ──────────────
void showFileList() {
  tft.fillScreen(TFT_BLACK);                    // очищаем экран
  tft.setCursor(10,10); tft.println("Эталоны:"); // заголовок
  File dir = SD.open("/refs");                   // открываем /refs
  int y = 40;                                    // начальная Y-координата
  while (File f = dir.openNextFile()) {          // перебор файлов
    if (!f.isDirectory() && String(f.name()).endsWith(".bin")) {
      tft.setCursor(10,y); tft.print(f.name());  // выводим имя
      y += 20;                                   // новая строка
      if (y > tft.height() - 20) break;          // если экран заполнен
    }
    f.close();                                   // закрываем файл
  }
  dir.close();                                   // закрываем папку
}

// ────────────── RUN TEST ──────────────
void startTest(const String &refFile) {
  File f = SD.open("/refs/" + refFile, FILE_READ); // открываем эталон
  uint32_t ts_ref, cnt;
  f.read((uint8_t*)&ts_ref, 4);                   // читаем timestamp
  f.read((uint8_t*)&cnt, 4);                      // читаем count
  std::vector<uint8_t> ref(cnt);                  // буфер данных
  f.read(ref.data(), cnt);                        // загружаем данные
  f.close();                                      // закрываем файл

  std::vector<uint32_t> diffs;                    // массив отличий
  size_t idx = 0;                                 // индекс пары
  for (int i = 0; i < TOTAL_PINS - 1; i++) {
    selectPinAsOutput(i, HIGH);                   // включаем i-й порт
    for (int j = i + 1; j < TOTAL_PINS; j++) {
      bool now = readPin(j);                      // текущее состояние
      if (now != (ref[idx] != 0)) diffs.push_back(idx); // если отличается — сохраняем
      ++idx;                                      // +1 к индексу
      float p = float(idx) / cnt;                 // прогресс
      int w = (tft.width() - 20) * p;
      tft.fillRect(10, tft.height() - 20, w, 10, TFT_BLUE); // рисуем бар
    }
    selectPinAsInput(i);                          // возвращаем вход
  }

  tft.fillScreen(TFT_BLACK);                      // очищаем экран
  tft.setCursor(10,10);
  tft.printf("Тест завершён\nОшибок: %d", diffs.size()); // выводим результат
}

// ────────────── LOW-LEVEL HELPERS ──────────────
void selectPinAsOutput(int idx, uint8_t level) {
  int exp = idx / 16;                             // номер расширителя
  int ch  = exp / DEVS_PER_CH;                    // канал TCA
  int pin = idx % 16;                             // номер пина внутри MCP
  tca.enableChannel(ch);                          // открываем канал
  mcp[exp].pinMode(pin, OUTPUT);                  // делаем пин выходом
  mcp[exp].digitalWrite(pin, level);              // задаём уровень
}

void selectPinAsInput(int idx) {
  int exp = idx / 16;
  int ch  = exp / DEVS_PER_CH;
  int pin = idx % 16;
  mcp[exp].pinMode(pin, INPUT);                   // ставим вход
  mcp[exp].pinMode(pin, INPUT_PULLUP);            // включаем pull-up
  tca.disableChannel(ch);                         // закрываем канал
}

bool readPin(int idx) {
  int exp = idx / 16;
  int ch  = exp / DEVS_PER_CH;
  int pin = idx % 16;
  tca.enableChannel(ch);                          // открываем канал
  bool v = mcp[exp].digitalRead(pin);             // читаем пин
  tca.disableChannel(ch);                         // закрываем канал
  return v;                                       // возвращаем значение
}
