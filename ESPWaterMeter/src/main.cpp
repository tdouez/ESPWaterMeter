//--------------------------------------------------------------------
//                 +/          
//                 `hh-        
//        ::        /mm:       
//         hy`      -mmd       
//         omo      +mmm.  -+` 
//         hmy     .dmmm`   od-
//        smmo    .hmmmy    /mh
//      `smmd`   .dmmmd.    ymm
//     `ymmd-   -dmmmm/    omms
//     ymmd.   :mmmmm/    ommd.
//    +mmd.   -mmmmm/    ymmd- 
//    hmm:   `dmmmm/    smmd-  
//    dmh    +mmmm+    :mmd-   
//    omh    hmmms     smm+    
//     sm.   dmmm.     smm`    
//      /+   ymmd      :mm     
//           -mmm       +m:    
//            +mm:       -o    
//             :dy             
//              `+:     
//--------------------------------------------------------------------
//   __|              _/           _ )  |                       
//   _| |  |   ` \    -_)   -_)    _ \  |   -_)  |  |   -_)     
//  _| \_,_| _|_|_| \___| \___|   ___/ _| \___| \_,_| \___|  
//--------------------------------------------------------------------    
// 2020/04/03 - FB V1.00
// 2020/07/13 - FB V1.01 - NTP fix
// 2020/09/16 - FB V1.01 - LittleFS implementation
// 2021/03/22 - FB V1.02 - Bug fix on getDatewithDLS
// 2021/03/31 - FB V1.03 - Add startup date and current date into index.html
// 2021/04/09 - FB V1.04 - date bugfix ans add timezone into index.html
// 2021/05/06 - FB V1.05 - Change volume/flow precision (2 digits after the decimal point)
// 2021/06/30 - FB V1.06 - Bug fix on check continuous consumption
// 2021/07/21 - FB V1.07 - Bug fix on GMT and add histo memorization after reboot
// 2021/08/28 - FB V1.08 - Bug fix on NTP
// 2021/10/03 - FB V1.09 - Change water sensor pin D4 to D6 and add POST request
// 2022/12/09 - FB V1.10 - Change AsyncWiFiManager to ESPAsync_WiFiManager library
//--------------------------------------------------------------------
#include <Arduino.h>

#ifdef ESP32
#include <WiFi.h>
#include <AsyncTCP.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#endif
#include <ESPAsyncUDP.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncDNSServer.h>
#include <ESPAsync_WiFiManager.h>
#include <ESPDateTime.h>
#include <LittleFS.h>
#include <U8g2lib.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <Streaming.h>
#include <main.h>
#include <EasyNTPClient.h>
#include <Timezone.h>
#include <ArduinoHttpClient.h>


#define BP_WIFI              13   // Bp - D7
#define DIGITAL_INPUT_SENSOR 12   // Water sensor - D6
#define MAX_FLOW 100              // Max flow (l/min) value to report.
#define HBD      24               // 24 hours
#define DEFAULT_PULSE_FACTOR 100	// Number of blinks per m3 of your meter (100: One rotation/10 liters)
#define DEFAULT_LEAK_THRESHOLD 800// Alert threshold, by default 800 l/day
#define DEFAULT_TPS_MAJ 30
#define DEFAULT_PORT_MQTT 1883
#define MAX_BUFFER      32
#define MAX_BUFFER_URL  64
#define VERSION "1.1.0"
#define PWD_OTA "fumeebleue"


