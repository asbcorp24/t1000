#include <Wire.h>                       // подключаем библиотеку для работы с I²C
#include <SPI.h>                        // подключаем библиотеку для работы с SPI
#include <SD.h>                         // подключаем библиотеку для работы с SD-картой
#include <ESPAsyncWebServer.h>          // подключаем библиотеку асинхронного веб-сервера
#include <ArduinoJson.h>                // подключаем библиотеку для работы с JSON
#include <TFT_eSPI.h>                   // подключаем драйвер для TFT-дисплеев
#include <XPT2046_Touchscreen.h>        // подключаем библиотеку для контроллера сенсора XPT2046
#include <RTClib.h>                     // подключаем Adafruit RTClib для работы с DS3231
#include <TCA9548.h>                    // подключаем библиотеку для I²C-мультиплексора TCA9548A
#include <Adafruit_MCP23X17.h>          // подключаем драйвер для расширителя MCP23017
#include <ETH.h>                        // подключаем библиотеку для Ethernet (LAN8720)
#include <HTTPClient.h>                 // подключаем HTTP-клиент для отправки данных

// ────────────── Ethernet LAN8720 ──────────────
#define ETH_PHY_TYPE  ETH_PHY_LAN8720   // задаём тип PHY — LAN8720
#define ETH_PHY_ADDR  0                 // задаём адрес PHY на шине MDIO
#define ETH_PHY_MDC   23                // пин ESP32, подключённый к MDC LAN8720
#define ETH_PHY_MDIO  18                // пин ESP32, подключённый к MDIO LAN8720
#define ETH_PHY_POWER -1                // если нужен пин питания PHY, иначе −1
#define ETH_CLK_MODE  ETH_CLOCK_GPIO0_IN// режим получения тактовой частоты PHY
const char* SERVER_URL = "http://192.168.1.100/api/testResult"; // URL сервера для POST

// ────────────── PIN & CONST DEFINITIONS ──────────────
#define SD_CS        13                // CS-пин SD-карты
#define TFT_CS       15                // CS-пин TFT-дисплея
#define TFT_DC       2                 // DC-пин TFT-дисплея
#define TFT_RST      4                 // RST-пин TFT-дисплея
#define TOUCH_CS     5                 // CS-пин сенсора XPT2046
#define TOUCH_IRQ    34                // IRQ-пин сенсора XPT2046 (необязательно)
#define I2C_SDA      21                // SDA-пин I²C
#define I2C_SCL      22                // SCL-пин I²C
#define SPI_SCLK     18                // SCLK-пин SPI
#define SPI_MISO     19                // MISO-пин SPI
#define SPI_MOSI     23                // MOSI-пин SPI

#define NUM_CHANNELS 8                 // число каналов в мультиплексоре
#define DEVS_PER_CH  8                 // число MCP23017 на каждом канале
#define TOTAL_EXPS   (NUM_CHANNELS*DEVS_PER_CH) // общее число расширителей
#define TOTAL_PINS   (TOTAL_EXPS*16)   // общее число GPIO (64×16)
#define PAIR_COUNT   (TOTAL_PINS*(TOTAL_PINS-1)/2) // общее число проверяемых пар

// ────────────── ГЛОБАЛЬНЫЕ ОБЪЕКТЫ ──────────────
TFT_eSPI            tft = TFT_eSPI();          // объект управления TFT
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);   // объект тач-контроллера
RTC_DS3231          rtc;                       // объект RTC DS3231
TCA9548             tca(0x70, &Wire);          // объект I²C-мультиплексора (адрес 0x70)
Adafruit_MCP23X17   mcp[TOTAL_EXPS];            // массив из 64 MCP23017
AsyncWebServer      server(80);                 // веб-сервер на порту 80

enum UIState { ST_MENU, ST_REF, ST_LIST, ST_TEST }; // состояния UI
UIState ui = ST_MENU;                           // текущее состояние — меню

static bool eth_connected = false;              // флаг Ethernet-соединения

// ────────────── ПРОТОТИПЫ ФУНКЦИЙ ──────────────
void initHardware();                            // инициализация аппаратуры
void initWebServer();                           // настройка веб-сервера
void drawMenu();                                // отрисовка меню на TFT
void handleTouch();                             // обработка касаний
void createReference(const String &name);       // создание эталонного файла
void showFileList();                            // отображение списка эталонов
void startTest(const String &refFile);          // запуск теста по эталону
void selectPinAsOutput(int idx, uint8_t level); // настроить пин как выхода
void selectPinAsInput(int idx);                 // настроить пин как входа
bool readPin(int idx);                          // прочитать состояние пина
void sendResult(const String &refFile, uint32_t ts_ref, const std::vector<uint32_t> &diffs); // отправить результат

