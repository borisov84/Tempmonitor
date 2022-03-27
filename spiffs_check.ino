// ESP32
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFSIniFile.h>
#include "SPIFFS.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <AsyncTCP.h>
#include <AsyncElegantOTA.h>
#include "ESP32FtpServer.h"

//для BME280
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

//для работы с microsd
#include "FS.h"
#include "SD.h"
#include "SPI.h"

//для таймера
#include <SimpleTimer.h>

//для LCD экрана
//#include <LiquidCrystal_I2C.h>
#include <LCD_1602_RUS.h> // русские буквы

#define Btn_GPIO 34
#define SEALEVELPRESSURE_HPA (1013.25)
#define loadingDelay 1000 //интервал между инициализацией компонентов при загрузке, в мс


bool firstStart = false; // если устройство не настроено
bool sdExist = true; // есть ли Sd карта

String wifi_ssid;
String wifi_pass;
String remoteServer;
int updateInterval;
String curTemperature;
String curHumidity;

//набор переменных для смены режима
String firstLine1;
String secondLine1;
String firstLine2;
String secondLine2;
boolean lcdmode = true; //Если True - 1 первый набор, если False - 2 набор переменных

// для Ini-файлов
const size_t bufferLen = 80;
char buffer[bufferLen];

volatile uint32_t debounce;
int md = 0; // режим отображения на экране
int prev_md = md; // предыдущий режим

int delayTime = 60000; //интервал измерения показаний, в мс (по умолчанию)

int timeOffset = 0; // смещение времени

String curDay;
String data_file;
char data_file_ch[14];

// Переменные для сохранения времени
String formattedDate;
String dayStamp;
String timeStamp;
String daySt_file;

// создание объекта Webserver
AsyncWebServer server(80);

// создание объекта FtpServer
FtpServer ftpSrv;   //set #define FTP_DEBUG in ESP32FtpServer.h to see ftp verbose on serial

// создание объекта Adafruit_BME280
Adafruit_BME280 bme;

// создание объекта SimpleTimer
SimpleTimer firstTimer;

// Создание объекта NTP клиента для получения времени
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// создание объекта lcd
LCD_1602_RUS lcd(0x3F, 16, 2);

// функция для изменения режимов отображения экрана (через прерывания)
void IRAM_ATTR chTxt()
{
  if (millis() - debounce >= 200 && digitalRead(34)) { //борьба с "дребезгом"
    debounce = millis();
    if (lcdmode) {
      md = 0;
    }
    else {
      md = 1;
    }
    lcdmode = !lcdmode;
  }
}

// функция вывода сообщений на экран
void lcd_change(int mod) {
  //mod - режим отображения, если 0 - первый комплект переменных
  if (mod == 0) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(firstLine1);
    lcd.setCursor(0, 1);
    lcd.print(secondLine1);
    lcdmode = true;
  }
  else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(firstLine2);
    lcd.setCursor(0, 1);
    lcd.print(secondLine2);
    lcdmode = false;
  }
}

// инициализация MicroSD модуля и карты памяти
void initMcSD() {
  Serial.println("Init microsd...");
  while (!SD.begin(5)) {
    // Если SD-модуль не инициализирован или отсутствует SD-карта
    Serial.println("Card Mount Failed");
    firstLine1 = "Ошибка!";
    secondLine1 = "SD карта не смонтирована";
    lcd_change(0);
    delay(500);
    sdExist = false;
  }

  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    secondLine1 = "SD карта не найдена";
    lcd_change(0);
    sdExist = false;
  }
  else {
    firstLine1 = "Загрузка...";
    secondLine1 = "SD Card: Ok";
    lcd_change(0);
    sdExist = true;
  }
}


// инициализация SPIFFS
void spiffs_begin() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    //  "При монтировании SPIFFS возникла ошибка"
    return;
  }
}

