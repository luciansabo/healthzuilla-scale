/**
 * Healthzuilla Scale
 * v 2022.2
 * Smart kitchen scale with WiFI and web API
 * 
 * Designed for Wemos D1 mini but should work with any ESP8266 board
 * Board in Arduino IDE: Lolin (Wemos) D1 R2 & mini
 * CPU Freq: 160 Mhz (needed for SSL)
 */
#include <HX711.h>  // https://github.com/bogde/HX711
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServerSecure.h>
#include <WiFiManager.h>         // https://github.com/tzapu/WiFiManager
#include <DoubleResetDetector.h> // https://github.com/datacute/DoubleResetDetector
#include <Adafruit_GFX.h> // https://github.com/adafruit/Adafruit-GFX-Library
#include <Adafruit_PCD8544.h> // see https://github.com/mcauser/WeMos-D1-mini-Nokia-5110-PCD8544
#include <SimpleTimer.h> // https://github.com/jfturcot/SimpleTimer
#include <MovingAverage.h> // http://github.com/sofian/MovingAverage
#include "DisplayHelper.h"
#include "EEPROMAnything.h"
#include "healthzuilla-scale.h"
#include "config.h"


#ifndef DEBUG
#define Serial if(0)Serial
#endif

#define _debugPrint(message) Serial.print(message);
#define _debugPrintln(message) Serial.println(message);

HX711 scale;
Adafruit_PCD8544 display = Adafruit_PCD8544(LCD_DC_PIN, 0, 0);
DisplayHelper displayHelper(&display);
WiFiManager wifiManager;
DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);
ESP8266WebServer httpServer(80);
SimpleTimer timer;
int readTimerId, powerOffTimerId, idleTimerId, wifiStartTimer;
float reading;
char displayBuffer[500];
MovingAverage average;
bool wifiConnectStarted = false;

HealthzuillaScaleSettings settings = {
  CONFIG_VERSION, // config version
  true, // useStaticIp
  "192.168.1.11", // ip
  "192.168.1.1", // gw
  "255.255.255.0", //mask
  414, // calibrationFactor
  132745, // zero factor (scale offset)
  152, // 152g calibration weight
  DEFAULT_POWEROFF_SEC, // 5 min powerOffTimer
  90, // 90s inactivityPowerOffTimer
  0.004967148 // voltage calibration factor
};

FoodInfo foodInfo;

volatile int lastButtonState = LOW;   // the last known state state of the button
// the following variables are unsigned longs because the time, measured in
// milliseconds, will quickly become a bigger number than can be stored in an int.
unsigned long lastDebounceTime = 0;  // the last time the button was toggled in ms

