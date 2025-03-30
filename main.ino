#include <DHT.h>

// Pin Definitions
  // Rain Drop Sensor Digital Pin
#define SOIL_MOISTURE_PIN A0 // Analog pin for Soil Moisture Sensor
#define VOLTAGE_SENSOR_PIN A1 // ACS712 analog pin for voltage

// Use Serial1 for SIM800L communication on MKR1010
#define SIM800_TX 14
#define SIM800_RX 13
#define GSM Serial1

// DHT Sensor Setup
DHT dht(DHT_PIN, DHT_TYPE);

// Variables
bool relayState = false;        // Store relay state
bool lastSwitchState = HIGH;    // Track last switch state
bool isRaining = false;         // Track rain status
float temperature = 0.0;
float humidity = 0.0;
unsigned long callStartTime = 0;
bool callActive = false;
bool rainSMSsent = false;        // Prevent multiple SMS for rain
bool highMoistureSMSsent = false;
bool powerAvailable = true;      // Track power supply status

// Registered Phone Number
const char *registeredNumber = "+919926211285";  // Update with your number

void setup() {
  Serial.begin(9600);
  GSM.begin(9600);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  pinMode(DHT_PIN, INPUT_PULLUP);
  pinMode(RAIN_SENSOR_PIN, INPUT);
  pinMode(SOIL_MOISTURE_PIN, INPUT);

  // Initialize DHT Sensor
  dht.begin();
  delay(2000);  // Allow DHT to stabilize

  // Turn relay off by default
  digitalWrite(RELAY_PIN, HIGH);  // Active LOW Relay
  Serial.println("System Initialized. Relay OFF.");

  initGSM();  // Initialize GSM Module
}

// ==========================
// Initialize GSM Module
// ==========================
void initGSM() {
  sendCommand("AT");
  delay(1000);
  sendCommand("ATE0");         // Disable echo
  delay(500);
  sendCommand("AT+CLIP=1");    // Enable caller ID
  delay(500);
  sendCommand("AT+DDET=1");    // Enable DTMF Detection
  delay(500);
  sendCommand("ATS0=1");       // Auto-answer after 1 ring
  delay(500);
  Serial.println("GSM Module Initialized.");
}

// ==========================
// Send Command to GSM
// ==========================
void sendCommand(const char *cmd) {
  GSM.println(cmd);
  delay(500);
  while (GSM.available()) {
    Serial.write(GSM.read());
  }
}

// ==========================
// Check Manual Switch
// ==========================
void checkSwitch() {
  bool currentSwitchState = digitalRead(SWITCH_PIN);

  // Check for switch press (LOW = Pressed)
  if (currentSwitchState == LOW && lastSwitchState == HIGH) {
    delay(50);  // Debounce
    if (digitalRead(SWITCH_PIN) == LOW) {
      toggleRelay();  // Toggle relay on button press
    }
  }
  lastSwitchState = currentSwitchState;  // Update last state
}

// ==========================
// Toggle Relay State
// ==========================
void toggleRelay() {
  relayState = !relayState;
  digitalWrite(RELAY_PIN, relayState ? LOW : HIGH);  // Active LOW Relay
  sendSMS(relayState ? "Motor Turned ON" : "Motor Turned OFF");
}

// ==========================
// Check Rain Sensor (Auto-OFF Only)
// ==========================
void checkRainSensor() {
  int rainValue = digitalRead(RAIN_SENSOR_PIN);
  isRaining = (rainValue == LOW);  // LOW means rain detected

  if (isRaining && relayState) {
    Serial.println("Rain Detected! Turning OFF Relay...");
    turnRelayOff();  // Auto-turn OFF if rain is detected
    if (!rainSMSsent) {
      sendSMS("Rain Detected! Motor Turned OFF.");
      rainSMSsent = true;  // Send SMS only once when turning off
    }
  }

  // Reset SMS flag when rain stops
  if (!isRaining) {
    rainSMSsent = false;
  }
}

// ==========================
// Check Soil Moisture (Auto-OFF Only)
// ==========================
void checkSoilMoisture() {
  int soilMoistureValue = analogRead(SOIL_MOISTURE_PIN);
  int moisturePercent = map(soilMoistureValue, 1023, 300, 0, 100);

  Serial.print("Soil Moisture: ");
  Serial.print(moisturePercent);
  Serial.println("%");

  // Auto-turn OFF if moisture exceeds 90% and relay is ON
  if (moisturePercent > 90 && relayState) {
    Serial.println("High Moisture! Turning OFF Relay...");
    turnRelayOff();
    if (!highMoistureSMSsent) {
      sendSMS("High Moisture! Motor Turned OFF.");
      highMoistureSMSsent = true;  // Send SMS only once when turning off
    }
  }

  // Reset SMS flag when moisture normalizes
  if (moisturePercent <= 90) {
    highMoistureSMSsent = false;
  }
}

