#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <WiFiClient.h>
#include <EEPROM.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <ThingSpeak.h>

/* wifi setup by 9SQ https://github.com/9SQ/esp8266-wifi-setup */

WiFiClient  client;

const IPAddress apIP(192, 168, 1, 1);
const char* apSSID = "ESP8266_SETUP";
boolean settingMode;
String ssidList;

DNSServer dnsServer;
AsyncWebServer  webServer(80);

unsigned long myChannelNumber; // Thingspeak channel number
const char * myWriteAPIKey; // Write key for thingspeak channel

#define DHTPIN     2         // Pin connected to the DHT sensor.
#define DHTTYPE    DHT11     // DHT 11
DHT_Unified dht(DHTPIN, DHTTYPE);

float avgt = 0; // Average temperature
float avgh = 0; // Average humidity

int readings = 0;
#define readingsPerAvg 16
#define measureDelay 20000
int last = millis();

String urlDecode(String input) {
    String s = input;
    s.replace("%20", " ");
    s.replace("+", " ");
    s.replace("%21", "!");
    s.replace("%22", "\"");
    s.replace("%23", "#");
    s.replace("%24", "$");
    s.replace("%25", "%");
    s.replace("%26", "&");
    s.replace("%27", "\'");
    s.replace("%28", "(");
    s.replace("%29", ")");
    s.replace("%30", "*");
    s.replace("%31", "+");
    s.replace("%2C", ",");
    s.replace("%2E", ".");
    s.replace("%2F", "/");
    s.replace("%2C", ",");
    s.replace("%3A", ":");
    s.replace("%3A", ";");
    s.replace("%3C", "<");
    s.replace("%3D", "=");
    s.replace("%3E", ">");
    s.replace("%3F", "?");
    s.replace("%40", "@");
    s.replace("%5B", "[");
    s.replace("%5C", "\\");
    s.replace("%5D", "]");
    s.replace("%5E", "^");
    s.replace("%5F", "-");
    s.replace("%60", "`");
    return s;
}

void logData() {
    avgt = avgt / readingsPerAvg;
    avgh = avgh / readingsPerAvg;
    Serial.print("Temperature: ");
    Serial.print(avgt);
    Serial.print("  |  Humidity: ");
    Serial.println(avgh);

    ThingSpeak.setField(1, avgt);
    ThingSpeak.setField(2, avgh);
    ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
}

boolean restoreConfig() {
    Serial.println("Reading EEPROM...");
    String ssid = "";
    String pass = "";
    if (EEPROM.read(0) != 0) {
        for (int i = 0; i < 32; ++i) {
            ssid += char(EEPROM.read(i));
        }
        Serial.print("SSID: ");
        Serial.println(ssid);
        for (int i = 32; i < 96; ++i) {
            pass += char(EEPROM.read(i));
        }
        Serial.print("Password: ");
        Serial.println(pass);
        WiFi.begin(ssid.c_str(), pass.c_str());
        return true;
    }
    else {
        Serial.println("Config not found.");
        return false;
    }
}

boolean checkConnection() {
    int count = 0;
    Serial.print("Waiting for Wi-Fi connection");
    while (count < 30) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println();
            Serial.println("Connected!");
            return (true);
        }
        delay(500);
        Serial.print(".");
        count++;
    }
    Serial.println("Timed out.");
    return false;
}

String makePage(String title, String contents) {
    String s = "<!DOCTYPE html><html><head>";
    s += "<meta name=\"viewport\" content=\"width=device-width,user-scalable=0\">";
    s += "<title>";
    s += title;
    s += "</title></head><body>";
    s += contents;
    s += "</body></html>";
    return s;
}

