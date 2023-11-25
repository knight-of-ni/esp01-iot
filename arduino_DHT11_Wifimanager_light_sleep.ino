/* ESP01 IoT device for Genetec Security Center

   Date of last revision: 11/20/23
   Written by Andrew Bauer

  https://moteino.blogspot.com/p/esp01-iot-device-for-genetec-security.html

  This sketch has been modified to sleep, when not sending data to security center.
  This should help the temperature senser take more accurate readings.

   DHT based off of Arduino_DHT11 sketch
   written by Mike Barela with Adafruit Industries
   https://www.makerfabs.com/desfile/files/ESP01%2001S%20DHT11%20Arduino%20demo%20code.zip

   WiFiManager sample code by tzapu
   https://github.com/tzapu/WiFiManager/blob/master/examples/Parameters/SPIFFS/AutoConnectWithFSParameters/AutoConnectWithFSParameters.ino

*/
#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#ifdef ESP32
  #include <SPIFFS.h>
#endif
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
// Required for LIGHT_SLEEP_T delay mode
extern "C" {
#include "user_interface.h"
}
#include <DHT.h>
#define DHTTYPE DHT11
#define DHTPIN 2
#define INTERVAL 2000         // interval at which to read sensor
#define UPDATEINTERVAL 60000  // How often to send our data

#define SERIAL_BAUD     115200
//#define SERIAL_EN                //comment out if you don't want any serial output

#ifdef SERIAL_EN
  #define DEBUG(input)   {Serial.print(input); delay(1);}
  #define DEBUGln(input) {Serial.println(input); delay(1);}
#else
  #define DEBUG(input);
  #define DEBUGln(input);
#endif

char sc_server[40] = "192.168.8.1";
char sc_port[6] = "42000";
char DeviceID[6] = "1";

bool shouldSaveConfig = false; //flag for saving wifimanager data

// Yes, I know. All the cool kids prefer character strings instead of String objects
String Hstr="";
String Fstr="";
String sendBuff="";
byte percentQ = 0;
byte netFails = 0;
unsigned long DHTMillis = 0;

// Initialize DHT sensor 
// NOTE: For working with a faster than ATmega328p 16 MHz Arduino chip, like an ESP8266,
// you need to increase the threshold for cycle counts considered a 1 or 0.
// You can do this by passing a 3rd parameter for this threshold.  It's a bit
// of fiddling to find the right value, but in general the faster the CPU the
// higher the value.  The default for a 16mhz AVR is a value of 6.  For an
// Arduino Due that runs at 84mhz a value of 30 works.
// This is for the ESP8266 processor on ESP-01 
DHT dht(DHTPIN, DHTTYPE, 11); // 11 works fine for ESP8266

WiFiClient client;

//callback notifying us of the need to save config
void saveConfigCallback () {
  DEBUGln("Should save config");
  shouldSaveConfig = true;
}

void sendToSC() {
  if (client.connect(sc_server, atoi(sc_port))) {
    sendBuff = "ID="+String(DeviceID)+" F="+Fstr+" H="+Hstr+" S="+percentQ+";";
    client.println(sendBuff);

    if (client.connected()) {
      DEBUGln();
      DEBUGln("Temperature data sent to Security Center");
      DEBUGln(sendBuff);
      netFails = 0;
    } else {
      DEBUGln();
      DEBUGln("Timed out connecting to Security Center");
    }
  } else {
    DEBUGln();
    DEBUGln("Connection refused connecting to Security Center");
    netFails++;
  }
}

void WiFiStatus() {
    if (WiFi.RSSI() <= -100) {
      percentQ = 0;
    } else if (WiFi.RSSI() >= -50) {
      percentQ = 100;
    } else {
      percentQ = 2 * (WiFi.RSSI() + 100);
    }

    DEBUGln("");
    DEBUGln("WiFi Status");
    DEBUG("IP address: "); DEBUGln(WiFi.localIP());
    DEBUG("MAC address: "); DEBUGln(WiFi.macAddress());
    DEBUG("SSID: "); DEBUGln(WiFi.SSID());
    DEBUG("Signal Strength (%): "); DEBUGln(percentQ);
}