void startConfigPortal()
{
  _debugPrintln("Starting config portal");
  display.setCursor(0, 30);
  display.fillRect(0, 30, 84, 18, WHITE);
  display.print("Configure me");
  display.display();
  
  // blink two times to signal Config Portal mode
  digitalWrite(LOGO_LED_PIN, LOW);
  delay(1000);
  digitalWrite(LOGO_LED_PIN, HIGH);
  delay(500);
  digitalWrite(LOGO_LED_PIN, LOW);
  delay(1000);
  digitalWrite(LOGO_LED_PIN, HIGH);
  delay(500);
  digitalWrite(LOGO_LED_PIN, LOW);
  wifiManager.setConfigPortalTimeout(SETUP_TIMEOUT);
  wifiManager.startConfigPortal(configPortalAP);
}
//=============================================================================================
//                         SETUP
//=============================================================================================
void setup()
{
  Serial.begin(115200, SERIAL_8N1,SERIAL_TX_ONLY); 
  
  pinMode(PW_SW_PIN, OUTPUT);
  pinMode(TARE_BTN_PIN, INPUT);
  pinMode(BUILTIN_LED, OUTPUT);
  pinMode(LOGO_LED_PIN, OUTPUT);  
  
  digitalWrite(BUILTIN_LED, HIGH); // built-in led HIGH means off
  digitalWrite(PW_SW_PIN, HIGH); // keep the power (sent to Mosfet)
  analogWrite(LOGO_LED_PIN, 600);  // logo led partially lit with PWM
  
  _debugPrintln("Healthzuilla Scale at your service !");
  display.begin();  
  display.setContrast(60);  // Adjust for your display
  display.setTextColor(BLACK);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.fillRoundRect(0, 0, 84, 25, 5, BLACK);
  display.setCursor(6, 4);
  display.print("Healthzuilla");
  display.setCursor(27, 14);
  display.println("Scale");
  display.setTextColor(BLACK);  

  display.setTextSize(1);
  display.fillRect(0, 35, 84, 18, WHITE);
  strcpy(displayBuffer, "Btn skips WiFi");    
  display.setCursor(0, 35);
  display.print(displayBuffer);
  
  display.display();

  EEPROM.begin(sizeof(settings));
  //HealthzuillaScaleSettings oldSettings = settings;
  EEPROM_readAnything(CONFIG_START_ADDR, settings);

  //EEPROM_writeAnything(CONFIG_START_ADDR, oldSettings, settings);

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  if (drd.detectDoubleReset()) {
    startConfigPortal();
  }

  scale.begin(SCALE_DOUT_PIN, SCALE_CLK_PIN);  
  scale.tare(5); //Reset the scale to 0  
  
  timer.setTimeout(3000, onReady);  
  wifiStartTimer = timer.setTimeout(3100, wifiConnect);

  httpServer.client().setTimeout(300);
  httpServer.on ( "/api/calibrate", HTTP_POST, handleCalibrate );
  httpServer.on ( "/api/settings", HTTP_PATCH, handleSettingsChanged );
  httpServer.on ( "/api/settings", HTTP_GET, handleGetSettings );
  httpServer.on ( "/api/weight", HTTP_GET, handleGetWeight );
  httpServer.on ( "/api/weight", HTTP_POST, handlePostWeight );
  httpServer.on ( "/api/tare", HTTP_POST, handleTare );
  httpServer.on ( "/api/device/poweroff", HTTP_POST, handlePowerOff );
  httpServer.on ( "/api/device/reset", HTTP_POST, handleReset );
  httpServer.on ( "/api/device/info", HTTP_GET, handleInfo );
  httpServer.on ( "/api/device/calibrate-adc", HTTP_POST, handleCalibrateAdc );
  httpServer.begin();   // Start the webserver;      
  _debugPrintln("Server listening");

  // Start the mDNS responder
  if (!MDNS.begin("healthzuilla-scale")) {
   _debugPrintln("Error setting up MDNS responder!");
  } else {
    _debugPrintln("mDNS responder started");
  }
  
  long calibrationError = abs(settings.zeroFactor - scale.get_offset());
  _debugPrint("Calibration error:");
  _debugPrintln(calibrationError);
  
  if (calibrationError > 1000 && calibrationError < 3000) {
    display.clearDisplay();
    display.setCursor(0, 10);
    sprintf(displayBuffer, "Scale needs calibration. Zero Factor: %d; Offset:", settings.zeroFactor, scale.get_offset());
    _debugPrintln(displayBuffer);
    strcpy(displayBuffer, calibrationError > 2000 ? "Calibrate me\nor eat my\nerrors!" : "With a body\nlike this, who\nneeds\ncalibration?");
    display.println(displayBuffer);
    display.display();
    safeDelay(calibrationError > 2000 ? 4000 : 2000);
  }

  scale.set_scale(settings.calibrationFactor);  

  // make sure the we have valid timers
  if (settings.powerOffTimerSec < 30) {
    settings.powerOffTimerSec = DEFAULT_POWEROFF_SEC;
  }

  if (settings.idlePowerOffTimerSec < 30) {
    settings.idlePowerOffTimerSec = DEFAULT_IDLE_POWEROFF_SEC;
  }

  // setup poweroff timer (setting is specified in seconds)
  powerOffTimerId = timer.setTimeout(settings.powerOffTimerSec * 1000, powerOff);
  idleTimerId = timer.setInterval(settings.idlePowerOffTimerSec * 1000, powerOff);
  
  // fade out the LOGO led after 5s
  timer.setTimeout(5000, []() {
    uint16_t brightness = 200;
    long lastTime = 0, currentTime;
    while (brightness > 0) {
      currentTime = millis();
      if (currentTime - lastTime > 25) {
        analogWrite(LOGO_LED_PIN, brightness);
        brightness -= 10;
        lastTime = currentTime; 
      }
    }

    digitalWrite(LOGO_LED_PIN, LOW);
  });
  
}

