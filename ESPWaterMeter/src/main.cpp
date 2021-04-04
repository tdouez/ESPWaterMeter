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
//--------------------------------------------------------------------
#include <Arduino.h>

#ifdef ESP32
#include <WiFi.h>
#include <AsyncTCP.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#endif
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <ESPDateTime.h>
#include <LittleFS.h>
#include <NTPClient.h>
#include <U8g2lib.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <Streaming.h>
#include <main.h>

#define BP_WIFI              13   // Bp - D7
#define DIGITAL_INPUT_SENSOR 2    // Water sensor - D4
#define MAX_FLOW 100              // Max flow (l/min) value to report.
#define HBD      24               // 24 hours
#define DEFAULT_PULSE_FACTOR 100	// Number of blinks per m3 of your meter (100: One rotation/10 liters)
#define DEFAULT_LEAK_THRESHOLD 800// Alert threshold, by default 800 l/day
#define DEFAULT_TPS_MAJ 5
#define DEFAULT_PORT_MQTT 1883
#define MAX_BUFFER      32
#define MAX_BUFFER_URL  64
#define VERSION "1.0.3"
#define PWD_OTA "fumeebleue"

const int RSSI_MAX =-50;          // define maximum strength of signal in dBm
const int RSSI_MIN =-100;         // define minimum strength of signal in dBm
uint32_t SEND_FREQUENCY =  30000; // Minimum time between send (in milliseconds). 
unsigned int  conso[HBD] = {0};   // historic consumption
boolean  flag_leak_type1 = false; // continuous consumption during 24hr
boolean  flag_leak_type2 = false; // consumption in excess of the 24hr threshold
boolean  flag_pulseCount = false; // Used after reboot in order to retreive value 
boolean  flag_volume = false;     // Used after reboot in order to retreive value
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
unsigned long tps_maj;
int last_hour = 0;                 // Last hour
int last_day = 0;                  // Last hour
unsigned long previousMillis_fuite = 0;
unsigned long previousMillis_maj = 0;
unsigned long lastReconnectAttempt_mqtt = 0;
boolean flag_memo = true;
char buffer[128]; 

String info_mqtt = "Non actif";
String information = "Pas de Fuite";
String erreur_info = "";
String info_config = "";
String erreur_config = "";
String startup_date = "";

AsyncWebServer server(80);
DNSServer dns;
WiFiUDP ntpUDP;
WiFiClient espClient;
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
PubSubClient client_mqtt(espClient);



// ---------------------------------------------------------------------- calcul DLS 
int calculDLS() // fonction qui calcule DLS (Day Light Saving : heure d'hiver DLS = 0 , d'heure d'été DLS = 1)
{
/*You can use the following equations to calculate when DST starts and ends.
 The divisions are integer divisions, in which remainders are discarded.
 "mod" means the remainder when doing integer division, e.g., 20 mod 7 = 6.
 That is, 20 divided by 7 is 2 and 6/7th (where six is the remainder).
 With: y = year.
        For the United States:
            Begin DST: Sunday April (2+6*y-y/4) mod 7+1
            End DST: Sunday October (31-(y*5/4+1) mod 7)
           Valid for years 1900 to 2006, though DST wasn't adopted until the 1950s-1960s. 2007 and after:
            Begin DST: Sunday March 14 - (1 + y*5/4) mod 7
            End DST: Sunday November 7 - (1 + y*5/4) mod 7;
        European Economic Community:
            Begin DST: Sunday March (31 - (5*y/4 + 4) mod 7) at 1h U.T.
            End DST: Sunday October (31 - (5*y/4 + 1) mod 7) at 1h U.T.
            Since 1996, valid through 2099
(Equations by Wei-Hwa Huang (US), and Robert H. van Gent (EC))
 
 Adjustig Time with DST Europe/France/Paris: UTC+1h in winter, UTC+2h in summer
 
 */
 
  // last sunday of march
  int beginDSTDate=  (31 - (5* DateTime.getParts().getYear() /4 + 4) % 7);
  //Serial.println(beginDSTDate);
  int beginDSTMonth=3;
  //last sunday of october
  int endDSTDate= (31 - (5 * DateTime.getParts().getYear() /4 + 1) % 7);
  //Serial.println(endDSTDate);
  int endDSTMonth=10;
  // DST is valid as:
  if (((DateTime.getParts().getMonth()+1 > beginDSTMonth) && (DateTime.getParts().getMonth()+1 < endDSTMonth))
      || ((DateTime.getParts().getMonth()+1 == beginDSTMonth) && (DateTime.getParts().getMonthDay() > beginDSTDate)) 
	  || ((DateTime.getParts().getMonth()+1 == beginDSTMonth) && (DateTime.getParts().getMonthDay() == beginDSTDate) && (DateTime.getParts().getHours() >= 1))
      || ((DateTime.getParts().getMonth()+1 == endDSTMonth) && (DateTime.getParts().getMonthDay() < endDSTDate))
	  || ((DateTime.getParts().getMonth()+1 == endDSTMonth) && (DateTime.getParts().getMonthDay() == endDSTDate) && (DateTime.getParts().getHours() < 1)))
  return 1;      // DST europe = utc +2 hour (summer time)
  else return 0; // nonDST europe = utc +1 hour (winter time)
}

