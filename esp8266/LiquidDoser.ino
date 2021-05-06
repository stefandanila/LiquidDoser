#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <FS.h>

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;

#define MICRO 17.5
#define MACRO 20
#define CARBO 20

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

int myStaticIp [4] = {192, 168, 0, 51};
int myGateway [4] = {192, 168, 0, 1};
int mySubnet [4] = {255, 255, 255, 0};
int myDns [4] = {192, 168, 0, 1};
int firstDot;
int secondDot;
int thirdDot;

const char* deviceName = "Aquarium Doser";
String ssid = "config";
String password = "12345678";
String userName = "admin";
String userPass = "1234";
boolean useUserPass = false;

String localToken = String(ESP.getChipId()) + String(ESP.getFreeHeap() * ESP.random() + millis());
long lastTokenUpdate = 0;

typedef struct {
  int id;
  String name;
  int timeOfDosing;
  float quantityMl;
} Dose;

typedef struct {
  boolean isUsed;
  Dose dose;
  boolean days[7];
} AssignedDose;

typedef struct {
  boolean used;
  String name;
  int pin;
  AssignedDose assignedDoses[20];
} Pump;


long lastTurnOn[4] = {};
long timeToTurnOff[4] = {};
boolean isOn[4] = {};

const char* host = "doser-webupdate";
String formattedTime = "0";

const int allDosesSize = 20;
Dose allDoses[allDosesSize];

Pump pumpOne = {true, "Macro", 0, {}};
Pump pumpTwo = {true, "Micro", 1, {}};
Pump pumpThree = {true, "Carbo", 2, {}};
Pump pumpFour = {true, "PO4", 3, {}};
Pump pumps[] = {pumpOne, pumpTwo, pumpThree, pumpFour};


long timeFor100ml = 100 * 1000;

const int maxEvents = 60;
String events[maxEvents];

int isSummerTime = 1; //1 -> true, 0 ->false
float timeZone = 2;

String controllerName = "Aquarium Doser";
int softwareVersion = 11;

void handleActionRequest() {
  String response = "{\"response\":\"ko\"}";
  int responseCode = 200;
  String responseType = "application/json";
  if (server.hasArg("action")) {
    String action = server.arg("action");
    if (server.hasHeader("Authorization")) {
      String receivedToken = server.header("Authorization");
      receivedToken.trim();
      if (receivedToken.equals("Bearer " + localToken)) {
        if (server.hasArg("pin")) {
          int pin = server.arg("pin").toInt();

          if (action.equals("ON")) {
            digitalWrite(pin, LOW);
            isOn[pin] = true;
            lastTurnOn[pin] = millis();
            if (server.hasArg("duration")) {
              int duration = server.arg("duration").toInt();
              timeToTurnOff[pin] = millis() + duration * 1000;
              addEvent("Dosing " + String(duration) + " seconds for " + pumps[pin].name);
            } else if (server.hasArg("quantity")) {
              float quantity = server.arg("quantity").toFloat();
              int timeToStop = millis() + ((timeFor100ml * quantity) / 100);
              timeToTurnOff[pin] = timeToStop;
              addEvent("Dosing " + String(quantity) + " ml of " + pumps[pin].name);
            }
            response = "{\"response\":\"on\"}";
          }
          if (action.equals("OFF")) {
            digitalWrite(pin, HIGH);
            isOn[pin] = false;
            response = "{\"response\":\"off\"}";
            addEvent("Manual stop " + pumps[pin].name);
          }
          if (action.equals("status")) {
            if (digitalRead(pin) == LOW) {
              response = "{\"response\":\"on\"}";
            } else {
              response = "{\"response\":\"off\"}";
            }
          }

        }
        if (action.equals("events")) {
          int offset = (server.hasArg("offset") ? (server.arg("offset").toInt() == 0 ? 0 : server.arg("offset").toInt()) : 0);
          int numberOfEvents = (server.hasArg("numberOfEvents") ? server.arg("numberOfEvents").toInt() : 10);
          response = getEvents(offset, numberOfEvents);
        }

        if (action.equals("doses")) {
          response = getAllDoses();
        }

        if (action.equals("pumps")) {
          response = getPumps();
        }

        if (action.equals("getGeneralSettings")) {
          response = getGeneralSettings();
        }

        if (action.equals("updatePumps")) {
          response = updatePumps();
        }

        if (action.equals("updateDoses")) {
          response = updateDoses();
        }

        if (action.equals("updateGeneral")) {
          response = updateGeneral();
        }

        if (action.equals("restart")) {
          ESP.restart();
        }
      } else {
        response = "";
        responseCode = 401;
      }
    } else {
      responseCode = 401;
    }


    if (action.equals("validate")) {
      response = "false";
      responseCode = 401;
      responseType = "plain/text";
      if (server.hasHeader("Authorization")) {
        String receivedToken = server.header("Authorization");
        receivedToken.trim();
        if (receivedToken.equals("Bearer " + localToken)) {
          response = "true";
          responseCode = 200;
        }
      }
    }

    if (action.equals("login")) {
      response = "";
      responseCode = 401;
      responseType = "plain/text";
      if (server.hasArg("user") && server.hasArg("pass")) {
        String receivedUser = server.arg("user");
        String receivedPass = server.arg("pass");
        if (receivedUser.equals(userName) && receivedPass.equals(userPass)) {
          response = localToken;
          responseCode = 200;
          //addEvent("Succes login, user " + receivedUser + " !");
        } else {
          addEvent("Fail login attempt, user " + receivedUser + " !");
        }
      }
    }

    if (action.equals("updateNetwork") && ssid.equals("config") && password.equals("12345678")) {
      response = updateNetwork();
    }
  }

  server.send(response.equals("{\"response\":\"ko\"}") ? 404 : responseCode, responseType , response);
}

