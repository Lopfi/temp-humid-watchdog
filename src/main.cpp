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
#include <LittleFS.h>

/* wifi setup by 9SQ https://github.com/9SQ/esp8266-wifi-setup */

WiFiClient  client;

const IPAddress apIP(192, 168, 1, 1); // IP-Address of the accespoint when in setup mode
const char* apSSID = "temp-humid-watchdog"; // Name of the Wifi when in setup mode
boolean settingMode;
String ssidList; // List of the ssids of all available networks

DNSServer dnsServer;
AsyncWebServer  webServer(80); // Asynchronous webserver on port 80

unsigned long channelID; // Thingspeak channel id
char *APIKey; // Write key for thingspeak channel
int tempField = 1; // thingspeak temperature field
int humidField = 2; // thingspeak humidity field

#define DHTPIN     2         // Pin connected to the DHT sensor.
#define DHTTYPE    DHT11     // DHT 11
DHT_Unified dht(DHTPIN, DHTTYPE); // create new dht object

float avgt = 0; // Average temperature
float avgh = 0; // Average humidity

int readings = 0; // Count of how many readings have been taken
#define readingsPerAvg 16 // How many readings to take per average 
#define measureDelay 20000 // Delay between each reading
int last = millis(); // Time since last reading

// Decode special signs from Strings
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

char *append(const char *s, char c) {
    int len = strlen(s);
    char buf[len+2];
    strcpy(buf, s);
    buf[len] = c;
    buf[len + 1] = 0;
    return strdup(buf);
}

// read all settings from eeprom
boolean restoreConfig() {
    Serial.println("Reading EEPROM...");
    String ssid = "";
    String pass = "";
    String chanID = "";
    char* apikey = "";

    if (EEPROM.read(0) != 0) {
        // Wifi SSID
        for (int i = 0; i < 32; ++i) {
            ssid += char(EEPROM.read(i));
        }
        Serial.print("SSID: ");
        Serial.println(ssid);
        // Wifi password
        for (int i = 32; i < 96; ++i) {
            pass += char(EEPROM.read(i));
        }
        
        Serial.print("Password: ");
        Serial.println(pass);
        WiFi.begin(ssid.c_str(), pass.c_str());

        // Thingspeak channel ID
        for (int i = 96; i < 103; ++i) {
            chanID += char(EEPROM.read(i));
        }
        channelID = chanID.toInt();
        Serial.print("Channel ID: ");
        Serial.println(chanID);
        // Thingspeak API key
        for (int i = 103; i < 119; ++i) {
            apikey = append(apikey, char(EEPROM.read(i)));
        }
        //apikey.toCharArray(APIKey, apikey.length());
        APIKey = apikey; //strdup(apikey);
        Serial.print("API key: ");
        Serial.println(apikey);
               
        return true;
    }
    else {
        Serial.println("Config not found.");
        return false;
    }
}

// check if connected to wifi
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

// start up the  webserver with eather settings or index page
void startWebServer() {
    if (settingMode) {
        Serial.print("Starting Web Server at ");
        Serial.println(WiFi.softAPIP());

        webServer.on("/", [](AsyncWebServerRequest* request) {
            request->send(LittleFS, "/settings.html", "text/html");
        });

        webServer.on("/setall", [](AsyncWebServerRequest* request) {
            for (int i = 0; i < 96; ++i) {
                EEPROM.write(i, 0);
            }
            String ssid = urlDecode(request->arg("ssid"));
            Serial.print("SSID: ");
            Serial.println(ssid);
            String pass = urlDecode(request->arg("pass"));
            Serial.print("Password: ");
            Serial.println(pass);
            String chanid = urlDecode(request->arg("chanid"));
            Serial.print("Channel ID: ");
            Serial.println(chanid);
            String apikey = urlDecode(request->arg("apikey"));
            Serial.print("API key: ");
            Serial.println(apikey);

            Serial.println("Writing to EEPROM...");
            for (int i = 0; i < ssid.length(); ++i) {
                EEPROM.write(i, ssid[i]);
            }
            for (int i = 0; i < pass.length(); ++i) {
                EEPROM.write(32 + i, pass[i]);
            }
            for (int i = 0; i < chanid.length(); ++i) {
                EEPROM.write(96 + i, chanid[i]);
            }
            for (int i = 0; i < apikey.length(); ++i) {
                EEPROM.write(103 + i, apikey[i]);
            }
            EEPROM.commit();
            Serial.println("Write EEPROM done!");
            ESP.restart();
    });
    
    }
    else {
        Serial.print("Starting Web Server at ");
        Serial.println(WiFi.localIP());

        webServer.on("/", [](AsyncWebServerRequest* request) {
            request->send(LittleFS, "/index.html", "text/html");
        });
        webServer.on("/reset", [](AsyncWebServerRequest* request) {
            for (int i = 0; i < EEPROM.length(); ++i) {
                EEPROM.write(i, 0);
            }
            EEPROM.commit();
            String s = "Settings have been restored. Please reset device.";
            request->send(200, "text/plain", s);
        });
    }
    webServer.begin();
    AsyncElegantOTA.begin(&webServer);
    
}

// set mode to setupMode
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
    // Open dir folder
    Dir dir = LittleFS.openDir("/");
    // Cycle all the content
    while (dir.next()) {
        // get filename
        Serial.print(dir.fileName());
        Serial.print(" - ");
        // If element have a size display It else write 0
        if(dir.fileSize()) {
            File f = dir.openFile("r");
            Serial.println(f.size());
            f.close();
        }else{
            Serial.println("0");
        }
    }
}

void setup() {
    Serial.begin(115200); // Beginn serial communication on 115200 baud
    EEPROM.begin(512); // Initialize a 512 byte eeprom space
    delay(10); // Wait for MCU to process
    
    dht.begin(); // start the dht sensor

    ThingSpeak.begin(client);  // Initialize ThingSpeak

    // Initialize FS
    Serial.println(F("Inizializing FS..."));
    if (LittleFS.begin()){
        Serial.println(F("done."));
    }else{
        Serial.println(F("fail."));
    }

    if (restoreConfig()) { // Check if old wireless configuration is available
        if (checkConnection()) { // Check if connencted to wifi
            settingMode = false;
            startWebServer(); // Start the webserver
            return;
        }
    }
    else
    {
        settingMode = true; // set mode to setting mode
        setupMode(); // execute setupmode
    }

    
}

void loop() {
    if (settingMode) {
        dnsServer.processNextRequest();
    }

    // if connected to wifi & the delay has passed as well as the max readings per average havent been reached
    if (readings <= readingsPerAvg && millis() > measureDelay + last && WiFi.status() == WL_CONNECTED) {
        last = millis();
        sensors_event_t event;
        dht.temperature().getEvent(&event); 
        avgt += event.temperature;
        dht.humidity().getEvent(&event);
        avgh += event.relative_humidity;
        readings++;
    }
    else if (readings >= readingsPerAvg)
    {
        // Calculate average and send them to serial as well as upload to thingspeak
        avgt = avgt / readingsPerAvg;
        avgh = avgh / readingsPerAvg;
        Serial.print("Temperature: ");
        Serial.print(avgt);
        Serial.print("  |  Humidity: ");
        Serial.println(avgh);

        ThingSpeak.setField(tempField, avgt);
        ThingSpeak.setField(humidField, avgh);
        int x = ThingSpeak.writeFields(channelID, APIKey);

        if(x == 200){
            Serial.println("Channel update successful.");
        }
        else{
            Serial.println("Problem updating channel. HTTP error code " + String(x));
        }
    }
}
