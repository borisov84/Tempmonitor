#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFSIniFile.h>
#include "SPIFFS.h"

bool firstStart = false; // если устройство не настроено

String wifi_ssid;
String wifi_pass;
String remoteServer;
int updateInterval;

// для Ini-файлов
const size_t bufferLen = 80;
char buffer[bufferLen];

// создание объекта Webserver
AsyncWebServer server(80);


void spiffs_begin() {
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    //  "При монтировании SPIFFS возникла ошибка"
    return;
  }
}

void remFile() {
  if (SPIFFS.remove("/settings.ini")) {
    Serial.println("File successfully deleted");
    //  "Файл успешно удален"
  }
  else {
    Serial.print("Deleting file failed!");
    //  "Не удалось удалить файл!"
  }
}


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


bool check_settings() {
  Serial.println("Checking settings.ini file...");

  if (SPIFFS.exists("/settings.ini")) {
    Serial.println("File settings: ok");
    firstStart = false;
  }
  else {
    Serial.println("File settings.ini not found");
    Serial.println("First start activated");
    firstStart = true;
  }
  return firstStart;
}


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

void readIni() {
  SPIFFSIniFile ini("/settings.ini");
  if (!ini.open()) {
    Serial.print("Ini file ");
    Serial.print("/settings.ini");
    Serial.println(" does not exist");
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

  if (ini.getValue("wifi", "wifi_ssid", buffer, bufferLen)) {
    Serial.print("section 'wifi' has an entry 'wifi_ssid' with value ");
    Serial.println(buffer);
    wifi_ssid = buffer;
  }
  else {
    Serial.print("Could not read 'wifi_ssid' from section 'wifi', error was ");
    printErrorMessage(ini.getError());
  }

  if (ini.getValue("wifi", "wifi_pass", buffer, bufferLen)) {
    Serial.print("section 'wifi' has an entry 'wifi_pass' with value ");
    Serial.println(buffer);
    wifi_pass = buffer;
  }
  else {
    Serial.print("Could not read 'wifi_pass' from section 'wifi', error was ");
    printErrorMessage(ini.getError());
  }

}




void initWifi(bool firstSt) {
  int connect_count;
  connect_count = 0;
  Serial.println("Starting wifi...");
  if (firstSt) {
    Serial.println("Starting acces point mode");
    WiFi.softAP("Thermo", "12345678");
    IPAddress IP = WiFi.softAPIP();
    Serial.println(IP);
  }
  else {
    readIni();
    Serial.println("Connecting to Wifi...");

    Serial.println(wifi_ssid.c_str());
    Serial.println(wifi_pass.c_str());

    WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
	  connect_count += 1;
	  Serial.print("Connect attempt: ");
	  Serial.println(connect_count);
	  if (connect_count == 15) {
		if (SPIFFS.remove("/settings.ini")) {
          Serial.println("File settings.ini succesfuly deleted");
		  ESP.restart();
        }
        else{
          Serial.println("File settings.ini not found. Nothing to delete");
        }
	  }
	  
    }

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }
}

// Replaces placeholder with LED state value
String processor(const String& var) {
  Serial.println(var);
  if (var == "STATE") {
    return "World";
  }
  return String();
}

// Функция создания нового файла с настройками при первом запуске
void makeIni() {
  String iniFile;
  iniFile = "[wifi]\nwifi_ssid=" + wifi_ssid + "\n";
  iniFile += "wifi_pass=" + wifi_pass + "\n";
  iniFile += "[general]\n";
  iniFile += "delayLoading=1000\n";
  iniFile += "updateInterval=" + String(updateInterval) + "\n";
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
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  spiffs_begin();
  //  get_index();
  initWifi(check_settings());
  server.begin();
  // стартовая страница
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
	request->send(SPIFFS, "/index.html", String());
  });
  
  // страница с показаниями
  server.on("/mes", HTTP_GET, [] (AsyncWebServerRequest * request){
	request->send(SPIFFS, "/mes.html",String(), false, processor);  
  });
  
  // страница базовых настроек
  server.on("/new", HTTP_GET, [](AsyncWebServerRequest * request) {
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
        wifi_ssid = p->value();
      }
      if (p->name() == "pass") {
        wifi_pass = p->value();
      }
      if (p->name() == "updateInterval") {
        updateInterval = (p->value()).toInt();
      }
      if (p->name() == "remoteServer") {
        remoteServer = p->value();
      }
    }
    request->send(200, "text/plain", "Data saved. Reboot...");
    makeIni();
  });
  
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
}



void loop() {
  // put your main code here, to run repeatedly:

}