void gettemperature() {
  // Wait at least 2 seconds between measurements.
  // if the difference between the current time and last time you read
  // the sensor is bigger than the interval you set, read the sensor
  // Works better than delay for things happening elsewhere also
  float humidity, temp_f;
  char Hchar[10], Fchar[10];
  unsigned long currentMillis = millis();

  if(currentMillis - DHTMillis >= INTERVAL) {
    // save the last time you read the sensor 
    DHTMillis = currentMillis;   

    // Reading temperature for humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor)
    humidity = dht.readHumidity();          // Read humidity (percent)
    temp_f = dht.readTemperature(true);     // Read temperature as Fahrenheit
    // Check if any reads failed and exit early (to try again).
    if (isnan(humidity) || isnan(temp_f)) {
      Fstr="";
      Hstr="";
      DEBUGln("Failed to read from DHT sensor!");
    } else {
      dtostrf(temp_f, 3,2, Fchar);
      dtostrf(humidity, 3,2, Hchar);
      Fstr=String(Fchar);
      Hstr=String(Hchar);
    }
  }
}

void LightSleep(unsigned long millisec) {
  // With LIGHT_SLEEP enabled, the esp should sleep when delay is called
  wifi_set_sleep_type(LIGHT_SLEEP_T);
  delay(millisec);
}

void setup() {
#ifdef SERIAL_EN
  Serial.begin(SERIAL_BAUD);
#else // turn off the blue activity led when are aren't using serial
  pinMode(1, OUTPUT);
  digitalWrite(1, HIGH);
#endif

  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
  dht.begin();           // initialize temperature sensor
  gettemperature();

  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  DEBUGln("mounting FS...");

  if (SPIFFS.begin()) {
    DEBUGln("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      DEBUGln("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        DEBUGln("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);

#if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
        DynamicJsonDocument json(1024);
        auto deserializeError = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if ( ! deserializeError ) {
#else
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
#endif
          DEBUGln("\nparsed json");
          strcpy(sc_server, json["sc_server"]);
          strcpy(sc_port, json["sc_port"]);
          strcpy(DeviceID, json["DeviceID"]);
        } else {
          DEBUGln("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    DEBUGln("failed to mount FS");
  }
  //end read

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_sc_server("server", "security center server", sc_server, 40);
  WiFiManagerParameter custom_sc_port("port", "security center port", sc_port, 6);
  WiFiManagerParameter custom_DeviceID("deviceid", "Unique Device ID", DeviceID, 6);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager; 

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_sc_server);
  wifiManager.addParameter(&custom_sc_port);
  wifiManager.addParameter(&custom_DeviceID);

  //reset settings - for testing
  //wifiManager.resetSettings();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wifiManager.setConfigPortalTimeout(180);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("ESPAutoConnectAP")) {
    DEBUGln("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  DEBUGln("connected...yeey :)");

  //read updated parameters
  strcpy(sc_server, custom_sc_server.getValue());
  strcpy(sc_port, custom_sc_port.getValue());
  strcpy(DeviceID, custom_DeviceID.getValue());
  DEBUGln("The values in the file are: ");
  DEBUGln("\tsc_server : " + String(sc_server));
  DEBUGln("\tsc_port : " + String(sc_port));
  DEBUGln("\tDeviceID : " + String(DeviceID));

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    DEBUGln("saving config");
#if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
    DynamicJsonDocument json(1024);
#else
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
#endif
    json["sc_server"] = sc_server;
    json["sc_port"] = sc_port;
    json["DeviceID"] = DeviceID;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      DEBUGln("failed to open config file for writing");
    }

#if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
    serializeJson(json, Serial);
    serializeJson(json, configFile);
#else
    json.printTo(Serial);
    json.printTo(configFile);
#endif
    configFile.close();
  }
  //end save

  DEBUGln("");
  DEBUGln("DHT Weather Reading Server");
  WiFiStatus();
}
 
void loop() {
      gettemperature();
      WiFiStatus();

      if ( Fstr.length() && Hstr.length() ) { // Don't send if we don't have any data
        sendToSC();
        LightSleep(UPDATEINTERVAL); // light_sleep after sending data
      } else {
        DEBUGln("Temp and/or Humidty values are empty. Skip sending to Security Center...");
        LightSleep(INTERVAL); // light_sleep for 2 seconds then try to read from the dht again
      }
} 