// -------------------------------------------------------------------------
/**
 * Called when device is ready for operation (after 3s from boot)
 */
void onReady()
{
  readTimerId = timer.setInterval(READ_INTERVAL, readScale);
  display.clearDisplay();
  display.display();
  
  // get battery level
  float voltage = readVoltage();
  uint8_t batteryLevel = getLiPoBatteryLevel(voltage);

  if (batteryLevel < 15) {
    // low battery alert
    timer.setInterval(LOW_BATTERY_BLINK_DELAY, blinkStatusLed);
    display.println("Low battery\nInsert USB\nbutplug");
    display.display();
    safeDelay(3000);
    display.clearDisplay();
  }  

  displayHelper.renderBatteryLevel(batteryLevel);  
  displayHelper.renderAll();  
   
}

// -------------------------------------------------------------------------

/**
 * Connect to WiFi
 */
void wifiConnect()
{
  wifiConnectStarted = true;  
  display.setTextSize(1);
  display.fillRect(0, 35, 84, 18, WHITE);
  strcpy(displayBuffer, "SSID ");
  strcat(displayBuffer, WiFi.SSID().c_str());
  _debugPrintln(displayBuffer);
  display.setCursor(0, 25);
  display.print(displayBuffer);
  display.display();  

  //set custom ip for portal
  IPAddress _ip, _gw, _sn;
  _ip.fromString(settings.ip);
  _gw.fromString(settings.gateway);
  _sn.fromString(settings.subnetMask);

  if (settings.useStaticIp) {
    _debugPrint("Using static IP: ");
    _debugPrintln(settings.ip);
    WiFi.config(_ip, _gw, _gw, _sn);
  }  
  WiFi.begin(WiFi.SSID().c_str(), WiFi.psk().c_str());

  display.setCursor(0, 35);

  uint startTime = millis(), lastUpdate = startTime;  
  while (WiFi.status() != WL_CONNECTED && (millis() - startTime) <= WIFI_CONNECT_TIMEOUT) {
    if (millis() - lastUpdate > 1000) {
      lastUpdate = millis();
      _debugPrint(".");
      display.print(".");
      display.display();
    }    
    delay(100);
  }

  if (WiFi.status() != WL_CONNECTED) {
    _debugPrintln("Could not connect to WiFi");
    displayHelper.renderWiFiDisconnected();
  } else {
    _debugPrint("Wifi connected. IP Address: ");
    _debugPrintln(WiFi.localIP());
    _debugPrint("Signal strength: ");
    _debugPrintln(WiFi.RSSI());
    displayHelper.renderSignalStrength(WiFi.RSSI());
  }
  displayHelper.renderAll();   
}

// -------------------------------------------------------------------------

/**
 * Read an average analog value from pin 0
 * Analog reading goes from 0 - 1023. ADC voltage range is 0 - 1V
 */
float readADC()
{ 
  float adcValue = 0;
  uint8_t numReading = 15;
  for (int i = 0; i < numReading; i++) {
    adcValue += analogRead(A0);
  }
  
  return adcValue / numReading;
}

// -------------------------------------------------------------------------

/**
 * Read voltage using ADC pin on ESP8266
 * Output voltage should be between 0 - ~5v
 */
float readVoltage()
{
  float adcValue = readADC();

  float voltage = adcValue * settings.voltageCalibrationFactor;
  
  sprintf(displayBuffer, "Read voltage: %.2f; ADC value: %d", voltage, adcValue);
  _debugPrintln(displayBuffer);
  
  return voltage;
}

// -------------------------------------------------------------------------

/**
 * Get battery level from voltage
 *
 * @param float voltage
 *
 * @return uint8_t battery level 0..100 
 */