//json responses
String getEvents(int offset, int numberOfEvents) {
  String response = "[";
  boolean isFirst = true;
  if (offset < maxEvents)
  {
    for (byte i = offset; i < maxEvents; i ++) {
      if (numberOfEvents == 0 ) {
        break;
      }
      if (events[i].length() > 0) {
        if (!isFirst) {
          response += ",";
        }
        numberOfEvents --;
        response += (events[i]);
        isFirst = false;
      }
    }
  }
  response += "]";
  return response;
}

String getAllDoses() {
  boolean isFirst = true;
  String response = "[";
  for (byte i = 0; i < allDosesSize; i ++) {
    if (allDoses[i].quantityMl == 0) {
      continue;
    }

    if (!isFirst) {
      response += ",";
    }
    String item = "{\"id\":" + String(allDoses[i].id)
                  + ",\"name\":\"" + allDoses[i].name
                  + "\",\"timeOfDosing\":\"" + jsonTime(allDoses[i].timeOfDosing)
                  + "\",\"quantityMl\":" + String(allDoses[i].quantityMl)
                  + "}";
    response += (item);
    isFirst = false;
  }
  response += "]";
  return response;
}

String jsonTime(int time) {
  if (time == 0)
  {
    return "00:00";
  }
  float timeWhitoutSeconds = time / 100;
  float fTime = timeWhitoutSeconds / 100;
  String strTime = String(fTime);
  strTime.replace(".", ":");
  return strTime;
}


String getPumps() {
  boolean isFirst = true;
  String response = "[";
  for (byte i = 0; i < 4; i ++) {
    if (!isFirst) {
      response += ",";
    }
    String strDoses = "[";
    boolean isFirstDose = true;
    for (byte j = 0; j < 20; j ++) {
      AssignedDose assignedDose = pumps[i].assignedDoses[j];
      if (assignedDose.dose.id == 0)
      {
        continue;
      }

      if (!isFirstDose) {
        strDoses += ",";
      }

      String daysUsage = "";
      boolean firstDayUsage = true;
      for (byte k = 0; k < 7; k ++)
      {
        if (!firstDayUsage)
        {
          daysUsage += ",";
        }
        daysUsage += String(assignedDose.days[k] == true ? "true" : "false");
        firstDayUsage = false;
      }
      String eachDose = "{\"used\":" + String((assignedDose.isUsed == true ? "true" : "false"))
                        + ",\"doseId\":" + String(assignedDose.dose.id)
                        + ",\"days\":[" + daysUsage + "]}";


      strDoses += eachDose;
      isFirstDose = false;
    }
    strDoses += "]";


    String pump = "{\"used\":" + String((pumps[i].used == true ?  "true" : "false"))
                  + ",\"name\":\"" + pumps[i].name
                  + "\",\"pin\":" + String(pumps[i].pin)
                  + ",\"doses\":" + strDoses
                  + "}";
    isFirst = false;
    response += pump;

  }
  response += "]";
  return response;
}