const int RSSI_MAX =-50;          // define maximum strength of signal in dBm
const int RSSI_MIN =-100;         // define minimum strength of signal in dBm
uint32_t SEND_FREQUENCY =  30000; // Minimum time between send (in milliseconds). 
unsigned int  conso[HBD] = {0};   // historic consumption
boolean  flag_leak_type1 = false; // continuous consumption during 24hr
boolean  flag_leak_type2 = false; // consumption in excess of the 24hr threshold
boolean  flag_pulseCount = false; // Used after reboot in order to retreive pulse 
boolean  flag_volume = false;     // Used after reboot in order to retreive value
boolean  flag_totalPulse = false;
boolean  flag_histo_conso = false;
int cpt_histo_conso = 0;
unsigned int leak_type = 0;       // 1 : continus comsumption, 2 : excess comsumption, 3 : both 
uint32_t lastSend = 0;
uint32_t lastPulse = 0;
volatile uint32_t pulseCount = 0;
volatile uint32_t lastBlink = 0;
volatile double flow = 0;
unsigned int totalPulse = 0;
unsigned int pulse_factor = DEFAULT_PULSE_FACTOR;     // Number of blinks per m3 of your meter (One rotation/1 liters)
unsigned int leak_threshold = DEFAULT_LEAK_THRESHOLD; // Alert threshold, by default 800 l/day
double ppl = ((double)pulse_factor)/1000;             // Pulses per liter
double volume = 0;
double volume_cumul = 0;
char module_name[MAX_BUFFER];
char memo_module_name[MAX_BUFFER];
char url_mqtt[MAX_BUFFER_URL];
char memo_url_mqtt[MAX_BUFFER_URL];
unsigned int port_mqtt;
unsigned int memo_port_mqtt;
char user_mqtt[MAX_BUFFER];
char memo_user_mqtt[MAX_BUFFER];
char pwd_mqtt[MAX_BUFFER];
char memo_pwd_mqtt[MAX_BUFFER];
char token_mqtt[MAX_BUFFER];
char memo_token_mqtt[MAX_BUFFER];
char ntp_server[MAX_BUFFER];
char memo_ntp_server[MAX_BUFFER];
char url_post[MAX_BUFFER];
char memo_url_post[MAX_BUFFER];
char token_post[MAX_BUFFER];
char memo_token_post[MAX_BUFFER];
unsigned long tps_maj;
int last_hour = 0;    
int last_day = 0;
unsigned long previousMillis_fuite = 0;
unsigned long previousMillis_maj = 0;
unsigned long lastReconnectAttempt_mqtt = 0;
boolean flag_memo = true;
boolean flag_startup = false;
char buffer[128]; 
String last_reset_counter;
int last_reset_counter_day = 0;
int last_reset_counter_hour = 0;
String last_reset_conso;

String info_mqtt = "Non actif";
String information = "Pas de Fuite";
String erreur_info = "";
String info_config = "";
String erreur_config = "";
String startup_date = "";

AsyncWebServer server(80);
AsyncDNSServer dns;
WiFiClient espClient;
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
PubSubClient client_mqtt(espClient);
WiFiUDP Udp_G; 
time_t Local_time;

// ----- Timezone configuration -----------------
EasyNTPClient ClientNtp_G(Udp_G, "fr.pool.ntp.org"); // NTP server "pool.ntp.org"
TimeChangeRule myDST = {"RHEE", Last, Sun, Mar, 2, 120}; // Daylight time - Règle de passage à l'heure d'été pour la France
TimeChangeRule mySTD = {"RHHE", Last, Sun, Oct, 3, 60}; // Standard time - Règle de passage à l'heure d'hiver la France
// ----- Timezone configuration -----------------

Timezone myTZ(myDST, mySTD);

//-----------------------------------------------------------------------
String return_current_time()
{
  return String(day(Local_time)) + String("/") + String(month(Local_time)) + String("/") + String(year(Local_time));
}

//-----------------------------------------------------------------------
String return_current_date()
{
  return String(hour(Local_time)) + String(":") + String(minute(Local_time)) + String(":") + String(second(Local_time));
}

//-----------------------------------------------------------------------
void IRAM_ATTR onPulse()
{

	uint32_t newBlink = micros();
	uint32_t interval = newBlink-lastBlink;

	if (interval!=0) {
		lastPulse = millis();
		if (interval<500000L) {
			// Sometimes we get interrupt on RISING,  500000 = 0.5 second debounce ( max 120 l/min)
			return;
		}
		flow = (60000000.0 /interval) / ppl;
	}
	lastBlink = newBlink;

	pulseCount++;
  //Serial.println(pulseCount);
  conso[hour(Local_time)]++;
  volume = 1000*((double)pulseCount/(double)pulse_factor);
  
}