// ────────────── ОБРАБОТЧИК ETHERNET ──────────────
void onEthEvent(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_GOT_IP:            // получили IP
      Serial.print("ETH IP: "); Serial.println(ETH.localIP());
      eth_connected = true;                   // устанавливаем флаг
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:      // отключились
    case ARDUINO_EVENT_ETH_STOP:              // Ethernet остановлен
      Serial.println("ETH Disconnected");
      eth_connected = false;                  // сбрасываем флаг
      break;
    default:
      break;
  }
}

// ────────────── SETUP ──────────────
void setup() {
  Serial.begin(115200);                        // старт Serial для отладки
  Wire.begin(I2C_SDA, I2C_SCL);                // инициализация I²C
  SPI.begin(SPI_SCLK, SPI_MISO, SPI_MOSI);     // инициализация SPI

  WiFi.onEvent(onEthEvent);                    // подписываемся на события Ethernet
  ETH.begin();                                 // запускаем Ethernet

  initHardware();                              // инициализируем SD, RTC, TCA, MCP

  tft.init(); tft.setRotation(1);              // инициализация TFT
  tft.fillScreen(TFT_BLACK);                   // очистка экрана
  tft.setTextColor(TFT_WHITE, TFT_BLACK);      // цвет текста
  tft.setTextSize(2);                          // размер шрифта

  ts.begin(); ts.setRotation(1);               // инициализация сенсора

  initWebServer();                             // запуск веб-сервера
  drawMenu();                                  // отрисовка меню
}

void loop() {
  handleTouch();                               // обработка касаний в цикле
}

// ────────────── ИНИЦИАЛИЗАЦИЯ HARDWARE ──────────────
void initHardware() {
  if (!SD.begin(SD_CS)) {                      // пробуем инициализировать SD
    Serial.println("SD init failed");          // сообщаем об ошибке
    while(1) delay(100);                       // застреваем навечно
  }
  rtc.begin();                                 // инициализация RTC

  tca.begin();                                 // инициализация мультиплексора
  for (int ch = 0; ch < NUM_CHANNELS; ch++) {  // по каналам
    tca.enableChannel(ch);                     // включаем канал
    for (int dev = 0; dev < DEVS_PER_CH; dev++) { // по устройствам на канале
      int idx = ch * DEVS_PER_CH + dev;        // вычисляем индекс
      if (!mcp[idx].begin_I2C(0x20 + dev)) {   // инициализация MCP
        Serial.printf("MCP %d init failed\n", idx);
      }
      for (int p = 0; p < 16; p++) {           // по портам MCP
        mcp[idx].pinMode(p, INPUT);            // ставим вход
        mcp[idx].pinMode(p, INPUT_PULLUP);     // включаем pull-up
      }
    }
    tca.disableChannel(ch);                    // отключаем канал
  }
}