//-----------------------------------------------------------------------
int getHourwithDLS() {
  // getHours - Get hours since midnight (0-23)
  // calculDLS - Get DLS (+0 or +1)
  int dls = calculDLS();
  int current_h = DateTime.getParts().getHours();

  if (dls == 1) {
    current_h += 1;
    if (current_h == 24) current_h = 0;
  }

  return current_h;
}

//-----------------------------------------------------------------------
String getDatewithDLS() {
  String c_date;

  // getHours - Get hours since midnight (0-23)
  // calculDLS - Get DLS (+0 or +1)
  int dls = calculDLS();
  int current_h = DateTime.getParts().getHours();

  if (dls == 1) {
    current_h += 1;
    if (current_h == 24) current_h = 0;
  }

  c_date += DateTime.getParts().getMonthDay();
  c_date += "/";
  c_date += DateTime.getParts().getMonth()+1;
  c_date += "/";
  c_date += DateTime.getParts().getYear();
  c_date += " ";
  c_date += String(current_h);
  c_date += ":";
  c_date += DateTime.getParts().getMinutes();
  c_date += ":";
  c_date += DateTime.getParts().getSeconds();
              
  return c_date;
}

//-----------------------------------------------------------------------
void ICACHE_RAM_ATTR onPulse()
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
  conso[getHourwithDLS()]++;
  volume = 1000*((double)pulseCount/(double)pulse_factor);
  
}

//-----------------------------------------------------------------------
void setupDateTime() {
  // setup this after wifi connected
  // you can use custom timeZone,server and timeout
  // DateTime.setTimeZone(-4);
  //   DateTime.setServer("asia.pool.ntp.org");
  //   DateTime.begin(15 * 1000);
  DateTime.setTimeZone("CET-1CEST,M3.5.0,M10.5.0/3");  // Paris ---
  DateTime.setServer("europe.pool.ntp.org");
  DateTime.begin();
  if (!DateTime.isTimeValid()) {
    Serial.println("Failed to get time from server.");
  }
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

  // current hour ---------------------
  strJson += F("\"hour\": \"");
  strJson += String(getHourwithDLS());
  strJson += F("\",\n");

  // current date ---------------------
  strJson += F("\"current_date\": \"");
  strJson += getDatewithDLS();
  strJson += F("\",\n");

  // date valid ---------------------
  strJson += F("\"date_valid\": \"");
  strJson += DateTime.isTimeValid();
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

  // token mqtt ---------------------
  strJson += F("\"pulse_factor\": \"");
  strJson += pulse_factor;
  strJson += F("\",\n");

  // token mqtt ---------------------
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
  sprintf(buffer_value, "%.0f", volume);
  client_mqtt.publish(buffer, buffer_value, true);

  sprintf(buffer, "%s/flow", token_mqtt);
  sprintf(buffer_value, "%.1f", flow);
  client_mqtt.publish(buffer, buffer_value);

  sprintf(buffer, "%s/totalPulse", token_mqtt);
  sprintf(buffer_value, "%d", totalPulse);
  client_mqtt.publish(buffer, buffer_value);

  sprintf(buffer, "%s/leak", token_mqtt);
  sprintf(buffer_value, "%d", leak_type);
  client_mqtt.publish(buffer, buffer_value);

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
  if (strstr(buffer, topic) && flag_pulseCount == false) {
    pulseCount = messageTemp.toInt();
    flag_pulseCount = true;
  }
  // retreive volume value after reboot ------------
  sprintf(buffer, "%s/volume", token_mqtt);
  if (strstr(buffer, topic) && flag_volume == false) {
    volume = messageTemp.toInt();
    flag_volume = true;
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
  AsyncWiFiManager wifiManager(&server,&dns);
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

  //----------------------------------------------------Setup Time/NTP
  setupDateTime();
  
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

  lastSend = lastPulse = millis();

  u8g2.clearBuffer();
  startup_date = getDatewithDLS();

  attachInterrupt(digitalPinToInterrupt(DIGITAL_INPUT_SENSOR), onPulse, FALLING);
}


//-----------------------------------------------------------------------
void loop()
{
unsigned long currentTime = millis();

  ArduinoOTA.handle();

  u8g2.setFont(u8g2_font_4x6_tr);

  if (!DateTime.isTimeValid()) {
      u8g2.drawStr(1,60, "*");
      DateTime.begin();
  }
  else {
    u8g2.drawStr(1,60, " ");
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
    uint8_t j = i+1;
    if(j==HBD) j = 0;
    if(conso[i] ==0 && conso[j] == 0) flag_leak_type1 = false;
  }
  
  // check consumption in excess of the 24hr threshold 
  totalPulse=0;
  for (uint8_t i=0; i<HBD; i++) {
    totalPulse += conso[i];
  }
  double volume_cumul = 1000*((double)totalPulse/(double)pulse_factor);
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
  if (getHourwithDLS() != last_hour)  {
    Serial.println(F("Reset conso"));
    conso[getHourwithDLS()] = 0;
    last_hour = getHourwithDLS();
  }
  
  // Every days reset counters --------
  if (DateTime.getParts().getMonthDay() != last_day)  {
    Serial.println(F("Reset counters"));
    pulseCount = 0;
    flow = 0;
    volume = 0;
    last_day = DateTime.getParts().getMonthDay();
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
  if (url_mqtt[0] != 0 && token_mqtt[0] != 0) {
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

  u8g2.sendBuffer();
  u8g2.clearBuffer();
}