//-----------------------------------------------------------------------
void loadConfig() {

  if (LittleFS.exists("/config.json")) {
    Serial.println(F("Lecture config.json"));
    File configFile = LittleFS.open("/config.json", "r");
    if (configFile) {
      DynamicJsonDocument jsonDoc(512);
      DeserializationError error = deserializeJson(jsonDoc, configFile);
      if (error) Serial.println(F("Impossible de parser config.cfg"));
      JsonObject json = jsonDoc.as<JsonObject>();
      Serial.println(F("Contenu:"));
      serializeJson(json, Serial);

      if (json["module_name"].isNull() == false) {
        strcpy(module_name, json["module_name"]);
        if (module_name[0] == 0) sprintf(module_name, "EMT_%06X", ESP.getChipId());
        strcpy(memo_module_name, module_name);
      } 
      else sprintf(module_name, "EMT_%06X", ESP.getChipId());
        
      if (json["tps_maj"].isNull() == false) tps_maj = json["tps_maj"];
        else tps_maj = DEFAULT_TPS_MAJ;

      if (json["ntp_server"].isNull() == false) {
        strcpy(ntp_server, json["ntp_server"]);
        strcpy(memo_ntp_server, json["ntp_server"]);
      }
      else ntp_server[0] = 0;
      
      if (json["url_mqtt"].isNull() == false) {
        strcpy(url_mqtt, json["url_mqtt"]);
        strcpy(memo_url_mqtt, json["url_mqtt"]);
      }
      else url_mqtt[0] = 0;

      if (json["user_mqtt"].isNull() == false) {
        strcpy(user_mqtt, json["user_mqtt"]);
        strcpy(memo_user_mqtt, json["user_mqtt"]);
      }
      else user_mqtt[0] = 0;

      if (json["pwd_mqtt"].isNull() == false) {
        strcpy(pwd_mqtt, json["pwd_mqtt"]);
        strcpy(memo_pwd_mqtt, json["pwd_mqtt"]);
      }
      else pwd_mqtt[0] = 0;

      if (json["token_mqtt"].isNull() == false) {
        strcpy(token_mqtt, json["token_mqtt"]);
        strcpy(memo_token_mqtt, json["token_mqtt"]);
      }
      else token_mqtt[0] = 0;

      if (json["port_mqtt"].isNull() == false) {
        port_mqtt = json["port_mqtt"];
        if (port_mqtt == 0) port_mqtt = DEFAULT_PORT_MQTT;
        memo_port_mqtt = port_mqtt;
      }
      else port_mqtt = DEFAULT_PORT_MQTT;

      if (json["pulse_factor"].isNull() == false) pulse_factor = json["pulse_factor"];
        else pulse_factor = DEFAULT_PULSE_FACTOR;

      if (json["leak_threshold"].isNull() == false) leak_threshold = json["leak_threshold"];
        else leak_threshold = DEFAULT_LEAK_THRESHOLD;

      if (json["url_post"].isNull() == false) {
        strcpy(url_post, json["url_post"]);
        strcpy(memo_url_post, json["url_post"]);
      }
      else url_post[0] = 0;

      if (json["token_post"].isNull() == false) {
        strcpy(token_post, json["token_post"]);
        strcpy(memo_token_post, json["token_post"]);
      }
      else token_post[0] = 0;

      configFile.close();
    }
    else Serial.println(F("Impossible de lire config.json !!"));
  }
}

//-----------------------------------------------------------------------
void saveConfig() {
  
  Serial.println(F("Sauvegarde config.json"));
  File configFile = LittleFS.open("/config.json", "w");
  DynamicJsonDocument jsonDoc(512);
  JsonObject json = jsonDoc.to<JsonObject>();

  json["module_name"] = module_name;
  json["tps_maj"] = tps_maj;
  json["url_mqtt"] = url_mqtt;
  json["user_mqtt"] = user_mqtt;
  json["pwd_mqtt"] = pwd_mqtt;
  json["token_mqtt"] = token_mqtt;
  json["port_mqtt"] = port_mqtt;
  json["url_post"] = url_post;
  json["token_post"] = token_post;
  json["ntp_server"] = ntp_server;
  json["pulse_factor"] = pulse_factor;
  json["leak_threshold"] = leak_threshold;
      
  serializeJson(json, configFile);
  configFile.close();
}

