// Tado interface and boiler temperature controller.
// Built on standard framework.
// Basic job each interval (minute or so) is to talk to the Tado API
// and gather the heating state of each zone (room), the home/away state
// and tado outside temperature. I have 9 Zones in my Tado setup.
// These then used to calculate a suitable boiler setpoint temperature.
// Boiler setpoint and away state send via mqtt to the boiler temperature controller.
// Provides info display to the excellent Android IoT MQTT Panel by Rahul Kundu
// Objective of all this is to balance the heating circuation temperature to
// the heating demand room temperature control is smoother and less on/off.
// Boiler efficiency is improved.
// Away state used on temperature controller to switch of the hot water heating. 
// Many thanks to Terence Eden's blog https://shkspr.mobi/blog/2019/02/tado-api-guide-updated-for-2019/
// that got me started on packaging this for ESP32.

const char* module = "Boiler Control V2";
const char* buildDate = __DATE__  "  "  __TIME__;

// ----------- start properties include 1 --------------------
#include <SPIFFS.h>
#include <ArduinoJson.h>
String propFile = "/props.properties";   // prop file name
#define PROPSSIZE 40
String propNames[PROPSSIZE];
int propNamesSz = 0;
#define BUFFLEN 200
char buffer[BUFFLEN];
int bufPtr = 0;
#include <esp_task_wdt.h>
#include <EEPROM.h>
// ----------- end properties include 1 --------------------

// ----------- start WiFi & Mqtt & Telnet include 1 ---------------------
#define HAVEWIFI 1
// wifi, time, mqtt
#include <ESP32Time.h>
ESP32Time rtc;
#include <WiFi.h>
#include "ESPTelnet.h"
#include "EscapeCodes.h"
#include <PicoMQTT.h>
ESPTelnet telnet;
IPAddress ip;
#define TIMESYNCHINTERVAL 300
PicoMQTT::Client mqttClient;

// ----------- end  WiFi & Mqtt & Telnet include 1 ---------------------

// ---------- start custom ------------------------
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// ---------- end custom ------------------------

// --------- standard properties ------------------
int logLevel = 2;
String logLevelN = "logLevel";
int eeWriteLimit = 100;
String eeWriteLimitN = "eeWriteLimit";
String wifiSsid = "<ssid>";
String wifiSsidN = "wifiSsid";        
String wifiPwd = "<pwd>";
String wifiPwdN = "wifiPwd";
byte wifiIp4 = 240;
String wifiIp4N = "wifiIp4";
byte mqttIp4 = 200;
String mqttIp4N = "mqttIp4";
int mqttPort = 1883;
String mqttPortN = "mqttPort";   
int telnetPort = 23;
String telnetPortN = "telnetport";   
String mqttId = "xx";          // is username-N and token xx/n/.. from unitId
String mqttIdN = "mqttId";   
int unitId  = 9;                  // uniquness of mqtt id
String unitIdN = "unitId";
int wdTimeout = 30;
String wdTimeoutN = "wdTimeout";
// generic way of setting property as 2 property setting operations
String propNameA = "";
String propNameN = "propName";
String propValue = "";
String propValueN = "propValue";
// these used to apply adjustment via props system
String restartN = "restart";
String writePropsN = "writeProps";

// ------- custom properties -----------
String tLogin = "<login>";
String tLoginN = "tLogin";
String tPassword = "<pwd>";
String tPasswordN = "tPassword";
String tSecret = "<secret>";
String tSecretN = "tSecret";
String tAHost = "auth.tado.com";
String tAHostN = "t1Host";
String tMHost = "my.tado.com";
String tMHostN = "t2Host";
int tPort = 443;
String tPortN = "tPort";
int tInterval = 30;                   // how often scrape data from Tado
String tIntervalN = "tInterval";
bool tDebug = false;                   // shows get and put data
String tDebugN = "tDebug";
double loOutsideComp = 0;                   // outside low temp ref
String loOutsideCompN = "loOutsideComp";
double hiOutsideComp = 15;                   // outside high temp ref
String hiOutsideCompN = "hiOutsideComp";
double hiBoilerComp = 50;                   // boiler high temp ref
String hiBoilerCompN = "hiBoilerComp";
double loBoilerComp = 65;                   // boiler low temp ref
String loBoilerCompN = "loBoilerComp";
double loPowerComp = -3;                   // tado power level low temp adjust (0%)
String loPowerCompN = "loPowerComp";
double hiPowerComp = +3;                   // tado power level high temp adjust (100%)
String hiPowerCompN = "hiPowerComp";

// ------- end custom properties -----------

// ----------- start properties include 2 --------------------

bool mountSpiffs()
{
   if(!SPIFFS.begin(true))
  {
    log(1, "SPIFFS Mount Failed");
    return false;
  }
  else
  {
    log(1, "SPIFFS formatted");
  }
  return true;
}

// checks a property name in json doc and keeps a list in propNames
bool checkProp(JsonDocument &doc, String propName, bool reportMissing)
{
  if (propNamesSz >= PROPSSIZE)
  {
    log(0, "!! props names limit");
  }
  else
  {
    propNames[propNamesSz++] = propName;
  }
  if (doc.containsKey(propName))
  {
    String val = doc[propName].as<String>();
    log(0, propName + "=" + val);
    return true;
  }
  if (reportMissing)
  {
    log(0, propName + " missing");
  }
  return false;
}