// ────────────── НАСТРОЙКА WEB-SERVER ──────────────
void initWebServer() {
  server.serveStatic("/", SD, "/www/")         // раздача статических файлов
        .setDefaultFile("index.html");         // default = index.html

  server.on("/api/reference", HTTP_POST, [](AsyncWebServerRequest *req){
    if (!req->hasParam("name", true)) {        // проверяем параметр name
      req->send(400, "text/plain", "missing name");
      return;
    }
    String name = req->getParam("name", true)->value();
    createReference(name);                     // создаём эталон
    req->send(200, "application/json", "{\"status\":\"ok\"}");
  });

  server.on("/api/list", HTTP_GET, [](AsyncWebServerRequest *req){
    File root = SD.open("/refs");               // открываем папку
    DynamicJsonDocument doc(2048);
    auto arr = doc.to<JsonArray>();
    while (File f = root.openNextFile()) {      // перебираем файлы
      if (!f.isDirectory() && String(f.name()).endsWith(".bin")) {
        auto o = arr.createNestedObject();
        o["file"] = f.name(); o["size"] = f.size();
      }
      f.close();
    }
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/api/upload", HTTP_POST, [](AsyncWebServerRequest *req){
    req->send(200, "text/plain", "uploaded");
  }, [](AsyncWebServerRequest *req, String fn, size_t idx, uint8_t *data, size_t len, bool fin){
    static File uf;
    if (idx == 0) uf = SD.open("/refs/" + fn, FILE_WRITE);
    uf.write(data, len);
    if (fin) uf.close();
  });

  server.on("/api/download", HTTP_GET, [](AsyncWebServerRequest *req){
    if (!req->hasArg("file")) {
      req->send(400, "text/plain", "no file");
      return;
    }
    String fn = req->arg("file");
    File f = SD.open("/refs/" + fn, FILE_READ);
    if (!f) {
      req->send(404, "text/plain", "not found");
      return;
    }
    uint32_t ts, cnt;
    f.read((uint8_t*)&ts, 4);
    f.read((uint8_t*)&cnt, 4);
    DynamicJsonDocument doc(cnt + 256);
    doc["file"] = fn; doc["timestamp"] = ts;
    auto data = doc.createNestedArray("data");
    for (uint32_t i = 0; i < cnt; i++) {
      uint8_t b; f.read(&b, 1);
      data.add(b != 0);
    }
    f.close();
    String out; serializeJson(doc, out);
    req->send(200, "application/json", out);
  });

  server.on("/api/test", HTTP_POST, [](AsyncWebServerRequest *req){
    if (!req->hasParam("file", true)) {
      req->send(400, "text/plain", "no ref");
      return;
    }
    startTest(req->getParam("file", true)->value());
    req->send(200, "application/json", "{\"status\":\"ok\"}");
  });

  server.begin();                              // запускаем сервер
}

void drawMenu() {
  tft.fillScreen(TFT_BLACK);                   // очищаем экран
  tft.setCursor(20, 40); tft.println("1. Новый эталон");   // пункт меню 1
  tft.setCursor(20, 80); tft.println("2. Список эталонов"); // пункт меню 2
  tft.setCursor(20,120); tft.println("3. Тест");          // пункт меню 3
}

void handleTouch() {
  if (!ts.tirqTouched()) return;               // если нет касания — выходим
  auto p = ts.getPoint();                      // получаем координаты касания
  int y = map(p.y, 240, 3800, 0, tft.height()); // нормируем Y
  if      (y < 80)  ui = ST_REF;               // выбор пункта меню
  else if (y < 120) ui = ST_LIST;
  else if (y < 160) ui = ST_TEST;
  else return;
  tft.fillScreen(TFT_BLACK);                   // очищаем экран
  if (ui == ST_REF) {                          // если выбор — эталон
    tft.setCursor(10,10); tft.println("Введите имя в Serial");
    Serial.println("REF_NAME?");
    while (!Serial.available()) delay(10);
    createReference(Serial.readStringUntil('\n'));
    ui = ST_MENU; drawMenu();
  } else if (ui == ST_LIST) {                  // если выбор — список
    showFileList(); ui = ST_MENU; drawMenu();
  } else if (ui == ST_TEST) {                  // если выбор — тест
    File root = SD.open("/refs"); String fn;
    while (File f = root.openNextFile()) {
      if (!f.isDirectory() && fn == "") fn = f.name();
      f.close();
    }
    root.close();
    startTest(fn);
    ui = ST_MENU; drawMenu();
  }
}

void createReference(const String &name) {
  File f = SD.open("/refs/" + name + ".bin", FILE_WRITE); // открываем файл
  uint32_t ts = rtc.now().unixtime();            // текущее время
  f.write((uint8_t*)&ts, 4);                     // записываем метку времени
  uint32_t cnt = PAIR_COUNT;                     // общее число пар
  f.write((uint8_t*)&cnt, 4);                    // записываем счёт
  size_t k = 0;
  for (int i = 0; i < TOTAL_PINS - 1; i++) {     // по всем GPIO
    selectPinAsOutput(i, HIGH);                  // включаем i-й
    for (int j = i + 1; j < TOTAL_PINS; j++) {   // по остальным
      bool v = readPin(j);                       // читаем состояние
      uint8_t b = v ? 1 : 0;                     // 0 или 1
      f.write(&b, 1);                            // записываем в файл
      k++; float p = float(k) / cnt;             // прогресс 0..1
      int w = (tft.width() - 20) * p;            // ширина прогресс-бара
      tft.fillRect(10, tft.height() - 20, w, 10, TFT_GREEN); // рисуем бар
    }
    selectPinAsInput(i);                         // возвращаем вход
  }
  f.close();                                     // закрываем файл
  tft.fillScreen(TFT_BLACK);                     // очищаем экран
  tft.setCursor(10,10); tft.println("Эталон сохранён"); // выводим сообщение
}