//-----------------------------------------------------------------------
void page_info_json(AsyncWebServerRequest *request)
{
String strJson = "{\n";

  //Serial.println(F("Page info.json"));

  // pulseCount ---------------------
  strJson += F("\"pulseCount\": \"");
  strJson += pulseCount;
  strJson += F("\",\n");

  // totalPulse ---------------------
  strJson += F("\"totalPulse\": \"");
  strJson += totalPulse;
  strJson += F("\",\n");

  // flow ---------------------
  strJson += F("\"flow\": \"");
  strJson += flow;
  strJson += F("\",\n");

  // volume ---------------------
  strJson += F("\"volume\": \"");
  strJson += volume;
  strJson += F("\",\n");

  // volume_cumul ---------------------
  strJson += F("\"volume_cumul\": \"");
  strJson += volume_cumul;
  strJson += F("\",\n");

  // info ---------------------
  strJson += F("\"info\": \"");
  strJson += information;
  strJson += F("\",\n");

  // conso ---------------------
  for (int i=0; i<HBD; i++) {
    strJson += F("\"h");
    strJson += i;
    strJson += F("\": \"");
    strJson += conso[i];
    strJson += F("\",\n");
  }

  // erreur info ---------------------
  strJson += F("\"erreur_info\": \"");
  strJson += erreur_info;
  strJson += F("\",\n");

  // info config ---------------------
  strJson += F("\"info_config\": \"");
  strJson += info_config;
  strJson += F("\",\n");

  // erreur config ---------------------
  strJson += F("\"erreur_config\": \"");
  strJson += erreur_config;
  strJson += F("\",\n");

  // last_reset_conso ---------------------
  strJson += F("\"last_reset_conso\": \"");
  strJson += last_reset_conso;
  strJson += F("\",\n");

  // last_reset_counter ---------------------
  strJson += F("\"last_reset_counter\": \"");
  strJson += last_reset_counter;
  strJson += F("\",\n");
    
  // last_reset_counter_hour ---------------------
  strJson += F("\"last_reset_counter_hour\": \"");
  strJson += last_reset_counter_hour;
  strJson += F("\",\n");

  // last_reset_counter_day ---------------------
  strJson += F("\"last_reset_counter_day\": \"");
  strJson += last_reset_counter_day;
  strJson += F("\",\n");
  
  // current hour ---------------------
  strJson += F("\"hour\": \"");
  strJson += hour(Local_time);
  strJson += F("\",\n");

  // current date ---------------------
  strJson += F("\"current_date\": \"");
  strJson += return_current_time() + String(" - ") + return_current_date();
  strJson += F("\",\n");

  // startup_date ---------------------
  strJson += F("\"startup_date\": \"");
  strJson += startup_date;
  strJson += F("\"\n");

  strJson += F("}");

  request->send(200, "text/json", strJson);
}

//-----------------------------------------------------------------------
void printConfig() {
    Serial << "Name       : " << module_name << "\n";
    Serial << "Tps maj    : " << tps_maj << "\n";
    Serial << "Url MQTT   : " << url_mqtt << "\n";
    Serial << "Port Mqtt  : " << port_mqtt << "\n";
    Serial << "User Mqtt  : " << user_mqtt << "\n";
    Serial << "Pwd Mqtt   : " << pwd_mqtt << "\n";
    Serial << "Token Mqtt : " << token_mqtt << "\n";
    Serial << "Ntp Server : " << ntp_server << "\n"; 
    Serial << "Pulse Factor   : " << pulse_factor << "\n";
    Serial << "Leak threshold : " << leak_threshold << "\n";
}

//-----------------------------------------------------------------------
void page_config_json(AsyncWebServerRequest *request)
{
String strJson = "{\n";

  Serial.println(F("Page config.json"));
  
  // module name---------------------
  strJson += F("\"module_name\": \"");
  strJson += module_name;
  strJson += F("\",\n");

  // module name---------------------
  strJson += F("\"version\": \"");
  strJson += VERSION;
  strJson += F("\",\n");

  // ntp server ---------------------
  strJson += F("\"ntp_server\": \"");
  strJson += ntp_server;
  strJson += F("\",\n");

  // url mqtt ---------------------
  strJson += F("\"url_mqtt\": \"");
  strJson += url_mqtt;
  strJson += F("\",\n");

  // port mqtt ---------------------
  strJson += F("\"port_mqtt\": \"");
  strJson += port_mqtt;
  strJson += F("\",\n");

  // user mqtt ---------------------
  strJson += F("\"user_mqtt\": \"");
  strJson += user_mqtt;
  strJson += F("\",\n");

  // password mqtt ---------------------
  strJson += F("\"pwd_mqtt\": \"");
  strJson += pwd_mqtt;
  strJson += F("\",\n");

  // token mqtt ---------------------
  strJson += F("\"token_mqtt\": \"");
  strJson += token_mqtt;
  strJson += F("\",\n");

  // url post ---------------------
  strJson += F("\"url_post\": \"");
  strJson += url_post;
  strJson += F("\",\n");

  // token post ---------------------
  strJson += F("\"token_post\": \"");
  strJson += token_post;
  strJson += F("\",\n");

  // pulse_factor ---------------------
  strJson += F("\"pulse_factor\": \"");
  strJson += pulse_factor;
  strJson += F("\",\n");

  // leak_threshold ---------------------
  strJson += F("\"leak_threshold\": \"");
  strJson += leak_threshold;
  strJson += F("\",\n");

  // tps_maj ---------------------
  strJson += F("\"tps_maj\": \"");
  strJson += tps_maj;
  strJson += F("\"\n");
  
  strJson += F("}");

  request->send(200, "text/json", strJson);
}

