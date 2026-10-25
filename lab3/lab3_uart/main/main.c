/*
 * İleri Gömülü Sistemler - Laboratuvar 3, Görev 1 (ESP32-S3)
 * UART geri döngü: UART1'in TX'i (GPIO17) kendi RX'ine (GPIO18) bağlanır.
 * Aynı hat ayrıca UART2'nin RX'ine (GPIO16) verilir; UART2 her turda farklı
 * bir baud hızına ayarlanarak "alıcı yanlış baud hızında" durumu gözlenir.
 *
 * Bağlantı (aynı breadboard satırında):
 *   GPIO17 (J1-10, U1TXD) ── GPIO18 (J1-11, U1RXD) ── GPIO16 (J1-9, UART2 RX)
 * GPIO16 bağlı değilse yalnızca doğru hızlı geri döngü testi çalışır.
 */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_log.h"

#define UART_VERICI     UART_NUM_1
#define UART_YANLIS     UART_NUM_2
#define PIN_TX          17
#define PIN_RX          18
#define PIN_RX_YANLIS   16
#define DOGRU_BAUD      115200
#define TAMPON          1024
#define BEKLEME_MS      100

static const char *TAG = "lab3_uart";

/* UART2'nin sırayla deneyeceği alıcı hızları. İlki doğru hızdır. */
static const uint32_t ALICI_HIZLARI[] = { 115200, 112000, 105000, 57600, 9600 };
#define HIZ_SAYISI (sizeof(ALICI_HIZLARI) / sizeof(ALICI_HIZLARI[0]))

static void uart_kur(uart_port_t no, uint32_t baud, int tx, int rx)
{
    uart_config_t cfg = {
        .baud_rate  = (int)baud,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(no, TAMPON, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(no, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(no, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

/* Alınan baytları okunur metne çevirir: yazdırılamayan baytlar \xHH olur. */
static void kacisli_yaz(char *hedef, size_t boy, const uint8_t *veri, int n)
{
    size_t k = 0;
    for (int i = 0; i < n && k + 5 < boy; i++) {
        uint8_t c = veri[i];
        if (c == '\r') {
            k += snprintf(hedef + k, boy - k, "\\r");
        } else if (c == '\n') {
            k += snprintf(hedef + k, boy - k, "\\n");
        } else if (c >= 0x20 && c < 0x7F) {
            hedef[k++] = (char)c;
        } else {
            k += snprintf(hedef + k, boy - k, "\\x%02X", c);
        }
    }
    hedef[k] = '\0';
}

static int fark_say(const uint8_t *a, int na, const uint8_t *b, int nb)
{
    int n = na > nb ? na : nb, fark = 0;
    for (int i = 0; i < n; i++) {
        if (i >= na || i >= nb || a[i] != b[i]) fark++;
    }
    return fark;
}

void app_main(void)
{
    /* UART2 önce kurulur; UART1 RX ile aynı hattı dinlemesi için ayrı pin kullanılır */
    uart_kur(UART_YANLIS, ALICI_HIZLARI[0], UART_PIN_NO_CHANGE, PIN_RX_YANLIS);
    uart_kur(UART_VERICI, DOGRU_BAUD, PIN_TX, PIN_RX);

    ESP_LOGI(TAG, "UART1: TX=GPIO%d RX=GPIO%d, %d baud 8N1", PIN_TX, PIN_RX, DOGRU_BAUD);
    ESP_LOGI(TAG, "UART2: RX=GPIO%d (yanlış baud denemesi için)", PIN_RX_YANLIS);
    ESP_LOGI(TAG, "1 bit = %.2f us, 8N1 ile 1 bayt = 10 bit = %.2f us",
             1e6 / DOGRU_BAUD, 10e6 / DOGRU_BAUD);

    static uint8_t gelen1[TAMPON], gelen2[TAMPON];
    static char metin[TAMPON * 4];
    char mesaj[64];

    for (uint32_t tur = 1;; tur++) {
        int n = snprintf(mesaj, sizeof(mesaj), "Lab3 UART #%" PRIu32 ": Merhaba ESP32-S3\r\n", tur);
        kacisli_yaz(metin, sizeof(metin), (const uint8_t *)mesaj, n);
        printf("\n===== Tur %" PRIu32 " | gönderilen %d bayt: \"%s\"\n", tur, n, metin);

        for (size_t h = 0; h < HIZ_SAYISI; h++) {
            uint32_t alici = ALICI_HIZLARI[h];
            ESP_ERROR_CHECK(uart_set_baudrate(UART_YANLIS, alici));
            uart_flush_input(UART_VERICI);
            uart_flush_input(UART_YANLIS);

            int64_t t0 = esp_timer_get_time();
            uart_write_bytes(UART_VERICI, mesaj, n);
            ESP_ERROR_CHECK(uart_wait_tx_done(UART_VERICI, pdMS_TO_TICKS(1000)));
            int64_t gonderim_us = esp_timer_get_time() - t0;

            vTaskDelay(pdMS_TO_TICKS(BEKLEME_MS));
            int n1 = uart_read_bytes(UART_VERICI, gelen1, sizeof(gelen1), 0);
            int n2 = uart_read_bytes(UART_YANLIS, gelen2, sizeof(gelen2), 0);
            if (n1 < 0) n1 = 0;
            if (n2 < 0) n2 = 0;

            if (h == 0) {
                /* Doğru hızlı geri döngü: UART1 kendi gönderdiğini okur */
                int f1 = fark_say((const uint8_t *)mesaj, n, gelen1, n1);
                printf("UART1 geri döngü (%d baud): %d bayt alındı, farklı bayt: %d -> %s\n",
                       DOGRU_BAUD, n1, f1, f1 == 0 ? "EŞLEŞTİ" : "EŞLEŞMEDİ");
                printf("  gönderim süresi: ölçülen %" PRId64 " us, beklenen %.0f us (%d bayt x 10 bit)\n",
                       gonderim_us, n * 10e6 / DOGRU_BAUD, n);
                if (n1 == 0) {
                    printf("  UYARI: hiç bayt gelmedi. GPIO17-GPIO18 kablosunu kontrol edin.\n");
                }
            }

            double sapma = 100.0 * ((double)alici - DOGRU_BAUD) / DOGRU_BAUD;
            int f2 = fark_say((const uint8_t *)mesaj, n, gelen2, n2);
            kacisli_yaz(metin, sizeof(metin), gelen2, n2);
            printf("UART2 alıcı %6" PRIu32 " baud (fark %+6.1f%%): %3d bayt, farklı %3d -> \"%s\"\n",
                   alici, sapma, n2, f2, metin);
        }
        if (tur == 1) {
            printf("\nNot: UART2'ye hiç bayt gelmiyorsa GPIO16 aynı satıra bağlı değildir.\n");
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
