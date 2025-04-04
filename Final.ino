#define BLYNK_TEMPLATE_ID "TMPL3MVwRdQeC"
#define BLYNK_TEMPLATE_NAME "EV Charging Socket"
#define BLYNK_PRINT Serial
#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_Fingerprint.h>
#include <ACS712.h>
#include <Keypad.h>
#include <BlynkSimpleEsp32.h>

// **📟 16x2 LCD I2C (Default SDA 21, SCL 22)**
LiquidCrystal_I2C lcd(0x27, 16, 2);

// **🔍 FINGERPRINT SENSOR (RX 20, TX 19)**
#define FP_RX 19
#define FP_TX 20
HardwareSerial mySerial(2);  
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// **⚡ CURRENT SENSOR (ACS712 30A, PIN 4)**
ACS712 sensor(ACS712_30A, 4);
float voltage = 220.0;
float rate_per_kwh = 9.0;
float zeroLoadOffset = 0.0;
const float NOISE_THRESHOLD = 0.09; // Threshold to filter out noise

// **🔌 RELAY PIN (PIN 21)**
#define RELAY_PIN 21

// **🔑 KEYPAD SETUP**
const byte ROWS = 4;
const byte COLS = 4;
byte rowPins[ROWS] = {33, 34, 26, 48};
byte colPins[COLS] = {41, 40, 1, 38};
char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// **🌐 Blynk Setup**
char auth[] = "j1G3UqgYcI8kMToYvtdnh5pdGOogtEcM";
char ssid[] = "Steamed Momo";
char pass[] = "@@laxmi22##";
bool wifiConnected = false;

// Blynk Virtual Pins
#define BLYNK_GRAPH_VPIN V1
#define BLYNK_ENERGY_VPIN V3
#define BLYNK_TIME_VPIN V4
#define BLYNK_POWER_VPIN V5

const int CALIBRATION_SAMPLES = 200;

void setup() {
    Serial.begin(115200);
    mySerial.begin(57600, SERIAL_8N1, FP_RX, FP_TX);

    Wire.begin(7, 6);
    lcd.init();
    lcd.backlight();

    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);

    sensor.calibrate();
    calibrateSensor();

    connectToWiFi();

    showWelcomeScreen();

    finger.begin(57600);
    if (finger.verifyPassword()) {
        Serial.println("Fingerprint sensor detected!");
    } else {
        Serial.println("Fingerprint sensor not found!");
        while (1);
    }
}

void loop() {
    if (wifiConnected) {
        Blynk.run();
    }

    showMainMenu();
}

void showMainMenu() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("1: Scan & Charge");
    lcd.setCursor(0, 1);
    lcd.print("2: Register FP");

    char option = waitForKeypadInput();
    if (option == '1') {
        if (fingerprintMatch()) {
            showChargingMenu();
        }
    } else if (option == '2') {
        registerFingerprint();
    }
}

void showWelcomeScreen() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Welcome to BIT");
    lcd.setCursor(0, 1);
    lcd.print("Charging System");
    delay(3000);
    lcd.clear();
}

bool fingerprintMatch() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Scan Finger...");
    if (getFingerprint()) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Fingerprint OK");
        delay(2000);
        return true;
    } else {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Access Denied");
        delay(2000);
        return false;
    }
}

void showChargingMenu() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("1: Time");
    lcd.setCursor(0, 1);
    lcd.print("2: kWh  3: Full");

    char option = waitForKeypadInput();
    if (option == '1') {
        startChargingByTime();
    } else if (option == '2') {
        startChargingByKwh();
    } else if (option == '3') {
        startFullCharging();
    }
}

void startChargingByTime() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Enter Time (Min):");
    int timeMinutes = waitForAmount();
    unsigned long chargeEndTime = millis() + (timeMinutes * 60000);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Charging...");
    digitalWrite(RELAY_PIN, HIGH);

    unsigned long startTime = millis();
    while (millis() < chargeEndTime) {
        updateChargingStatus(startTime);
        if (keypad.getKey() == 'A') {
            break;
        }
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Charging Done");
    digitalWrite(RELAY_PIN, LOW);
    delay(3000);
}

void startChargingByKwh() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Enter kWh:");
    float maxEnergyKwh = waitForAmount();

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Charging...");
    digitalWrite(RELAY_PIN, HIGH);

    float totalEnergyConsumed = 0.0;
    while (totalEnergyConsumed < maxEnergyKwh) {
        float current = getCurrent();
        totalEnergyConsumed += calculateEnergyConsumed();

        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Current: ");
        lcd.print(current, 3);
        lcd.print(" A");

        lcd.setCursor(0, 1);
        lcd.print("Energy: ");
        lcd.print(totalEnergyConsumed, 3);
        lcd.print(" kWh");

        if (wifiConnected) {
            Blynk.virtualWrite(BLYNK_GRAPH_VPIN, current);
            Blynk.virtualWrite(BLYNK_ENERGY_VPIN, totalEnergyConsumed);
        }

        if (keypad.getKey() == 'A') {
            break;
        }

        delay(1000);
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Charging Done");
    digitalWrite(RELAY_PIN, LOW);
    delay(3000);
}

void startFullCharging() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Full Charging");
    digitalWrite(RELAY_PIN, HIGH);

    unsigned long startChargeTime = millis();
    while (true) {
        updateChargingStatus(startChargeTime);
        if (keypad.getKey() == 'A') {
            break;
        }
        if (isChargeComplete()) {
            break;
        }
    }

    unsigned long elapsedTime = millis() - startChargeTime;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Charging Done");
    lcd.setCursor(0, 1);
    lcd.print("Time: ");
    lcd.print(elapsedTime / 60000);
    lcd.print(" Min");
    digitalWrite(RELAY_PIN, LOW);
    delay(3000);
}