bool readProps()
{
  log(0, "Reading file " + propFile);
  File file = SPIFFS.open(propFile);
  if(!file || file.isDirectory())
  {
    log(0, "− failed to open file for reading");
    return false;
  }
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  if (error) 
  {
    log(0, "deserializeJson() failed: ");
    log(0, error.f_str());
    return false;
  }
  extractProps(doc, true);
  return true;
}

// writes/displays the props
bool writeProps(bool noWrite)
{
  JsonDocument doc;
  addProps(doc);
  String s;
  serializeJsonPretty(doc, s);
  s = s.substring(3,s.length()-2);
  log(0, s);
  if (noWrite)
  {
    return true;
  }
  log(0, "Writing file:" + propFile);
  File file = SPIFFS.open(propFile, FILE_WRITE);
  if(!file)
  {
    log(0, "− failed to open file for write");
    return false;
  }
  serializeJson(doc, file);
  file.close();
  return true;
}

// is expected to be form 'name=value' or 'name value' and can be a comma sep list
// name can be case insensitve match on first unique characters..
// converted to Json to update
void adjustProp(String s)
{
  String ss = s;
  while (true)
  {
    int p1 = ss.indexOf(',');
    if (p1 < 0)
    {
      adjustProp2(ss);
      return;
    }
    String s1 = ss.substring(0, p1);
    adjustProp2(s1);
    ss = ss.substring(p1+1);
  }
}
void adjustProp2(String s)
{
  int p1 = s.indexOf('=');
  if (p1 < 0)
  {
    p1 = s.indexOf(' ');
  }
  if (p1 < 0)
  {
    log(0, "no = or space found");
    return;
  }
  String p = s.substring(0,p1);
  String pl = p;
  pl.toLowerCase();
  String v = s.substring(p1+1);
  int ip;
  int m = 0;
  for (int ix = 0; ix < propNamesSz; ix++)
  {
    if (propNames[ix].length() >= pl.length())
    {
      String pn = propNames[ix].substring(0, pl.length());
      pn.toLowerCase();
      if (pl == pn)
      {
        if (m == 0)
        {
          ip = ix;
          m++;
        }
        else
        {
          if (m == 1)
          {
            log(0, "duplicate match " + p + " " + propNames[ip]);
          }
          m++;
          log(0, "duplicate match " + p + " " + propNames[ix]);
        }
      }
    }
  }
  if (m > 1)
  {
    return;
  }
  else if (m==0)
  {
    log(0, "no match " + p);
    return;
  }
  s = "{\"" + propNames[ip] + "\":\"" + v + "\"}";
 
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, s);
  if (error) 
  {
    log(0, "deserializeJson() failed: ");
    log(0, error.f_str());
    return;
  }
  extractProps(doc, false);
}

// logger
void log(int level, String s)
{
  if (level > logLevel) return;
  Serial.println(s);
  #if HAVEWIFI
  telnet.println(s);
  #endif
}
void log(int level)
{
  if (level > logLevel) return;
  Serial.println();
  #if HAVEWIFI
  telnet.println();
  #endif
}
void loga(int level, String s)
{
  if (level > logLevel) return;
  Serial.print(s);
  #if HAVEWIFI
  telnet.print(s);
  #endif
}

void checkSerial()
{
  while (Serial.available())
  {
    char c = Serial.read();
    switch (c)
    {
      case 0:
        break;
      case '\r':
        break;
      case '\n':
        buffer[bufPtr++] = 0;
        processCommandLine(String(buffer));
        bufPtr = 0;
        break;
      default:
        if (bufPtr < BUFFLEN -1)
        {
          buffer[bufPtr++] = c;
        }
        break;
    }
  } 
}
// for counting restarts - write to eprom
struct eeStruct
{
  unsigned int writes = 0;
  unsigned int wdtRestart = 0;
  unsigned int isrRestart = 0;
  unsigned int panicRestart = 0;
  unsigned int otherRestart = 0;
};

eeStruct eeData;

// for reliability stats from each restart
int getGatewayCount = 0;
int getWifiCount = 0;
int reConnWifiCount = 0;
unsigned long mqttDiscMs = 0;
int mqttConnCount = 0;
int mqttDiscCount = 0;
int mqttConnFailCount = 0;
int mqttConnTimeMs = 0;
int mqttSendCount = 0;
int mqttInCount = 0;

void eeDataReset()
{
  eeData.writes = 0;
  eeData.isrRestart = 0;
  eeData.wdtRestart = 0;
  eeData.panicRestart = 0;
  eeData.otherRestart = 0;
}

void eepromInit()
{
  int eeSize = sizeof(eeData);
  EEPROM.begin(eeSize);
  log(0, "ee size "+ String(eeSize));
}
void eepromWrite()
{
  eeData.writes++;
  if (eeData.writes > eeWriteLimit)
  {
    log(0, "eeprop Write limit..");
    return;
  }
  EEPROM.put(0, eeData);
  EEPROM.commit();
  log(0, "eewrite:" + String(eeData.writes));
}
void eepromRead()
{
  EEPROM.get(0, eeData);
  log(0, "eeWrites: " + String(eeData.writes));
}
void checkRestartReason()
{
  eepromRead();
  int resetReason = esp_reset_reason();
  log(0, "ResetReason: " + String(resetReason));
  switch (resetReason)
  {
    case ESP_RST_POWERON:
      return;// ok
    case ESP_RST_PANIC:
      eeData.panicRestart++;
      break;
    case ESP_RST_INT_WDT:
      eeData.isrRestart++;
      break;
    case ESP_RST_TASK_WDT:
      eeData.wdtRestart++;
      break;
    default:
      eeData.otherRestart++;
      break;
  }
  eepromWrite();
  logResetStats();
}