String getGeneralSettings() {
  String response = "{\"controllerName\":\"" + controllerName;
  response += "\",\"isSummerTime\":";
  response += (isSummerTime == 1 ?  "true" : "false");
  response += (",\"selectedTimeZone\":" + String(timeZone));
  response += (",\"timeFor100ml\":" + String(timeFor100ml / 1000));
  response += (",\"serialNo\":" + String(ESP.getChipId()));
  response += (",\"version\":" + String(softwareVersion));
  response += "}";
  return response;
}

//end responses

void handleWifi() {
  //Static IP address configuration
  loadWifiSettings();
  IPAddress gateway(myGateway[0], myGateway[1], myGateway[2], myGateway[3]);//IP Address of your WiFi Router (Gateway)
  IPAddress subnet(mySubnet[0], mySubnet[1], mySubnet[2], mySubnet[3]);//Subnet mask
  IPAddress dns(myDns[0], myDns[1], myDns[2], myDns[3]);//DNS
  IPAddress staticIP(myStaticIp[0], myStaticIp[1], myStaticIp[2], myStaticIp[3]); //ESP static ip

  WiFi.disconnect();
  delay(1000);
  WiFi.hostname(deviceName);      // DHCP Hostname (useful for finding device for static lease)
  WiFi.mode(WIFI_STA); //WiFi mode station (connect to wifi router only)
  if (!(ssid.equals("config") && password.equals("12345678"))) {
    WiFi.config(staticIP, dns, gateway, subnet);
  }

  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.begin(ssid, password);

  int maxWaitTime = 30000;
  while ( maxWaitTime > 0 && WiFi.status() != WL_CONNECTED) {
    maxWaitTime = maxWaitTime - 500;
    delay(500);
  }
}

void addEvent(String event) {
  //shift events to free the first position
  for (byte i = maxEvents - 1; i > 0; i = i - 1) {
    events[i] = events[i - 1];
  }
  String entry = "{\"time\":\"" + getStringDayOfWeek(getDayOfWeek()) + " " + String(timeClient.getFormattedTime()) + "\",\"event\":\"" + event + "\"}";
  events[0] = entry;
}

void handlePumps() {
  delay(1000);
  for (byte i = 0; i < 4; i = i + 1) {
    //turn off pump if time is elapsed
    if (isOn[i] && millis() > timeToTurnOff[pumps[i].pin]) {
      digitalWrite(pumps[i].pin, HIGH);
      isOn[i] = false;
    }
  }
}

String getStringDayOfWeek(int day) {
  if (day == 0) {
    return "Monday";
  } else if (day == 1) {
    return "Tuesday";
  } else if (day == 2) {
    return "Wednesday";
  } else if (day == 3) {
    return "Thursday";
  } else if (day == 4) {
    return "Friday";
  } else if (day == 5) {
    return "Saturday";
  } else {
    return "Sunday";
  }
}

void handleDosing(int now) {
  for (byte i = 0; i < 4; i = i + 1) {
    Pump pump = pumps[i];
    if (pump.used) {
      int dayOfWeek = getDayOfWeek();
      //Day today = pump.days[dayOfWeek];
      for (byte j = 0; j < 20; j = j + 1) {
        AssignedDose aDose = pump.assignedDoses[j];
        if (aDose.isUsed && aDose.days[dayOfWeek]) {
          Dose thisDose = aDose.dose;
          if (thisDose.timeOfDosing > 0)
          {
            if (millis() - lastTurnOn[pump.pin] > 59000) {
              int diference = now - thisDose.timeOfDosing;
              if (diference > 0 && diference < 59) {
                digitalWrite(pump.pin, LOW);
                isOn[pump.pin] = true;
                lastTurnOn[pump.pin] = millis();
                int timeToStop = millis() + ((timeFor100ml * thisDose.quantityMl) / 100);
                timeToTurnOff[pump.pin] = timeToStop;
                addEvent("Dosing " + String(thisDose.quantityMl) + " ml of " + pump.name);
              }
            }
          }
        }
      }
    }
  }
}