//-----------------------------------------------------------------------
void page_config_htm(AsyncWebServerRequest *request)
{
boolean flag_restart = false;

  Serial.println(F("Page config"));

  int params = request->params();
  for(int i=0;i<params;i++){
    AsyncWebParameter* p = request->getParam(i);
    Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
    
    if (strstr(p->name().c_str(), "module_name")) strcpy(module_name, p->value().c_str());
    if (strstr(p->name().c_str(), "ntp_server")) strcpy(ntp_server, p->value().c_str());
    if (strstr(p->name().c_str(), "url_mqtt")) strcpy(url_mqtt, p->value().c_str());
    if (strstr(p->name().c_str(), "port_mqtt")) port_mqtt = atoi(p->value().c_str());
    if (strstr(p->name().c_str(), "user_mqtt")) strcpy(user_mqtt, p->value().c_str());
    if (strstr(p->name().c_str(), "pwd_mqtt")) strcpy(pwd_mqtt, p->value().c_str());
    if (strstr(p->name().c_str(), "token_mqtt")) strcpy(token_mqtt, p->value().c_str());
    if (strstr(p->name().c_str(), "pulse_factor")) pulse_factor = atoi(p->value().c_str());
    if (strstr(p->name().c_str(), "leak_threshold")) leak_threshold = atoi(p->value().c_str());
    if (strstr(p->name().c_str(), "tps_maj")) tps_maj = atoi(p->value().c_str());

    // check if restart required 
    if (strcmp(module_name, memo_module_name) != 0) flag_restart = true;
    if (strcmp(ntp_server, memo_ntp_server) != 0) flag_restart = true;
    if (strcmp(url_mqtt, memo_url_mqtt) != 0) flag_restart = true;
    if (strcmp(user_mqtt, memo_user_mqtt) != 0) flag_restart = true;
    if (strcmp(pwd_mqtt, memo_pwd_mqtt) != 0) flag_restart = true;
    if (strcmp(token_mqtt, memo_token_mqtt) != 0) flag_restart = true;
    if (port_mqtt != memo_port_mqtt) flag_restart = true;

  }
  saveConfig();
  request->send (200, "text/plain", "OK");
  delay(500);

  if (flag_restart == true) {
    erreur_config = "Reboot module";
    ESP.reset();
  }
}


//-----------------------------------------------------------------------
void loadPages()
{
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.on("/w3.css", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(LittleFS, "/w3.css", "text/css");
  });

  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(LittleFS, "/script.js", "text/javascript");
  });

  server.on("/jquery.js", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(LittleFS, "/jquery.js", "text/javascript");
  });

  server.on("/notify.js", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(LittleFS, "/notify.js", "text/javascript");
  });

  server.on("/fb.svg", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(LittleFS, "/fb.svg", "image/svg+xml");
  });

  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    request->send(LittleFS, "/favicon.ico", "image/x-icon");
  });

  server.on("/config.json", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    page_config_json(request);
  });

  server.on("/info.json", HTTP_GET, [](AsyncWebServerRequest *request)
  {
    page_info_json(request);
  });

  server.on("/config.htm", HTTP_POST, [](AsyncWebServerRequest *request)
  {
    page_config_htm(request);
  });

  server.onNotFound([](AsyncWebServerRequest *request){
    Serial.println("Page not found");
    Serial.println(request->method());
    Serial.println(request->url());
    request->send(404, "text/plain", "The content you are looking for was not found.");
  });
}

//------------------------------------------------------------------------------------
void Publish_mqtt()
{
char buffer_value[32];

  Serial.println("MQTT publish.");
  
  sprintf(buffer, "%s/pulseCount", token_mqtt);
  sprintf(buffer_value, "%d", pulseCount);
  client_mqtt.publish(buffer, buffer_value, true);

  sprintf(buffer, "%s/volume", token_mqtt);
  sprintf(buffer_value, "%.2f", volume);
  client_mqtt.publish(buffer, buffer_value, true);

  sprintf(buffer, "%s/flow", token_mqtt);
  sprintf(buffer_value, "%.2f", flow);
  client_mqtt.publish(buffer, buffer_value);

  sprintf(buffer, "%s/totalPulse", token_mqtt);
  sprintf(buffer_value, "%d", totalPulse);
  client_mqtt.publish(buffer, buffer_value);

  sprintf(buffer, "%s/leak", token_mqtt);
  sprintf(buffer_value, "%d", leak_type);
  client_mqtt.publish(buffer, buffer_value);

  for (int i=0; i<HBD; i++) {
    sprintf(buffer, "%s/h%02d", token_mqtt, i);
    sprintf(buffer_value, "%d", conso[i]);
    client_mqtt.publish(buffer, buffer_value, true);
  }

}