void logResetStats()
{
  log(0, "eeWrites: " + String(eeData.writes));
  log(0, "panic: " + String(eeData.panicRestart));
  log(0, "taskwd: " + String(eeData.wdtRestart));
  log(0, "irswd: " + String(eeData.isrRestart));
  log(0, "other: " + String(eeData.otherRestart));
  #if HAVEWIFI
  log(0, "getGateway: " + String(getGatewayCount));
  log(0, "getWifi: " + String(getWifiCount));
  log(0, "reconnWifi: " + String(reConnWifiCount));
  log(0, "mqttConn: " + String(mqttConnCount));
  log(0, "mqttConnT: " + String(mqttConnTimeMs));
  log(0, "mqttDisc: " + String(mqttDiscCount));
  log(0, "mqttFail: " + String(mqttConnFailCount));
  log(0, "mqttSend: " + String(mqttSendCount));
  log(0, "mqttIn: " + String(mqttInCount));
  log(0, "wfChannel: " + String(WiFi.channel()));
  log(0, "wfRSSI: " + String(WiFi.RSSI()));
  log(0, "wfPower: " + String(WiFi.getTxPower()));
  #endif
}
// ----------- end properties include 2 --------------------

// ----------- custom properties modify section  --------------------
// extract and add properties to json doc
// customize this for props expected and data types - watch with bools

void extractProps(JsonDocument &doc, bool reportMissing)
{
  propNamesSz = 0;
  log(0, "setting properties:");
  String propName;
  propName = logLevelN; if (checkProp(doc, propName, reportMissing)) logLevel = doc[propName].as<int>();
  propName = eeWriteLimitN; if (checkProp(doc, propName, reportMissing)) eeWriteLimit = doc[propName].as<int>();
  propName = wifiSsidN; if (checkProp(doc, propName, reportMissing)) wifiSsid = doc[propName].as<String>();
  propName = wifiPwdN;  if (checkProp(doc, propName, reportMissing)) wifiPwd = doc[propName].as<String>();
  propName = wifiIp4N;  if (checkProp(doc, propName, reportMissing)) wifiIp4 = doc[propName].as<byte>();
  propName = mqttPortN; if (checkProp(doc, propName, reportMissing)) mqttPort = doc[propName].as<int>();
  propName = mqttIp4N;  if (checkProp(doc, propName, reportMissing)) mqttIp4 = doc[propName].as<byte>();
  propName = telnetPortN;if (checkProp(doc, propName, reportMissing)) telnetPort = doc[propName].as<int>();
  propName = mqttIdN;   if (checkProp(doc, propName, reportMissing)) mqttId = doc[propName].as<String>();
  propName = unitIdN;   if (checkProp(doc, propName, reportMissing)) unitId = doc[propName].as<int>();
  propName = wdTimeoutN;if (checkProp(doc, propName, reportMissing)) wdTimeout = max(doc[propName].as<int>(),30);
  // these just for adjustment
  propName = restartN; if (checkProp(doc, propName, false)) ESP.restart();
  propName = writePropsN; if (checkProp(doc, propName, false)) writeProps(false);
  propName = propNameN; if (checkProp(doc, propName, false)) propNameA = doc[propName].as<String>();
  propName = propValueN;if (checkProp(doc, propName, false)) propValue = doc[propName].as<String>();  // picked up in checkState()

  // ----- start custom extract -----
  propName = tLoginN;   if (checkProp(doc, propName, reportMissing)) tLogin = doc[propName].as<String>();
  propName = tPasswordN;   if (checkProp(doc, propName, reportMissing)) tPassword = doc[propName].as<String>();
  propName = tSecretN;   if (checkProp(doc, propName, reportMissing)) tSecret = doc[propName].as<String>();
  propName = tAHostN;   if (checkProp(doc, propName, reportMissing)) tAHost = doc[propName].as<String>();
  propName = tMHostN;   if (checkProp(doc, propName, reportMissing)) tMHost = doc[propName].as<String>();
  propName = tPortN;   if (checkProp(doc, propName, reportMissing)) tPort = doc[propName].as<int>();
  propName = tIntervalN;   if (checkProp(doc, propName, reportMissing)) tInterval = doc[propName].as<int>();
  propName = tDebugN;   if (checkProp(doc, propName, reportMissing)) tDebug = doc[propName].as<int>();
  propName = loOutsideCompN;   if (checkProp(doc, propName, reportMissing)) loOutsideComp = doc[propName].as<double>();
  propName = hiOutsideCompN;   if (checkProp(doc, propName, reportMissing)) hiOutsideComp = doc[propName].as<double>();
  propName = loBoilerCompN;   if (checkProp(doc, propName, reportMissing)) loBoilerComp = doc[propName].as<double>();
  propName = hiBoilerCompN;   if (checkProp(doc, propName, reportMissing)) hiBoilerComp = doc[propName].as<double>();
  propName = loPowerCompN;   if (checkProp(doc, propName, reportMissing)) loPowerComp = doc[propName].as<double>();
  propName = hiPowerCompN;   if (checkProp(doc, propName, reportMissing)) hiPowerComp = doc[propName].as<double>();

  // ----- end custom extract -----
}