int getDayOfWeek() {
  // Convert to number of days since 1 Jan 1970
  int days_since_epoch = timeClient.getEpochTime() / 86400;
  // 1 Jan 1970 was a Thursday, so add 4 so Sunday is day 0, and mod 7
  int day_of_week = (days_since_epoch + 3) % 7;
  return day_of_week;
}

int getCurrentTime() {
  while (!timeClient.update()) {
    timeClient.forceUpdate();
  }
  formattedTime = timeClient.getFormattedTime();
  formattedTime.replace(":", "");
  return formattedTime.toInt();
}

void renewApiToken() {
  if (millis() - lastTokenUpdate > 86400000) {
    lastTokenUpdate = millis();
    localToken = String(ESP.getChipId()) + String(ESP.getFreeHeap() * ESP.random() + millis());
  }
}

//persitance
String getMyString(String config, String key) {
  String temp = config.substring(config.indexOf(key));
  return temp.substring(temp.indexOf(":") + 1, temp.indexOf(";"));
}

float getFloat(String config, String key) {
  return getMyString(config, key).toFloat();
}

int getInt(String config, String key) {
  return getMyString(config, key).toInt();
}

boolean getBoolean(String config, String key) {
  return getInt(config, key) == 1;
}
//doses
void loadDoses() {
  File file = SPIFFS.open("/doses.config", "r");
  String conf = "";
  if (file) {
    while (file.available()) {
      conf = file.readString();
    }
    file.close();
  } else {
    return;
  }
  if (conf.length() > 0) {
    for (byte i = 0; i < allDosesSize; i ++) {
      int id = getInt(conf, String(i) + "_id");
      if (id > 0) {
        allDoses[i].id = getInt(conf, String(i) + "_id");
        allDoses[i].name = getMyString(conf, String(i) + "_name");
        allDoses[i].timeOfDosing = getInt(conf, String(i) + "_timeOfDosing");
        allDoses[i].quantityMl = getFloat(conf, String(i) + "_quantityMl");
      }
    }
  }
}

  String updateDoses() {
    String dosesToSave = "";
    for (byte i = 0; i < allDosesSize; i ++) {
      String idKey = String(i) + "_id";
      if (server.hasArg(idKey)) {
        String idValue = server.arg(idKey);
        dosesToSave += addParam(idKey, idValue);

        String nameKey = String(i) + + "_name";
        dosesToSave += addParam(nameKey, server.arg(nameKey));

        String timeOfDosingKey = String(i) + "_timeOfDosing";
        String timeValue = server.arg(timeOfDosingKey);
        timeValue.replace(":", "");
        timeValue = timeValue + "00";
        dosesToSave += addParam(timeOfDosingKey, timeValue);

        String quantityMlKey = String(i) + "_quantityMl";
        dosesToSave += addParam(quantityMlKey, server.arg(quantityMlKey));
      }
    }

    writeToFile("/doses.config", dosesToSave, false);

    addEvent("Done writing doses updates!");
    return "{\"status\":\"ok\"}";
  }


  void loadPumps() {
    File file = SPIFFS.open("/pumps.config", "r");
    String conf = "";
    if (file) {
      while (file.available()) {
        conf = file.readString();
      }
      file.close();
    } else {
      return;
    }
    if (conf.length() > 0) {
      for (byte i = 0; i < 4; i ++) { //there are only 4 pumps
        pumps[i].pin = i;
        pumps[i].name = getMyString(conf, String(pumps[i].pin) + "_name");
        pumps[i].used = getBoolean(conf, String(pumps[i].pin) + "_used");
        for (byte j = 0; j < 20; j ++) {
          AssignedDose aDose = {};
          aDose.isUsed = getBoolean(conf, String(pumps[i].pin) + "_doses_" + String(j) + "_used");
          int doseId = getInt(conf, String(pumps[i].pin) + "_doses_" + String(j) + "_id");
          Dose thisDose;
          for (byte k = 0; k < allDosesSize; k ++) {
            if (allDoses[k].id == doseId) {
              thisDose = allDoses[k];
              break;
            }
          }

          aDose.dose = thisDose;

          for (byte d = 0; d < 7; d ++) {
            aDose.days[d] = getBoolean(conf, String(pumps[i].pin) + "_doses_" + String(j) + "_day_" + String(d));
          }
          pumps[i].assignedDoses[j] = aDose;
        }
      }
    }
  }

  String updatePumps() {
    String pumpsToSave = "";
    for (byte i = 0; i < 4; i ++) { //the size of the structure
      String nameKey = String(pumps[i].pin) + "_name";
      pumpsToSave += addParam(nameKey, server.arg(nameKey));

      String usedKey = String(pumps[i].pin) + "_used";
      pumpsToSave += addParam(usedKey, String(server.arg(usedKey).equals("true")));
      for (byte j = 0; j < 20; j ++) {
        String doseIdKey = String(pumps[i].pin) + "_doses_" + String(j) + "_id";
        String doseUsedKey =  String(pumps[i].pin) + "_doses_" + String(j) + "_used";
        String doseDayKey = String(pumps[i].pin) + "_doses_" + String(j) + "_day_";
        if (server.hasArg(doseUsedKey)) {
          pumpsToSave += addParam(doseUsedKey, String(server.arg(doseUsedKey).equals("true")));
          pumpsToSave += addParam(doseIdKey, server.arg(doseIdKey));
          for (byte d = 0; d < 7; d ++) {
            pumpsToSave += addParam(doseDayKey + String(d), String(server.arg(doseDayKey + String(d)).equals("true")));
          }
        }
      }
    }

    writeToFile("/pumps.config", pumpsToSave, false);

    addEvent("Done writing pumps updates!");
    return "{\"status\":\"ok\"}";
  }

  void writeToFile(String fileName, String toSave, boolean append) {
    File file = SPIFFS.open(fileName, (append ? "a" : "w"));
    if (file) {
      file.print(toSave);
      file.close();
    }
  }

  String addParam(String key, String value) {
    return key + ":" + value + ";";
  }

  void loadWifiSettings() {
    File file = SPIFFS.open("/network.config", "r");
    String conf = "";
    if (file) {
      while (file.available()) {
        conf = file.readString();
      }
      file.close();
    } else {
      return;
    }

    if (conf.length() > 0) {
      String confIp = getMyString(conf, "ip");
      firstDot = confIp.indexOf(".") + 1;
      secondDot = confIp.indexOf(".", firstDot) + 1;
      thirdDot = confIp.indexOf(".", secondDot) + 1;
      myStaticIp[0] = confIp.substring(0, firstDot).toInt();
      myStaticIp[1] = confIp.substring(firstDot, secondDot).toInt();
      myStaticIp[2] = confIp.substring(secondDot, thirdDot).toInt();
      myStaticIp[3] = confIp.substring(thirdDot).toInt();

      String confGateway = getMyString(conf, "gateway");
      firstDot = confGateway.indexOf(".") + 1;
      secondDot = confGateway.indexOf(".", firstDot) + 1;
      thirdDot = confGateway.indexOf(".", secondDot) + 1;
      myGateway[0] = confGateway.substring(0, firstDot).toInt();
      myGateway[1] = confGateway.substring(firstDot, secondDot).toInt();
      myGateway[2] = confGateway.substring(secondDot, thirdDot).toInt();
      myGateway[3] = confGateway.substring(thirdDot).toInt();

      String confSubnet = getMyString(conf, "subnet");
      firstDot = confSubnet.indexOf(".") + 1;
      secondDot = confSubnet.indexOf(".", firstDot) + 1;
      thirdDot = confSubnet.indexOf(".", secondDot) + 1;

      String confDns = getMyString(conf, "dns");
      firstDot = confDns.indexOf(".") + 1;
      secondDot = confDns.indexOf(".", firstDot) + 1;
      thirdDot = confDns.indexOf(".", secondDot ) + 1;
      myDns[0] = confDns.substring(0, firstDot).toInt();
      myDns[1] = confDns.substring(firstDot, secondDot).toInt();
      myDns[2] = confDns.substring(secondDot, thirdDot).toInt();
      myDns[3] = confDns.substring(thirdDot).toInt();

      ssid = getMyString(conf, "ssid");
      password = getMyString(conf, "pass");
      userName = getMyString(conf, "userName");
      userPass = getMyString(conf, "userPass");
      useUserPass = getBoolean(conf, "useUserPass");
    }
  }

  String updateNetwork() {
    String toSave = "";
    toSave = toSave + addParam("ssid", server.arg("ssid"));
    toSave = toSave + addParam("pass", server.arg("pass"));
    toSave = toSave + addParam("ip", server.arg("ip"));
    toSave = toSave + addParam("dns", server.arg("dns"));
    toSave = toSave + addParam("subnet", server.arg("subnet"));
    toSave = toSave + addParam("gateway", server.arg("gateway"));
    toSave = toSave + addParam("useUserPass", String(server.arg("useUserPass")));
    toSave = toSave + addParam("userPass", server.arg("userPass"));
    toSave = toSave + addParam("userName", server.arg("user"));
    saveNetworkSettings(toSave);
    return "{\"status\":\"ok\"}";
  }

  void saveNetworkSettings(String toSave) {
    File file = SPIFFS.open("/network.config", "w");
    if (file) {
      file.print(toSave);
      file.close();
      addEvent("Done updating network settings");
    } else {
      addEvent("Error updating network settings");
    }
    ESP.restart();
  }

  void loadGeneral() {
    File file = SPIFFS.open("/options.config", "r");
    String conf = "";
    if (file) {
      while (file.available()) {
        conf = file.readString();
      }
      file.close();
    } else {
      return;
    }

    if (conf.length() > 0) {
      //general
      isSummerTime = getBoolean(conf, "isSummerTime");
      timeZone = getInt(conf, "timeZone");
      controllerName = getMyString(conf, "controllerName");
      timeFor100ml = getInt(conf, "timeFor100ml") * 1000;
    }
  }

  String updateGeneral() {
    //general
    String generalToSave = addParam("controllerName", server.arg("controllerName"));
    generalToSave += addParam("isSummerTime", String(server.arg("isSummerTime").equals("true")));
    generalToSave += addParam("timeZone", server.arg("selectedTimeZone"));
    generalToSave += addParam("timeFor100ml", server.arg("timeFor100ml"));

    writeToFile("/options.config", generalToSave, false);
    addEvent("Done writing general updates!");
    return "{\"status\":\"ok\"}";
  }
  //end of peristance

  void setup(void) {
    Dose macro = {2, "MacroDaily", 90000, MACRO};
    Dose micro = {3, "MicroDaily", 130000, MICRO};
    Dose carbo = {4, "CarboDaily", 140000, CARBO};

    allDoses[0] = macro;
    allDoses[1] = micro;
    allDoses[2] = carbo;

    pumps[0].assignedDoses[0] = {true, macro, {true, true, true, true, true, true, true}};
    pumps[1].assignedDoses[0] = {true, micro, {true, true, true, true, true, true, true}};
    pumps[2].assignedDoses[0] = {true, carbo, {true, true, true, true, true, true, true}};
    pumps[3].assignedDoses[0] = {false, macro, {false, false, false, false, false, false, false}};

    SPIFFS.begin();

    loadDoses();
    loadPumps();
    loadGeneral();
    
    pinMode(1, FUNCTION_3);
    pinMode(1, OUTPUT);
    digitalWrite(1, HIGH);

    pinMode(3, FUNCTION_3);
    pinMode(3, OUTPUT);
    digitalWrite(3, HIGH);

    pinMode(0, OUTPUT);
    digitalWrite(0, HIGH);

    pinMode(2, OUTPUT);
    digitalWrite(2, HIGH);

    handleWifi();

    MDNS.begin(host);

    if (!(ssid.equals("config") && password.equals("12345678"))) {
      server.serveStatic("/", SPIFFS, "/index.html");
    } else {
      server.serveStatic("/", SPIFFS, "/settings.html");
    }

   //httpUpdater.setup(&server);
    httpUpdater.setup(&server, "/update", userName, userPass);
 
    server.on("/action", handleActionRequest);
    server.begin();
    MDNS.addService("http", "tcp", 80);

    if (!(ssid.equals("config") && password.equals("12345678"))) {
      timeClient.begin();
      int offset = (isSummerTime + timeZone) * 3600;
      timeClient.setTimeOffset(offset);
      timeClient.forceUpdate();
    }
    addEvent("Done starting");
  }

  void loop(void) {
    MDNS.update();
    server.handleClient();
    if (WiFi.status() != WL_CONNECTED) {
      handleWifi();
    }
    if (!(ssid.equals("config") && password.equals("12345678"))) {
      handlePumps();
      handleDosing(getCurrentTime());
      renewApiToken();
    }
  }