uint8_t getLiPoBatteryLevel(float voltage)
{
  if (voltage >= 4.13) {
    return 100;
  } else if (voltage > 4.10) {
    return 95;
  } else if (voltage > 4.04) {
    return 90;
  } else if (voltage > 3.98) {
    return 85;
  } else if (voltage > 3.95) {
    return 80;
  } else if (voltage > 3.9) {
    return 75;
  } else if (voltage > 3.86) {
    return 70;
  } else if (voltage > 3.82) {
    return 65;
  } else if (voltage >= 3.8) {
    return 60;
  } else if (voltage > 3.77) {
    return 55;
  } else if (voltage > 3.75) {
    return 50;
  } else if (voltage > 3.74) {
    return 45;
  } else if (voltage > 3.73) {
    return 40;
  } else if (voltage > 3.72) {
    return 35;
  } else if (voltage > 3.71) {
    return 30;
  } else if (voltage > 3.7) {
    return 25;
  } else if (voltage > 3.68) {
    return 20;
  } else if (voltage > 3.65) {
    return 15;
  } else if (voltage > 3.6) {
    return 10;
  } else if (voltage > 3.57) {
    return 5;
  } else {
    return 0;
  }
}

// -------------------------------------------------------------------------
/**
 * Turn tbe device off by putting the keep-alive pin low
 * turn off wifi, display and enter deep sleep when doing a soft power off.
 * In case you don't build the power-on latching circuitry this might come handy
 */
void powerOff()
{
  _debugPrintln("Power off.");
  WiFi.mode(WIFI_OFF);  
  display.clearDisplay();
  display.display();        // copy buffer to display memory
  display.command( PCD8544_FUNCTIONSET | PCD8544_POWERDOWN);
  digitalWrite(PW_SW_PIN, LOW);  
  ESP.deepSleep(0);
}

// -------------------------------------------------------------------------

void handleCalibrateAdc()
{
  _debugPrintln("POST calibrateAdc API request");
  float refVoltage;
  char result[100];
  HealthzuillaScaleSettings oldSettings = settings;
  
  if (httpServer.hasArg("voltage")) {
    refVoltage = httpServer.arg("voltage").toFloat();
    if (refVoltage >= 3 && refVoltage <= 4.35) {
      // oldFactor is needed for revert in case the calibration failed
      double oldFactor = settings.voltageCalibrationFactor;
      settings.voltageCalibrationFactor = (double)(refVoltage / (double)readADC());
      
      // if calibration failed do not save the new value
      float diff = fabs(readVoltage() - refVoltage);
      if (diff > 0.1) {
        settings.voltageCalibrationFactor = oldFactor;
        
        snprintf(result, sizeof(result), "{\"error\": \"true\", \"message\": \"Calibration failed. Difference is %.2f\"}", diff);
        httpServer.send ( 400, "application/json", result );  
        return;
      } else {
        // save to EEPROM
        EEPROM_writeAnything(CONFIG_START_ADDR, settings, oldSettings);
    
        snprintf(result, sizeof(result), "{\"success\": \"true\", \"message\": \"Calibration successful. Difference is %.2f\"}", diff);
        httpServer.send ( 200, "application/json", result );  
        return;
      }
    }
  }

  _debugPrintln("Invalid voltage parameter.");
  sendCORSHeaders();
  httpServer.send ( 400, "application/json", "{\"error\": \"true\", \"message\": \"Invalid voltage. Must be between 3 - 4.35v\"}" );  
}

// -------------------------------------------------------------------------

void sendCORSHeaders()
{
  httpServer.sendHeader("Access-Control-Allow-Origin", "*");
  httpServer.sendHeader("Access-Control-Allow-Methods", "POST, GET, PATCH");
  httpServer.sendHeader("Access-Control-Max-Age", "1000");
  httpServer.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}
// -------------------------------------------------------------------------

void handleInfo()
{
  _debugPrintln("Get info API request");
  float voltage = readVoltage();
  uint8_t batteryLevel = getLiPoBatteryLevel(voltage);
  char result[500];  
 
  snprintf(result, sizeof(result),
      "{\n"
          "\t\"battery\": {\n"
            "\t\t\"voltage\": %.2f,\n"
            "\t\t\"chargeLevel\": %d\n"
          "\t},\n"
          "\t\"network\": {\n"
          " \t\t\"deviceMac\": \"%s\",\n"
            "\t\t\"ip\": \"%s\",\n"
            "\t\t\"subnetMask\": \"%s\",\n"
            "\t\t\"gateway\": \"%s\",\n"
            "\t\t\"dnsIP\": \"%s\",\n"
            "\t\t\"SSID\": \"%s\",\n"
            "\t\t\"BSSID\": \"%s\",\n"
            "\t\t\"signalStrength\": %d\n"
          "\t}\n"
      "}\n",
  voltage,
  batteryLevel,
  WiFi.macAddress().c_str(),
  WiFi.localIP().toString().c_str(),
  WiFi.subnetMask().toString().c_str(),
  WiFi.gatewayIP().toString().c_str(),
  WiFi.dnsIP().toString().c_str(),
  WiFi.SSID().c_str(),
  WiFi.BSSIDstr().c_str(),
  WiFi.RSSI()
  );

  sendCORSHeaders();
  httpServer.send ( 200, "application/json", result );
}
// ---------------------------------------------------------------------------

