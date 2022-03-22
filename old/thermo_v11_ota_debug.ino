//подключение библиотек

//для BME280
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

//для работы с microsd
#include "FS.h"
#include "SD.h"
#include "SPI.h"

//для LCD экрана
#include <LiquidCrystal_I2C.h>

//для ini файлов
#include <IniFile.h>

//для ESP32
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include "ESP32FtpServer.h"
// для OTA
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>

//для таймера
#include <SimpleTimer.h>


#define Btn_GPIO 34
#define SEALEVELPRESSURE_HPA (1013.25)
#define loadingDelay 1000 //интервал между инициализацией компонентов при загрузке, в мс

// Создание объекта NTP клиента для получения времени
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// создание объекта Webserver
AsyncWebServer server(80);

// создание объекта FtpServer
FtpServer ftpSrv;   //set #define FTP_DEBUG in ESP32FtpServer.h to see ftp verbose on serial

// создание объекта Adafruit_BME280
Adafruit_BME280 bme;

// создание объекта SimpleTimer
SimpleTimer firstTimer;

// создание объекта lcd
LiquidCrystal_I2C lcd(0x3F, 16, 2);

const char *inifile = "/settings.ini"; // название файла с настройками
const size_t bufferLen = 80;
char buffer[bufferLen];
String wifi_ssid;
String wifi_pass;
String curTemperature;
String curHumidity;

//набор переменных для смены режима
String firstLine1;
String secondLine1;
String firstLine2;
String secondLine2;
boolean lcdmode = true; //Если True - 1 первый набор, если False - 2 набор переменных

volatile uint32_t debounce;
int md = 0; // режим отображения на экране
int prev_md = md; // предыдущий режим

int delayTime = 60000; //интервал измерения показаний, в мс (по умолчанию)

int timeOffset = 0; // смещение времени

String remServer; // сервер для передачи данных

String curDay;
String data_file;
char data_file_ch[14];

// Переменные для сохранения времени
String formattedDate;
String dayStamp;
String timeStamp;
String daySt_file;

//File root;

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

//функция вывода сообщений на экран
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
    firstLine1 = "Error!";
    secondLine1 = "Card Mount Failed";
    lcd_change(0);
    delay(500);
  }

  uint8_t cardType = SD.cardType();

  while (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    secondLine1 = "SD card not found";
    lcd_change(0);
  }
  firstLine1 = "Loading...";
  secondLine1 = "SD Card: Ok";
  lcd_change(0);
}

// чтение настроек из ini-файла
void getValues() {
  IniFile ini(inifile);
  while (!ini.open()) {
    Serial.print("Ini file ");
    Serial.print(inifile);
    Serial.println(" does not exist");
    secondLine1 = "Ini file not found";
    lcd_change(0);
  }
  Serial.println("Ini file exists");
  secondLine1 = "Ini file: Ok";
  lcd_change(0);
  delay(loadingDelay);
  secondLine1 = "Reading settings...";
  Serial.println(secondLine1);
  lcd_change(0);

  // чтение настроек из ini-файла
  // настройки wifi-подключения в режиме STA
  ini.getValue("wifi", "wifi_ssid", buffer, bufferLen);
  wifi_ssid = buffer;
  Serial.println("Wifi SSID: " + wifi_ssid);
  ini.getValue("wifi", "wifi_pass", buffer, bufferLen);
  wifi_pass = buffer;
  Serial.println("Wifi Password: " + wifi_pass);
  // пауза между измерениями
  ini.getValue("general", "updateInterval", buffer, bufferLen);
  delayTime = atoi(buffer);
  Serial.println("Delay time: " + String(delayTime));
  // смещение времени
  ini.getValue("general", "timeOffset", buffer, bufferLen);
  timeOffset = atoi(buffer);
  Serial.println("Time offset: " + String(timeOffset));
  // удаленный сервер передачи данных
  ini.getValue("remote", "server", buffer, bufferLen);
  remServer = buffer;
  Serial.println("Remote server: " + remServer);
  ini.close();
}

// подключение к Wifi (режим STA)
void wifiConnect() {
  Serial.println("Trying to connect: " + wifi_ssid);
  secondLine1 = wifi_ssid;
  lcd_change(0);
  delay(loadingDelay / 2);
  //подключение к Wifi
  firstLine1 = "Connecting...";
  lcd_change(0);
  WiFi.begin((const char*)wifi_ssid.c_str(), (const char*)wifi_pass.c_str());
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println(WiFi.status());
  }
  Serial.println("");
  Serial.println("WiFi:" + wifi_ssid + " connected");
  Serial.println(WiFi.localIP());
}