// ==========================
// Turn Relay ON
// ==========================
void turnRelayOn() {
  if (!relayState) {
    relayState = true;
    digitalWrite(RELAY_PIN, LOW);  // Active LOW Relay
    sendSMS("Motor Turned ON");
  }
}

// ==========================
// Turn Relay OFF
// ==========================
void turnRelayOff() {
  if (relayState) {
    relayState = false;
    digitalWrite(RELAY_PIN, HIGH);  // Active LOW Relay
    sendSMS("Motor Turned OFF");
  }
}

// ==========================
// Send SMS
// ==========================
void sendSMS(String message) {
  GSM.print("AT+CMGF=1\r");  // Set SMS mode
  delay(100);
  GSM.print("AT+CMGS=\"");
  GSM.print(registeredNumber);
  GSM.println("\"");
  delay(100);
  GSM.print(message);
  delay(100);
  GSM.write(26);  // CTRL+Z to end SMS
  delay(1000);
}

// ==========================
// Handle Incoming Call and DTMF
// ==========================
void handleCall() {
  if (GSM.available()) {
    String data = GSM.readString();
    Serial.println("GSM Data: " + data);  // Debug incoming data

    // Check if Incoming Call
    if (data.indexOf("+CLIP:") >= 0) {
      if (data.indexOf(registeredNumber) != -1) {
        Serial.println("Call from registered number... Auto picking up.");
        answerCall();  // Auto answer the call
      }
    }

    // Check for DTMF Input
    if (data.indexOf("+DTMF:") >= 0) {
      int dtmfTone = data.substring(data.indexOf("+DTMF:") + 6).toInt();
      handleDTMF(dtmfTone);
    }
  }
}

// ==========================
// Answer Incoming Call
// ==========================
void answerCall() {
  sendCommand("ATA");  // Auto-answer the call
  delay(2000);         // Wait for the call to connect
  callStartTime = millis();
  callActive = true;
  Serial.println("Call Answered Successfully.");
}

// ==========================
// Disconnect Call after 20s Inactivity
// ==========================
void checkCallTimeout() {
  if (callActive && millis() - callStartTime > 20000) {
    Serial.println("No response, disconnecting call.");
    sendCommand("ATH");  // Hang up the call
    callActive = false;
    callStartTime = 0;
  }
}

// ==========================
// Handle DTMF Actions
// ==========================
void handleDTMF(int tone) {
  switch (tone) {
    case 1:
      turnRelayOn();
      sendSMS("Motor Turned ON via DTMF.");
      break;
    case 2:
      turnRelayOff();
      sendSMS("Motor Turned OFF via DTMF.");
      break;
    case 3:
      turnRelayOn();
      sendSMS("Motor Turned ON for 2 min.");
      delay(120000);  // 2 minutes
      turnRelayOff();
      sendSMS("Motor Turned OFF after 2 min.");
      break;
    case 4:
      turnRelayOn();
      sendSMS("Motor Turned ON for 5 min.");
      delay(300000);  // 5 minutes
      turnRelayOff();
      sendSMS("Motor Turned OFF after 5 min.");
      break;
    case 6:
      checkPowerStatus();  // Check power supply status using voltage sensor
      break;
    case 0:
      sendSMS("Current Location: https://maps.app.goo.gl/QANq4T3xrD9umteKA");
      break;
    default:
      Serial.println("Invalid DTMF Tone Received.");
      break;
  }
}

// ==========================
// Check Power Supply Status via DTMF 6
// ==========================
void checkPowerStatus() {
  int voltageValue = analogRead(VOLTAGE_SENSOR_PIN);
  float voltage = (voltageValue * 5.0 * 11) / 1023.0;  // Scale to 0-55V

  Serial.print("Voltage Sensor Reading: ");
  Serial.print(voltage);
  Serial.println(" V");

  if (voltage >= 150.0) {
    sendSMS("Power is Available.");
  } 
  else {
    sendSMS("No Power Supply Detected.");
  }
}

// ==========================
// Update Temperature & Humidity for Debug
// ==========================
void updateSensorData() {
  float newTemp = dht.readTemperature();
  float newHum = dht.readHumidity();

  if (isnan(newTemp) || isnan(newHum)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  temperature = newTemp;
  humidity = newHum;

  Serial.print("Temp: ");
  Serial.print(temperature);
  Serial.print("°C, Hum: ");
  Serial.print(humidity);
  Serial.println("%");
}

// ==========================
// Main Loop
// ==========================
void loop() {
  checkSwitch();
  checkRainSensor();
  checkSoilMoisture();
  updateSensorData();
  handleCall();
  checkCallTimeout();
  delay(2000);  // Delay for sensor updates
}
