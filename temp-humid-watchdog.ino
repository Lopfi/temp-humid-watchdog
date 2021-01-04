#include <ESP8266WiFi.h>
#include "ThingSpeak.h"
// Network SSID
const char* ssid = "wifi-ssid";
const char* password = "wifi-password";

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#define DHTPIN     2         // Pin connected to the DHT sensor.
#define DHTTYPE    DHT11     // DHT 11
DHT_Unified dht(DHTPIN, DHTTYPE);

unsigned long myChannelNumber = ; //thingspeak channel number
const char * myWriteAPIKey = ""; //write key for thingspeak channel
float avgt = 0;
float avgh = 0;
WiFiClient  client;

void setup() {
  Serial.begin(115200);
  delay(10);
  // Connect WiFi
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.hostname("watchdog");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  // Print the IP address
  Serial.print("IP address: ");
  Serial.print(WiFi.localIP());
  ThingSpeak.begin(client);

  dht.begin();
  Serial.println(F("DHTxx Unified Sensor Example"));
  sensor_t sensor;
}

void loop() {
  for (int x = 0; x < 15; x++) {
    sensors_event_t event;
    dht.temperature().getEvent(&event);
    avgt = avgt + event.temperature;
    dht.humidity().getEvent(&event);
    avgh = avgh + event.relative_humidity;
    delay(20000);
  }
  avgt = avgt / 16;
  avgh = avgh / 16;
  Serial.print("Temperature: ");
  Serial.print(avgt);
  Serial.print("  |  Humidity: ");
  Serial.println(avgh);

  ThingSpeak.setField(1, avgt);
  ThingSpeak.setField(2, avgh);
  ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
}
