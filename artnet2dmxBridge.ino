#include <WiFi.h>
#include <LittleFS.h>
#include <ArtnetWifi.h> // Include the ArtnetWifi library
#include <DmxOutput.h>
#include <AsyncWebServer_RP2040W.h>  // Include the AsyncWebServer library
#include <hardware/watchdog.h>  // Include watchdog for reset
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Crypto.h>
#include <AES.h>
#include <string.h>
#include "pico/stdlib.h" // Include for random number generation
#include "hardware/adc.h"

// AES encryption key and IV (must be 16 bytes each)
const uint8_t aesKey[16] = {0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C};
//const uint8_t aesIV[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

// XOR encryption key (change this to a more secure key)
const char* encryptionKey = "YourEncryptionKey123";

// SSD1306
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  // Set to -1 if using a hardwired reset pin
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Art-Net
ArtnetWifi artnet; // Create an instance of the ArtnetWifi class
int startUniverseA = 0; // DMX universe 0
int startUniverseB = 1; // DMX universe 1

// DMX
const int dmxPinA = 2; // GPIO pin for DMX universe 0
const int dmxPinB = 3; // GPIO pin for DMX universe 1
DmxOutput dmxA; // Initialize DMX output for universe 0
DmxOutput dmxB; // Initialize DMX output for universe 1

// Web server
AsyncWebServer server(80);

// WiFi credentials
const char* apSSID = "PicoW-Setup";
const char* apPassword = "setup1234"; // AP mode password
bool isInAPMode = false; // Flag to track if we're in AP mode or not

// File to store WiFi credentials
const char* wifiCredentialsFile = "/wifi.txt";
const char* UniverseCredentialsFile = "/Universe.txt";

// Variables to store WiFi credentials
String ssid = "";
String password = "";

// DMX channel data for both universes
uint8_t dmxDataA[513]; // DMX data buffer for universe A (512 channels + start code)
uint8_t dmxDataB[513]; // DMX data buffer for universe B (512 channels + start code)

String encryptData(const String &data) {
  AES128 aes128;
  uint8_t plaintext[16];
  uint8_t ciphertext[16];
  uint8_t iv[16];

  // Generate a random IV
  for (int i = 0; i < 16; i++) {
    iv[i] = (uint8_t)random(256); // Generates a number between 0 and 255
  }

  // Pad the data to 16 bytes
  memset(plaintext, 0, 16);
  memcpy(plaintext, data.c_str(), min(data.length(), 16));

  // Encrypt the data
  aes128.setKey(aesKey, 16);
  aes128.encryptBlock(ciphertext, plaintext);

  // Prepend the IV to the ciphertext (for storage)
  String encryptedData = "";
  for (int i = 0; i < 16; i++) { // Add IV to encrypted data
    char hex[3];
    sprintf(hex, "%02X", iv[i]);
    encryptedData += hex;
  }
  for (int i = 0; i < 16; i++) { // Add Ciphertext to encrypted data
    char hex[3];
    sprintf(hex, "%02X", ciphertext[i]);
    encryptedData += hex;
  }

  return encryptedData;
}

String decryptData(const String &encryptedData) {
  AES128 aes128;
  uint8_t ciphertext[16];
  uint8_t plaintext[16];
  uint8_t iv[16];

    // Extract the IV from the beginning of the string
  for (int i = 0; i < 16; i++) {
    char hex[3];
    hex[0] = encryptedData.charAt(i * 2);
    hex[1] = encryptedData.charAt(i * 2 + 1);
    hex[2] = '\0';
    iv[i] = strtol(hex, NULL, 16);
  }

  // Extract the ciphertext after the IV (Corrected Offset!)
  for (int i = 0; i < 16; i++) {
    char hex[3];
    hex[0] = encryptedData.charAt(i * 2 + 32); // Offset is now correct!
    hex[1] = encryptedData.charAt(i * 2 + 33); // Offset is now correct!
    hex[2] = '\0';
    ciphertext[i] = strtol(hex, NULL, 16);
  }

  // Decrypt the data
  aes128.setKey(aesKey, 16);
  aes128.decryptBlock(plaintext, ciphertext);

  // Convert plaintext to a string
  String decryptedData = ""; // Declare decryptedData HERE!
  for (int i = 0; i < 16; i++) {
    if (plaintext[i] == 0) break; // Stop at null terminator
    decryptedData += (char)plaintext[i];
  }

  return decryptedData;
}

