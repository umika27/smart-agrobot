#include <Arduino.h>
#include <WiFi.h>
#include <FirebaseESP32.h>
#include <DHT.h>

// --- 1. FIREBASE & WIFI CREDENTIALS (MUST BE UPDATED) ---
// IMPORTANT: The FIREBASE_HOST URL should NOT include https:// and should NOT end with a slash /.
#define FIREBASE_HOST "agrobot25bel0014-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "<token key>" // The token/key you use in App Inventor
#define WIFI_SSID "<Wifi Name>"
#define WIFI_PASSWORD "<Wifi Password>"
FirebaseConfig config;
FirebaseAuth auth;
// --- 2. DATABASE NODES & PATHS ---
#define AGROBOT_NODE "/agrobot"
// Sensor Nodes (Data sent by ESP32)
#define TEMP_NODE AGROBOT_NODE "/temp"
#define HUMIDITY_NODE AGROBOT_NODE "/humidity"
#define MOISTURE_NODE AGROBOT_NODE "/soil_moisture"
#define PUMP_STATUS_NODE AGROBOT_NODE "/pump_status" // Status reported by ESP32

// Control Node (Commands received from App Inventor)
#define MOVEMENT_CONTROL_NODE AGROBOT_NODE "/movement" // Tag from App Inventor: "movement"

// --- 3. HARDWARE PINS & SENSORS ---
// DHT Sensor
const int dhtPin = 5;
DHT dht(dhtPin, DHT11);
// Soil Moisture Sensor
const int soilMoisturePin = 34; // Analog Input
// Motor Driver Pins (L298N)
const int EN_LEFT = 27; 
const int IN1_LEFT = 26; 
const int IN2_LEFT = 25; 
const int EN_RIGHT = 33; 
const int IN3_RIGHT = 32; 
const int IN4_RIGHT = 14; 
// Pump Relay (Pumping is now AUTOMATIC, but pin defined for status)
const int relayPin = 13; 
const int PUMP_ON_STATE = LOW; // Assuming relay is active-low
const int PUMP_OFF_STATE = HIGH;

// --- 4. FIREBASE OBJECTS & STREAM SETUP ---
FirebaseData firebaseData;
FirebaseData firebaseStream;
// New Firebase Config and Auth objects
FirebaseConfig firebaseConfig;
FirebaseAuth firebaseAuth;


// --- 5. ACTUATOR FUNCTIONS  ---

void setMotorPins(int leftDir1, int leftDir2, int rightDir1, int rightDir2) {
  digitalWrite(IN1_LEFT, leftDir1);
  digitalWrite(IN2_LEFT, leftDir2);
  digitalWrite(IN3_RIGHT, rightDir1);
  digitalWrite(IN4_RIGHT, rightDir2);
}

void moveForward() {
  setMotorPins(HIGH, LOW, HIGH, LOW);
  analogWrite(EN_LEFT, 255);
  analogWrite(EN_RIGHT, 255);
}

void moveReverse() {
  setMotorPins(LOW, HIGH, LOW, HIGH);
  analogWrite(EN_LEFT, 255);
  analogWrite(EN_RIGHT, 255);
}

void turnLeft() {
  setMotorPins(LOW, HIGH, HIGH, LOW); // Left wheel reverse, right wheel forward
  analogWrite(EN_LEFT, 255);
  analogWrite(EN_RIGHT, 255);
}

void turnRight() {
  setMotorPins(HIGH, LOW, LOW, HIGH); // Left wheel forward, right wheel reverse
  analogWrite(EN_LEFT, 255);
  analogWrite(EN_RIGHT, 255);
}

void stopRobot() {
  setMotorPins(LOW, LOW, LOW, LOW); // Set all direction pins LOW
  analogWrite(EN_LEFT, 0); // Stop power
  analogWrite(EN_RIGHT, 0); // Stop power
}

void initializeActuators() {
  // Motor Enable Pins
  pinMode(EN_LEFT, OUTPUT);
  pinMode(EN_RIGHT, OUTPUT);
  // Motor Direction Pins
  pinMode(IN1_LEFT, OUTPUT);
  pinMode(IN2_LEFT, OUTPUT);
  pinMode(IN3_RIGHT, OUTPUT);
  pinMode(IN4_RIGHT, OUTPUT);
  // Pump Relay Pin (Output)
  pinMode(relayPin, OUTPUT);
  // Start pump OFF
  digitalWrite(relayPin, PUMP_OFF_STATE);
  
  stopRobot(); // Ensure robot is stationary on startup
}

