// ============================================================
//  Pengukur Kebisingan Ruangan — ESP32 + MAX4466 + LCD I2C
//  ------------------------------------------------------------
//  Mikrofon MAX4466 menangkap suara ruangan, ESP32 menghitung
//  nilai RMS sinyalnya selama 50 ms lalu mengubahnya menjadi
//  angka desibel (dB) yang tampil di LCD I2C 16x2 beserta
//  statusnya: Tenang, Normal, atau Bising. Begitu angka melewati
//  ambang 70 dB, buzzer berbunyi dan LED merah menyala.
//
//  PENTING — soal angka dB:
//  Nilai dB di sini adalah ESTIMASI RELATIF, bukan SPL yang
//  terkalibrasi laboratorium. Modul MAX4466 punya penguatan
//  (gain) yang bisa diubah lewat trimpot, dan tiap ruangan
//  punya pantulan suara yang berbeda. Angka ini cukup untuk
//  membandingkan "ruangan ini lebih bising dari tadi", dan
//  ambang 70 dB-nya bisa digeser lewat konstanta di bawah.
//  Untuk angka absolut yang akurat, kalibrasi dengan sound
//  level meter sungguhan lalu ubah OFFSET_KALIBRASI_DB.
//
//  Wiring:
//    MAX4466 OUT -> GPIO34 (ADC1)   VCC -> 3V3   GND -> GND
//    LCD I2C   SDA -> GPIO21        SCL -> GPIO22
//              VCC -> 5V (VIN)     GND -> GND   alamat 0x27
//    Buzzer    (+) -> GPIO25        (-) -> GND   (aktif HIGH)
//    LED merah anoda -> GPIO26 via resistor 220 ohm -> GND
//
//  Library: LiquidCrystal_I2C 1.1.2
//  Board: ESP32 DevKit (esp32:esp32:esp32), core 4.0.0
// ============================================================

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <math.h>

// ---------------- pin ----------------
const uint8_t PIN_MIC     = 34;  // MAX4466 OUT (ADC1, input-only)
const uint8_t PIN_BUZZER  = 25;  // buzzer aktif HIGH
const uint8_t PIN_LED     = 26;  // LED merah via resistor 220 ohm
const uint8_t PIN_LCD_SDA = 21;  // I2C data
const uint8_t PIN_LCD_SCL = 22;  // I2C clock

// ---------------- konstanta pengukuran ----------------
const uint8_t  ADC_BIT          = 12;    // resolusi ADC ESP32
const float    TEGANGAN_REF     = 3.3f;  // tegangan referensi ADC (volt)
const uint16_t JENDELA_MS       = 50;    // lama sampling tiap pengukuran
const float    REFERENSI_RMS_V  = 0.001f; // 1 mV — acuan 0 dB relatif
// Geser nilai ini bila angka dB tidak cocok dengan sound level meter.
// Naikkan bila LCD menampilkan angka terlalu kecil, turunkan bila
// terlalu besar. Rentang wajar sekitar 40 sampai 60.
const float    OFFSET_KALIBRASI_DB = 50.0f;

// ---------------- ambang status (dB) ----------------
const float AMBANG_TENANG_DB = 50.0f;  // di bawah ini: Tenang
const float AMBANG_BISING_DB = 70.0f;  // di atas ini: Bising + alarm
// Alarm mati lagi saat angka turun di bawah nilai ini (histeresis,
// supaya buzzer tidak hidup-mati terus di sekitar ambang).
const float AMBANG_LEPAS_DB  = 68.0f;

// ---------------- konstanta tampilan & alarm ----------------
const uint16_t INTERVAL_LCD_MS     = 500;  // seberapa sering LCD di-update
const uint8_t  JUMLAH_BIP          = 3;    // bip per rangkaian alarm
const uint16_t DURASI_BIP_MS       = 150;  // lama satu bip
const uint16_t JEDA_ANTAR_BIP_MS   = 150;  // jeda antar bip
const uint16_t JEDA_ANTAR_ALARM_MS = 2000; // jeda antar rangkaian bip
const uint8_t  ALAMAT_LCD          = 0x27; // alamat I2C backpack LCD
const uint8_t  KOLOM_LCD           = 16;
const uint8_t  BARIS_LCD           = 2;

LiquidCrystal_I2C lcd(ALAMAT_LCD, KOLOM_LCD, BARIS_LCD);

float    desibelTerakhir = 0.0f;
uint32_t waktuLcdTerakhir = 0;
bool     alarmAktif = false;

// State machine bip buzzer (non-blocking, pakai millis).
uint8_t  bipKe = 0;
uint32_t waktuBipTerakhir = 0;
bool     buzzerBunyi = false;

