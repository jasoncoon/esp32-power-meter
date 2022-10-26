#include <LittleFS.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

#include <Adafruit_GFX.h>     // https://github.com/adafruit/Adafruit-GFX-Library
#include <Adafruit_INA219.h>  // https://github.com/adafruit/Adafruit_INA219
#include <Adafruit_SH110X.h>  // https://github.com/adafruit/Adafruit_SH110x
#include <CircularBuffer.h>   // https://github.com/rlogiacco/CircularBuffer
#include <WebSocketsServer.h> // https://github.com/Links2004/arduinoWebSockets
#include <WiFiManager.h>      // https://github.com/tzapu/WiFiManager

#define BUTTON_A 15
#define BUTTON_B 32
#define BUTTON_C 14
#define bufferSize 128

Adafruit_SH1107 display = Adafruit_SH1107(64, 128, &Wire);
Adafruit_INA219 ina219;
WebServer webServer(80);
WebSocketsServer webSocket = WebSocketsServer(81);
WiFiManager wifiManager;

CircularBuffer<float, bufferSize> voltageBuffer;
CircularBuffer<float, bufferSize> currentBuffer;

float voltage = 0;
float current = 0;

float lastVoltage = -1;
float lastCurrent = -1;

float maxVoltage = -1000;
float minVoltage = 1000;
float maxCurrent = -1000;
float minCurrent = 1000;