// Placeholder for Firebase timeout handler
void streamTimeoutCallback(bool timeout) {
    if (timeout) {
        Serial.println("[FIREBASE] Stream timed out!");
    }
}


// --- 6. FIREBASE STREAM CALLBACK (Reads App Inventor Commands) ---
void streamCallback(StreamData data) {
  // Ignore complex data types or empty paths
  if (data.dataType() == "json" || data.dataType() == "array" || data.dataPath().length() < 2) {
      return; 
  }
  
  String path = data.dataPath(); 
  String command = data.stringData(); 

  Serial.print("[FIREBASE] Command received on path: ");
  Serial.print(path);
  Serial.print(" | Value: ");
  Serial.println(command);

  // --- MOVEMENT CONTROL LOGIC (Tag: "movement") ---
  if (path == MOVEMENT_CONTROL_NODE) {
    
    if (command == "FORWARD") { 
      moveForward();
    } else if (command == "BACKWARD") { 
      moveReverse();
    } else if (command == "LEFT") {
      turnLeft();
    } else if (command == "RIGHT") {
      turnRight();
    } else if (command == "STOP") { 
      stopRobot();
    }
    
    // Reset the command in Firebase to "STANDBY" after execution
    Firebase.setString(firebaseData, MOVEMENT_CONTROL_NODE, "STANDBY");
    Serial.print("[MOVEMENT] Robot executed: ");
    Serial.println(command);
  }
  
  // No need for PUMP control logic since those buttons were removed.
}


// --- 7. SENSOR READING & DATA SENDING ---
void sendSensorData() {
  // Read sensor values
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  int m = analogRead(soilMoisturePin); // Reads a value between 0 (wet) and 4095 (dry)

  // Basic Automatic Irrigation Logic (If soil is dry, turn pump ON)
  if (m > 3000) { // Example threshold: if reading is high (dry)
      digitalWrite(relayPin, PUMP_ON_STATE);
      Firebase.setString(firebaseData, PUMP_STATUS_NODE, "ON - Auto");
  } else {
      digitalWrite(relayPin, PUMP_OFF_STATE);
      Firebase.setString(firebaseData, PUMP_STATUS_NODE, "OFF - Auto");
  }

  // Check if any reading failed
  if (isnan(h) || isnan(t) || m == 0) {
    Serial.println("[SENSOR] Failed to read from sensor!");
    return;
  }

  // Send data to Firebase (App Inventor Monitoring Dashboard)
  Firebase.setFloat(firebaseData, TEMP_NODE, t);
  Firebase.setFloat(firebaseData, HUMIDITY_NODE, h);
  Firebase.setInt(firebaseData, MOISTURE_NODE, m);
}


// --- 8. SETUP & LOOP FUNCTIONS ---

void setup() {
  Serial.begin(115200);
  
  // 1. Initialize Hardware Pins
  initializeActuators();
  dht.begin();
  
  // 2. Connect to Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nConnected to WiFi!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // 3. Initialize Firebase Configuration
  // Set the host name (URL without https://)
  firebaseConfig.database_url = FIREBASE_HOST; 
  
  // FIX 1: Use databaseSecret instead of legacy_token for modern Firebase Client
  // Set the Database Secret Token (Authentication)
  config.database_url="agrobot25bel0014-default-rtdb.firebaseio.com";
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  
  // Begin the Firebase connection with the configuration objects
  Firebase.begin(&firebaseConfig, &firebaseAuth);
  Firebase.reconnectWiFi(true); // Auto-reconnect
  
  // 4. Set up Real-time Stream Listener for Commands
  if (!Firebase.beginStream(firebaseStream, AGROBOT_NODE)) {
    Serial.println("Could not start Firebase stream: " + firebaseStream.errorReason());
  }
  
  // Set the function that runs whenever data changes in the AGROBOT_NODE
  Firebase.setStreamCallback(firebaseStream, streamCallback, streamTimeoutCallback);
  Serial.println("Firebase stream ready for commands.");
}


void loop() {
  // Send sensor data every 5 seconds
  static unsigned long lastSendTime = 0;
  if (millis() - lastSendTime > 5000) {
    sendSensorData();
    lastSendTime = millis();
  }
  
  // 5. Keep the Firebase stream connection active
  // FIX 2: Use httpConnected() which is the correct function in the latest library
  // to check if the stream (which uses an HTTP connection) is still open.
  if (Firebase.ready() && !firebaseStream.httpConnected()) {
    Firebase.beginStream(firebaseStream, AGROBOT_NODE);
  }
  
  delay(10); 
}