// ---------------- ukur dB ----------------
// Baca sinyal mikrofon selama JENDELA_MS, hitung RMS komponen AC-nya
// (nilai rata-rata DC ~1,65 V dari bias MAX4466 dibuang dulu), lalu
// ubah ke desibel relatif: dB = 20 * log10(RMS / REFERENSI) + OFFSET.
float ukurDesibel() {
  uint32_t mulai = millis();
  uint32_t jumlahSampel = 0;
  uint32_t jumlahNilai = 0;

  // Tahap 1: cari rata-rata DC (titik tengah sinyal).
  while (millis() - mulai < JENDELA_MS) {
    jumlahNilai += analogRead(PIN_MIC);
    jumlahSampel++;
  }
  float rataRataDc = (float)jumlahNilai / (float)jumlahSampel;

  // Tahap 2: hitung RMS selisih dari rata-rata DC.
  mulai = millis();
  jumlahSampel = 0;
  float jumlahKuadrat = 0.0f;
  while (millis() - mulai < JENDELA_MS) {
    float selisih = (float)analogRead(PIN_MIC) - rataRataDc;
    jumlahKuadrat += selisih * selisih;
    jumlahSampel++;
  }
  float rmsAdc = sqrt(jumlahKuadrat / (float)jumlahSampel);
  float rmsVolt = rmsAdc * TEGANGAN_REF / ((1 << ADC_BIT) - 1);
  if (rmsVolt < 0.000001f) {
    rmsVolt = 0.000001f;  // jaga-jaga: log10(0) tidak terdefinisi
  }
  return 20.0f * log10(rmsVolt / REFERENSI_RMS_V) + OFFSET_KALIBRASI_DB;
}

// ---------------- status ruangan ----------------
const char* statusDariDesibel(float db) {
  if (db >= AMBANG_BISING_DB) {
    return "BISING!";
  }
  if (db >= AMBANG_TENANG_DB) {
    return "Normal";
  }
  return "Tenang";
}

// ---------------- tampilkan di LCD ----------------
void tampilkanLcd(float db) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Kebisingan:");
  lcd.setCursor(0, 1);
  lcd.print(db, 1);
  lcd.print(" dB ");
  lcd.print(statusDariDesibel(db));
}

// ---------------- alarm (buzzer + LED) ----------------
void perbaruiAlarm(float db, uint32_t sekarang) {
  // Histeresis: nyala di atas 70 dB, mati di bawah 68 dB.
  if (!alarmAktif && db >= AMBANG_BISING_DB) {
    alarmAktif = true;
    bipKe = 0;
    waktuBipTerakhir = sekarang;
    buzzerBunyi = false;
  } else if (alarmAktif && db < AMBANG_LEPAS_DB) {
    alarmAktif = false;
    digitalWrite(PIN_BUZZER, LOW);
    digitalWrite(PIN_LED, LOW);
    buzzerBunyi = false;
    return;
  }
  if (!alarmAktif) {
    return;
  }

  digitalWrite(PIN_LED, HIGH);  // LED menyala selama alarm aktif

  // Rangkaian 3 bip, lalu diam 2 detik, lalu ulang selama masih bising.
  if (bipKe < JUMLAH_BIP) {
    uint16_t jeda = buzzerBunyi ? DURASI_BIP_MS : JEDA_ANTAR_BIP_MS;
    if (sekarang - waktuBipTerakhir >= jeda) {
      buzzerBunyi = !buzzerBunyi;
      digitalWrite(PIN_BUZZER, buzzerBunyi ? HIGH : LOW);
      waktuBipTerakhir = sekarang;
      if (!buzzerBunyi) {
        bipKe++;
      }
    }
  } else if (sekarang - waktuBipTerakhir >= JEDA_ANTAR_ALARM_MS) {
    bipKe = 0;
    waktuBipTerakhir = sekarang;
  }
}

// ---------------- setup & loop ----------------
void setup() {
  Serial.begin(115200);
  analogReadResolution(ADC_BIT);

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED, LOW);

  Wire.begin(PIN_LCD_SDA, PIN_LCD_SCL);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Pengukur dB");
  lcd.setCursor(0, 1);
  lcd.print("MAX4466 + ESP32");
  delay(1500);
  lcd.clear();

  Serial.println("Pengukur kebisingan ruangan siap.");
  Serial.println("dB di sini estimasi relatif, bukan SPL terkalibrasi.");
}

void loop() {
  desibelTerakhir = ukurDesibel();
  uint32_t sekarang = millis();

  perbaruiAlarm(desibelTerakhir, sekarang);

  if (sekarang - waktuLcdTerakhir >= INTERVAL_LCD_MS) {
    waktuLcdTerakhir = sekarang;
    tampilkanLcd(desibelTerakhir);
    Serial.print("Kebisingan: ");
    Serial.print(desibelTerakhir, 1);
    Serial.print(" dB - ");
    Serial.println(statusDariDesibel(desibelTerakhir));
  }
}