// Function to load WiFi credentials from LittleFS
bool loadWiFiCredentials() {
  if (LittleFS.begin()) {
    File file = LittleFS.open(wifiCredentialsFile, "r");
    if (file) {
      String encryptedSSID = file.readStringUntil('\n');
      String encryptedPassword = file.readStringUntil('\n');
      file.close();
      // Decrypt the credentials
      ssid = decryptData(encryptedSSID);
      password = decryptData(encryptedPassword);      
      ssid.trim();
      password.trim();
      return true;
    }
  }
  return false;
}

// Function to save WiFi credentials to LittleFS
void saveWiFiCredentials(const String& newSSID, const String& newPassword) {
  // Encrypt the credentials before saving
  String encryptedSSID = encryptData(newSSID);
  String encryptedPassword = encryptData(newPassword);

  File file = LittleFS.open(wifiCredentialsFile, "w");
  if (!file) {
    Serial.println("Failed to open wifi config for writing");
  } else {
    file.println(encryptedSSID);
    file.println(encryptedPassword);
    file.close();
  }
}

void saveUniverseSetting(int universe, int universe1) {
  File file = LittleFS.open(UniverseCredentialsFile, "w");
  if (!file) {
    Serial.println("Failed to open universe config for writing");
  } else {
    file.println(universe);
    file.println(universe1);
    file.close();
  }
}

bool loadUniverseSetting() {
  if (LittleFS.begin()) { // Check if LittleFS is mounted
    File file = LittleFS.open(UniverseCredentialsFile, "r");
    if (file) {
      String universe = file.readStringUntil('\n');
      String universe1 = file.readStringUntil('\n');
      file.close();
      startUniverseA = universe.toInt();
      startUniverseB = universe1.toInt();
      Serial.println("Loaded universe settings:");
      Serial.println("Universe A: " + String(startUniverseA));
      Serial.println("Universe B: " + String(startUniverseB));
      return true;
    } else {
      Serial.println("Error opening Universe file");
      return false;
    }
  } else {
      Serial.println("LittleFS failed to mount in loadUniverseSetting");
      return false;
  }
}

// Function to scan available WiFi networks
String scanWiFiNetworks() {
  int n = WiFi.scanNetworks();
  String networks = "";
  for (int i = 0; i < n; i++) {
    networks += "<option value=\"" + String(WiFi.SSID(i)) + "\">" + String(WiFi.SSID(i)) + "</option>";
  }
  WiFi.scanDelete();  // Clear the scan list after we're done
  return networks;
}

// Function to start AP mode
void startAPMode() {
  isInAPMode = true; // No credentials, starting in AP mode
  WiFi.softAP(apSSID, apPassword);
  Serial.println("AP Mode Started");
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());
}

// Function to connect to WiFi
bool connectToWiFi() {
  if (ssid.length() > 0 && password.length() > 0) {
    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.print("Connecting to WiFi...");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConnected to WiFi!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
      return true;
    }
  }
  return false;
}