// adds props for props write - customize
void addProps(JsonDocument &doc)
{
  doc[logLevelN] = logLevel;
  doc[eeWriteLimitN] = eeWriteLimit;
  doc[wifiSsidN] = wifiSsid;
  doc[wifiPwdN] = wifiPwd;
  doc[wifiIp4N] = wifiIp4;
  doc[mqttIp4N] = mqttIp4;
  doc[telnetPortN] = telnetPort;
  doc[mqttPortN] = mqttPort;
  doc[mqttIdN] = mqttId;
  doc[unitIdN] = unitId;
  doc[wdTimeoutN] = wdTimeout;

  // ----- start custom add -----
  doc[tLoginN] = tLogin; 
  doc[tPasswordN] = tPassword; 
  doc[tSecretN] = tSecret;
  doc[tAHostN] = tAHost; 
  doc[tMHostN] = tMHost;
  doc[tPortN] = tPort;
  doc[tIntervalN] = tInterval;
  doc[tDebugN] = tDebug;
  doc[loOutsideCompN] = loOutsideComp;
  doc[hiOutsideCompN] = hiOutsideComp;
  doc[loBoilerCompN] = loBoilerComp;
  doc[hiBoilerCompN] = hiBoilerComp;
  doc[loPowerCompN] = loPowerComp;
  doc[hiPowerCompN] = hiPowerComp;

  // ----- end custom add -----
}

// custom modified section for props control and general commands
void processCommandLine(String cmdLine)
{
  if (cmdLine.length() == 0)
  {
    return;
  }
  
  switch (cmdLine[0])
  {
    case 'h':
    case '?':
      log(0, "v:version, w:writeprops, d:dispprops, l:loadprops p<prop>=<val>: change prop, r:restart");
      log(0, "s:showstats, z:zerostats, 0,1,2:loglevel = " + String(logLevel));
      return;
    case 'w':
      writeProps(false);
      return;
    case 'd':
      writeProps(true);
      return;
    case 'l':
      readProps();
      return;
    case 'p':
      adjustProp(cmdLine.substring(1));
      return;
    case 'r':
      ESP.restart();
      return;
    case 'v':
      loga(0, module);
      loga(0, " ");
      log(0, buildDate);
      return;
    case 'z':
      eeDataReset();
      return;
    case 's':
      logResetStats();
      return;
    case 't':
      {
        tm now = rtc.getTimeStruct();
        log(0, "ESP time: " + dateTimeIso(now));
        return;
      }
    case '0':
      logLevel = 0;
      log(0, " loglevel=" + String(logLevel));
      return;
    case '1':
      logLevel = 1;
      log(0, " loglevel=" + String(logLevel));
      return;
    case '2':
      logLevel = 2;
      log(0, " loglevel=" + String(logLevel));
      return;

  // ----- start custom cmd -----
    case 'b':
      checkAccessToken();
      return; 
    case 'i':
      tadoInit();
      return;
    case 'c':
      getZoneStates();
      return;
  // ----- end custom cmd -----
    default:
      log(0, "????");
      return;
  }
}
// ----------- end custom properties modify section  --------------------

// ------------ start wifi and mqtt include section 2 ---------------
IPAddress localIp;
IPAddress gatewayIp;
IPAddress primaryDNSIp;
String mqttMoniker;

int recoveries = 0;
unsigned long seconds = 0;
unsigned long lastSecondMs = 0;
unsigned long startRetryDelay = 0;
int long retryDelayTime = 10;  // seconds
bool retryDelay = false;


// state engine
#define START 0
#define STARTGETGATEWAY 1
#define WAITGETGATEWAY 2
#define STARTCONNECTWIFI 3
#define WAITCONNECTWIFI 4
#define ALLOK 5

String stateS(int state)
{
  if (state == START) return "start";
  if (state == STARTGETGATEWAY) return "startgetgateway";
  if (state == WAITGETGATEWAY) return "waitgetgateway";
  if (state == STARTCONNECTWIFI) return "startconnectwifi";
  if (state == WAITCONNECTWIFI) return "waitconnectwifi";
  if (state == ALLOK) return "allok";
  return "????";
}
int state = START;

unsigned long startWaitWifi;

bool startGetGateway()
{
  getGatewayCount++;
  WiFi.disconnect();
  delay (500);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  log(1, "Start wifi dhcp" + wifiSsid + " " + wifiPwd);
  WiFi.begin(wifiSsid, wifiPwd);
  startWaitWifi = millis();
  return true;
}
bool startWifi()
{
  getWifiCount++;
  WiFi.disconnect();
  WiFi.setAutoReconnect(true);
  delay (500);
  unsigned long startWaitWifi = millis();
  WiFi.mode(WIFI_STA);
  IPAddress subnet(255, 255, 0, 0);
  IPAddress fixedIp = localIp;
  fixedIp[3] = wifiIp4;
  if (!WiFi.config(fixedIp, gatewayIp, subnet, primaryDNSIp, primaryDNSIp)) 
  {
    log(1, "STA Failed to configure");
    return false;
  }
  log(1, "Start wifi fixip: " + wifiSsid + " " + wifiPwd);
  WiFi.begin(wifiSsid, wifiPwd);
  return true;
}

