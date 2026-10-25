# İleri Gömülü Sistemler – Laboratuvar 3

**Öğrenci:** Berkay Derin – 2510140024  
**Ders:** İleri Gömülü Sistemler (Dr. Murat Sürücü)  
**Kart:** ESP32-S3-N16R8 (DevKitC-1 düzeni), işlemci saati 240 MHz  
**Sensör:** GY-521 modülü  
**Araçlar:** ESP-IDF v6.1, Windows  
**Tarih:** 25.10.2026

Lab 3'te üç seri haberleşme yolu denendi: UART, I²C ve SPI. Lojik analizör olmadığından her görevin çıktısı ESP-IDF monitöründe dosyaya kaydedildi; kayıtlar `lab3/olcum/` altında.

| Görev | Proje | Bağlantı | Kayıt |
|---|---|---|---|
| 1. UART geri döngü | `lab3/lab3_uart` | GPIO17 (TX) – GPIO18 (RX) – GPIO16 (UART2 RX) | `olcum/uart_kayit.txt` |
| 2. I²C sensör | `lab3/lab3_i2c` | 3V3–VCC, GND–GND, GPIO8–SDA, GPIO9–SCL | `olcum/i2c_kayit.txt` |
| 3. SPI geri döngü | `lab3/lab3_spi` | GPIO11 (MOSI) – GPIO13 (MISO), GPIO12 (SCLK) – GPIO14 | `olcum/spi_kayit.txt` |

## 1. UART geri döngü

UART1 115200 baud 8N1 ile her turda bir satır metin gönderir ve kendi RX ucundan geri okur. Aynı hat UART2'ye de bağlı; UART2 her turda sırayla 115200, 112000, 105000, 57600 ve 9600 baud'a ayarlanarak "alıcı yanlış hızda" durumu gözlenir.

- **Geri döngü:** 19 turun 19'unda gönderilen ve alınan metin aynı (EŞLEŞTİ). Gönderim süresi 2,81–2,91 ms ölçüldü; 32 bayt × 10 bit / 115200 = 2,78 ms hesabına yakın.
- **Yanlış baud** (gönderilen 32–33 bayt):

| UART2 hızı | Fark | Alınan bayt | Sonuç |
|---|---|---|---|
| 115200 | %0 | 32–33 | metin doğru |
| 112000 | −%2,8 | 32–33 | metin doğru |
| 105000 | −%8,9 | 25–26 | tamamen bozuk |
| 57600 | −%50 | 16 | tamamen bozuk |
| 9600 | −%91,7 | 3 | tamamen bozuk |

UART her baytı start bitiyle yeniden eşitlediği için küçük hız farkları birikmez. %2,8'lik fark, 10 bitlik bir çerçevede son bitin örnekleme noktasını yarım bitten az kaydırdığı için tolere edildi. %8,9'da kayma yarım biti aştı ve baytlar bozuldu. Yavaş alıcı birden fazla biti tek bit sandığı için daha az bayt aldı.

## 2. I²C sensör

Program veri yolunu tarar, 0x50 adresiyle bir NACK örneği gösterir, WHO_AM_I'yi okur, sensörü uykudan çıkarır ve `xTaskDelayUntil` ile saniyede 10 kez 14 bayt (ivme, sıcaklık, dönü) okur.

- **Tarama:** Yalnızca 0x68 adresinde cihaz bulundu. 0x50 adresine yoklama NACK aldı (`ESP_ERR_NOT_FOUND`).
- **WHO_AM_I = 0x70.** MPU-6050'de bu değer 0x68'dir. 0x70, modülde uyumlu bir sensör (MPU-6500) olduğunu gösterir. İvme ve dönü registerları aynı adreslerde olduğu için okuma sorunsuz çalıştı.
- **PWR_MGMT_1 = 0x00** (uyku kapalı).
- **Okuma:** 213 okuma, 0 hata. 14 baytlık okuma 1,69–1,74 ms sürdü (100 kHz'te yaklaşık 156 bit, 1,6 ms hesaplanır).
- **İvme:** ax ≈ 1,01 g, ay ≈ 0,02 g, az ≈ 0,05 g; |a| ≈ 1,01 g. Modül dik tutulduğu için yerçekimi x ekseninde görülüyor.
- **Sıcaklık:** Kod MPU-6050 formülünü (ham/340 + 36,53) kullandığı için 48,7 °C gösterdi. MPU-6500'ün formülüyle (ham/333,87 + 21) aynı ham değer yaklaşık 33 °C eder; doğru değer budur.
- **Dönü:** Hareketsizken birkaç °/s'lik sabit kayma (bias) görüldü; kalibrasyon yapılmadı.

Modülün pinleri lehimsiz olduğundan ilk denemelerde temas kopuk kopuk kuruldu. Taramada var olmayan adresler göründü ve "bus is still busy" hataları alındı. Kod bu yüzden 0x68 cevap verene kadar taramayı tekrarlayacak ve sensöre yazma başarısız olursa yeniden deneyecek şekilde düzeltildi. Kayıttaki ölçüm, temas sağlandıktan sonraki hatasız çalışmadır.

## 3. SPI geri döngü

SPI2 her modda (0–3) 1 MHz'de `A5 3C 0F F0` gönderir ve MISO'dan geri okur. SCLK'nin boştaki düzeyi GPIO14'ten okunur. Ardından mod 0'da 1, 5, 10 ve 20 MHz denenir.

- **Mod testi:** 10 turun hepsinde 4 modda da gönderilen = alınan (4/4 mod eşleşti). SCLK boşta: mod 0–1'de 0, mod 2–3'te 1; CPOL ile aynı.
- **Hız denemesi** (işlem başına süre, 10 tur ortalaması):

| Hız | Sonuç | Süre | Yalnız veri (32 bit) |
|---|---|---|---|
| 1 MHz | eşleşti | 72,6 µs | 32 µs |
| 5 MHz | eşleşti | 45,4 µs | 6,4 µs |
| 10 MHz | eşleşti | 41,6 µs | 3,2 µs |
| 20 MHz | eşleşti | 39,4 µs | 1,6 µs |

Hız arttıkça süre 32 bitlik aktarım kadar kısalmıyor; her işlemde yaklaşık 38 µs'lik sabit bir gider kalıyor. Bu, sürücünün işlemi hazırlama ve bitirme adımlarıdır; program her ölçümden önce cihazı yeniden eklediği için SPI ayarlarının donanıma yüklenmesi de bu süreye giriyor.

## Derleme

```
cd lab3/lab3_uart
idf.py set-target esp32s3
idf.py -p COMx flash monitor
```

Diğer görevler için klasör adını `lab3_i2c` ya da `lab3_spi` yapın.

---

Not: Kod yazarken yapay zekâdan yardım alınmıştır.