void handleTare()
{
  _debugPrintln("Tare API request");
  sendCORSHeaders();
  httpServer.send ( 200, "application/json", "{\"success\": true}");
  scale.tare(5);
}

// ---------------------------------------------------------------------------

void handleReset()
{
  _debugPrintln("Reset API request");
  sendCORSHeaders();
  httpServer.send ( 200, "application/json", "{\"success\": true}");
  delay(900);
  ESP.reset();
}

// -------------------------------------------------------------------------

void handlePowerOff()
{
  _debugPrintln("Power Off API request");
  sendCORSHeaders();
  httpServer.send ( 200, "application/json", "{\"success\": true}");
  delay(900);
  powerOff();
}

// -------------------------------------------------------------------------

void handleGetWeight()
{
  char result[100];
  _debugPrintln("Get weight API request");
  snprintf(result, sizeof(result),
          "{\n"
          "\t\"foodId\": \"%s\",\n"
          "\t\"weight\": \"%.1f\",\n"
          "\t\"unit\": \"g\"\n"
          "}\n",
          foodInfo.foodId,
          reading
         );

  sendCORSHeaders();
  httpServer.send ( 200, "application/json", result );
}

// -------------------------------------------------------------------------

void handleGetSettings()
{
  _debugPrintln("Get settings API request");
  char result[500];
  snprintf(result, sizeof(result),
          "{\n"
          "\t\"useStaticIp\": %s,\n"
          "\t\"ip\": \"%s\",\n"
          "\t\"gateway\": \"%s\",\n"
          "\t\"subnetMask\": \"%s\",\n"
          "\t\"zeroFactor\": %d,\n"
          "\t\"calibrationFactor\": %.2f,\n"
          "\t\"calibrationWeight\": %.2f,\n"
          "\t\"powerOffTimerSec\": %d,\n"
          "\t\"idlePowerOffTimerSec\": %d,\n"
          "\t\"voltageCalibrationFactor\": %lf\n"
          "}\n",
          (settings.useStaticIp ? "true" : "false"),
          settings.ip,
          settings.gateway,
          settings.subnetMask,
          settings.zeroFactor,
          settings.calibrationFactor,
          settings.calibrationWeight,
          settings.powerOffTimerSec,
          settings.idlePowerOffTimerSec,
          settings.voltageCalibrationFactor
         );

  sendCORSHeaders();
  httpServer.send ( 200, "application/json", result );
}

// -------------------------------------------------------------------------