// удаление файла настроек (сброс на заводские настройки)
void factReset(String mod) {
  if (mod == "all"){
    if (SPIFFS.remove("/settings.ini")) {
      Serial.println("File settings.ini successfully deleted");
      //  "Файл успешно удален"
    }
    else {
      Serial.print("Deleting file failed!");
      //  "Не удалось удалить файл!"
    }
  }
  if (mod == "wifi") {
    if (SPIFFS.remove("/wifi.ini")) {
      Serial.println("File wifi.ini successfully deleted");
      //  "Файл успешно удален"
    }
    else {
      Serial.print("Deleting file failed!");
      //  "Не удалось удалить файл!"
    }
  }
}

// чтение index.html TODO: потом удалить
void get_index() {
  File file = SPIFFS.open("/index.html");
  if (!file) {
    Serial.println("Failed to open file for reading");
    //  "Не удалось открыть файл для чтения"
    return;
  }

  Serial.println("File Content:");
  //  "Содержимое файла:"
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}

// проверка - есть ли файл settings.ini
bool check_settings() {
  Serial.println("Checking settings.ini file...");

  if (SPIFFS.exists("/settings.ini") & SPIFFS.exists("/wifi.ini")) {
    Serial.println("File settings and wifi: ok");
    firstStart = false;
  }
  else {
    Serial.println("File settings.ini or wifi.ini not found");
    Serial.println("First start activated");
    firstStart = true;
  }
  return firstStart;
}

// вывод сообщений об ошибке
void printErrorMessage(uint8_t e, bool eol = true)
{
  switch (e) {
    case SPIFFSIniFile::errorNoError:
      Serial.print("no error");
      break;
    case SPIFFSIniFile::errorFileNotFound:
      Serial.print("file not found");
      break;
    case SPIFFSIniFile::errorFileNotOpen:
      Serial.print("file not open");
      break;
    case SPIFFSIniFile::errorBufferTooSmall:
      Serial.print("buffer too small");
      break;
    case SPIFFSIniFile::errorSeekError:
      Serial.print("seek error");
      break;
    case SPIFFSIniFile::errorSectionNotFound:
      Serial.print("section not found");
      break;
    case SPIFFSIniFile::errorKeyNotFound:
      Serial.print("key not found");
      break;
    case SPIFFSIniFile::errorEndOfFile:
      Serial.print("end of file");
      break;
    case SPIFFSIniFile::errorUnknownError:
      Serial.print("unknown error");
      break;
    default:
      Serial.print("unknown error value");
      break;
  }
  if (eol)
    Serial.println();
}

void readIniWifi() {
  SPIFFSIniFile ini_w("/wifi.ini");
  if (!ini_w.open()) {
    Serial.println("Wifi.ini file does not exist");
  }
  if (!ini_w.validate(buffer, bufferLen)) {
    Serial.print("ini file ");
    Serial.print(ini_w.getFilename());
    Serial.print(" not valid: ");
    printErrorMessage(ini_w.getError());
    // Cannot do anything else
    while (1)
      ;
  }
  Serial.println("Getting settings from wifi.ini");
  // Wifi SSID
  if (ini_w.getValue("wifi", "wifi_ssid", buffer, bufferLen)) {
    Serial.print("section 'wifi' has an entry 'wifi_ssid' with value ");
    Serial.println(buffer);
    wifi_ssid = buffer;
  }
  else {
    Serial.print("Could not read 'wifi_ssid' from section 'wifi', error was ");
    printErrorMessage(ini_w.getError());
  }
	// WIFI Password
  if (ini_w.getValue("wifi", "wifi_pass", buffer, bufferLen)) {
    Serial.print("section 'wifi' has an entry 'wifi_pass' with value ");
    Serial.println(buffer);
    wifi_pass = buffer;
  }
  else {
    Serial.print("Could not read 'wifi_pass' from section 'wifi', error was ");
    printErrorMessage(ini_w.getError());
  }
}


