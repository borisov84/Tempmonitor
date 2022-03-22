#include <WiFi.h>
#include "SPIFFS.h"
#include <ESPAsyncWebServer.h>

bool firstStart = false; // если устройство не настроено

// создание объекта Webserver
AsyncWebServer server(80);


void spiffs_begin(){
  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
               //  "При монтировании SPIFFS возникла ошибка"
    return;
  }
}

void remFile(){
  if(SPIFFS.remove("/settings.ini")){
    Serial.println("File successfully deleted");
               //  "Файл успешно удален"
  }
  else{
    Serial.print("Deleting file failed!");
             //  "Не удалось удалить файл!"
  }
}


void get_index(){
  File file = SPIFFS.open("/index.html");
  if(!file){
    Serial.println("Failed to open file for reading");
               //  "Не удалось открыть файл для чтения"
    return;
  }
  
  Serial.println("File Content:");
             //  "Содержимое файла:"
  while(file.available()){
    Serial.write(file.read());
  }
  file.close();
}


bool check_settings(){
  Serial.println("Checking settings.ini file...");

  if(SPIFFS.exists("/settin.ini")){
    Serial.println("File settings.ok");  
    firstStart = false;
  }
  else{
    Serial.println("File settings.ini not found");
    Serial.println("First start activated");
    firstStart = true;
  }
  return firstStart;
}


void initWifi(bool firstSt){
  Serial.println("Starting wifi...");
  if(firstSt){
    Serial.println("Starting acces point mode");
    WiFi.softAP("Thermo", "12345678");
    IPAddress IP = WiFi.softAPIP();
    Serial.println(IP);
  }
  else{
    Serial.println("Connecting to Wifi...");
  }
}

// Replaces placeholder with LED state value
String processor(const String& var){
  Serial.println(var);
  if(var == "STATE"){
    return "World";
  }
  return String();
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  spiffs_begin();
  get_index();
  initWifi(check_settings());
  server.begin();
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  
  
}

void loop() {
  // put your main code here, to run repeatedly:

}