void handlePostWeight()
{
  _debugPrintln("Post weight API request");

  sendCORSHeaders();

  if (httpServer.hasArg("foodId") && httpServer.hasArg("calories")) {
    String foodId = httpServer.arg("foodId");
    String foodName = httpServer.arg("name");
    
    if (foodId.length() < sizeof(foodInfo.foodId)) {
      foodId = foodId.substring(0, sizeof(foodInfo.foodId));
    }
    strcpy(foodInfo.foodId, foodId.c_str());

    if (foodName.length() < sizeof(foodInfo.name)) {
      foodName = foodName.substring(0, sizeof(foodInfo.name));
    }
    strcpy(foodInfo.name, foodName.c_str());
    foodInfo.calories = httpServer.arg("calories").toInt();
    httpServer.send ( 200, "application/json", "{\"success\": true}");

    // display the food name
    display.fillRect(0, 0, 84, 18, WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(foodInfo.name);
    display.setTextSize(2);
    display.display();

  } else {
    httpServer.send ( 400, "application/json", "{\"error\": \"Invalid params.\"}");
  }

}

// -------------------------------------------------------------------------

void handleCalibrate()
{
  _debugPrintln("Calibration API request");

  timer.disable(readTimerId);
  display.clearDisplay();
  display.setTextSize(1);
  display.println("Calibration\ntime already?");
  display.display();
  HealthzuillaScaleSettings oldSettings = settings;

  _debugPrint("Old Zero factor:");
  _debugPrintln(settings.zeroFactor);

  long zeroFactor = scale.read_average(20); //Get a baseline reading
  display.clearDisplay();
  sprintf(displayBuffer, "Place that %.1fg\n skinny\nbitch on me!", settings.calibrationWeight);
  display.println(displayBuffer);
  display.display();

  sprintf(displayBuffer, "New Zero factor: %d\nPlace a %.1fg weight on the scale.", zeroFactor, settings.calibrationWeight);
  _debugPrintln(displayBuffer);

  int counter = 0;
  bool hasNoWeight = true;
  while ((hasNoWeight = ((scale.read_average() - zeroFactor) < 5000))  && counter++ < 50) {
    delay(500);
  }

  if (hasNoWeight) {
    display.clearDisplay();
    _debugPrintln("Calibration weight not detected.");
    display.println("Bitch not here?\nI'll jerkoff\nuncalibrated.");
    display.display();
    safeDelay(3000);
    
    timer.enable(readTimerId);
    return;
  }

  display.clearDisplay();
  display.println("Mmmm... I like\nmy woman\non top !");
  display.display();
  long average = scale.read_average(20);

  settings.zeroFactor = zeroFactor;
  scale.set_offset(settings.zeroFactor);
  settings.calibrationFactor = ((average - settings.zeroFactor) / settings.calibrationWeight);
  scale.set_scale(settings.calibrationFactor);
  // save to EEPROM
  EEPROM_writeAnything(CONFIG_START_ADDR, settings, oldSettings);

  sendCORSHeaders();
  httpServer.send ( 200, "application/json", "{\"success\": true}" );
  
  timer.enable(readTimerId);
  display.clearDisplay();
  display.println("Yeah baby!\nFeel like new\nnow.");
  display.display();
  safeDelay(3000);
  display.clearDisplay();
}

// -------------------------------------------------------------------------

void handleSettingsChanged()
{
  _debugPrintln("Settings changed API request");

  HealthzuillaScaleSettings oldSettings = settings;

  if (httpServer.hasArg("useStaticIp")) {
    settings.useStaticIp = httpServer.arg("useStaticIp") == "1" ? true : false;
    _debugPrint("useStaticIp:")
    _debugPrintln(settings.useStaticIp);
  }

  if (httpServer.hasArg("ip")) {
    strcpy(settings.ip, httpServer.arg("ip").c_str());
    _debugPrint("ip:");
    _debugPrintln(settings.ip);
  }

  if (httpServer.hasArg("gateway")) {
    strcpy(settings.gateway, httpServer.arg("gateway").c_str());
    _debugPrint("gateway:");
    _debugPrintln(settings.gateway);
  }

  if (httpServer.hasArg("subnetMask")) {
    strcpy(settings.subnetMask, httpServer.arg("subnetMask").c_str());
    _debugPrint("subnetMask:");
    _debugPrintln(settings.subnetMask);
  }

  if (httpServer.hasArg("calibrationWeight")) {
    float calibrationWeight = httpServer.arg("calibrationWeight").toFloat();
    if (calibrationWeight > 0 && calibrationWeight < 5000) {
      settings.calibrationWeight = calibrationWeight;
    }
    _debugPrint("calibrationWeight:");
    _debugPrintln(settings.calibrationWeight);
  }

  if (httpServer.hasArg("powerOffTimerSec")) {
    uint16_t powerOffTimerSec = httpServer.arg("powerOffTimerSec").toInt();
    if (powerOffTimerSec > 30) { // at least 30s
      settings.powerOffTimerSec = powerOffTimerSec;
    }
    _debugPrint("powerOffTimerSec:");
    _debugPrintln(settings.powerOffTimerSec);
  }

  if (httpServer.hasArg("idlePowerOffTimerSec")) {
    uint16_t idlePowerOffTimerSec = httpServer.arg("idlePowerOffTimerSec").toInt();
    if (idlePowerOffTimerSec > 30) { // at least 30s
      settings.idlePowerOffTimerSec = idlePowerOffTimerSec;
    }
    _debugPrint("idlePowerOffTimerSec:");
    _debugPrintln(settings.idlePowerOffTimerSec);
  }


  EEPROM_writeAnything(CONFIG_START_ADDR, settings, oldSettings);

  sendCORSHeaders();
  httpServer.send ( 200, "application/json", "{\"success\": true}" );
}

// -------------------------------------------------------------------------

void saveConfigCallback()
{
  _debugPrintln("Saving WIFI config");
  HealthzuillaScaleSettings oldSettings = settings;
  settings.useStaticIp = false;
  //WiFi.disconnect();
  EEPROM_writeAnything(CONFIG_START_ADDR, settings, oldSettings);
}

// -------------------------------------------------------------------------

void poolTareButton()
{
  int reading = digitalRead(TARE_BTN_PIN);

  if ((millis() - lastDebounceTime) > SHORT_PRESS_TIME && reading == HIGH) {
      if (!wifiConnectStarted) {
        display.clearDisplay();        
        wifiConnectStarted = true; // set true for next time we press the button        
        timer.disable(wifiStartTimer);
        display.fillRect(0, 35, 84, 18, WHITE);
        display.display();        
      } else {
        _debugPrintln("Tare Pressed");      
        updateScaleDisplayReading(0);
        scale.tare(3);        
        _debugPrintln("Tare done");
      }
  }

  // save the reading. Next time through the loop, it'll be the lastButtonState:
  lastButtonState = reading;
}

//=============================================================================================
//                         LOOP
//=============================================================================================
void loop()
{  
  httpServer.handleClient();    //Handling of incoming requests
  // Call the double reset detector loop method every so often,
  // so that it can recognise when the timeout expires.
  // You can also call drd.stop() when you wish to no longer
  // consider the next reset as a double reset.
  drd.loop();
  timer.run();
  poolTareButton();
  MDNS.update();
}

// -------------------------------------------------------------------------

void updateScaleDisplayReading(float reading)
{
  display.fillRect(0, 18, 84, 38, WHITE);
  display.setTextSize(2);  
  display.setCursor(0, 18);
  sprintf(displayBuffer, "%.1fg", round(reading * 10) / 10.0, 1);
  display.println(displayBuffer);
  if (foodInfo.calories > 0) {
    display.setCursor(0, 34);
    sprintf(displayBuffer, "%dcal", round((reading * foodInfo.calories) / 100.0));
    display.println(displayBuffer);
  }
  display.display();
}

// -------------------------------------------------------------------------

void readScale()
{
  long prevAvg = average.get();
  reading = scale.get_units(5);
  average.update(reading);
  
  // when diff between average and current reading is less than 1 gram stabilize reading
  // if more, we need to reset the average because the weight in not yet stable
  if (fabs(average.get() - reading) > 1) {
    average.reset(reading);
  }
  
  // on more than 5 grams we surely have a different reading, thus activity on scale
  if (fabs(prevAvg - reading) > 5) {
    #ifdef DEBUG
     _debugPrintln("Restarting idle timer.");
    #endif
    timer.restartTimer(idleTimerId);
  }

  yield();

  #ifdef DEBUG
  sprintf(displayBuffer, "Reading: %.1fg; Calibration factor: %.2f", reading, settings.calibrationFactor);
  _debugPrintln(displayBuffer);
  #endif

  // set the reading to the moving average
  reading = average.get();

    // normalize reading
  if (fabs(reading) < 1 && fabs(reading) > 0) {
    reading = 0;
  }

  updateScaleDisplayReading(reading);  
}

// -------------------------------------------------------------------------

/**
 * Delay, but prevent WDT reset
 */
void safeDelay(uint32_t milliseconds)
{
  uint8_t delayTime = 750;
  uint16_t iterations = milliseconds / delayTime; 
  for (uint16_t i = 0; i < iterations; i++) {
    delay(delayTime);
    yield();
  }

  // at the end delay for the difference
  delay(milliseconds % delayTime);
}

// -------------------------------------------------------------------------

/**
 * Simple routine to blink the built-in led once with a 300ms delay
 * Used with a timer
 */
void blinkStatusLed()
{
  digitalWrite(LED_BUILTIN, LOW);
  delay(300); // 0.5s lit then turn off
  digitalWrite(LED_BUILTIN, HIGH);
}
