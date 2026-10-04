// WHY2025 screen backlight on the ESP32-C6; ESP-Hosted CustomRpc ID "why1".
#include "driver/ledc.h"
#include "esp_hosted_peer_data.h"
#include "esp_log.h"
#include "esp_err.h"
#define WHY_BACKLIGHT_MSG 0x77687931u
#define WHY_KEYBOARD_MSG 0x77687932u
static void brightness_request(uint32_t id, const uint8_t *data, size_t len, void *ctx) {
    if (len != 1 || data[0] > 100 || (id == WHY_BACKLIGHT_MSG && data[0] < 5)) return;
    ledc_channel_t channel = id == WHY_KEYBOARD_MSG ? LEDC_CHANNEL_1 : LEDC_CHANNEL_0;
    // Stock WHY2025 firmware clamps the 8-bit screen duty to 10..80.
    // Lower raw duties were tested on this badge: dropout and severe flicker.
    // Map the UI range to the working electrical range, rather than 0..255.
    uint32_t duty = id == WHY_BACKLIGHT_MSG
        ? 10 + ((uint32_t)(data[0] - 5) * 70 + 47) / 95
        : ((uint32_t)data[0] * 255 + 50) / 100;
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, channel, duty);
    if (err == ESP_OK) err = ledc_update_duty(LEDC_LOW_SPEED_MODE, channel);
    ESP_LOGI("why_backlight", "Brightness %u%%: %s", data[0], esp_err_to_name(err));
    uint8_t reply[3] = {id == WHY_KEYBOARD_MSG, data[0], err == ESP_OK ? (uint8_t)((ledc_get_duty(LEDC_LOW_SPEED_MODE, channel)*100+127)/255) : 255};
    esp_hosted_send_custom_data(0x77687933u, reply, sizeof(reply));
}
void why_backlight_init(void) {
    ledc_timer_config_t timer = {
        .speed_mode=LEDC_LOW_SPEED_MODE, .duty_resolution=LEDC_TIMER_8_BIT,
        .timer_num=LEDC_TIMER_0, .freq_hz=25000, .clk_cfg=LEDC_USE_XTAL_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t channel = {
        .gpio_num=15, .speed_mode=LEDC_LOW_SPEED_MODE, .channel=LEDC_CHANNEL_0,
        .intr_type=LEDC_INTR_DISABLE, .timer_sel=LEDC_TIMER_0, .duty=80, .hpoint=0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
    channel.gpio_num=10;
    channel.channel=LEDC_CHANNEL_1;
    channel.duty=51;
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
    ESP_ERROR_CHECK(esp_hosted_register_custom_callback(WHY_BACKLIGHT_MSG, brightness_request, NULL));
    ESP_ERROR_CHECK(esp_hosted_register_custom_callback(WHY_KEYBOARD_MSG, brightness_request, NULL));
}
