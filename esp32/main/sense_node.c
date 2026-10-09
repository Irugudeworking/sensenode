/*
 * Flipper Sense ESP32-WROOM node.
 *
 * This is a deliberately bounded CSI application: it listens only on one
 * fixed 2.4 GHz channel and only accepts CSI from one allow-listed MAC. It
 * exports room-level activity confidence over a wired UART; it never stores
 * raw CSI, scans networks, identifies people, or sends data over a network.
 */
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define TAG "flipper_sense"
/* ESP32-WROOM backpacks expose UART0 (GPIO1/3) through Flipper pins 13/14. */
#define SENSE_UART UART_NUM_0
#define SENSE_BAUD 115200
#define STATUS_PERIOD_MS 250
#define FRAME_WINDOW_MS 1000

typedef struct {
    volatile bool armed;
    volatile bool calibrating;
    volatile uint32_t frames_total;
    volatile uint32_t calibration_frames;
    volatile float baseline;
    volatile float baseline_noise;
    volatile float last_magnitude;
    volatile float motion_ema;
} sense_state_t;

static sense_state_t s_state = {
    .armed = true,
    .calibrating = true,
    .baseline_noise = 1.0f,
};
static uint8_t s_peer_mac[6];

static bool parse_mac(const char* text, uint8_t out[6]) {
    unsigned value[6];
    if(sscanf(text, "%x:%x:%x:%x:%x:%x", &value[0], &value[1], &value[2], &value[3], &value[4], &value[5]) != 6)
        return false;
    for(size_t i = 0; i < 6; i++) {
        if(value[i] > 0xff) return false;
        out[i] = (uint8_t)value[i];
    }
    return true;
}

static uint8_t clamp_percent(float value) {
    if(value <= 0.0f) return 0;
    if(value >= 100.0f) return 100;
    return (uint8_t)value;
}

/* This callback is run by the Wi-Fi task. Keep it bounded and allocation-free. */
static void csi_callback(void* context, wifi_csi_info_t* info) {
    (void)context;
    if(!info || !info->buf || info->len < 4 || memcmp(info->mac, s_peer_mac, sizeof(s_peer_mac)) != 0)
        return;

    /* ESP-IDF CSI represents each subcarrier as signed imaginary/real bytes. */
    uint32_t total = 0;
    uint16_t count = 0;
    const uint16_t offset = info->first_word_invalid ? 4 : 0;
    for(uint16_t i = offset; i + 1 < info->len && count < 64; i += 2) {
        int im = info->buf[i];
        int re = info->buf[i + 1];
        /* L1 magnitude avoids slow floating-point square roots in the Wi-Fi task. */
        total += (uint32_t)((im < 0 ? -im : im) + (re < 0 ? -re : re));
        count++;
    }
    if(!count) return;

    const float magnitude = (float)total / (float)count;
    s_state.last_magnitude = magnitude;
    s_state.frames_total++;

    if(s_state.calibrating) {
        const uint32_t n = ++s_state.calibration_frames;
        const float delta = magnitude - s_state.baseline;
        s_state.baseline += delta / (float)n;
        const float absolute_delta = delta < 0.0f ? -delta : delta;
        s_state.baseline_noise += (absolute_delta - s_state.baseline_noise) / (float)n;
        if(n >= CONFIG_SENSE_CALIBRATION_FRAMES) {
            if(s_state.baseline_noise < 0.25f) s_state.baseline_noise = 0.25f;
            s_state.calibrating = false;
            ESP_LOGI(TAG, "empty-room baseline captured: %.2f +/- %.2f", (double)s_state.baseline, (double)s_state.baseline_noise);
        }
        return;
    }

    const float delta = magnitude - s_state.baseline;
    const float absolute_delta = delta < 0.0f ? -delta : delta;
    const float normalised = absolute_delta / s_state.baseline_noise;
    s_state.motion_ema = (s_state.motion_ema * 0.85f) + (normalised * 0.15f);
}

static void wifi_init_csi(void) {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_SENSE_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));

    wifi_csi_config_t csi_config = {
        .lltf_en = true,
        .htltf_en = true,
        .stbc_htltf2_en = true,
        .ltf_merge_en = true,
        .channel_filter_en = true,
        .manu_scale = false,
        .shift = false,
    };
    ESP_ERROR_CHECK(esp_wifi_set_csi_config(&csi_config));
    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(csi_callback, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_csi(true));
}

static void uart_init(void) {
    const uart_config_t config = {
        .baud_rate = SENSE_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(SENSE_UART, 256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(SENSE_UART, &config));
    ESP_ERROR_CHECK(uart_set_pin(SENSE_UART, CONFIG_SENSE_UART_TX_GPIO, CONFIG_SENSE_UART_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

static void send_status(void) {
    static uint32_t prior_frames = 0;
    const uint32_t frames = s_state.frames_total;
    const uint32_t delta_frames = frames - prior_frames;
    prior_frames = frames;
    const uint8_t quality = clamp_percent((float)delta_frames * (100.0f / 20.0f));
    const uint8_t motion = clamp_percent(s_state.motion_ema * 12.0f);
    /* Presence here means RF activity above the calibrated empty-room noise floor. */
    const uint8_t presence = (s_state.armed && !s_state.calibrating && motion >= 18) ? motion : 0;
    char message[64];
    int length = snprintf(
        message,
        sizeof(message),
        "SENSE,1,%u,%u,%u,%u,%u\n",
        s_state.armed ? 1U : 0U,
        s_state.calibrating ? 1U : 0U,
        presence,
        motion,
        quality);
    if(length > 0) uart_write_bytes(SENSE_UART, message, length);
}

static void start_calibration(void) {
    s_state.calibrating = true;
    s_state.calibration_frames = 0;
    s_state.baseline = 0.0f;
    s_state.baseline_noise = 1.0f;
    s_state.motion_ema = 0.0f;
}

static void control_task(void* ignored) {
    (void)ignored;
    char line[32] = {0};
    size_t used = 0;
    while(true) {
        uint8_t byte;
        if(uart_read_bytes(SENSE_UART, &byte, 1, pdMS_TO_TICKS(50)) == 1) {
            if(byte == '\n' || byte == '\r') {
                line[used] = '\0';
                if(strcmp(line, "CAL") == 0) start_calibration();
                else if(strcmp(line, "ARM 1") == 0) s_state.armed = true;
                else if(strcmp(line, "ARM 0") == 0) s_state.armed = false;
                else if(strcmp(line, "PING") == 0) send_status();
                used = 0;
            } else if(isprint(byte) && used + 1 < sizeof(line)) {
                line[used++] = (char)byte;
            } else {
                used = 0;
            }
        }
        send_status();
        vTaskDelay(pdMS_TO_TICKS(STATUS_PERIOD_MS));
    }
}

void app_main(void) {
    esp_err_t nvs = nvs_flash_init();
    if(nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);
    if(!parse_mac(CONFIG_SENSE_PEER_MAC, s_peer_mac)) {
        ESP_LOGE(TAG, "Invalid CONFIG_SENSE_PEER_MAC: %s", CONFIG_SENSE_PEER_MAC);
        return;
    }
    uart_init();
    wifi_init_csi();
    ESP_LOGI(TAG, "CSI only from " MACSTR " on channel %d", MAC2STR(s_peer_mac), CONFIG_SENSE_WIFI_CHANNEL);
    xTaskCreate(control_task, "sense_control", 3072, NULL, 5, NULL);
}
