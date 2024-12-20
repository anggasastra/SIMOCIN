//kode 2 dengan milis
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_SHT31.h>
#include <HX711.h>
#include <ESP32Servo.h>

// Objek Firebase
FirebaseData firebaseData;
FirebaseConfig config;
FirebaseAuth auth;

// Pengaturan WiFi
const char* ssid = "Ranie";
const char* password = "kamunanya";

unsigned long lastPressTime = 0; // Waktu terakhir tombol ditekan
const unsigned long debounceDelay = 200; // Delay debounce dalam milidetik

// Servo
Servo myServo;
const int servoPin = 1;
int positionIndex = 0;
int servoAngle = 0;
const int maxAngle = 90;

// Konfigurasi pin sensor
const int pinVoltageSensor1 = 4; // Sensor 1 (terhubung ke baterai)
const int pinVoltageSensor2 = 5; // Sensor 2 (terhubung ke daya DC)
const int relayPin = 6; // Pin untuk relay
const int batteryPin = 14; // Pin untuk membaca tegangan baterai

// Kalibrasi tegangan sensor (sesuaikan jika perlu)
const float calibrationFactor = 5.0 / 4095.0; // Asumsi ADC 12-bit pada ESP32
const float fullBatteryVoltage = 4.2;
const float adcMaxValue = 4095.0; // Nilai maksimum ADC pada ESP32 (12-bit ADC)
const float referenceVoltage = 3.3; // Tegangan referensi ESP32 (biasanya 3.3V)
float voltage1 = 0.0;
float voltage2 = 0.0;

// Pengaturan OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Pin untuk HX711
const int LOADCELL_DOUT_PIN = 7;
const int LOADCELL_SCK_PIN = 12;
HX711 scale;

// Variabel Load Cell
float calibration_factor = -7050;
float weight = 0.0;
float rawValue = 0.0;

// Pengaturan SHT31
Adafruit_SHT31 sht31 = Adafruit_SHT31();
float suhu = 0.0;
float kelembaban = 0.0;

// Definisikan pin push button
const int PB1 = 35; //atas
const int PB2 = 36; //bawah
const int PB3 = 37; //ok

// Pin untuk sensor IR
const int irPinTetes = 15;

// Pin untuk mikroswitch (terhubung dengan infus)
const int microSwitchPin = 45; // Ganti dengan pin yang sesuai
bool lastInfusStatus = false;

// Variabel untuk menghitung kecepatan tetesan
unsigned long lastTime = 0;
unsigned long interval = 0;
int lastTetesPerMenit = -1;  // Variabel untuk menyimpan nilai kecepatan tetes sebelumnya
bool flag = false;

// Variabel kontrol menu
int currentMenu = 0;
int menuSelection = 0;
int tetesPerMenit = 10;

// Variabel kalibrasi Load Cell
bool isCalibrating = false;

// Tombol servo
bool buttonUpPressed = false;
bool buttonDownPressed = false;

// Deklarasi fungsi sebelum setup
void showMainMenu();
void showMonitoringMenu();
void showControllingMenu();

void setup() {
  Serial.begin(115200);

  // Konfigurasi FirebaseF
  config.database_url = "https://simocin-f76e2-default-rtdb.firebaseio.com/";
  config.signer.tokens.legacy_token = "8LM7kJnoaaTiTHU30MsP3SaDyZV06KUtSrRtKA4Z";

  // Inisialisasi Firebase
  Firebase.begin(&config, &auth);

  // Koneksi ke WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Sukses terkoneksi wifi!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Inisialisasi I2C untuk ESP32-S3 dengan SDA di GPIO 8 dan SCL di GPIO 9
  Wire.begin(8, 9);

  // Inisialisasi servo
  myServo.attach(servoPin);
  myServo.write(servoAngle);

  // Inisialisasi OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED tidak terdeteksi!"));
    while (true);
  }

  display.clearDisplay();
  display.display();

  // Inisialisasi SHT31
  if (!sht31.begin(0x44)) {
    Serial.println("Sensor SHT31 tidak ditemukan!");
    while (true);
  }
  // Inisialisasi pin mikroswitch
  pinMode(microSwitchPin, INPUT_PULLUP);  // Mikroswitch biasanya terhubung ke ground saat tertekan
  
  // Inisialisasi HX711
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);
  scale.tare();

  // Inisialisasi pin sensor tegangan
  pinMode(pinVoltageSensor1, INPUT);
  pinMode(pinVoltageSensor2, INPUT);
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW); // Matikan relay awalnya

  // Atur pin button sebagai input
  pinMode(PB1, INPUT_PULLUP);
  pinMode(PB2, INPUT_PULLUP);
  pinMode(PB3, INPUT_PULLUP);

  // Atur pin sensor IR sebagai input
  pinMode(irPinTetes, INPUT);

  // Menampilkan pesan awal
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("OLED Siap");
  display.display();
  delay(2000);
  display.clearDisplay();
}