// чтение настроек из файла
void readIni() {
  SPIFFSIniFile ini("/settings.ini");
  if (!ini.open()) {
    Serial.print("Settings.ini does not exist");
    // Cannot do anything else
    while (1)
      ;
  }
  // Check the file is valid. This can be used to warn if any lines
  // are longer than the buffer.
  if (!ini.validate(buffer, bufferLen)) {
    Serial.print("ini file ");
    Serial.print(ini.getFilename());
    Serial.print(" not valid: ");
    printErrorMessage(ini.getError());
    // Cannot do anything else
    while (1)
      ;
  }

  Serial.println("Getting settings from settings.ini");

  // Смещение времени
  if (ini.getValue("general", "timeOffset", buffer, bufferLen)) {
    Serial.print("section 'general' has an entry 'timeOffset' with value ");
    Serial.println(buffer);
    timeOffset = atoi(buffer);
  }
  else {
    Serial.print("Could not read 'timeOffset' from section 'general', error was ");
    printErrorMessage(ini.getError());
  }
  // интервал измерения
  if (ini.getValue("general", "updateInterval", buffer, bufferLen)) {
    Serial.print("section 'general' has an entry 'updateInterval' with value ");
    Serial.println(buffer);
    updateInterval = atoi(buffer);
  }
  else {
    Serial.print("Could not read 'updateInterval' from section 'general', error was ");
    printErrorMessage(ini.getError());
  }
  // удаленный сервер для передачи данных
  if (ini.getValue("remote", "server", buffer, bufferLen)) {
    Serial.print("section 'remote' has an entry 'server' with value ");
    Serial.println(buffer);
    remoteServer = buffer;
  }
  else {
    Serial.print("Could not read 'remoteServer' from section 'remote', error was ");
    printErrorMessage(ini.getError());
  }

}

// инициализация Wifi: если firstStart = true, то создаем точку доступа,
// если false, то пробуем подключиться к сети Wifi
void initWifi(bool firstSt) {
  int connect_count;
  connect_count = 0;
  Serial.println("Starting wifi...");

  // Есил firstSt = true, то необходимо создать точку доступа

  if (firstSt) {
    secondLine1 = "Запуск AP";
    lcd_change(0);
    delay(loadingDelay);
    Serial.println("Starting access point mode");
    WiFi.softAP("Thermo", "12345678");
    IPAddress IP = WiFi.softAPIP();
    Serial.println(IP);
    firstLine1="Thermo";
    secondLine1=IP.toString();
    lcd_change(0);
    // страница базовых настроек
    server.begin();
    server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
      request->send(SPIFFS, "/new.html", String());
    });
    // при первой настройке вводятся настройки wifi сети и др параметров
    server.on("/send", HTTP_GET, [](AsyncWebServerRequest * request) {
      int paramsNr = request->params();
      Serial.println(paramsNr);
      for (int i = 0; i < paramsNr; i++) {
        AsyncWebParameter* p = request->getParam(i);
        Serial.print("Param name: ");
        Serial.println("Param " + p->name() + ": " + p->value());

        if (p->name() == "ssid") {
          if (p->value() != "") wifi_ssid = p->value();
        }
        if (p->name() == "pass") {
          if (p->value() != "") wifi_pass = p->value();
        }
        if (p->name() == "updateInterval") {
          if (p->value() != "") updateInterval = (p->value()).toInt();
        }
        if (p->name() == "remoteServer") {
          if (p->value() != "") remoteServer = p->value();
          Serial.println(remoteServer);
        }
        if (p->name() == "timeOffset") {
          if (p->value() != "") timeOffset = (p->value()).toInt();
        }
      }
      request->send(200, "text/plain", "Data saved. Reboot...");
      makeIni();
    });
  }
  else {
    // читаем настройки перед подключением
	readIniWifi();
  // вывод в com-порт данных о Wifi сети и пароле, для контроля
    Serial.println("Connecting to Wifi...");
    Serial.println("Wifi: " + wifi_ssid);
    Serial.println("Password: " + wifi_pass);

    firstLine1 = "Подключение";
    secondLine1 = wifi_ssid;
    lcd_change(0);


    WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
	  connect_count += 1;
	  Serial.print("Connect attempt: ");
	  Serial.println(connect_count);
	  // если количество попыток превышено, то сбрасываем на заводские (вдруг ошибка в параметрах wifi) и перезагружаем
	  if (connect_count == 15) {
		factReset("wifi");
		delay(3000); // ждем 3 секунды для удаления файла
		ESP.restart();
	  }
	}
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    server.begin();
    readIni();
  }
}

