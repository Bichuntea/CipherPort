#include "app_config.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "password_manager_app.h"
#include "web_manager.h"
#include "vault_store.h"

static const char *TAG = "cipherport";

static void on_app_key(bsp_btn_t button, bsp_btn_ev_t event, void *context)
{
    (void)context;
    (void)password_manager_app_post_key(button, event);
}

void app_main(void)
{
    ESP_LOGI(TAG, "CipherPort starting; radios default off");
    /* Display bring-up must not depend on optional settings storage.  A stale
     * or protected NVS partition must not leave the panel at its reset line. */
    esp_err_t config_error = app_config_init();
    if (config_error != ESP_OK) {
        ESP_LOGW(TAG, "configuration storage unavailable (%s); using defaults",
                 esp_err_to_name(config_error));
    }
    (void)bsp_i2c_init();
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display initialization failed");
        return;
    }
    bsp_display_backlight(100);
    (void)bsp_battery_init();
    if (vault_store_init() != ESP_OK) {
        ESP_LOGW(TAG, "vault partition unavailable");
    }
    if (web_manager_init() != ESP_OK) {
        ESP_LOGW(TAG, "Web Manager unavailable; device remains offline");
    }
    if (bsp_button_init(on_app_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "button initialization failed");
        return;
    }
    if (bsp_lvgl_lock(1000)) {
        (void)password_manager_app_start();
        bsp_lvgl_unlock();
    }
}
