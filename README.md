# Pengukur Kebisingan Ruangan — ESP32 + MAX4466 + LCD I2C

Kamar kosmu berisik, tapi kamu tidak tahu seberapa berisik? Alat kecil
ini mengubah suara ruangan menjadi angka desibel (dB) yang bisa kamu
baca di layar. Mikrofon MAX4466 menangkap suara, ESP32 menghitung
kekerasannya, lalu LCD I2C menampilkan angkanya beserta statusnya:
Tenang, Normal, atau Bising. Begitu angka melewati 70 dB, buzzer
berbunyi dan LED merah menyala.

Video penjelasan (60 detik):
link menyusul setelah upload

## Penting: soal angka dB

Nilai dB di alat ini adalah **estimasi relatif, bukan SPL yang
terkalibrasi laboratorium**. Modul MAX4466 punya penguatan (gain) yang
bisa diubah lewat trimpot, dan tiap ruangan punya pantulan suara yang
berbeda. Angka ini cukup untuk membandingkan "ruangan ini lebih bising
dari tadi" dan untuk memicu alarm di atas ambang — bukan untuk
pengukuran akustik resmi. Untuk angka absolut yang akurat, kalibrasi
dengan sound level meter sungguhan lalu ubah konstanta
`OFFSET_KALIBRASI_DB` di awal sketch (naikkan bila angka di LCD terlalu
kecil, turunkan bila terlalu besar).

## Komponen

| Bagian | Keterangan |
|---|---|
| ESP32 DevKit | Otak alat ukur |
| Modul mikrofon MAX4466 | Mikrofon electret + penguat, output analog |
| LCD 16x2 I2C (alamat 0x27) | Menampilkan dB + status |
| Buzzer aktif HIGH | Berbunyi saat di atas ambang |
| LED merah + resistor 220 ohm | Menyala saat di atas ambang |

## Wiring

| Dari | Ke |
|---|---|
| MAX4466 OUT | ESP32 GPIO34 (ADC1) |
| MAX4466 VCC | ESP32 3V3 |
| MAX4466 GND | GND |
| LCD SDA | ESP32 GPIO21 |
| LCD SCL | ESP32 GPIO22 |
| LCD VCC | ESP32 VIN 5V |
| LCD GND | GND |
| Buzzer (+) | ESP32 GPIO25 |
| Buzzer (-) | GND |
| LED merah anoda | ESP32 GPIO26 via resistor 220 ohm |
| LED merah katoda | GND |

Skema lengkap: [schematic.png](schematic.png)

ESP32 dicatu lewat USB 5V. LCD VCC diambil dari VIN 5V (bukan dari
pin 3V3), MAX4466 dari 3V3. Semua GND digabung di satu rel.

## Cara kerja

Sinyal mikrofon dibaca selama 50 milidetik dalam dua tahap: pertama
dicari rata-rata DC-nya (titik tengah sinyal, sekitar 1,65 V dari
bias MAX4466), lalu dihitung nilai RMS selisih dari rata-rata itu.
RMS diubah ke desibel relatif dengan rumus
`dB = 20 × log10(RMS / 1 mV) + OFFSET_KALIBRASI_DB`.

| Angka dB | Status di LCD | Alarm |
|---|---|---|
| di bawah 50 dB | Tenang | mati |
| 50–70 dB | Normal | mati |
| di atas 70 dB | BISING! | buzzer + LED menyala |

Alarm memakai histeresis: menyala di atas 70 dB, baru mati lagi saat
angka turun di bawah 68 dB, supaya buzzer tidak hidup-mati terus di
sekitar ambang. Saat alarm aktif, buzzer berbunyi 3 bip pendek,
diam 2 detik, lalu mengulang selama ruangan masih bising. LED merah
menyala terus selama alarm aktif. LCD diperbarui tiap 500 milidetik
dan angka yang sama dikirim ke Serial Monitor.

## Konfigurasi (di awal sketch)

- `JENDELA_MS` (50) — lama sampling tiap pengukuran.
- `REFERENSI_RMS_V` (0,001) — acuan 1 mV untuk 0 dB relatif.
- `OFFSET_KALIBRASI_DB` (50,0) — geser angka dB supaya cocok dengan
  sound level meter; rentang wajar sekitar 40 sampai 60.
- `AMBANG_TENANG_DB` (50,0) — di bawah ini status Tenang.
- `AMBANG_BISING_DB` (70,0) — di atas ini status Bising + alarm.
- `AMBANG_LEPAS_DB` (68,0) — alarm mati di bawah nilai ini.
- `JUMLAH_BIP` (3) / `DURASI_BIP_MS` (150) /
  `JEDA_ANTAR_ALARM_MS` (2000) — pola bunyi buzzer.
- `INTERVAL_LCD_MS` (500) — seberapa sering LCD diperbarui.
- `ALAMAT_LCD` (0x27) — alamat I2C backpack LCD; kalau LCD tidak
  tampil, cek alamatnya dengan I2C scanner (umumnya 0x27 atau 0x3F).

Kalau angka di LCD selalu terlalu besar atau terlalu kecil, dua hal
yang bisa diubah: putar trimpot gain di modul MAX4466, atau ubah
`OFFSET_KALIBRASI_DB`. Kalau LCD blank, cek dulu alamat I2C-nya.

## Library yang dibutuhkan

- LiquidCrystal_I2C 1.1.2 — sudah disertakan di
  [libraries/LiquidCrystal_I2C.zip](libraries/LiquidCrystal_I2C.zip).
  Cara install: di Arduino IDE, Sketch → Include Library →
  Add .ZIP Library, lalu pilih file ZIP di atas.

## Compile

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 PengukurKebisinganMax4466
```

Terverifikasi 24 Nov 2026: sketch 325.997 byte (24% flash),
RAM 24.720 byte (7%), ESP32 core 4.0.0.

## Lisensi

MIT — lihat [LICENSE](LICENSE).