// функция обновления времени с NTP-сервера
void getNTPtime() {
  timeClient.begin();
  Serial.println("Установка времени");
  // Установка часового пояса (смещения от GMT): 1 час - 3600, 18000 - Екатеринбург +5
  timeClient.setTimeOffset(timeOffset);

  // обновление времени с NTP-сервера
  while (!timeClient.update()) {
    timeClient.forceUpdate();
  }
  // вывод даты и времени в формате YYYY-MM-DDTHH:mm:ssZ
  formattedDate = timeClient.getFormattedDate();
  Serial.println("Текущая дата: " + formattedDate);

  // вывод даты и времени
  int splitT = formattedDate.indexOf("T");
  dayStamp = formattedDate.substring(0, splitT);
  timeStamp = formattedDate.substring(splitT + 1, formattedDate.length() - 1); // время
  Serial.print("Сегодня: ");
  Serial.println(dayStamp);

  firstLine1 = "Сегодня";
  secondLine1 = dayStamp;
  lcd_change(0);
}


// Замена плейсхолдеров на значение температуры, влажности, времени, даты
String processor(const String& var) {
  Serial.println(var);
  if (var == "DATE") {
    return dayStamp;
  }
  if (var == "TIME") {
	return timeStamp;
  }
  if (var == "TEMP") {
	  return curTemperature;
  }
  if (var == "HUM") {
	  return curHumidity;
  }
  if (var == "IPADDR") {
    return WiFi.localIP().toString();
  }
  if (var == "UPDATEINTERVAL") {
    return String(updateInterval);
  }
  if (var == "SSID") {
    return wifi_ssid;
  }
  if (var == "PASS") {
    return wifi_pass;
  }
  if (var == "TIMEOFFSET") {
    return String(timeOffset);
  }
  if (var == "REMSERVER") {
    return remoteServer;
  }
  return String();
}

// Функция создания нового файла с настройками при первом запуске
void makeIni() {
  String iniFile;
  iniFile = "[general]\n";
  iniFile += "delayLoading=1000\n";
  iniFile += "updateInterval=" + String(updateInterval) + "\n";
  iniFile += "timeOffset=" + String(timeOffset) + "\n";
  iniFile += "[remote]\nserver=" + remoteServer;

  File file = SPIFFS.open("/settings.ini", FILE_WRITE);
  if (!file) {
    Serial.println("Cant open file for write");
  }
  else {
    file.print(iniFile);
    Serial.println("File settings.ini: ok");
  }
  file.close();

  iniFile = "[wifi]\nwifi_ssid=" + wifi_ssid + "\n";
  iniFile += "wifi_pass=" + wifi_pass + "\n";

  file = SPIFFS.open("/wifi.ini", FILE_WRITE);
  if (!file) {
    Serial.println("Cant open file wifi.ini for write");
  }
  else {
    file.print(iniFile);
    Serial.println("File wifi.ini: ok");
  }
  file.close();
  ESP.restart();
}


// запись данных в файл
void writeFile(fs::FS &fs, const char * path, String message) // const char * message)
{
  Serial.printf("Writing file: %s\n", path);

  if (fs.exists(path)) {
    Serial.println("File already exists");
  }
  else {
    File nFile = fs.open(path, FILE_WRITE);
    if (!nFile) {
      Serial.println("Failed to open file for writing");
    }
    if (nFile.print("Date;Time;Temperature;Humidity\n")) {
      Serial.println("File created. Headers written");
    }
    else {
      Serial.println("Error creating file");
    }
    nFile.close();
  }

  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}