void startWebServer() {
    if (settingMode) {
        Serial.print("Starting Web Server at ");
        Serial.println(WiFi.softAPIP());
        webServer.on("/settings", [](AsyncWebServerRequest* request) {
            String s = "<h1>Wi-Fi Settings</h1><p>Please enter your password by selecting the SSID.</p>";
            s += "<form method=\"get\" action=\"setap\"><label>SSID: </label><select name=\"ssid\">";
            s += ssidList;
            s += "</select><br>Password: <input name=\"pass\" length=64 type=\"password\"><input type=\"submit\"></form>";
            request->send(200, "text/html", makePage("Wi-Fi Settings", s));
        });
        webServer.on("/setap", [](AsyncWebServerRequest* request) {
            for (int i = 0; i < 96; ++i) {
                EEPROM.write(i, 0);
            }
            String ssid = urlDecode(request->getParam("ssid")->value().c_str());
            Serial.print("SSID: ");
            Serial.println(ssid);
            String pass = urlDecode(request->getParam("pass")->value().c_str());
            Serial.print("Password: ");
            Serial.println(pass);
            Serial.println("Writing SSID to EEPROM...");
            for (int i = 0; i < ssid.length(); ++i) {
                EEPROM.write(i, ssid[i]);
            }
            Serial.println("Writing Password to EEPROM...");
            for (int i = 0; i < pass.length(); ++i) {
                EEPROM.write(32 + i, pass[i]);
            }
            EEPROM.commit();
            Serial.println("Write EEPROM done!");
            String s = "<h1>Setup complete.</h1><p>device will be connected to \"";
            s += ssid;
            s += "\" after the restart.";
            request->send(200, "text/html", makePage("Wi-Fi Settings", s));
            ESP.restart();
        });
        webServer.onNotFound([](AsyncWebServerRequest* request) {
            String s = "<h1>AP mode</h1><p><a href=\"/settings\">Wi-Fi Settings</a></p>";
            request->send(200, "text/html", makePage("AP mode", s));
        });
    }
    else {
        Serial.print("Starting Web Server at ");
        Serial.println(WiFi.localIP());
        webServer.on("/", [](AsyncWebServerRequest* request) {
            String s = "<h1>STA mode</h1><p><a href=\"/reset\">Reset Wi-Fi Settings</a></p>";
            request->send(200, "text/html", makePage("STA mode", s));
        });
        webServer.on("/reset", [](AsyncWebServerRequest* request) {
            for (int i = 0; i < 96; ++i) {
                EEPROM.write(i, 0);
            }
            EEPROM.commit();
            String s = "<h1>Wi-Fi settings was reset.</h1><p>Please reset device.</p>";
            request->send(200, "text/html", makePage("Reset Wi-Fi Settings", s));
        });
    }
    AsyncElegantOTA.begin(&webServer);
    webServer.begin();
}

void setupMode() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    int n = WiFi.scanNetworks();
    delay(100);
    Serial.println("");
    for (int i = 0; i < n; ++i) {
        ssidList += "<option value=\"";
        ssidList += WiFi.SSID(i);
        ssidList += "\">";
        ssidList += WiFi.SSID(i);
        ssidList += "</option>";
    }
    delay(100);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(apSSID);
    dnsServer.start(53, "*", apIP);
    startWebServer();
    Serial.print("Starting Access Point at \"");
    Serial.print(apSSID);
    Serial.println("\"");
}

void setup() {
    Serial.begin(115200);
    EEPROM.begin(512);
    delay(10);
    if (restoreConfig()) {
        if (checkConnection()) {
            settingMode = false;
            startWebServer();
            return;
        }
    }
    else
    {
        settingMode = true;
        setupMode();
    }

    dht.begin();
    sensor_t sensor;
}

void loop() {
    if (settingMode) {
        dnsServer.processNextRequest();
    }

    if (readings <= readingsPerAvg && millis() - last > measureDelay && WiFi.status() == WL_CONNECTED) {
        sensors_event_t event;
        dht.temperature().getEvent(&event);
        avgt = avgt + event.temperature;
        dht.humidity().getEvent(&event);
        avgh = avgh + event.relative_humidity;
        readings++;
    }
    else if (readings >= readingsPerAvg)
    {
        logData();
    }
}