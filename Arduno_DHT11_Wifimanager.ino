/* ESP01 IoT device for Genetec Security Center

   Date of last revision: 11/13/23
   Written by Andrew Bauer

  https://moteino.blogspot.com/p/esp01-iot-device-for-genetec-security.html

   DHT and portions of ESP8266Webserver based off of Arduino_DHT11 sketch
   written by Mike Barela with Adafruit Industries
   https://www.makerfabs.com/desfile/files/ESP01%2001S%20DHT11%20Arduino%20demo%20code.zip

   WiFiManager sample code by tzapu
   https://github.com/tzapu/WiFiManager/blob/master/examples/Parameters/SPIFFS/AutoConnectWithFSParameters/AutoConnectWithFSParameters.ino

*/
#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#ifdef ESP32
  #include <SPIFFS.h>
#endif
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <DHT.h>
#define DHTTYPE DHT11
#define GPIO0 0
#define GPIO2 2
#define INTERVAL 2000         // interval at which to read sensor
#define UPDATEINTERVAL 60000  // How often to send our data

char sc_server[40] = "192.168.8.1";
char sc_port[6] = "42000";
char DeviceID[6] = "1";

//flag for saving data
bool shouldSaveConfig = false;

// Yes, I know. All the cool kids prefer character strings instead of String objects
const String htmlHead = "<html>\r\n<head>\r\n<meta http-equiv='refresh' content='61'>\r\n</head>\r\n<body>\r\n";
const String htmlTail = "\r\n</body>\r\n</html>\r\n";
String errorMsg = "No Error Reported";
String successMsg = "No Success Reported";
String Hstr="";
String Fstr="";
String webString="";
String sendBuff="";
byte percentQ = 0;
byte netFails = 0;
unsigned long previousMillis = 0;
unsigned long DHTMillis = 0;

// Initialize DHT sensor 
// NOTE: For working with a faster than ATmega328p 16 MHz Arduino chip, like an ESP8266,
// you need to increase the threshold for cycle counts considered a 1 or 0.
// You can do this by passing a 3rd parameter for this threshold.  It's a bit
// of fiddling to find the right value, but in general the faster the CPU the
// higher the value.  The default for a 16mhz AVR is a value of 6.  For an
// Arduino Due that runs at 84mhz a value of 30 works.
// This is for the ESP8266 processor on ESP-01 
DHT dht(GPIO2, DHTTYPE, 11); // 11 works fine for ESP8266