void loop() {
  // Baca suhu dan kelembaban dari SHT31
  suhu = sht31.readTemperature();
  kelembaban = sht31.readHumidity();

  if (isnan(suhu) || isnan(kelembaban)) {
    Serial.println("Gagal membaca sensor SHT31");
  } else {
    // Kirim data suhu ke Firebase
    Firebase.RTDB.setFloat(&firebaseData, "/SHT31/suhu", suhu);
    Firebase.RTDB.setFloat(&firebaseData, "/SHT31/kelembaban", kelembaban);
  }

    // Membaca status mikroswitch
  bool infusStatus = digitalRead(microSwitchPin) == LOW; // LOW berarti mikroswitch aktif (infus terpasang)

  // Jika status berubah, kirimkan notifikasi ke Firebase
  if (infusStatus != lastInfusStatus) {
    if (infusStatus) {
      Serial.println("Infus Terpasang");
      Firebase.RTDB.setString(&firebaseData, "/Infus", "Terpasang");
    } else {
      Serial.println("Infus Terlepas");
      Firebase.RTDB.setString(&firebaseData, "/Infus", "Terlepas");
    }

    // Perbarui status terakhir
    lastInfusStatus = infusStatus;
  }

  weight = scale.get_units(10); // Rata-rata 10 pembacaan
  // Kirim data berat ke Firebase di path /HX711
  Firebase.RTDB.setFloat(&firebaseData, "/HX711", weight);

  // Baca nilai dari kedua voltage sensor
  int rawValue1 = analogRead(pinVoltageSensor1);
  int rawValue2 = analogRead(pinVoltageSensor2);

  // Konversi nilai ADC ke tegangan
  voltage1 = rawValue1 * calibrationFactor;
  voltage2 = rawValue2 * calibrationFactor;
  //kirim data voltage
  Firebase.RTDB.setFloat(&firebaseData, "/voltage1", voltage1);
  Firebase.RTDB.setFloat(&firebaseData, "/voltage1", voltage2);

  // Kontrol relay berdasarkan tegangan baterai
  if (voltage1 >= fullBatteryVoltage) {
    Serial.println("Baterai penuh! Mematikan daya...");
    digitalWrite(relayPin, LOW); // Matikan relay
  } else {
    Serial.println("Baterai belum penuh. Mengisi daya...");
    digitalWrite(relayPin, HIGH); // Nyalakan relay
  }

  // Tampilkan menu sesuai dengan currentMenu
  if (currentMenu == 0) {
    showMainMenu();
  } else if (currentMenu == 1) {
    showMonitoringMenu();
  } else if (currentMenu == 2) {
    showControllingMenu();
  } else if (currentMenu == 3) {
    showCalibrationMenu();
  }

  // Navigasi di menu utama
  if (currentMenu == 0) {
    if (digitalRead(PB1) == LOW && millis() - lastPressTime > debounceDelay) {
        menuSelection = 0;
        lastPressTime = millis();
    }
    if (digitalRead(PB2) == LOW && millis() - lastPressTime > debounceDelay) {
        menuSelection = 1;
        lastPressTime = millis();
    }
    if (digitalRead(PB3) == LOW && millis() - lastPressTime > debounceDelay) {
        currentMenu = menuSelection == 0 ? 1 : (menuSelection == 1 ? 2 : 3);
        lastPressTime = millis();
    }
}

  if (currentMenu == 1 || currentMenu == 2 || currentMenu == 3) {
    if (digitalRead(PB3) == LOW && millis() - lastPressTime > debounceDelay) {
        currentMenu = 0;
        lastPressTime = millis();
    }
}


  // Kontrol servo di menu Kontroling
  if (currentMenu == 2) {
    if (digitalRead(PB1) == LOW && !buttonUpPressed) {
      buttonUpPressed = true;
      if (positionIndex < 9) {
        positionIndex++;
        servoAngle = map(positionIndex, 0, 9, 0, maxAngle);
        myServo.write(servoAngle);
        lastPressTime = millis();

      Firebase.RTDB.setFloat(&firebaseData, "/Servo", servoAngle);
      Serial.print("Posisi Servo (Naik): ");
      Serial.println(servoAngle);
      }
    } else if (digitalRead(PB1) == HIGH) {
      buttonUpPressed = false;
    }

    if (digitalRead(PB2) == LOW && !buttonDownPressed) {
      buttonDownPressed = true;
      if (positionIndex > 0) {
        positionIndex--;
        servoAngle = map(positionIndex, 0, 9, 0, maxAngle);
        myServo.write(servoAngle);
        lastPressTime = millis();

      Firebase.RTDB.setFloat(&firebaseData, "/Servo", servoAngle);
      Serial.print("Posisi Servo (Naik): ");
      Serial.println(servoAngle);
      }
    } else if (digitalRead(PB2) == HIGH) {
      buttonDownPressed = false;
    }
  }

  // Deteksi kecepatan tetesan infus
  int irStatusTetes = digitalRead(irPinTetes);
  if (irStatusTetes == LOW && !flag) {
    unsigned long currentTime = millis();
    if (lastTime > 0) {
      interval = currentTime - lastTime;
      tetesPerMenit = 60000 / interval;
      Serial.print("Kecepatan: ");
      Serial.print(tetesPerMenit);
      Serial.println(" tetes per menit");
          // Hanya kirim ke Firebase jika ada perubahan signifikan
      if (tetesPerMenit != lastTetesPerMenit) {
        Firebase.RTDB.setFloat(&firebaseData, "/IR1", tetesPerMenit);
        lastTetesPerMenit = tetesPerMenit;  // Simpan kecepatan terakhir yang dikirim
      }
    }
    lastTime = currentTime;
    flag = true;
  }
  if (irStatusTetes == HIGH && flag) {
    flag = false;
  }
}