// Art-Net DMX callback
void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* data) {
  if (data == NULL) {
    Serial.println("Received NULL data");
    displayerror("NULL data");
    return;  // Don't process if data is invalid
  }
  // removed as library deals with this part
  //if (data[0] != 'A' || data[1] != 'r' || data[2] != 't' || data[3] != '-' || data[4] != 'N' || data[5] != 'e') {
  //  Serial.println("Invalid Art-Net header");
  //  displayerror("Iv Art-Net header");
  //  return; // Early exit if packet is invalid
  //}
  if (universe == startUniverseA) {
    dmxDataA[0] = 0x00; // VERY IMPORTANT: Reset start code before using buffer
    length = min(length, 512); // Ensure we don't write past the end of the buffer
    memcpy(dmxDataA + 1, data, length); // Copy Art-Net data to DMX buffer for universe 0
    for (int i = 1; i <= length; i++) {
      if (dmxDataA[i+1] < 0 || dmxDataA[i+1] > 255) {  // Check for valid DMX range
        Serial.print("Invalid DMX data at channel ");
        Serial.println(i + 1);
        displayerror("Invalid DMX data");
        dmxDataA[i+1] = 0; // Set invalid data to default (0)
      }
    }    
    dmxA.write(dmxDataA, length + 1);
  } else if (universe == startUniverseB) {
    dmxDataB[0] = 0x00; // VERY IMPORTANT: Reset start code before using buffer
    length = min(length, 512); // Ensure we don't write past the end of the buffer
    memcpy(dmxDataB + 1, data, length); // Copy Art-Net data to DMX buffer for universe 1
    for (int i = 1; i <= length; i++) {
      if (dmxDataB[i+1] < 0 || dmxDataB[i+1] > 255) {  // Check for valid DMX range
        Serial.print("Invalid DMX data at channel ");
        Serial.println(i + 1);
        displayerror("Invalid DMX data");
        dmxDataB[i+1] = 0; // Set invalid data to default (0)
      }
    }    
    dmxB.write(dmxDataB, length + 1);
  }
}

void resetPico() {
  Serial.println("Resetting Pico...");
  watchdog_reboot(0, 0, 0);  // Trigger reset with default arguments (0 will reset the system)
}