// функция отрпавки данных на удаленный сервер
void sendData(String date, String tim, String temp, String hum) {
  HTTPClient http;
  WiFiClient client;
  http.begin(client, "http://" + remoteServer + "/getdata.php");
  // добавляем заголовок к запросу
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  // Данные для отправки запроса HTTP POST на сервер
  String httpRequestData = "Date=" + date + "&Time=" + tim + "&Temper=" + temp + "&Hum=" + hum;
  Serial.println("http://" + remoteServer + "/getdata.php" + httpRequestData);
  // отправка запроса
  int httpResponseCode = http.POST(httpRequestData);
  Serial.print("HTTP Response code: ");
  Serial.println(httpResponseCode);

  // освобождение ресурсов
  http.end();
}

// инициализация экрана
void lcdinit() {
  lcd.init();
  lcd.backlight();
  firstLine1 = "Загрузка...";
  lcd_change(0);
}

// инициализация датчика
void bmeinit() {
  Serial.println("Init BME280...");
  bool status;
  status = bme.begin(0x76);
  if (!status) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    secondLine1 = "BME280 не найден";
    lcd_change(0);
    while (1);
  }
  else {
    Serial.println("BME280 found!");
    secondLine1 = "BME280: Ok";
    lcd_change(0);
  }
}

void setup() {
  // обработка прерывания
  pinMode(Btn_GPIO, INPUT);
  attachInterrupt(Btn_GPIO, chTxt, RISING);

  // Инициализация последовательного соединение и выбор скорость передачи данных в бит/c
  Serial.begin(115200);
  Serial.println("Загрузка...");
  spiffs_begin();
  //  get_index();

  // инициализация LCD-экрана
  lcdinit();
  delay(loadingDelay);

  // инициализация датчика BME280
  bmeinit();
  delay(loadingDelay);

  // инициализация модуля MicroSD
  initMcSD();
  delay(loadingDelay);

  // подключение к Wifi
  initWifi(check_settings());

  // получение времени от NTP
  getNTPtime();
  delay(loadingDelay * 2);

  firstLine2 = "WiFi:" + wifi_ssid;
  lcd_change(1);
  secondLine2 = "IP:" + WiFi.localIP().toString();
  lcd_change(1);
  delay(loadingDelay);
  firstLine1 = "Темп: " + String(bme.readTemperature(), 2) + " C";
  secondLine1 = "Влаж: " + String(bme.readHumidity(), 2) + " %";
  lcd_change(0);

  // установка таймера
  firstTimer.setInterval(updateInterval*1000);

  // запуск FTP сервера
  ftpSrv.begin("esp32", "esp32");

  // запуск ElegantOTA и сервера
  AsyncElegantOTA.begin(&server);
  // server.begin();
  // стартовая страница
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
	request->send(SPIFFS, "/index.html", String(), false, processor);
  });

  // страница с показаниями
  server.on("/mes", HTTP_GET, [] (AsyncWebServerRequest * request){
	request->send(SPIFFS, "/mes.html",String(), false, processor);
  });

  // страница базовых настроек
  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(SPIFFS, "/settings.html", String(), false, processor);
  });
  // при первой настройке вводятся настройки wifi сети и др параметров
  server.on("/send", HTTP_GET, [](AsyncWebServerRequest * request) {
    int paramsNr = request->params();
    Serial.println(paramsNr);
    for (int i = 0; i < paramsNr; i++) {
      AsyncWebParameter* p = request->getParam(i);
      Serial.print("Param name: ");
      Serial.println("Param " + p->name() + ": " + p->value());

      if (p->name() == "ssid") {
        if (p->value() != "") wifi_ssid = p->value();
      }
      if (p->name() == "pass") {
        if (p->value() != "") wifi_pass = p->value();
      }
      if (p->name() == "updateInterval") {
        if (p->value() != "") updateInterval = (p->value()).toInt();
      }
      if (p->name() == "remoteServer") {
        if (p->value() != "") remoteServer = p->value();
      }
      if (p->name() == "timeOffset") {
        if (p->value() != "") timeOffset = (p->value()).toInt();
      }
    }
    request->send(200, "text/plain", "Data saved. Reboot...");
    makeIni();
  });
  curTemperature = String(bme.readTemperature(), 2);
  curHumidity = String(bme.readHumidity(), 2);
}