// получение времени по NTP
void getNTPtime() {
  timeClient.begin();
  // Установка часового пояса (смещения от GMT): 1 час - 3600
  timeClient.setTimeOffset(timeOffset);

  // обновление времени с NTP-сервера
  while (!timeClient.update()) {
    timeClient.forceUpdate();
  }
  // вывод даты и времени в формате YYYY-MM-DDTHH:mm:ssZ
  formattedDate = timeClient.getFormattedDate();
  Serial.println("Current date: " + formattedDate);

  // вывод даты и времени
  int splitT = formattedDate.indexOf("T");
  dayStamp = formattedDate.substring(0, splitT);
  timeStamp = formattedDate.substring(splitT + 1, formattedDate.length() - 1); // время
  Serial.print("Today is: ");
  Serial.println(dayStamp);

  firstLine1 = "Current date";
  secondLine1 = formattedDate;
  lcd_change(0);
}

void setup() {
  //обработка прерывания
  pinMode(Btn_GPIO, INPUT);
  //pinMode(2, OUTPUT);
  attachInterrupt(Btn_GPIO, chTxt, RISING);
  // bнициализация последовательного соединение и выбор скорость передачи данных в бит/c
  Serial.begin(115200);
  Serial.println("Start loading...");
  // инициализация LCD-экрана
  lcd.init();
  lcd.backlight();
  firstLine1 = "Loading...";
  lcd_change(0);
  delay(loadingDelay);

  //инициализация датчика BME280
  Serial.println("Init BME280...");
  bool status;
  status = bme.begin(0x76);
  if (!status) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    secondLine1 = "BME280 not found";
    lcd_change(0);
    while (1);
  }
  else {
    Serial.println("BME280 found!");
    secondLine1 = "BME280: Ok";
    lcd_change(0);
  }
  delay(loadingDelay);

  //инициализация модуля MicroSD
  initMcSD();
  delay(loadingDelay);

  //загрузки ini файла с настройками
  getValues();
  delay(loadingDelay);

  // подключение к Wifi
  wifiConnect();
  delay(loadingDelay);

  // получение времени от NTP
  getNTPtime();
  delay(loadingDelay * 2);

  firstLine2 = "WiFi:" + wifi_ssid;
  lcd_change(1);

  secondLine2 = "IP:" + WiFi.localIP().toString();
  lcd_change(1);
  delay(loadingDelay);
  firstLine1 = "Temp: " + String(bme.readTemperature(), 2) + " C";
  secondLine1 = "Hum:  " + String(bme.readHumidity(), 2) + " %";
  lcd_change(0);

  // установка таймера
  firstTimer.setInterval(delayTime); 

  // запуск FTP сервера
  ftpSrv.begin("esp32", "esp32");
  
  // запуск ElegantOTA и сервера
  AsyncElegantOTA.begin(&server);    
  server.begin();
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", SendHTML(dayStamp, timeStamp, String(bme.readTemperature(), 2), String(bme.readHumidity(), 2)));
  });
  
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

void sendData(String date, String tim, String temp, String hum) {
  HTTPClient http;
  WiFiClient client;
  http.begin(client, "http://" + remServer + "/getdata.php");
  // добавляем заголовок к запросу
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  // Данные для отправки запроса HTTP POST на сервер
  String httpRequestData = "Date=" + date + "&Time=" + tim + "&Temper=" + temp + "&Hum=" + hum;
  Serial.println("http://" + remServer + "/getdata.php" + httpRequestData);
  // отправка запроса
  int httpResponseCode = http.POST(httpRequestData);
  Serial.print("HTTP Response code: ");
  Serial.println(httpResponseCode);

  // освобождение ресурсов
  http.end();
}