//------------------------------------------------------------------
void draw_splashscreen() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawXBMP(0, 0, 128, 64, fb_bits);
    u8g2.drawStr(1,10, VERSION);
    u8g2.sendBuffer();
}

//------------------------------------------------------------------
int dBmtoPercentage(int dBm)
{
  int quality;
    if(dBm <= RSSI_MIN)
    {
        quality = 0;
    }
    else if(dBm >= RSSI_MAX)
    {  
        quality = 100;
    }
    else
    {
        quality = 2 * (dBm + 100);
   }

     return quality;
}

//------------------------------------------------------------------
void draw_rssi() {
      
  int rssi = dBmtoPercentage(WiFi.RSSI());

  if (rssi >= 0) u8g2.drawXBMP(100, 2, 17, 7, radio0_bits);
  if (rssi >= 20 && rssi < 30) u8g2.drawXBMP(100, 2, 17, 9, radio1_bits);
  if (rssi >= 30 && rssi < 40) u8g2.drawXBMP(100, 2, 17, 9, radio2_bits);
  if (rssi >= 40 && rssi < 50) u8g2.drawXBMP(100, 2, 17, 9, radio3_bits);
  if (rssi >= 50 && rssi < 65) u8g2.drawXBMP(100, 2, 17, 9, radio4_bits);
  if (rssi >= 65 && rssi < 75) u8g2.drawXBMP(100, 2, 17, 9, radio5_bits);
  if (rssi >= 75 && rssi < 90) u8g2.drawXBMP(100, 2, 17, 9, radio6_bits);
  if (rssi >= 90) u8g2.drawXBMP(100, 2, 17, 9, radio7_bits);
}

// ----------------------------------------------------- Callback MQTT 
void callback_mqtt(char* topic, byte* payload, unsigned int length) {
  //Serial.print("Message arrived on topic: ");
  //Serial.print(topic);
  //Serial.print(". Message: ");
  String messageTemp;
  
  for (unsigned int i = 0; i < length; i++) {
    //Serial.print((char)payload[i]);
    messageTemp += (char)payload[i];
  }
  //Serial.println();

  // retreive pulseCount value after reboot ------------
  sprintf(buffer, "%s/pulseCount", token_mqtt);
  if (strstr(topic, buffer) && flag_pulseCount == false) {
    pulseCount = messageTemp.toInt();
    flag_pulseCount = true;
  }
  // retreive volume value after reboot ------------
  sprintf(buffer, "%s/volume", token_mqtt);
  if (strstr(topic, buffer) && flag_volume == false) {
    volume = messageTemp.toInt();
    flag_volume = true;
  }
  // retreive histo after reboot -----------
  if (flag_histo_conso == false) {
    for (int i=0; i<HBD; i++) {
      sprintf(buffer, "%s/h%02d", token_mqtt, i);
      if (strstr(topic, buffer) && flag_histo_conso == false) {
        conso[i] = messageTemp.toInt();
        if (cpt_histo_conso >= HBD) flag_histo_conso = true;
        cpt_histo_conso++;
      }
    }
  }
  
}

//-----------------------------------------------------------------------
boolean reconnect_mqtt() {

  if (client_mqtt.connect("ESP8266Client", user_mqtt, pwd_mqtt)) {
    sprintf(buffer, "%s/+", token_mqtt);
    //Serial << "Subscribe " << buffer << "\n";
    client_mqtt.subscribe(buffer);
  }
  return client_mqtt.connected();

}