int waitWifi()
{
  // 0=waiting, <0=fail, >0=ok
  unsigned long et = millis() - startWaitWifi;
  if (WiFi.status() == WL_CONNECTED)
  {
    localIp = WiFi.localIP();
    gatewayIp = WiFi.gatewayIP();
    primaryDNSIp = WiFi.dnsIP();
    log(1, "connected, t=" + String(et) + ", local=" + localIp.toString() + " gateway=" + gatewayIp.toString() + " dns=" + primaryDNSIp.toString());
    reConnWifiCount--;
    return 1;
  }
  
  if (et > 30000)
  {
    log(1, "... fail wifi connection timeout");
    return -1;
  }
  return 0;
}


// ------------- mqtt section -----------------

// mqtt 
void setupMqttClient()
{
  IPAddress fixedIp = localIp;
  fixedIp[3] = mqttIp4;
  String server = fixedIp.toString();
  mqttClient.host = server;
  mqttClient.port = mqttPort;
  mqttMoniker = mqttId + "/" + String(unitId);
  mqttClient.client_id = mqttMoniker;
  String topic = mqttMoniker + "/c/#";
  mqttClient.subscribe(topic, &mqttMessageHandler);
  mqttSubscribeAdd();
  mqttClient.connected_callback = [] {mqttConnHandler();};
  mqttClient.disconnected_callback = [] {mqttDiscHandler();};
  mqttClient.connection_failure_callback = [] {mqttFailHandler();};
  mqttClient.begin();
}

// mqtt handlers
void mqttConnHandler()
{
  log(0, "MQTT connected: " + String(millis() - mqttDiscMs));
  mqttConnCount++;
}
void mqttDiscHandler()
{
  log(0, "MQTT disconnected");
  mqttDiscCount++;
  mqttDiscMs = millis();
}
void mqttFailHandler()
{
  log(0, "MQTT CONN FAIL");
  if (WiFi.isConnected())
  {
    mqttConnFailCount++;
  }
  mqttDiscMs = millis();
}
void mqttMessageHandler(const char * topicC, Stream & stream)
{
  mqttInCount++;
  String topic = String(topicC);
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, stream);
  if (error) 
  {
    log(2, "deserialize fail: " +  String(error.f_str()) + " " + topic);
    return;
  }
  if (logLevel >=2)
  {
    String s;
    serializeJson(doc, s);
    log(2, "in:" + topic + " " + s);
   }
  if (topic.endsWith("/p"))
  {   
    // its a property setting
    adjustProp(doc["p"].as<String>());
  }
  else if (topic.endsWith("/t"))
  {   
    // its a timeSynch
    setEspTime(doc);
  }
  else
  {
    handleIncoming(topic, doc);
  }
}

void mqttSend(String topic, JsonDocument &doc)
{
  if (logLevel >=2)
  {
    String s;
    serializeJson(doc, s);
    log(2, "out: " + topic + " " + s);
  }
  if (WiFi.isConnected() && mqttClient.connected())
  {
    // publish using begin_publish()/send() API
    auto publish = mqttClient.begin_publish(topic, measureJson(doc));
    serializeJson(doc, publish);
    publish.send();
    mqttSendCount++;
  }
}

// ------------ telnet --------------
void setupTelnet() 
{  
  telnet.stop();
  // passing on functions for various telnet events
  telnet.onConnect(onTelnetConnect);
  telnet.onDisconnect(onTelnetDisconnect);
  telnet.onConnectionAttempt(onTelnetConnectionAttempt);
  telnet.onReconnect(onTelnetReconnect);
  telnet.onInputReceived(onTelnetInput);

  if (telnet.begin(telnetPort)) 
  {
    log(1, "telnet running");
  } 
  else 
  {
    log(1, "telnet start fail");
  }
}

void onTelnetConnect(String ip) 
{
  Serial.println("telnet connected");
  telnet.println("hello..");
}
void onTelnetDisconnect(String ip) 
{
  Serial.println("telnet disconnected");
}

void onTelnetReconnect(String ip) 
{
  Serial.println("telnet reconnected");
}

void onTelnetConnectionAttempt(String ip) 
{
  Serial.println("another telnet tried to connected - disconnecting");
  telnet.println("Another session trying. disconnecting you..");
  telnet.disconnectClient();
}

void onTelnetInput(String str) 
{
  processCommandLine(str);
}
// ESP32 Time
int timeSynchIntervalAct = 5;    // initially 5 sec, then TIMESYNCHINTERVAL
unsigned long lastTimeSynch;

String formatd2(int i)
{
  if (i < 10)
  {
    return "0" + String(i);
  }
  return String(i);
}
String dateTimeIso(tm d)
{
  return String(d.tm_year+1900)+"-"+formatd2(d.tm_mon+1)+"-"+formatd2(d.tm_mday)+"T"+formatd2(d.tm_hour)+":"+formatd2(d.tm_min)+":"+formatd2(d.tm_sec);
}

