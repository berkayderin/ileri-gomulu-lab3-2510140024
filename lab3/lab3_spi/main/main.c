/*
 * İleri Gömülü Sistemler - Laboratuvar 3, Görev 3 (ESP32-S3)
 * SPI geri döngü: MOSI (GPIO11) bir kabloyla MISO'ya (GPIO13) bağlanır.
 * 4 bayt gönderilir ve alınanla karşılaştırılır; mod 0..3 sırayla denenir.
 * Lojik analizör olmadığından saatin boşta kalma düzeyi (CPOL), SCLK'ye
 * bağlanan bir giriş pininden (GPIO14) okunur.
 *
 * Bağlantı:
 *   GPIO11 (J1-17, MOSI) ── GPIO13 (J1-19, MISO)    zorunlu
 *   GPIO12 (J1-18, SCLK) ── GPIO14 (J1-20)          isteğe bağlı, CPOL gözlemi
 *   GPIO10 (J1-16) CS, bağlantı gerekmez
 */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#define SPI_BIRIM     SPI2_HOST
#define PIN_MOSI      11
#define PIN_MISO      13
#define PIN_SCLK      12
#define PIN_CS        10
#define PIN_SCLK_IZLE 14

static const char *TAG = "lab3_spi";

static const uint8_t TX[4] = { 0xA5, 0x3C, 0x0F, 0xF0 };
static const int HIZLAR_HZ[] = { 1000000, 5000000, 10000000, 20000000 };

/* Bir aygıtı verilen mod ve hızla ekler, 4 bayt gönderir, sonucu yazar. */
static bool dene(int mod, int hiz_hz, bool cpol_yaz)
{
    spi_device_interface_config_t dev = {
        .clock_speed_hz = hiz_hz,
        .mode = (uint8_t)mod,          /* bit1 = CPOL, bit0 = CPHA */
        .spics_io_num = PIN_CS,
        .queue_size = 1,
    };
    spi_device_handle_t h;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI_BIRIM, &dev, &h));

    uint8_t rx[4] = { 0 };
    spi_transaction_t t = {
        .length = 8 * sizeof(TX),      /* bit cinsinden */
        .tx_buffer = TX,
        .rx_buffer = rx,
    };
    int64_t t0 = esp_timer_get_time();
    ESP_ERROR_CHECK(spi_device_polling_transmit(h, &t));
    int64_t sure = esp_timer_get_time() - t0;

    bool esit = memcmp(TX, rx, sizeof(TX)) == 0;
    printf("mod %d (CPOL=%d CPHA=%d) %5.1f MHz | tx: %02X %02X %02X %02X | rx: %02X %02X %02X %02X | %s | %3" PRId64 " us",
           mod, (mod >> 1) & 1, mod & 1, hiz_hz / 1e6,
           TX[0], TX[1], TX[2], TX[3], rx[0], rx[1], rx[2], rx[3],
           esit ? "EŞLEŞTİ  " : "EŞLEŞMEDİ", sure);
    if (cpol_yaz) {
        /* İşlem bitti, hat boşta: SCLK'nin düzeyi CPOL'u gösterir */
        int duzey = gpio_get_level(PIN_SCLK_IZLE);
        printf(" | SCLK boşta = %d (beklenen %d)", duzey, (mod >> 1) & 1);
    }
    printf("\n");

    ESP_ERROR_CHECK(spi_bus_remove_device(h));
    return esit;
}

void app_main(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_BIRIM, &bus, SPI_DMA_CH_AUTO));

    gpio_config_t izle = {
        .pin_bit_mask = 1ULL << PIN_SCLK_IZLE,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&izle));

    ESP_LOGI(TAG, "SPI2: MOSI=GPIO%d MISO=GPIO%d SCLK=GPIO%d CS=GPIO%d", PIN_MOSI, PIN_MISO, PIN_SCLK, PIN_CS);
    ESP_LOGI(TAG, "SCLK izleme: GPIO%d (GPIO%d'ye bağlı değilse 'SCLK boşta' değeri anlamsızdır)", PIN_SCLK_IZLE, PIN_SCLK);

    for (uint32_t tur = 1;; tur++) {
        printf("\n===== Tur %" PRIu32 " =====\n", tur);
        printf("-- Mod karşılaştırması, 1 MHz --\n");
        int esit = 0;
        for (int mod = 0; mod < 4; mod++) {
            esit += dene(mod, 1000000, true);
        }
        printf("   %d/4 mod eşleşti\n", esit);

        printf("-- Hız denemesi, mod 0 --\n");
        for (size_t i = 0; i < sizeof(HIZLAR_HZ) / sizeof(HIZLAR_HZ[0]); i++) {
            dene(0, HIZLAR_HZ[i], false);
        }
        printf("Beklenen süre: 32 bit / hız (1 MHz'te 32 us) + sürücünün sabit gideri\n");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