void showMainMenu() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(2);
  display.println("SIMOCIN");
  display.setTextSize(1);

  if (menuSelection == 0) {
    display.setTextColor(BLACK, WHITE);
  }
  display.println("> 1. Monitoring");
  display.setTextColor(WHITE, BLACK);

  if (menuSelection == 1) {
    display.setTextColor(BLACK, WHITE);
  }
  display.println("> 2. Controlling");
  display.setTextColor(WHITE, BLACK);

  if (menuSelection == 2) {
    display.setTextColor(BLACK, WHITE);
  }
  display.println("> 3. Calibration");
  display.setTextColor(WHITE, BLACK);
  display.display();
}


void showMonitoringMenu() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println("Monitoring");
  display.print("Suhu: ");
  display.print(suhu);
  display.println(" C");
  display.print("Kelembaban: ");
  display.print(kelembaban);
  display.println(" %");
  display.print("Berat: ");
  display.print(weight);
  display.println(" g");
  display.display();
}

void showControllingMenu() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println("Kontrol Servo");
  display.print("Posisi: ");
  display.println(positionIndex);
  display.display();
}

void showCalibrationMenu() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);

  if (!isCalibrating) {
    display.println("Tekan PB3 untuk mulai");
    if (digitalRead(PB3) == LOW) {
      isCalibrating = true;
      scale.tare(); // Reset Load Cell
    }
  } else {
    rawValue = scale.get_units(10); // Baca rata-rata
    display.print("Nilai Mentah: ");
    display.println(rawValue);

    if (digitalRead(PB3) == LOW) {
      float knownWeight = 1000; // Misal 1 kg
      calibration_factor = rawValue / knownWeight;
      scale.set_scale(calibration_factor);
      display.println("Kalibrasi Selesai!");
      isCalibrating = false;
    }
  }

  display.display();
}