//-----------------------------------------------------------------------
void setup()
{
  pinMode(BP_WIFI, INPUT_PULLUP);
  pinMode(DIGITAL_INPUT_SENSOR, INPUT_PULLUP);

  //----------------------------------------------------Oled
  u8g2.begin();
  
  //----------------------------------------------------Splash screen
  draw_splashscreen();
    
  //----------------------------------------------------Serial
  Serial.begin(115200);
  Serial.println(F("   __|              _/           _ )  |"));
  Serial.println(F("   _| |  |   ` \\    -_)   -_)    _ \\  |   -_)  |  |   -_)"));
  Serial.println(F("  _| \\_,_| _|_|_| \\___| \\___|   ___/ _| \\___| \\_,_| \\___|"));
  Serial.print(F("                                             "));

  //----------------------------------------------------LittleFS
  if(!LittleFS.begin())
  {
    Serial.println("Erreur LittleFS...");
    u8g2.clearBuffer();
    u8g2.drawStr(0,10,"Erreur LittleFS...");
    u8g2.sendBuffer();
    return;
  }
  else {
    loadConfig();
    printConfig();
  }

  //----------------------------------------------------WIFI
  ESPAsync_WiFiManager wifiManager(&server,&dns);
  if ( digitalRead(BP_WIFI) == LOW ) { // --------------Reset WIfi
    Serial.println("Reset Wifi settings");
    u8g2.setFont(u8g2_font_fub17_tf);
    u8g2.drawStr(10,50,"Reset Wifi");
    u8g2.sendBuffer();
    u8g2.clearBuffer();
    wifiManager.resetSettings();
    delay(2000);
  }
  WiFi.hostname(module_name);
  wifiManager.setMinimumSignalQuality(10);
  wifiManager.setConfigPortalTimeout(360);
  wifiManager.autoConnect("FumeeBleue");
  
  //----------------------------------------------------SERVER
  loadPages();
  server.begin();
  Serial.println("Serveur actif!");

  //----------------------------------------------------OTA
  ArduinoOTA.setPassword(PWD_OTA);
  ArduinoOTA.onStart([]() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_fub17_tf);
    u8g2.drawStr(20,50,"OTA Start");
    u8g2.sendBuffer();
  });
  ArduinoOTA.onEnd([]() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_fub17_tf);
    u8g2.drawStr(20,50,"OTA End");
    u8g2.sendBuffer();
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    u8g2.clearBuffer();
    sprintf(buffer,"%u%%", (progress / (total / 100)));
    u8g2.setFont(u8g2_font_fub17_tf);
    u8g2.drawStr(10,50, buffer);
    u8g2.sendBuffer();
  });
  ArduinoOTA.onError([](ota_error_t error) {
    sprintf(buffer,"Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) sprintf(buffer,"%s", "Auth Failed");
    else if (error == OTA_BEGIN_ERROR) sprintf(buffer,"%s", "Begin Failed");
    else if (error == OTA_CONNECT_ERROR) sprintf(buffer,"%s", "Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) sprintf(buffer,"%s", "Receive Failed");
    else if (error == OTA_END_ERROR) sprintf(buffer,"%s", "End Failed");
    u8g2.clearBuffer();
    
    u8g2.setFont(u8g2_font_fub17_tf);
    u8g2.drawStr(20,50, buffer);
    u8g2.sendBuffer();
  });
  ArduinoOTA.setHostname(module_name);
  ArduinoOTA.begin();

  //---------------------------------------------------- MQTT
  client_mqtt.setServer(url_mqtt, port_mqtt);
  client_mqtt.setCallback(callback_mqtt);

  u8g2.clearBuffer();
  Local_time = myTZ.toLocal(ClientNtp_G.getUnixTime());
  startup_date = return_current_time() + String(" - ") + return_current_date();
  
  attachInterrupt(digitalPinToInterrupt(DIGITAL_INPUT_SENSOR), onPulse, FALLING);
  
  previousMillis_maj = lastPulse = millis();
}


//-----------------------------------------------------------------------
void loop()
{
unsigned long currentTime = millis();
int current_year, current_day, current_hour, current_minute;

  ArduinoOTA.handle();

  u8g2.setFont(u8g2_font_4x6_tr);

  if (WiFi.status() == WL_CONNECTED) {  
    Local_time = myTZ.toLocal(ClientNtp_G.getUnixTime());
  }
  
  // No Pulse count received in 2min
  if(currentTime - lastPulse > 120000) {
    flow = 0;
  }
  
  // Check leak ------------------------------------------------------
  flag_leak_type1=true;
  flag_leak_type2=true;
  information = "Pas de fuite";
  erreur_info = "";

  // check continuous consumption during last 24hr
  for (uint8_t i=0; i<HBD; i++) {
    if(conso[i] == 0) flag_leak_type1 = false;
  }
  
  // check consumption in excess of the 24hr threshold 
  totalPulse=0;
  for (uint8_t i=0; i<HBD; i++) {
    totalPulse += conso[i];
  }
  
  volume_cumul = 1000*((double)totalPulse/(double)pulse_factor);
  if (volume_cumul < leak_threshold) flag_leak_type2 = false;
  //Serial << volume_cumul << "/" << leak_threshold << "\n";
  
  // flag leak type 1 or 2 has changed -----------------------------
  leak_type = 0;
  if (flag_leak_type1 == true) {
    leak_type += 1;
    information = "";
    erreur_info = "Fuite possible, conso continue sur 24h";
  }
  if (flag_leak_type2 == true) {
    leak_type += 2;
    information = "";
    erreur_info = "Fuite possible, conso > " + String(leak_threshold);
  }
  //Serial << "Leak type:" << leak_type << "\n";

  // Every hours reset conso --------
  current_year = year(Local_time);
  if (current_year > 1970) {
    current_hour = hour(Local_time);
    current_minute = minute(Local_time);
    if (current_hour != last_hour && current_minute == 0)  {
      Serial.println(F("Change hour - reset conso"));
      last_reset_counter_hour = last_hour;
      last_reset_conso = return_current_time() + " - " + return_current_date();
      conso[hour(Local_time)] = 0;
      last_hour = current_hour;
    }
    
    // Every days reset counters --------
    current_day = day(Local_time);
    if (current_day != last_day && current_hour == 0 && current_minute == 0)  {
      Serial.println(F("Change day - reset counters"));
      last_reset_counter_day = last_day;
      last_reset_counter = return_current_time() + " - " + return_current_date();
      pulseCount = 0;
      flow = 0;
      volume = 0;
      last_day = current_day;
    }
  } 
  else {
    erreur_info = "Date non valide, vérifier serveur NTP";
    u8g2.drawStr(10,50,"!Pb date");
  }

  // draw rssi wifi -----------------------------
  draw_rssi();

  // draw volume -----------------------------
  sprintf(buffer,"%8.0f l.", volume);
  u8g2.setFont(u8g2_font_fub17_tf);
  u8g2.drawStr(20,47,buffer);

  if(currentTime - previousMillis_fuite > 800L) {
    flag_memo = !flag_memo;
    previousMillis_fuite = currentTime;
  }
  if (leak_type > 0 && flag_memo == true) {
    u8g2.drawStr(10,20,"Fuite !!");
  }
  else {
    // adresse IP ----------------------------
    u8g2.setFont(u8g2_font_5x7_tr);
    sprintf(buffer,"%s", WiFi.localIP().toString().c_str());
    u8g2.drawStr(1,6, buffer);
  }
        
  // Maj MQTT ----------------------------
  if (url_mqtt[0] != 0 && token_mqtt[0] != 0 && current_year > 1970) {
    if (!client_mqtt.connected()) {
      long now = millis();
      if (now - lastReconnectAttempt_mqtt > 5000) {
        lastReconnectAttempt_mqtt = now;
        // Attempt to reconnect
        Serial << "MQTT (re)connect " << url_mqtt << ":" << port_mqtt << "\n";
        if (reconnect_mqtt()) {
          lastReconnectAttempt_mqtt = 0;
          Serial.println("MQTT actif");
          info_config = "MQTT actif";
          erreur_config = "";
        }
        else {
          erreur_config = "Unable to connect MQTT";
        }
      }
    }
    else {
      if(tps_maj > 0) {
        if (currentTime - previousMillis_maj > 1000*tps_maj) {
          Publish_mqtt(); 
          previousMillis_maj = currentTime;
        }
      }
      client_mqtt.loop(); 
    }
  }
  else erreur_config = "";

  // Maj POST ----------------------------
  if (url_post[0] != 0 && token_post[0] != 0) {
    String url = F("/maj_post.php?token=");
    url += token_post;
    url += F("&pulseCount=");
    url += pulseCount;
    url += F("&volume=");
    url += volume;
    url += F("&flow=");
    url += flow;
    url += F("&totalPulse=");
    url += totalPulse;
    url += F("&leak=");
    url += leak_type;

    Serial.print(F("Send post:"));
    Serial.println(url);

    HttpClient http_client = HttpClient(espClient, url_post, 80);
    http_client.get(url);
    int httpCode = http_client.responseStatusCode();

    if(httpCode > 0) {
      Serial.print(F("Retour http get: "));
    }
    else {
      Serial.print(F("Erreur http get: "));
      info_config = "Erreur http get:" + httpCode;
      u8g2.setFont(u8g2_font_5x7_tr);
      u8g2.drawStr(1, 5, info_config.c_str());
      delay(1000);
    }
    Serial.println(httpCode);
  }

  u8g2.sendBuffer();
  u8g2.clearBuffer();
}