void showFileList() {
  tft.fillScreen(TFT_BLACK);                    // очищаем экран
  tft.setCursor(10,10); tft.println("Эталоны:"); // заголовок
  File dir = SD.open("/refs");                   // открываем папку
  int y = 40;
  while (File f = dir.openNextFile()) {          // перебираем файлы
    if (!f.isDirectory() && String(f.name()).endsWith(".bin")) {
      tft.setCursor(10,y); tft.print(f.name());  // выводим имя
      y += 20;                                   // смещаем вниз
      if (y > tft.height() - 20) break;          // проверяем границу
    }
    f.close();
  }
  dir.close();
}

void startTest(const String &refFile) {
  File f = SD.open("/refs/" + refFile, FILE_READ); // читаем эталон
  uint32_t ts_ref, cnt;
  f.read((uint8_t*)&ts_ref, 4);                   // читаем время
  f.read((uint8_t*)&cnt, 4);                      // читаем счёт
  std::vector<uint8_t> ref(cnt);
  f.read(ref.data(), cnt);                        // загружаем данные
  f.close();

  std::vector<uint32_t> diffs;
  size_t idx = 0;
  for (int i = 0; i < TOTAL_PINS - 1; i++) {
    selectPinAsOutput(i, HIGH);
    for (int j = i + 1; j < TOTAL_PINS; j++) {
      bool now = readPin(j);
      if (now != (ref[idx] != 0)) // если состояние изменилось
        diffs.push_back(idx);     // сохраняем индекс
      idx++;
      float p = float(idx) / cnt;
      int w = (tft.width() - 20) * p;
      tft.fillRect(10, tft.height() - 20, w, 10, TFT_BLUE);
    }
    selectPinAsInput(i);
  }

  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10,10);
  tft.printf("Тест завершён\nОшибок: %d", diffs.size()); // выводим количество

  sendResult(refFile, ts_ref, diffs);              // отправляем результат
}

void sendResult(const String &refFile, uint32_t ts_ref, const std::vector<uint32_t> &diffs) {
  if (!eth_connected) {                           // проверяем связь
    Serial.println("Нет Ethernet-соединения, не отправлено");
    return;
  }
  DynamicJsonDocument doc(1024 + diffs.size()*4);
  doc["file"] = refFile;                          // имя файла
  doc["timestamp"] = ts_ref;                      // метка времени
  auto arr = doc.createNestedArray("diffs");      // создаём массив отличий
  for (auto &d : diffs) arr.add(d);               // заполняем
  String payload; serializeJson(doc, payload);     // сериализуем JSON

  HTTPClient http;
  http.begin(SERVER_URL);                         // указываем URL
  http.addHeader("Content-Type","application/json");
  int code = http.POST(payload);                  // POST запрос
  if (code > 0)
    Serial.printf("POST %s => %d\n", SERVER_URL, code);
  else
    Serial.printf("HTTP error: %s\n", http.errorToString(code).c_str());
  http.end();
}

void selectPinAsOutput(int idx, uint8_t level) {
  int exp = idx / 16;                             // номер расширителя
  int ch  = exp / DEVS_PER_CH;                    // номер канала
  int pin = idx % 16;                             // номер пина внутри MCP
  tca.enableChannel(ch);                          // открываем канал
  mcp[exp].pinMode(pin, OUTPUT);                  // устанавливаем выход
  mcp[exp].digitalWrite(pin, level);              // задаём уровень
}

void selectPinAsInput(int idx) {
  int exp = idx / 16;
  int ch  = exp / DEVS_PER_CH;
  int pin = idx % 16;
  mcp[exp].pinMode(pin, INPUT);                   // устанавливаем вход
  mcp[exp].pinMode(pin, INPUT_PULLUP);            // включаем pull-up
  tca.disableChannel(ch);                         // закрываем канал
}

bool readPin(int idx) {
  int exp = idx / 16;
  int ch  = exp / DEVS_PER_CH;
  int pin = idx % 16;
  tca.enableChannel(ch);                          // открываем канал
  bool v = mcp[exp].digitalRead(pin);             // читаем состояние
  tca.disableChannel(ch);                         // закрываем канал
  return v;                                       // возвращаем значение
}