void setEspTime(JsonDocument &doc)
{
  rtc.setTime(doc["se"].as<int>(), doc["mi"].as<int>(), doc["hr"].as<int>(), doc["dy"].as<int>(), doc["mn"].as<int>(), doc["yr"].as<int>());
  tm now = rtc.getTimeStruct();
  log(2, "esp time synch:" + dateTimeIso(now));
  timeSynchIntervalAct = TIMESYNCHINTERVAL;
}
void requestTimeSynch()
{
  JsonDocument doc;
  doc["topic"] = mqttMoniker + "/c/t";
  mqttSend("mb/t", doc);
}
void setRetryDelay()
{
  startRetryDelay = seconds;
  retryDelay = true;
  log(1, "retry delay....");
  recoveries++;
}
bool lastWifiState = false;
// state engine manager
void checkState()
{
  if (propValue != "")
  {
    adjustProp(propNameA + "=" + propValue);
    propValue = "";
  }  
  unsigned long nowMs = millis();
  while (nowMs - lastSecondMs > 1000)
  {
    seconds++;
    lastSecondMs+= 1000;
  }
  if (seconds - lastTimeSynch > timeSynchIntervalAct)
  {
    lastTimeSynch = seconds;
    requestTimeSynch();
  }
  bool thisWifiState = WiFi.isConnected();
  if (thisWifiState != lastWifiState)
  {
    if (thisWifiState)
    {
      log(0, "WiFi Connected..");
      reConnWifiCount++;
    }
    else
    {
      log(0, "WiFi Disconnected..");
    }
    lastWifiState = thisWifiState;
  }
 
 
  if (retryDelay)
  {
    if (seconds - startRetryDelay < (retryDelayTime))
    {
      return; // retry wait
    }
    else
    {
      retryDelay = false;
    }
  }

  int res;
  switch (state)
  {
    case START:
      state = STARTGETGATEWAY;
      return;
    case STARTGETGATEWAY:
      // mandatory we get gateway info before proceeding
      if (!startGetGateway())
      {
        setRetryDelay();
        return;
      }
      state = WAITGETGATEWAY;
      return;
    case WAITGETGATEWAY:
      res = waitWifi();
      if (res == 0)
      {
        return;
      }
      if (res < 0)
      {
        setRetryDelay();
        state = STARTGETGATEWAY;
        return;
      }
    case STARTCONNECTWIFI:
      if (!startWifi())
      {
        setRetryDelay();
        return;
      }
      state = WAITCONNECTWIFI;
      return;
    case WAITCONNECTWIFI:
      // mandatory we get connected once before proceeding
      res = waitWifi();
      if (res == 0)
      {
        return;
      }
      if (res < 0)
      {
        setRetryDelay();
        state = STARTCONNECTWIFI;
        return;
      }
      setupTelnet();
      setupMqttClient();
      state = ALLOK;

      return;

    case ALLOK:
      return;
  }
}
// ------------ end wifi and mqtt include section 2 ---------------

// ------------ start wifi and mqtt custom section 2 ---------------
void mqttSubscribeAdd()
{
  // use standard or custom handler
  // start subscribe add
 
  // end subscribe add
}
void handleIncoming(String topic, JsonDocument &doc)
{
  // start custom additional incoming
  if (topic.endsWith("/u"))
  {
    triggerTadoUpdate();
  }
  if (topic.endsWith("/r"))
  {
    triggerSendZoneStats();
  }
  // end custom additional incoming
}
// ------------ end wifi and mqtt custom section 2 ---------------

// ---- start custom code --------------

// -------------- start tado specific code --------------------------------

String accessToken  = "";
unsigned long accessTokenTime = 0;    
int accessTokenTimeout  = 550;          // seconds - times out at 10 mins
String homeId = "";

// data collected from Tado
struct zone
{
  int id;
  String name;
  double setTemp;
  double ovrTemp;
  double power;
  double actTemp;
  double humidity;
};
zone zones[20];
int numZones = 0;
double solarIntensity = 50;
double outsideTemp = 15;
bool tadoAway = false;

// what is calculated
int setTemp;         // for boiler temp conrrol along with away state

// working data
unsigned long lastTadoTime;  //last time got data (seconds)
int tadoPower;               // calculation of power level
bool doSendZoneStats;        // remote trigger to update mqtt display
bool doTadoUpdate;           // immediate tado data update
byte mqttSeq;                // incremented sequence number with mqtt data

void triggerSendZoneStats()
{
  doSendZoneStats = true;
}
void triggerTadoUpdate()
{
  doTadoUpdate = true;
}

bool getTadoConnection(String host, int port, WiFiClientSecure &httpsClient)
{
  httpsClient.setInsecure();
  httpsClient.setTimeout(15000); // 15 Seconds
  int res = httpsClient.connect(host.c_str(), port); 
  if (res != 1)
  {
    log(1, "tado connection res=" + String(res));
    return false;
  }
  return true;
}