void loop() {
  // Обработка FTP
	ftpSrv.handleFTP();

	if (prev_md != md) // если нажималась кнопка и режим md сменился, то меняем текст на экране
  {
    lcd_change(md);
    prev_md = md;
    Serial.println("Режим отображения изменился");
  }

  if (firstTimer.isReady()) {


    if (sdExist) writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "timerReady\n");
    //    Serial.println(ESP.getFreeHeap());
    if (sdExist) writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "updateTime\n");
    if (WiFi.status() == WL_CONNECTED) {
      while (!timeClient.update()) {
        Serial.println("Время обновлено");
        if (sdExist) writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "time Updated\n");
        timeClient.forceUpdate();
        if (WiFi.status() != WL_CONNECTED){
          break;
        }
      }
    }


    // вывод даты и времени в формате YYYY-MM-DDTHH:mm:ssZ
    formattedDate = timeClient.getFormattedDate();

    // выделение даты и времени
    int splitT = formattedDate.indexOf("T");
    dayStamp = formattedDate.substring(0, splitT); // дата
    timeStamp = formattedDate.substring(splitT + 1, formattedDate.length() - 1); // время
    daySt_file = dayStamp;
    daySt_file.replace("-", "");

    data_file = "/" + daySt_file + ".csv";
    data_file.toCharArray(data_file_ch, sizeof(data_file_ch));

    // если таймер сработал, то опрашиваем датчик и выводим на экран
    curTemperature = String(bme.readTemperature(), 2);
    curHumidity = String(bme.readHumidity(), 2);
    firstLine1 = "Темп: " + curTemperature + " C";
    secondLine1 = "Влаж: " + curHumidity + " %";
    Serial.println(dayStamp + " " + timeStamp + " " + firstLine1 + " " + secondLine1);
    //записываем данные в файл на сд-карту
    if (sdExist) writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "write data to file\n");
    if (sdExist) writeFile(SD, data_file_ch, dayStamp + ";" + timeStamp + ";" + String(bme.readTemperature(), 2) + " C;" + String(bme.readHumidity(), 2) + "%\n");
    if (sdExist) writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "Data wrote ok\n");

    // отправляем страницу на сервер
    if (sdExist) writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "webserver update page\n");

    // если надо сбросить настройки прибора
    server.on("/reset", HTTP_GET, [](AsyncWebServerRequest * request) {
      request->send(SPIFFS, "/reset.html", String());
    });
    // сброс устройства по запросу get
    server.on("/factoryreset", HTTP_GET, [](AsyncWebServerRequest * request) {
        AsyncWebParameter* p = request->getParam(0);
        if (p->name() == "yes") {
          if (SPIFFS.remove("/settings.ini")) {
            Serial.println("File settings.ini succesfuly deleted");
          }
          else{
            Serial.println("File settings.ini not found. Nothing to delete");
          }
  		ESP.restart();
        }

      request->send(200, "text/plain", "Reseting...");
    });

    // стартовая страница
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/index.html", String(), false, processor); // SendHTML(dayStamp, timeStamp, curTemperature, curHumidity));
    });

//    server.send(200, "text/html", SendHTML(dayStamp, timeStamp, curTemperature, curHumidity));
    if (sdExist) writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "webserver update ok\n");
    if (md == 0) lcd_change(0);

    // отправка на сервер
    if (sdExist) writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "Send data to remote server\n");
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Wifi connected. Sending data...");
      if (sdExist) writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "Wifi connected. Send data\n");
      sendData(dayStamp, timeStamp, curTemperature, curHumidity);
      if (sdExist) writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "Send data ok\n");
    }
    else {
      Serial.println("No wifi connection!");
      if (sdExist) writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "Wifi not connected. Restart\n");
      ESP.restart();
    }
    firstTimer.reset();
  }
}