void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
  Serial.printf("Listing directory: %s\r\n", dirname);

  File root = fs.open(dirname);
  if (!root)
  {
    Serial.println("- failed to open directory");
    return;
  }
  if (!root.isDirectory())
  {
    Serial.println(" - not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file)
  {
    if (file.isDirectory())
    {
      Serial.print("  DIR : ");

#ifdef CONFIG_LITTLEFS_FOR_IDF_3_2
      Serial.println(file.name());
#else
      Serial.print(file.name());
      time_t t = file.getLastWrite();
      struct tm *tmstruct = localtime(&t);
      Serial.printf("  LAST WRITE: %d-%02d-%02d %02d:%02d:%02d\n", (tmstruct->tm_year) + 1900, (tmstruct->tm_mon) + 1, tmstruct->tm_mday, tmstruct->tm_hour, tmstruct->tm_min, tmstruct->tm_sec);
#endif

      if (levels)
      {
        listDir(fs, file.name(), levels - 1);
      }
    }
    else
    {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("  SIZE: ");

#ifdef CONFIG_LITTLEFS_FOR_IDF_3_2
      Serial.println(file.size());
#else
      Serial.print(file.size());
      time_t t = file.getLastWrite();
      struct tm *tmstruct = localtime(&t);
      Serial.printf("  LAST WRITE: %d-%02d-%02d %02d:%02d:%02d\n", (tmstruct->tm_year) + 1900, (tmstruct->tm_mon) + 1, tmstruct->tm_mday, tmstruct->tm_hour, tmstruct->tm_min, tmstruct->tm_sec);
#endif
    }
    file = root.openNextFile();
  }
}

void setup(void)
{
  Serial.begin(115200);

  if (!LittleFS.begin())
  {
    Serial.println(F("An error occurred when attempting to mount the flash file system"));
  }
  else
  {
    Serial.println("FS contents:");
    listDir(LittleFS, "/", 0);
  }

  delay(250);                // wait for the OLED to power up
  display.begin(0x3C, true); // Address 0x3C default

  display.display();
  // delay(1000);

  setupWifi();

  // Clear the buffer.
  display.clearDisplay();
  display.display();

  display.setRotation(1);

  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(BUTTON_B, INPUT_PULLUP);
  pinMode(BUTTON_C, INPUT_PULLUP);

  // text display tests
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.print("Power");
  display.display(); // actually display all of the above

  // set LED to be an output pin
  // pinMode(LED_BUILTIN, OUTPUT);

  uint32_t currentFrequency;
  // Initialize the INA219.
  // By default the initialization will use the largest range (32V, 2A).  However
  // you can call a setCalibration function to change this range (see comments).
  if (!ina219.begin())
  {
    Serial.println("Failed to find INA219 chip");
    while (1)
    {
      delay(10);
    }
  }
  // To use a slightly lower 32V, 1A range (higher precision on amps):
  // ina219.setCalibration_32V_1A();
  // Or to use a lower 16V, 400mA range (higher precision on volts and amps):
  ina219.setCalibration_16V_400mA();
}

void broadcastTXT(float value, const char *format, signed int width, unsigned int prec)
{
  char bufferV[10];
  dtostrf(value, width, prec, bufferV);

  char bufferTxt[40];
  sprintf(bufferTxt, format, bufferV);
  webSocket.broadcastTXT(bufferTxt);
}

bool updateRange = false;

void updateDisplay()
{
  // Update display text
  display.clearDisplay();
  display.setCursor(0, 0);

  // Mode 0, display volts and amps.
  printSIValue(voltage, "V:", 2, 9);
  display.setCursor(63, 0);
  printSIValue(current, "A:", 5, 9);

  // update display line chart
  voltageBuffer.push(voltage);
  currentBuffer.push(current);

  float maxVoltage = -100000;
  float maxCurrent = -100000;

  for (uint8_t i = 0; i < bufferSize; i++)
  {
    float voltage = voltageBuffer[i];
    float current = currentBuffer[i];

    if (voltage > maxVoltage)
      maxVoltage = voltage;
    if (current > maxCurrent)
      maxCurrent = current;

    float vf = 57 - ((voltage / maxVoltage) * 57.0);
    float cf = 57 - ((current / maxCurrent) * 57.0);

    display.drawPixel(i, vf + 8, SH110X_WHITE);
    display.drawPixel(i, cf + 8, SH110X_WHITE);
  }

  display.setCursor(0, 56);
  display.print(WiFi.localIP().toString());

  display.display();
}

void broadcastCurrentValues()
{
  char bufferV[10];
  dtostrf(voltage, 5, 2, bufferV);

  char bufferC[10];
  dtostrf(current, 6, 4, bufferC);

  char bufferTxt[60];
  sprintf(bufferTxt, "{\"v\":%s,\"c\":%s}", bufferV, bufferC);
  webSocket.broadcastTXT(bufferTxt);
}

void broadcastRange()
{
  char bufferMinV[10];
  dtostrf(minVoltage, 5, 2, bufferMinV);

  char bufferMaxV[10];
  dtostrf(maxVoltage, 5, 2, bufferMaxV);

  char bufferMinC[10];
  dtostrf(minCurrent, 6, 4, bufferMinC);

  char bufferMaxC[10];
  dtostrf(maxCurrent, 6, 5, bufferMaxC);

  char bufferTxt[60];
  sprintf(bufferTxt, "{\"minV\":%s,\"maxV\":%s,\"minC\":%s,\"maxC\":%s}", bufferMinV, bufferMaxV, bufferMinC, bufferMaxC);
  webSocket.broadcastTXT(bufferTxt);
}

void broadcastIfNeeded()
{
  static const unsigned long REFRESH_INTERVAL = 100; // ms
  static unsigned long lastRefreshTime = 0;

  if (millis() - lastRefreshTime >= REFRESH_INTERVAL)
  {
    lastRefreshTime += REFRESH_INTERVAL;

    bool update = false;
    if (voltage != lastVoltage)
    {
      lastVoltage = voltage;
      update = true;
    }
    if (current != lastCurrent)
    {
      lastCurrent = current;
      update = true;
    }

    if (update)
    {
      broadcastCurrentValues();
    }

    if (voltage < minVoltage)
    {
      minVoltage = voltage;
      updateRange = true;
    }
    if (voltage > maxVoltage)
    {
      maxVoltage = voltage;
      updateRange = true;
    }
    if (current < minCurrent)
    {
      minCurrent = current;
      updateRange = true;
    }
    if (current > maxCurrent)
    {
      maxCurrent = current;
      updateRange = true;
    }

    if (updateRange)
    {
      updateRange = false;
      broadcastRange();
    }
  }
}

void loop(void)
{
  wifiManager.process();
  handleWeb();

  // digitalWrite(LED_BUILTIN, HIGH); // turn the LED on (HIGH is the voltage level)

  if (!digitalRead(BUTTON_A))
    display.print("A");
  if (!digitalRead(BUTTON_B))
    display.print("B");
  if (!digitalRead(BUTTON_C))
    display.print("C");

  display.display();

  // float shuntvoltage = 0;
  // float loadvoltage = 0;
  // float power_W = 0;

  // shuntvoltage = ina219.getShuntVoltage_mV() / 1000.0;
  voltage = ina219.getBusVoltage_V();
  current = ina219.getCurrent_mA() / 1000.0;
  // power_W = ina219.getPower_mW() / 1000.0;
  // loadvoltage = voltage + (shuntvoltage / 1000.0);

  updateDisplay();

  // Serial.print("busV:");
  // Serial.print(busvoltage);
  // Serial.print(",currentA:");
  // Serial.print(current);
  // Serial.print(",maxV:");
  // Serial.print(maxV);
  // Serial.print(",maxC:");
  // Serial.print(maxC);
  // Serial.println();

  broadcastIfNeeded();

  // digitalWrite(LED_BUILTIN, LOW); // turn the LED off by making the voltage LOW
}

void setupWifi()
{
  WiFi.mode(WIFI_STA);

  wifiManager.setConfigPortalBlocking(false);
  // wifiManager.setConfigPortalTimeout(60);

  // automatically connect using saved credentials if they exist
  // If connection fails it starts an access point with the specified name
  if (wifiManager.autoConnect())
  {
    Serial.println("Wi-Fi connected");
  }
  else
  {
    Serial.println("Wi-Fi manager portal running");
  }
}

void setupWeb()
{
  webServer.enableCORS(true);
  webServer.serveStatic("/", LittleFS, "/");
  webServer.serveStatic("/", LittleFS, "/index.htm");
  webServer.serveStatic("/index.htm", LittleFS, "/index.htm");
  webServer.serveStatic("/favicon.ico", LittleFS, "/favicon.ico");
  webServer.serveStatic("/styles.css", LittleFS, "/styles.css");
  webServer.serveStatic("/app.js", LittleFS, "/app.js");
  webServer.serveStatic("/atom196.png", LittleFS, "/atom196.png");

  webServer.begin();

  webSocket.begin(); // init the Websocketserver
  webSocket.onEvent(webSocketEvent);
}

void handleWeb()
{
  static bool webServerStarted = false;

  // check for connection
  if (WiFi.status() == WL_CONNECTED)
  {
    if (!webServerStarted)
    {
      Serial.println();
      Serial.println("WiFi connected");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      webServerStarted = true;
      setupWeb();
    }
    webServer.handleClient();
    webSocket.loop();
  }
}

void webSocketEvent(byte num, WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
  case WStype_DISCONNECTED: // enum that read status this is used for debugging.
    Serial.print("WS Type ");
    Serial.print(type);
    Serial.println(": DISCONNECTED");
    break;

  case WStype_CONNECTED: // Check if a WebSocket client is connected or not
    Serial.print("WS Type ");
    Serial.print(type);
    Serial.println(": CONNECTED");
    // initialize new web socket client
    broadcastCurrentValues();
    broadcastRange();
    break;

  case WStype_TEXT:   // check response from client
    Serial.println(); // the payload variable stores the status internally
    // Serial.println(payload[0]);
    // if (payload[0] == '1')
    // {
    //   pin_status = "ON";
    //   digitalWrite(22, HIGH);
    // }
    // if (payload[0] == '0')
    // {
    //   pin_status = "OFF";
    //   digitalWrite(22, LOW);
    // }
    break;
  }
}

