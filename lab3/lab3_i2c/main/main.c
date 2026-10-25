/*
 * İleri Gömülü Sistemler - Laboratuvar 3, Görev 2 (ESP32-S3)
 * I2C: veri yolu taraması, MPU-6050 WHO_AM_I okuma, sensörü uykudan çıkarma
 * ve ivme verisini saniyede 10 kez yazdırma.
 *
 * Bağlantı: MPU-6050 VCC -> 3V3 (J1-1), GND -> GND, SDA -> GPIO8 (J1-12),
 *           SCL -> GPIO9 (J1-15). Modül 5V'a bağlanmaz.
 */
#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "esp_log.h"

#define PIN_SDA          8
#define PIN_SCL          9
#define I2C_HIZ_HZ       100000
#define MPU_ADRES        0x68
#define REG_WHO_AM_I     0x75
#define REG_PWR_MGMT_1   0x6B
#define REG_ACCEL_XOUT_H 0x3B
#define IVME_LSB_G       16384.0f   /* ±2 g aralığı */
#define DONU_LSB_DPS     131.0f     /* ±250 °/s aralığı */
#define OKUMA_HZ         10
#define BOS_ADRES        0x50       /* NACK örneği için kullanılmayan bir adres */

static const char *TAG = "lab3_i2c";

static i2c_master_bus_handle_t veri_yolu;
static i2c_master_dev_handle_t mpu;

static int veri_yolunu_tara(void)
{
    int bulunan = 0;
    printf("I2C taraması (0x08-0x77):\n");
    for (uint16_t a = 0x08; a < 0x78; a++) {
        if (i2c_master_probe(veri_yolu, a, 50) == ESP_OK) {
            const char *ad = (a == 0x68 || a == 0x69) ? " (MPU-6050)" :
                             (a == 0x76 || a == 0x77) ? " (BME280?)" :
                             (a == 0x3C || a == 0x3D) ? " (SSD1306?)" : "";
            printf("  cihaz bulundu: 0x%02X%s\n", a, ad);
            bulunan++;
        }
    }
    printf("  toplam %d cihaz\n", bulunan);
    return bulunan;
}

static esp_err_t reg_oku(uint8_t reg, uint8_t *veri, size_t n)
{
    /* Yaz (register adresi) + tekrarlı START + oku */
    return i2c_master_transmit_receive(mpu, &reg, 1, veri, n, 100);
}

static esp_err_t reg_yaz(uint8_t reg, uint8_t deger)
{
    uint8_t buf[2] = { reg, deger };
    return i2c_master_transmit(mpu, buf, sizeof(buf), 100);
}

static int16_t s16(const uint8_t *p)
{
    return (int16_t)((p[0] << 8) | p[1]);
}

void app_main(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,                       /* otomatik seç */
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, /* modüldeki dış pull-up'lara ek */
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &veri_yolu));
    ESP_LOGI(TAG, "I2C: SDA=GPIO%d SCL=GPIO%d, %d kHz", PIN_SDA, PIN_SCL, I2C_HIZ_HZ / 1000);

    /* 1. Tarama: her adrese adres baytı gönderilir, ACK gelirse cihaz vardır */
    while (veri_yolunu_tara() == 0 || i2c_master_probe(veri_yolu, MPU_ADRES, 50) != ESP_OK) {
        printf("  Cihaz yok. SDA/SCL yer değiştirmiş, sensör beslenmiyor ya da pinler lehimsiz olabilir.\n");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    /* NACK örneği: kullanılmayan adres */
    esp_err_t r = i2c_master_probe(veri_yolu, BOS_ADRES, 50);
    printf("Adres 0x%02X: %s (%s)\n", BOS_ADRES,
           r == ESP_OK ? "ACK geldi" : "ACK gelmedi, bu adreste cihaz yok", esp_err_to_name(r));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPU_ADRES,
        .scl_speed_hz = I2C_HIZ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(veri_yolu, &dev_cfg, &mpu));

    /* 2. WHO_AM_I */
    uint8_t kim = 0;
    r = reg_oku(REG_WHO_AM_I, &kim, 1);
    printf("WHO_AM_I (0x%02X) = 0x%02X, beklenen 0x68 -> %s\n", REG_WHO_AM_I, kim,
           (r == ESP_OK && kim == 0x68) ? "DOĞRU" : "FARKLI");
    if (r != ESP_OK) {
        printf("  Okuma hatası: %s. 0x68 adresinde cihaz yok mu? AD0 pini 3V3'e bağlıysa adres 0x69'dur.\n",
               esp_err_to_name(r));
    } else if (kim != 0x68) {
        printf("  Not: 0x70 MPU-6500, 0x71/0x73 MPU-9250/9255 demektir; bazı klon MPU-6050'ler de farklı değer döndürür.\n");
    }

    /* 3. Uykudan çıkar: PWR_MGMT_1 = 0 */
    while (reg_yaz(REG_PWR_MGMT_1, 0x00) != ESP_OK) { printf("PWR_MGMT_1 yazilamadi, tekrar deneniyor\n"); vTaskDelay(pdMS_TO_TICKS(500)); }
    uint8_t pwr = 0xFF;
    reg_oku(REG_PWR_MGMT_1, &pwr, 1);
    printf("PWR_MGMT_1 = 0x%02X (0x00: uyku modu kapalı)\n", pwr);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* 4. Saniyede 10 kez ivme (ve dönü) verisi */
    printf("\n   t(ms)       ax       ay       az |   ax(g)   ay(g)   az(g) | T(°C) |  gx(°/s)  gy(°/s)  gz(°/s) | okuma(us)\n");
    TickType_t son = xTaskGetTickCount();
    uint32_t satir = 0, hata = 0;
    for (;;) {
        uint8_t b[14];
        int64_t t0 = esp_timer_get_time();
        r = reg_oku(REG_ACCEL_XOUT_H, b, sizeof(b));
        int64_t sure = esp_timer_get_time() - t0;
        if (r == ESP_OK) {
            int16_t ax = s16(&b[0]), ay = s16(&b[2]), az = s16(&b[4]);
            int16_t tr = s16(&b[6]);
            int16_t gx = s16(&b[8]), gy = s16(&b[10]), gz = s16(&b[12]);
            printf("%8" PRId64 " %8d %8d %8d | %7.3f %7.3f %7.3f | %5.1f | %8.2f %8.2f %8.2f | %6" PRId64 "\n",
                   t0 / 1000, ax, ay, az,
                   ax / IVME_LSB_G, ay / IVME_LSB_G, az / IVME_LSB_G,
                   tr / 340.0f + 36.53f,
                   gx / DONU_LSB_DPS, gy / DONU_LSB_DPS, gz / DONU_LSB_DPS, sure);
        } else {
            hata++;
            printf("okuma hatası: %s (toplam %" PRIu32 ")\n", esp_err_to_name(r), hata);
        }
        if (++satir % 50 == 0) {
            printf("--- %" PRIu32 " okuma, %" PRIu32 " hata. 14 bayt okuma ~%d bit eder, 100 kHz'te ~%.1f ms ---\n",
                   satir, hata, (1 + 1 + 1 + 14) * 9 + 3, ((1 + 1 + 1 + 14) * 9 + 3) / 100.0);
        }
        xTaskDelayUntil(&son, pdMS_TO_TICKS(1000 / OKUMA_HZ));
    }
}