String SendHTML(String date, String tim, String temp, String hum) {
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr += "<meta http-equiv=\"Content-type\" content=\"text/html; charset=utf-8\"><head><meta http-equiv=\"refresh\" content=\"" + String(delayTime / 1000) + "\" ><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr += "<title>Мониторинг температуры и влажности</title>\n";
  ptr += "<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr += "body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;} h3 {color: #444444;margin-bottom: 50px;}\n";
  ptr += ".button {display: block;width: 80px;background-color: #3498db;border: none;color: white;padding: 13px 30px;text-decoration: none;font-size: 25px;margin: 0px auto 35px;cursor: pointer;border-radius: 4px;}\n";
  ptr += ".button-on {background-color: #3498db;}\n";
  ptr += ".button-on:active {background-color: #2980b9;}\n";
  ptr += ".button-off {background-color: #34495e;}\n";
  ptr += ".button-off:active {background-color: #2c3e50;}\n";
  ptr += "p {font-size: 14px;color: #888;margin-bottom: 10px;}\n";
  ptr += "</style>\n";
  ptr += "</head>\n";
  ptr += "<body>\n";
  ptr += "<h1>Мониторинг температры и влажности</h1>\n";
  ptr += "<p>Дата: " + date + "</p>\n";
  ptr += "<p>Время: " + tim + "</p>\n";
  ptr += "<p>Температура: " + temp + " °C</p>\n";
  ptr += "<p>Влажность: " + hum + " %</p>\n";
  ptr += "<br><br>\n";
  ptr += "<p>Файлы с данными доступны по FTP: " + WiFi.localIP().toString() + ":21 Логин/пароль: esp32/esp32</p>\n";
  ptr += "</body>\n";
  ptr += "</html>\n";
  return ptr;
}

void loop() {
  //  writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "server.handleClient()\n");
//   server.handleClient();
  //  writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "ftpSrv.handleFTP()\n");
  ftpSrv.handleFTP();
  //    while (!timeClient.update()) {
  //      timeClient.forceUpdate();
  //    }


  if (prev_md != md) // если нажималась кнопка и режим md сменился, то меняем текст на экране
  {
    lcd_change(md);
    prev_md = md;
    Serial.println("Prev_md changed");
  }

  if (firstTimer.isReady()) {


    writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "timerReady\n");
    //    Serial.println(ESP.getFreeHeap());
    writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "updateTime\n");
    if (WiFi.status() == WL_CONNECTED) {
      while (!timeClient.update()) {
        Serial.println("Time is updated");
        writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "time Updated\n");
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
    firstLine1 = "Temp: " + curTemperature + " C";
    secondLine1 = "Hum:  " + curHumidity + " %";
    Serial.println(dayStamp + " " + timeStamp + " " + firstLine1 + " " + secondLine1);
    //записываем данные в файл на сд-карту
    writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "write data to file\n");
    writeFile(SD, data_file_ch, dayStamp + ";" + timeStamp + ";" + String(bme.readTemperature(), 2) + " C;" + String(bme.readHumidity(), 2) + "%\n");
    writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "Data wrote ok\n");

    // отправляем страницу на сервер
    writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "webserver update page\n");
    
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", SendHTML(dayStamp, timeStamp, curTemperature, curHumidity));
    });
    
//    server.send(200, "text/html", SendHTML(dayStamp, timeStamp, curTemperature, curHumidity));
    writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "webserver update ok\n");
    if (md == 0) lcd_change(0);

    // отправка на сервер
    writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "Send data to remote server\n");
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Wifi connected. Sending data...");
      writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "Wifi connected. Send data\n");
      sendData(dayStamp, timeStamp, curTemperature, curHumidity);
      writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "Send data ok\n");
    }
    else {
      Serial.println("No wifi connection!");
      writeFile(SD, "/log.txt", dayStamp + " " + timeStamp + " " + "Wifi not connected. Restart\n");
      ESP.restart();
    }
    firstTimer.reset();
  }
}



//void printValues() {
//    Serial.print("Temperature = ");
//    Serial.print(bme.readTemperature());
//    Serial.println(" *C");
//
//    Serial.print("Pressure = ");
//
//    Serial.print(bme.readPressure() / 100.0F);
//    Serial.println(" hPa");
//
//    Serial.print("Approx. Altitude = ");
//    Serial.print(bme.readAltitude(SEALEVELPRESSURE_HPA));
//    Serial.println(" m");
//
//    Serial.print("Humidity = ");
//    Serial.print(bme.readHumidity());
//    Serial.println(" %");
//
//    Serial.println();
//}

//
//File dataFile = SD.open(filename, FILE_WRITE);//;fileName, FILE_WRITE);
//
//  // if the file is available, write to it:
//  if (dataFile) {
//    dataFile.println(dataString);
//    dataFile.close();
//    // print to the serial port too:
//    Serial.println(dataString);
//  }
//  // if the file isn't open, pop up an error:
//  else {
//    Serial.print("error opening ");
//    Serial.print(filename);
//  }


// $sql = "INSERT INTO 'Data' (Date, Time, Temper, Hun) VALUES (".$dat.", ".$tim.", ".$temper.", ".$hum.")";