bool callTado(WiFiClientSecure &httpsClient, String rqType, bool addAuth, String host, String link, String header, String body, JsonDocument &doc)
{
  // clear any outstanding reply data
  while (true)
  {
    int ix = httpsClient.available();
    if (ix == 0)
    {
      break;
    }
    char b[1000];
    ix = min(ix,1000);
    httpsClient.readBytes(b,ix);
    String s = String(b);
    log(1, "Spilled:" + s);
  }
  int httpBodyLength = body.length();

  String httpText = rqType + " " + link + " HTTP/1.1\r\n";
  httpText+= "Host: " + host + "\r\n";
  if (addAuth)
  {
    httpText+= "Authorization: Bearer " + accessToken + "\r\n";
  }
  if (body.length() > 0 && body[0] == '{')
  {
    httpText+= "Content-Type: application/json\r\n"; 
  }
  else
  {
    httpText+= "Content-Type: application/x-www-form-urlencoded\r\n";
  }
  httpText+= "Accept: */*\r\n";
  httpText+= "Cache-Control: no-cache\r\n";
  httpText+= "Content-Length: " + String(httpBodyLength) + "\r\n";
  httpText+= "Connection: keep-alive\r\n";
  httpText+= String("\r\n") + body + "\r\n";

  unsigned long startMs = millis();
  httpsClient.print(httpText);

  if (tDebug)
  {
    log(1,"request: + httpText");
  }
  
  String requestError = "";
  while (true)
  {
    if (!httpsClient.connected())
    {
      log(1,"lost tado connection");
      return false;
    }
    if (millis() - startMs > 15000)
    {
      log(1,"tado reply timeout");
      httpsClient.stop();
      return false;
    }
    String response = httpsClient.readStringUntil('\n');
    if (response.startsWith("HTTP/1.1 200"))
    {
      if (!response.startsWith("HTTP/1.1 200"))
      {
        requestError = response;
      }
    }
    if (tDebug)
    {
      log(1,response);
    }
    if (response == "\r")
    {
      break;
    }
  }
  if (tDebug)
  {
    log(1,"Getting data..");
  }
  String jsonData = "";
  
  while (true)
  {
    if (!httpsClient.connected())
    {
      log(1,"lost tado connection");
      return false;
    }
    if (millis() - startMs > 15000)
    {
      log(1,"tado reply timeout");
      httpsClient.stop();
      return false;
    }
    String response = httpsClient.readStringUntil('\n');
    if (tDebug)
    {
      log(2, response);
    }
    if (response.startsWith("[") || response.startsWith("{"))
    {
      jsonData = response;
    }
    if (response == "\r")
    {
      break;
    }
  }
  log(2, "T=" + String(millis() - startMs) + ", size=" + String(jsonData.length()));
  if (requestError != "")
  {
    log(1,"RequestError: " + requestError);
    log(1, "jsonData");
    return false;
  }
  if (jsonData == "")
  {
    log(1,"didnt find json data");
    return false;
  }

  DeserializationError error = deserializeJson(doc, jsonData);
  
  // Test if parsing succeeds.
  if (error) 
  {
    log(1, "deserializeJson() failed: ");
    log(1, error.f_str());
    return false;
  }
  if (tDebug)
  {
    // only do to serial..
    int size = serializeJsonPretty(doc, Serial);
    log(1, "prettySize=" + String(size));
  }
  return true;
}

bool checkAccessToken()
{
  if (accessToken != "" && seconds - accessTokenTime < accessTokenTimeout)
  {
    return true;
  }
  accessToken = "";
  WiFiClientSecure httpsClient;
  if (!getTadoConnection(tAHost, tPort, httpsClient))
  {
    httpsClient.stop();
    return false;
  }
  String body =  "client_id=tado-web-app&grant_type=password&scope=home.user&username=" + tLogin + "&password=" + tPassword + "&client_secret=" + tSecret;
  JsonDocument doc;
  if (!callTado(httpsClient, "POST", false, tAHost, "/oauth/token","", body, doc))
  {
    httpsClient.stop();
    return false;
  }
  httpsClient.stop();
  accessToken = doc["access_token"].as<String>();
  accessTokenTime = seconds;
  log(2, "accessToken=" + accessToken);
  return true;
}

bool tadoInit()
{
  if (!checkAccessToken())
  {
    return false;
  }
  WiFiClientSecure httpsClient;
  if (!getTadoConnection(tMHost, tPort, httpsClient))
  {
    httpsClient.stop();
    return false;
  }
  JsonDocument doc;
  if (!callTado(httpsClient, "GET", true, tMHost, "/api/v1/me", "", "", doc))
  {
    httpsClient.stop();
    return false;
  }
  homeId = doc["homeId"].as<String>();
  log(1, "homeid=" + homeId);

  // zones
  numZones = 0;
  doc.clear();
  if (!callTado(httpsClient, "GET", true, tMHost, "/api/v2/homes/" + homeId + "/zones", "", "", doc))
  {
    httpsClient.stop();
    return false;
  }
  
  for (int ix = 0; ix < 12; ix++)
  {
    zone z;
    JsonDocument d = doc[ix];
    if (d.isNull())
    {
      break;
    }
    z.id = d["id"].as<int>();
    z.name = d["name"].as<String>();
    log(1, String(z.id) + " " + z.name);
    zones[numZones++] = z;
  }
  httpsClient.stop();
  return true;
}