void setupWatchdog() {
  // Initialize the watchdog with a timeout of 4 seconds (4000 ms)
  watchdog_enable(8000, 1);  // Enable the watchdog timer with 4 seconds timeout
}
void displayDMXUniverses(int universeA, int universeB) {
  display.clearDisplay();
  
  display.setTextSize(1);      
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("DMX Universes:");
  
  display.setCursor(0, 20);
  display.print("Universe A: ");
  display.println(universeA);
  
  display.setCursor(0, 40);
  display.print("Universe B: ");
  display.println(universeB);
  
  display.display();
}
void displayerror(const String& status) {
  display.clearDisplay();
  
  display.setTextSize(1);      
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Error: ");
  
  display.setCursor(0, 20);
  display.println(status);
  display.display();
}
void displayWiFiStatus(const String& status, const String& ssid, const String& ip) {
  display.clearDisplay();
  
  display.setTextSize(1);      
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("WiFi Status: ");
  display.println(status);
  
  display.setCursor(0, 20);
  display.print("SSID: ");
  display.println(ssid);
  
  display.setCursor(0, 40);
  display.print("IP: ");
  display.println(ip);
  
  display.display();
}
float readInternalTemperature() {
    adc_select_input(4); // Select the temperature sensor input (ADC channel 4)
    uint16_t raw_value = adc_read(); // Read the raw ADC value
    float voltage = raw_value * 3.3f / (1 << 12); // Convert to voltage (3.3V reference, 12-bit ADC)
    float temperature = 27.0f - (voltage - 0.706f) / 0.001721f; // Convert to temperature in °C
    return temperature;
}
uint32_t generateRandomSeed() {
    uint32_t seed = 0;

    // 1. Add noise from the internal temperature sensor
    float temperature = readInternalTemperature();
    seed ^= (uint32_t)(temperature * 1000); // Multiply by 1000 to get more significant bits

    // 2. Add noise from ADC (if available)
    adc_select_input(0); // Select ADC input 0 (GPIO 26)
    uint16_t adc_value = adc_read();
    seed ^= adc_value;

    // 3. Add some pseudo-randomness from a fixed point
    seed ^= 0x15B233FF; // A somewhat arbitrary constant
    seed ^= (uint32_t)&__bss_end__; // Use the address of a known symbol

    // 4. Seed the PRNG
    randomSeed(seed);

    return seed;
}
void setup() {
  Serial.begin(115200);

  // Initialize ADC
  adc_init();
  adc_set_temp_sensor_enabled(true); // Enable the internal temperature sensor

  // Generate a random seed using the temperature sensor and other sources
  generateRandomSeed();

  // Initialize SSD1306 display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (true);
  }
  display.clearDisplay();
  display.display();

  // Initialize DMX data buffers and set start code
  dmxDataA[0] = 0x00; // Set start code for universe A
  dmxDataB[0] = 0x00; // Set start code for universe B


  // Initialize LittleFS
  if (!LittleFS.begin()) {
    Serial.println("Failed to mount LittleFS");
    return;
  }

  // Load saved universe settings
  loadUniverseSetting();
  Serial.println("Loaded universe settings:");
  Serial.println("Universe A: " + startUniverseA);
  Serial.println("Universe B: " + startUniverseB);
  displayDMXUniverses(startUniverseA, startUniverseB);
  delay(2000);

  // Load saved WiFi credentials
  if (loadWiFiCredentials()) {
    Serial.println("Loaded WiFi credentials:");
    Serial.println("SSID: " + ssid);
    Serial.println("Password: " + password);

    // Try to connect to WiFi
    if (connectToWiFi()) {
      Serial.println("Connected to WiFi!");
      displayWiFiStatus("Connected", ssid, WiFi.localIP().toString());
    } else {
      Serial.println("Failed to connect to WiFi. Starting AP mode...");
      startAPMode();
      displayWiFiStatus("AP Mode", "Not Connected", WiFi.softAPIP().toString());
    }
  } else {
    Serial.println("No WiFi credentials found. Starting AP mode...");
    startAPMode();
    displayWiFiStatus("AP Mode", "No SSID", "Not Connected");
  }

  // Initialize Art-Net (using ArtnetWifi)
  artnet.begin();
  artnet.setArtDmxCallback(onDmxFrame); // This will receive Art-Net DMX frames

  // Initialize DMX for both universes (using Pico-DMX)
  dmxA.begin(dmxPinA, pio0); // Initialize DMX output for universe A
  dmxB.begin(dmxPinB, pio0); // Initialize DMX output for universe A

  // Setup the watchdog timer
  setupWatchdog();
  
  // Setup asynchronous web server handlers
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
  // Only show WiFi setup and DMX universe setup forms if in AP mode
  String html ="";
  if (isInAPMode) {
    String availableNetworks = scanWiFiNetworks();  // Scan networks
    html +=R"(
    <html>
      <body>
        <h1>WiFi Setup</h1>
        <form action="/save" method="POST">
          <select name="ssid">)";
    html += availableNetworks;  // Add the SSIDs to the dropdown
    html += R"(</select><br>
          Password: <input type="password" name="password"><br>
          <input type="submit" value="Save">
        </form>

      <h1>DMX Universe Setup</h1>
      <form action="/setUniverse" method="POST">
        Universe A: <input type="number" name="universeA" min="0" max="1023" value=")" + String(startUniverseA) + R"("><br>
        Universe B: <input type="number" name="universeB" min="0" max="1023" value=")" + String(startUniverseB) + R"("><br>
        <input type="submit" value="Set Universe 0">
      </form>
      </body>
    </html>    )";
  } else {
    html += R"(
<html>
<head>
<title>DMX Control</title>
<style>
      body {font-family: Arial, sans-serif;background-color: #f4f4f9;color: #333;margin: 0;padding: 0;display: flex;justify-content: center;align-items: center;height: 100vh;}
      .container {width: 80%;max-width: 1200px;padding: 20px;background-color: #fff;box-shadow: 0 0 15px rgba(0, 0, 0, 0.1);border-radius: 10px;text-align: center;}
      h1 {font-size: 2.5em;color: #1a73e8;}
      h2 {font-size: 1.8em;margin-top: 30px;color: #333;}
      table {width: 100%;border-collapse: collapse;margin-top: 20px;margin-bottom: 20px;}
      table, th, td {border: 1px solid #ddd;}
      th, td {padding: 10px;text-align: center;}
      th {background-color: #4CAF50;color: white;}
      td {background-color: #f9f9f9;font-size: 1.1em;}
      tr:nth-child(even) td {background-color: #f1f1f1;}
      .dmx-section {margin-top: 40px;}
      .dmx-section p {font-size: 1.2em;color: #333;}
      .footer {font-size: 0.9em;color: #777;margin-top: 40px;border-top: 1px solid #ddd;padding-top: 10px;}
      .update-time {font-size: 1.1em;color: #999;}
      .warning {color: #ff4e00;font-weight: bold;}
      .success {color: #28a745;}
      .status-box {margin-top: 20px;padding: 10px;background-color: #fafafa;border-radius: 5px;font-size: 1.1em;text-align: center;}
      .status-box.error {background-color: #ffebeb;color: #f44336;}
      .status-box.success {background-color: #e1f7e1;color: #4caf50;}
      .status-box.warning {background-color: #fff3cd;color: #ffcc00;}
      .value-high {background-color: #ffcccc; /* Red for high values */}
      .value-medium {background-color: #ffffcc; /* Yellow for medium values */}
      .value-low {background-color: #ccffcc; /* Green for low values */}
      .error {color: #ff4e00;font-weight: bold;}
      .channel-group {display: flex; /* Arrange channels in a row */flex-wrap: wrap; /* Allow wrapping to the next line */gap: 10px; /* Spacing between channels */}
      .channel-control {display: flex;flex-direction: column; /* Arrange label and slider vertically */align-items: center;}
</style>
</head>
<body>
  <div class="container">
    <h1>DMX Status Page</h1>
    <div id="dmxStatusSection">
      <h2>Universe {{UniverseA}}</h2>
      <div id="dmxChannelsA"></div>
      <h2>Universe {{UniverseB}}</h2>
      <div id="dmxChannelsB"></div>
      <div class="update-time">
        <p>Last Updated: <span id="lastUpdate"></span></p>
      </div>
    </div>

    <div id="dmxControlSection" style="display: none;">  <h1>DMX Control</h1>
      <h2>Universe {{UniverseA}}</h2>
      <div id="universeAControls" class="channel-group"></div>
      <button id="prevA" onclick="showPreviousChannelsA() " disabled>Previous 32 Channels</button>
      <button id="nextA" onclick="showNextChannelsA() ">Next 32 Channels</button>
      <h2>Universe {{UniverseB}}</h2>
      <div id="universeBControls" class="channel-group"></div>
      <button id="prevB" onclick="showPreviousChannelsB() " disabled>Previous 32 Channels</button>
      <button id="nextB" onclick="showNextChannelsB() ">Next 32 Channels</button>
    </div>

    <button onclick="toggleView() ">Toggle DMX Control</button>
<button onclick="resetUniverse('A') ">Reset Universe A</button>
<button onclick="resetUniverse('B') ">Reset Universe B</button>  </div>
  <script>
    let startChannelA = 1;
    let startChannelB = 1;
    const channelsToShow = 32;
    let showControl = false;
    let lastUpdateTimestamp = 0;

    function toggleView() {
        showControl = !showControl;
        document.getElementById("dmxStatusSection").style.display = showControl ? "none" : "block";
        document.getElementById("dmxControlSection").style.display = showControl ? "block" : "none";
    }


// Function to calculate gradient color based on DMX value
function getGradientColor(value) {
  // Normalize the value to a range of 0 to 1
  const normalizedValue = value / 255;

  // Define color stops for the gradient
  const colorStops = [
    { color: "#ccffcc", position: 0 },   // Green for low values
    { color: "#ffffcc", position: 0.5 }, // Yellow for medium values
    { color: "#ffcccc", position: 1 }    // Red for high values
  ];

  // Find the two nearest color stops
  let startColor = colorStops[0];
  let endColor = colorStops[colorStops.length - 1];
  for (let i = 0; i < colorStops.length - 1; i++) {
    if (normalizedValue >= colorStops[i].position && normalizedValue <= colorStops[i + 1].position) {
      startColor = colorStops[i];
      endColor = colorStops[i + 1];
      break;
    }
  }

  // Interpolate between the two colors
  const t = (normalizedValue - startColor.position) / (endColor.position - startColor.position);
  const color = interpolateColor(startColor.color, endColor.color, t);

  return color;
}

// Function to interpolate between two hex colors
function interpolateColor(color1, color2, t) {
  const r1 = parseInt(color1.slice(1, 3), 16);
  const g1 = parseInt(color1.slice(3, 5), 16);
  const b1 = parseInt(color1.slice(5, 7), 16);

  const r2 = parseInt(color2.slice(1, 3), 16);
  const g2 = parseInt(color2.slice(3, 5), 16);
  const b2 = parseInt(color2.slice(5, 7), 16);

  const r = Math.round(r1 + (r2 - r1) * t);
  const g = Math.round(g1 + (g2 - g1) * t);
  const b = Math.round(b1 + (b2 - b1) * t);

  return `#${((1 << 24) + (r << 16) + (g << 8) + b).toString(16).slice(1)}`;
}


function createChannelControls(universe, containerId, startChannel) {
  const container = document.getElementById(containerId);
  container.innerHTML = "";
  for (let i = 0; i < channelsToShow; i++) {
    const channelNumber = startChannel + i;
    const channelControl = document.createElement("div");
    channelControl.className = "channel-control";

    const label = document.createElement("label");
    label.textContent = `Ch ${channelNumber}:`;
    channelControl.appendChild(label);

    const slider = document.createElement("input");
    slider.type = "range";
    slider.min = "0";
    slider.max = "255";
    slider.value = "0";
    slider.id = `universe${universe}Channel${channelNumber}`;
    slider.addEventListener("input", () => {
      updateDMXChannel(universe, channelNumber, slider.value);
      valueDisplay.textContent = slider.value;
      numberInput.value = slider.value;
      updateDMXTable(universe);
    });
    channelControl.appendChild(slider);

    const numberInput = document.createElement("input");
    numberInput.type = "number";
    numberInput.min = "0";
    numberInput.max = "255";
    numberInput.value = "0";
    numberInput.addEventListener("change", () => {
      slider.value = numberInput.value;
      updateDMXChannel(universe, channelNumber, numberInput.value);
      valueDisplay.textContent = numberInput.value;
      updateDMXTable(universe);
    });
    channelControl.appendChild(numberInput);

    const valueDisplay = document.createElement("span");
    valueDisplay.id = `universe${universe}Channel${channelNumber}Value`;
    valueDisplay.textContent = "0";
    channelControl.appendChild(valueDisplay);

    container.appendChild(channelControl);
  }
}

    function showPreviousChannelsA() {
      startChannelA -= channelsToShow;
      if (startChannelA < 1) startChannelA = 1;
      createChannelControls("A", "universeAControls", startChannelA);
      document.getElementById("prevA").disabled = startChannelA === 1;
      document.getElementById("nextA").disabled = startChannelA + channelsToShow > 512;
    }

    function showNextChannelsA() {
      startChannelA += channelsToShow;
      if (startChannelA > 512) startChannelA = 512 - channelsToShow + 1;
      createChannelControls("A", "universeAControls", startChannelA);
      document.getElementById("prevA").disabled = startChannelA === 1;
      document.getElementById("nextA").disabled = startChannelA + channelsToShow > 512;
    }

    function showPreviousChannelsB() {
      startChannelB -= channelsToShow;
      if (startChannelB < 1) startChannelB = 1;
      createChannelControls("B", "universeBControls", startChannelB);
      document.getElementById("prevB").disabled = startChannelB === 1;
      document.getElementById("nextB").disabled = startChannelB + channelsToShow > 512;
    }

    function showNextChannelsB() {
      startChannelB += channelsToShow;
      if (startChannelB > 512) startChannelB = 512 - channelsToShow + 1;
      createChannelControls("B", "universeBControls", startChannelB);
      document.getElementById("prevB").disabled = startChannelB === 1;
      document.getElementById("nextB").disabled = startChannelB + channelsToShow > 512;
    }

    function updateDMXChannel(universe, channel, value) {
      fetch(`/setChannel?universe=${universe}&channel=${channel}&value=${value}`, {
        method: "POST"
      })
      .then(response => {
        if (!response.ok) {
          console.error("Error setting DMX channel:", response.status);
        }
      });
    }

    function updateDMX() { // Combined function
      fetch("/dmx")
        .then(response => {
          if (!response.ok) throw new Error("Network response was not ok");
          return response.json();
        })
        .then(data => {
          if (data.timestamp > lastUpdateTimestamp) {
            lastUpdateTimestamp = data.timestamp; // Update the last known timestamp
            updateDMXTable("A", data.universeA.channels);
            updateDMXTable("B", data.universeB.channels);
          }
        })
        .catch(error => {
          console.error("Error fetching DMX data:", error);
          document.getElementById("dmxChannelsA").innerHTML = "<p class='error'>Error loading DMX data.</p>";
          document.getElementById("dmxChannelsB").innerHTML = "<p class='error'>Error loading DMX data.</p>";
        });

      const timestamp = new Date().toLocaleTimeString();
      document.getElementById("lastUpdate").textContent = timestamp;
    }

    function updateDMXTable(universe, channels) {
      const table = document.getElementById(`dmxChannels${universe}`);
      let html = "<table><tr><th>Channel</th><th>Value</th></tr>";
      channels.forEach(channel => {
        const color = getGradientColor(channel.value);
        html += `<tr><td>${channel.channel}</td><td style="background-color: ${color};" title="Channel ${channel.channel}: ${channel.value}">${channel.value}</td></tr>`;
      });
      html += "</table>";
      table.innerHTML = html;
    
          // Update slider and number input values
      for (let i = 0; i < channels.length; i++) {
        const channelNumber = startChannelA + i;
        const slider = document.getElementById(`universe${universe}Channel${channelNumber}`);
        const numberInput = document.getElementById(`universe${universe}Channel${channelNumber}`);
        const valueDisplay = document.getElementById(`universe${universe}Channel${channelNumber}Value`);
        if (slider && numberInput && valueDisplay) {
            slider.value = channels[i].value;
            numberInput.value = channels[i].value;
            valueDisplay.textContent = channels[i].value;
        }
      }
    }
function resetUniverse(universe) {
  fetch(`/resetUniverse?universe=${universe}`, {
    method: "POST"
  })
  .then(response => {
    if (!response.ok) {
      throw new Error(`HTTP error! Status: ${response.status}`);
    }
    return response.text();
  })
  .then(message => {
    console.log(message);
    updateDMX(); // Refresh the DMX table
  })
  .catch(error => {
    console.error("Error resetting universe:", error);
    alert("Failed to reset universe. Please check your connection.");
  });
}

    // Initial channel display and DMX table update
    createChannelControls("A", "universeAControls", startChannelA);
    createChannelControls("B", "universeBControls", startChannelB);
    updateDMX();
    setInterval(updateDMX, 1000);

  </script>
</body>
</html>
    )";
  html.replace("{{UniverseA}}", String(startUniverseA));
  html.replace("{{UniverseB}}", String(startUniverseB));
  }
    request->send(200, "text/html", html);
  });

  // Handle Resetting universe
server.on("/resetUniverse", HTTP_POST, [](AsyncWebServerRequest *request){
  if (request->hasParam("universe")) {
    String universeStr = request->arg("universe");
    int universe = (universeStr == "A") ? startUniverseA : startUniverseB; // Determine which universe

    uint8_t* dmxData = (universe == startUniverseA) ? dmxDataA : dmxDataB;

    // Reset all channels to 0
    memset(dmxData + 1, 0, 512); // Channels start at index 1

    // Send the reset data to the DMX output
    if (universe == startUniverseA) {
      dmxA.write(dmxData, 513); // Send all 513 channels
    } else {
      dmxB.write(dmxData, 513); // Send all 513 channels
    }

    request->send(200, "text/plain", "Universe reset");
  } else {
    request->send(400, "text/plain", "Missing universe parameter");
  }
});
  // Handle saving WiFi credentials
  server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
  if (isInAPMode) {
    String ssid = request->arg("ssid");
    String password = request->arg("password");

    saveWiFiCredentials(ssid, password);
    request->send(200, "text/plain", "Credentials saved. Restarting...");
    displayWiFiStatus("Restarting...", ssid, "Not Available");
    delay(1000);
    resetPico();  // Trigger the reset here using the watchdog
  } else {
    // If not in AP mode, deny the request
    request->send(403, "text/plain", "Settings can only be changed in AP mode.");
  }
  });

server.on("/setChannel", HTTP_POST, [](AsyncWebServerRequest *request){
  if (request->hasParam("universe") && request->hasParam("channel") && request->hasParam("value")) {
    String universeStr = request->arg("universe");
    String channelStr = request->arg("channel");
    String valueStr = request->arg("value");

    int universe = (universeStr == "A") ? startUniverseA : startUniverseB; // Determine which universe
    int channel = channelStr.toInt();
    int value = valueStr.toInt();

    if (channel >= 1 && channel <= 512 && value >= 0 && value <= 255) {
      uint8_t* dmxData = (universe == startUniverseA) ? dmxDataA : dmxDataB;

      dmxData[channel] = value; // Update the DMX data
      if (universe == startUniverseA) {
        dmxA.write(dmxData, 513); // Send all 513 channels
      } else {
        dmxB.write(dmxData, 513); // Send all 513 channels
      }

      request->send(200, "text/plain", "Channel updated");
    } else {
      request->send(400, "text/plain", "Invalid channel or value");
    }
  } else {
    request->send(400, "text/plain", "Missing parameters");
  }
});

server.on("/setUniverse", HTTP_POST, [](AsyncWebServerRequest *request){
  if (isInAPMode) {
    // Get the universe values from the form
    if (request->hasParam("universeA", true) && request->hasParam("universeB", true)) {
      String universeValueA = request->arg("universeA");
      String universeValueB = request->arg("universeB");

      // Convert to integer and validate that it is within the range [0, 1023]
      int universeA = universeValueA.toInt();
      int universeB = universeValueB.toInt();

      // Check if the universe values are valid
      if (universeA >= 0 && universeA < 1024 && universeB >= 0 && universeB < 1024) {
        // Valid values, save them
        startUniverseA = universeA;
        startUniverseB = universeB;
        saveUniverseSetting(startUniverseA, startUniverseB);

        String message = "Start Universe 0 updated to: " + String(startUniverseA) + " Universe 1 updated to: " + String(startUniverseB);
        request->send(200, "text/plain", message);
      } else {
        // Invalid values, send an error message
        String errorMessage = "Error: Universe values must be between 0 and 1023.";
        request->send(400, "text/plain", errorMessage); // Send a 400 Bad Request status
      }
    } else {
      // If parameters are missing, return an error
      request->send(400, "text/plain", "Error: Missing universe parameters.");
    }
  } else {
    // If not in AP mode, deny the request
    request->send(403, "text/plain", "Settings can only be changed in AP mode.");
  }
});

server.on("/dmx", HTTP_GET, [](AsyncWebServerRequest *request){
  AsyncResponseStream *response = request->beginResponseStream("application/json");
  response->print("{");
  
    // Add timestamp
  response->print("\"timestamp\": " + String(millis()) + ",");
  
  // Add universe A data
  response->print("\"universeA\": {");
  response->print("\"universe\": " + String(startUniverseA) + ",");
  response->print("\"channels\": [");
  for (int i = 1; i <= 512; i++) {
    if (i > 1) response->print(",");
    response->print("{");
    response->print("\"channel\": " + String(i) + ",");
    response->print("\"value\": " + String(dmxDataA[i]));
    response->print("}");
  }
  response->print("]");
  response->print("},");

  // Add universe B data
  response->print("\"universeB\": {");
  response->print("\"universe\": " + String(startUniverseB) + ",");
  response->print("\"channels\": [");
  for (int i = 1; i <= 512; i++) {
    if (i > 1) response->print(",");
    response->print("{");
    response->print("\"channel\": " + String(i) + ",");
    response->print("\"value\": " + String(dmxDataB[i]));
    response->print("}");
  }
  response->print("]");
  response->print("}");

  response->print("}");
  request->send(response);
});

// Start the web server
server.begin();
}

void loop() {
  // Reset the watchdog timer to avoid triggering a reset
  watchdog_update();

  // Handle Art-Net packets
  artnet.read();  // Read incoming Art-Net packets
}