void printSIValue(float value, char *units, int precision, int maxWidth)
{
  // Print a value in SI units with the units left justified and value right justified.
  // Will switch to milli prefix if value is below 1.

  // Add milli prefix if low value.
  if (fabs(value) < 1.0)
  {
    display.print('m');
    maxWidth -= 1;
    value *= 1000.0;
    precision = max(0, precision - 3);
  }

  // Print units.
  display.print(units);
  maxWidth -= strlen(units);

  // Leave room for negative sign if value is negative.
  if (value < 0.0)
  {
    maxWidth -= 1;
  }

  // Find how many digits are in value.
  int digits = ceil(log10(fabs(value)));
  if (fabs(value) < 1.0)
  {
    digits = 1; // Leave room for 0 when value is below 0.
  }

  // Handle if not enough width to display value, just print dashes.
  if (digits > maxWidth)
  {
    // Fill width with dashes (and add extra dash for negative values);
    for (int i = 0; i < maxWidth; ++i)
    {
      display.print('-');
    }
    if (value < 0.0)
    {
      display.print('-');
    }
    return;
  }

  // Compute actual precision for printed value based on space left after
  // printing digits and decimal point.  Clamp within 0 to desired precision.
  int actualPrecision = constrain(maxWidth - digits - 1, 0, precision);

  // Compute how much padding to add to right justify.
  int padding = maxWidth - digits - 1 - actualPrecision;
  for (int i = 0; i < padding; ++i)
  {
    display.print(' ');
  }

  // Finally, print the value!
  display.print(value, actualPrecision);
}