bool getZoneStates()
{
   if (!checkAccessToken())
  {
    return false;
  }
  WiFiClientSecure httpsClient;
  if (!getTadoConnection(tMHost, tPort, httpsClient))
  {
    httpsClient.stop();
    return false;
  }

  for (int ix=0; ix < numZones; ix++)
  {
    JsonDocument doc;

    if (!callTado(httpsClient, "GET", true, tMHost, "/api/v2/homes/" + homeId + "/zones/" + String(zones[ix].id) + "/state", "", "", doc))
    {
      httpsClient.stop();
      return false;
    }
   
    zones[ix].setTemp = doc["setting"]["temperature"]["celsius"].as<double>();
    zones[ix].ovrTemp = doc["overlay"]["setting"]["temperature"]["celsius"].as<double>();
    zones[ix].power = doc["activityDataPoints"]["heatingPower"]["percentage"].as<double>();
    zones[ix].actTemp = doc["sensorDataPoints"]["insideTemperature"]["celsius"].as<double>();
    zones[ix].humidity = doc["sensorDataPoints"]["humidity"]["percentage"].as<double>();
    if (logLevel >=2)
    {
      log (2, String(zones[ix].id) + "=" + String(zones[ix].setTemp) + "," + String(zones[ix].ovrTemp) + "," + String(zones[ix].power) + ","+ String(zones[ix].actTemp) + "," + String(zones[ix].humidity));
    }
  }

  // weather
  {
    JsonDocument doc;
    if (!callTado(httpsClient, "GET", true, tMHost, "/api/v2/homes/" + homeId + "/weather", "", "", doc))
    {
      httpsClient.stop();
      return false;
    }
    outsideTemp = doc["outsideTemperature"]["celsius"].as<double>();
    solarIntensity = doc["solarIntensity"]["percentage"].as<double>();
    if (logLevel >=2)
    {
      log (2, "outside temp/solar=" + String(outsideTemp) + ", " + String(solarIntensity));
    }

  }
  // presence
  {
    JsonDocument doc;
    if (!callTado(httpsClient, "GET", true, tMHost, "/api/v2/homes/" + homeId + "/state", "", "", doc))
    {
      httpsClient.stop();
      return false;
    }
    String presenceS = doc["presence"].as<String>();
    tadoAway = presenceS == "AWAY"; 
    if (logLevel >=2)
    {
      log (2, "presence=" + presenceS + " away=" + String(tadoAway));
    }
  }
  httpsClient.stop();
  return true;
}

 // as nn.n
String dp1(double d)
{
  String s = String(int(d*10 + 0.5));
  s = s.substring(0,s.length()-1) + "." + s.substring(s.length()-1);
  return s;
}
unsigned long lastGraphTime = 0;
void sendZoneStates(bool andHive)
{
  // send mqtt data
  JsonDocument doc;
  
  for (int ix = 0; ix < numZones; ix++)
  {
    doc["z" + String(ix)] = zones[ix].name.substring(0,2) + ":" + dp1(zones[ix].setTemp) + " " + dp1(zones[ix].actTemp) + " " + String(int(zones[ix].power));
  }
  doc["p"] = tadoPower;
  doc["t"] = int(setTemp*10 + 0.5);
  doc["o"] = int(outsideTemp*10 + 0.5);
  if (seconds - lastGraphTime >= 300)
  {
    doc["tg"] = int(setTemp*10 + 0.5);   // no more than graph interval
    lastGraphTime = seconds;
  }
  doc["s"] = mqttSeq++;
  String topic = mqttMoniker + "/d";
  mqttSend(topic, doc);
  if (andHive)
  {
    mqttSend(">/" + topic, doc);
  }
}

// currrently as max power for any room - to be worked on
// pretty much same as tado overall power.
void calcPower()
{
  int power = 0;
  for (int ix = 0; ix < numZones; ix++)
  {
    if (zones[ix].power > power)
    {
      power = zones[ix].power;
    }
  }
  tadoPower = constrain(power, 0, 100);
}

void calcSetTemp()
{
  double d = loBoilerComp + ((outsideTemp - loOutsideComp)/(hiOutsideComp-loOutsideComp) * (hiBoilerComp - loBoilerComp));
  d += tadoPower/100.0 * (hiPowerComp - loPowerComp);
  setTemp = constrain(d, hiBoilerComp, loBoilerComp);
  // send to temp controller
  JsonDocument doc;
  doc["st"] = setTemp;
  doc ["aw"] = tadoAway;
  String topic = "bt/1/c/o";
  mqttSend(topic, doc);
}

void tadoLoop()
{
  if (seconds - lastTadoTime >= tInterval || doTadoUpdate)
  {
    lastTadoTime = seconds;
    doTadoUpdate = false;
    if (homeId == "" || numZones == 0)
    {
      if (!tadoInit())
      {
        return;
      }
    }
    getZoneStates();
    calcPower();
    calcSetTemp();
    sendZoneStates(false);         // default exclude hive
  }
  if (doSendZoneStats)
  {
    sendZoneStates(true);          // include hive 
    doSendZoneStats = false;
  }
}

// ---- end custom code --------------

void setup()
{
  loga(1, module);
  loga(1, " ");
  log(1, buildDate);
  Serial.begin(115200);
  checkRestartReason();
  mountSpiffs();
  readProps();
  esp_task_wdt_config_t config;
  int wdt = max(wdTimeout*1000,2000);
  config.timeout_ms = max(wdTimeout*1000,2000);;
  config.idle_core_mask = 3; //both cores
  config.trigger_panic = true;
  esp_task_wdt_reconfigure(&config);
  esp_task_wdt_add(NULL);
}

void loop()
{
  esp_task_wdt_reset();
  telnet.loop();
  mqttClient.loop();
  checkSerial();
  checkState();
  tadoLoop();

  delay(1);
}