WiFiClient client;
ESP8266WebServer server(80);

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void sendToSC() {
  if (client.connect(sc_server, atoi(sc_port))) {
    sendBuff = "ID="+String(DeviceID)+" F="+Fstr+" H="+Hstr+" S="+percentQ+";";
    client.println(sendBuff);

    if (client.connected()) {
      successMsg = "Temperature data sent to Security Center\r\n";
      successMsg += sendBuff;

      Serial.println();
      Serial.println(successMsg);
      Serial.println(sendBuff);
      netFails = 0;
    } else {
      errorMsg = "Timed out connecting to Security Center";
      Serial.println();
      Serial.println(errorMsg);
    }
  } else {
    errorMsg="Connection refused connecting to Security Center";
    Serial.println();
    Serial.println(errorMsg);
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

    Serial.println("");
    Serial.println("WiFi Status");
    Serial.print("IP address: "); Serial.println(WiFi.localIP());
    Serial.print("MAC address: "); Serial.println(WiFi.macAddress());
    Serial.print("SSID: "); Serial.println(WiFi.SSID());
    Serial.print("Signal Strength (%): "); Serial.println(percentQ);
}

void gettemperature() {
  // Wait at least 2 seconds between measurements.
  // if the difference between the current time and last time you read
  // the sensor is bigger than the interval you set, read the sensor
  // Works better than delay for things happening elsewhere also
  unsigned long currentMillis = millis();
  float humidity, temp_f;
  char Hchar[10], Fchar[10];

  if(currentMillis - DHTMillis >= INTERVAL) {
    // save the last time you read the sensor 
    DHTMillis = currentMillis;   

    // Reading temperature for humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor)
    humidity = dht.readHumidity();          // Read humidity (percent)
    temp_f = dht.readTemperature(true);     // Read temperature as Fahrenheit
    // Check if any reads failed and exit early (to try again).
    if (isnan(humidity) || isnan(temp_f)) {
      errorMsg = "Failed to read from DHT sensor!";
      Serial.println(errorMsg);
      return;
    } else {
      dtostrf(temp_f, 3,2, Fchar);
      dtostrf(humidity, 3,2, Hchar);
      Fstr=String(Fchar);
      Hstr=String(Hchar);
    }
  }
}

void handle_root() {
  String Msg = "Hello from the ESP8266!<br><br>This server responds to the following urls:<br>";
  Msg += "<a href=\"/gpio0\">/gpio0</a><br>";
  Msg += "<a href=\"/gpio2\">/gpio2</a><br>";
  Msg += "<a href=\"/error\">/error</a><br>";
  Msg += "<a href=\"/success\">/success</a><br>";
  Msg += "<a href=\"/status\">/status</a><br>";
  Msg += "<a href=\"/temp\">/temp</a><br>";
  Msg += "<a href=\"/humidity\">/humidity</a><br>";
  server.send(200, "text/html", htmlHead+Msg+htmlTail);
  delay(100);
}
 
void setup(void)
{
  // You can open the Arduino IDE Serial Monitor window to see what the code is doing
  Serial.begin(115200);  // Serial connection from ESP-01 via 3.3v console cable
  dht.begin();           // initialize temperature sensor

  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
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
          Serial.println("\nparsed json");
          strcpy(sc_server, json["sc_server"]);
          strcpy(sc_port, json["sc_port"]);
          strcpy(DeviceID, json["DeviceID"]);
        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
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
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(sc_server, custom_sc_server.getValue());
  strcpy(sc_port, custom_sc_port.getValue());
  strcpy(DeviceID, custom_DeviceID.getValue());
  Serial.println("The values in the file are: ");
  Serial.println("\tsc_server : " + String(sc_server));
  Serial.println("\tsc_port : " + String(sc_port));
  Serial.println("\tDeviceID : " + String(DeviceID));

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
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
      Serial.println("failed to open config file for writing");
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

  gettemperature();
  Serial.println("");
  Serial.println("DHT Weather Reading Server");
  WiFiStatus();
   
  server.on("/", handle_root);
  
  server.on("/temp", [](){  // if you add this subdirectory to your webserver call, you get text below :)
    gettemperature();       // read sensor
    webString="Temperature: "+Fstr+" F";   // Arduino has a hard time with float to string
    server.send(200, "text/plain", webString);            // send to someones browser when asked
  });

  server.on("/humidity", [](){  // if you add this subdirectory to your webserver call, you get text below :)
    gettemperature();           // read sensor
    webString="Humidity: "+Hstr+"%";
    server.send(200, "text/plain", webString);               // send to someones browser when asked
  });

    server.on("/gpio0", [](){  // url to display the current value of GPIO0
    server.send(200, "text/html", htmlHead+"Current value of GPIO0: "+String(digitalRead(GPIO0))+htmlTail);
  });

  server.on("/gpio2", [](){  // url to display the current value of GPIO2
    server.send(200, "text/html", htmlHead+"Current value of GPIO2: "+String(digitalRead(GPIO2))+htmlTail);
  });

  server.on("/error", [](){  // url to display the last reported error message with html formatting
    errorMsg.replace("\r\n","<br>"); // replace new lines with html <br> for readability
    server.send(200, "text/html", htmlHead+"Last reported error message:<br><br>"+errorMsg+htmlTail);
  });

  server.on("/success", [](){  // url to display the last reported success message with html formatting
    successMsg.replace("\r\n","<br>"); // replace new lines with html <br> for readability
    server.send(200, "text/html", htmlHead+"Last reported success message:<br><br>"+successMsg+htmlTail);
  });

  server.on("/status", [](){  // url to display esp8266 status
    server.send(200, "text/html", htmlHead+"IP Address: "+WiFi.localIP().toString().c_str()+"<br>"+
                                           "MAC address: "+WiFi.macAddress()+"<br>"+
                                           "SSID: "+WiFi.SSID()+"<br>"+
                                           "Signal Strength (%): "+percentQ+"<br>"+
                                           "Number of Consecutive Network Errors: "+netFails+"<br>"+
                                            htmlTail);
    });

  server.begin();
  successMsg = "HTTP server started";
  Serial.println(successMsg);
}
 
void loop(void)
{
  server.handleClient();

  // Reset previousMillis when millis overflows back to zero
  if ( millis() - previousMillis < 0 ) {
    previousMillis = 0;
  }

  if ( millis() - previousMillis > UPDATEINTERVAL ) {
      gettemperature();
      WiFiStatus();
      if ( Fstr.length() && Hstr.length() ) { // Don't send if we don't have any data
        sendToSC();
      } else {
        errorMsg = "Temp and/or Humidty values are empty. Skip sending to Security Center...";
        Serial.println(errorMsg);
      }
      previousMillis = millis();
  }

/*
  if ( netFails >= 5 ) {
    Serial.println("Too many network failures. Restarting WiFi....");
    //delay(3000);
    WiFi.reconnect();
    netFails = 0;
    // ESP.reset();
    delay(3000);
  }
*/

} 