void updateChargingStatus(unsigned long startTime) {
    float current = getCurrent();

    float power = voltage * current;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Current: ");
    lcd.print(current, 2);
    lcd.print(" A");

    if (startTime > 0) {
        unsigned long elapsedTime = (millis() - startTime) / 1000;
        lcd.setCursor(0, 1);
        lcd.print("Time: ");
        lcd.print(elapsedTime / 60);
        lcd.print(":");
        lcd.print(elapsedTime % 60);

        if (wifiConnected) {
            Blynk.virtualWrite(BLYNK_TIME_VPIN, elapsedTime / 60);
        }
    } else {
        lcd.setCursor(0, 1);
        lcd.print("Power: ");
        lcd.print(power, 2);
        lcd.print(" W");

        if (wifiConnected) {
            Blynk.virtualWrite(BLYNK_POWER_VPIN, power);
        }
    }

    if (wifiConnected) {
        Blynk.virtualWrite(BLYNK_GRAPH_VPIN, current);
    }

    delay(1000);
}

float calculateEnergyConsumed() {
    float current = getCurrent();

    float power = voltage * current;
    float energyInterval = ((power * 3) / 3600.0) / 1000;
    return energyInterval;
}

bool isChargeComplete() {
    static unsigned long lastCurrentTime = 0;
    static float lastCurrentReading = 0.0;
    float current = getCurrent();

    if (fabs(current) < NOISE_THRESHOLD) {
        if (millis() - lastCurrentTime > 30000) {
            return true;
        }
    } else {
        lastCurrentTime = millis();
        lastCurrentReading = current;
    }

    return false;
}

int waitForAmount() {
    String amountStr = "";
    lcd.setCursor(0, 1);
    lcd.print("Input: ");

    while (true) {
        char key = keypad.getKey();
        if (key) {
            if (key == '#') {
                if (amountStr.length() > 0) {
                    return amountStr.toInt();
                }
            } else if (key == '*') {
                amountStr = "";
                lcd.setCursor(8, 1);
                lcd.print("     ");
            } else if (key >= '0' && key <= '9') {
                amountStr += key;
                lcd.setCursor(8 + amountStr.length() - 1, 1);
                lcd.print(key);
            }
        }
    }
}

char waitForKeypadInput() {
    char key;
    while (true) {
        key = keypad.getKey();
        if (key == '1' || key == '2' || key == '3') {
            return key;
        }
    }
}

void registerFingerprint() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Register Finger");
    int id = finger.getTemplateCount() + 1;
    lcd.setCursor(0, 1);
    lcd.print("ID: ");
    lcd.print(id);

    if (fingerEnroll(id)) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("FP Registered!");
        delay(2000);
    } else {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Failed. Try Again");
        delay(2000);
    }
}

bool fingerEnroll(int id) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Place Finger...");
    while (finger.getImage() != FINGERPRINT_OK);
    if (finger.image2Tz(1) != FINGERPRINT_OK) return false;

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Remove Finger...");
    delay(2000);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Place Again...");
    while (finger.getImage() != FINGERPRINT_OK);
    if (finger.image2Tz(2) != FINGERPRINT_OK) return false;

    if (finger.createModel() != FINGERPRINT_OK) return false;
    return (finger.storeModel(id) == FINGERPRINT_OK);
}

bool getFingerprint() {
    int id = -1;
    while (id != FINGERPRINT_OK) {
        if (finger.getImage() == FINGERPRINT_OK) {
            if (finger.image2Tz() == FINGERPRINT_OK) {
                if (finger.fingerFastSearch() == FINGERPRINT_OK) {
                    return true;
                }
            }
        }
    }
    return false;
}

// **🔹 Function to Calibrate the Sensor at Zero Load**
void calibrateSensor() {
    digitalWrite(RELAY_PIN, HIGH);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Calibrating");
    lcd.setCursor(0, 1);
    lcd.print("Current Sensor.");
    Serial.println("Calibrating Sensor... Ensure No Load!");
    delay(5000);  // Wait for stability

    float offsetSum = 0.0;
    for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
        offsetSum += sensor.getCurrentAC();
        delay(20); // Small delay for better readings
    }
    zeroLoadOffset = offsetSum / CALIBRATION_SAMPLES; // Compute average offset

    Serial.print("✅ Calibration Done! Zero Load Offset: ");
    Serial.println(zeroLoadOffset, 3);
    digitalWrite(RELAY_PIN, LOW);
}

// **🔹 Function to Get Current Value**
float getCurrent() {
    float currentReading = sensor.getCurrentAC() - zeroLoadOffset; // Apply Offset
    currentReading = currentReading * 0.1; // Apply Scaling
    // Apply noise threshold
    if ((currentReading) < NOISE_THRESHOLD) {
        currentReading = 0.0;
    }

    return currentReading;
}

void connectToWiFi() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connecting to");
    lcd.setCursor(0, 1);
    lcd.print("WiFi...");
    Serial.println("Connecting to WiFi...");
    Blynk.begin(auth, ssid, pass);

    unsigned long startAttemptTime = millis();
    
    // Attempt to connect to WiFi
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 30000) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.println("\nWiFi Connected.");
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("WiFi Connected.");
    } else {
        wifiConnected = false;
        Serial.println("\nWiFi Connection Failed.");
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("WiFi Failed.");
        lcd.setCursor(0, 1);
        lcd.print("Offline Mode.");
    }
}