#pragma once

#include <stdbool.h>

#include "bsp_button.h"
#include "esp_err.h"

esp_err_t password_manager_app_start(void);
/* Non-blocking button callback entry point. UI work is drained by the LVGL task. */
bool password_manager_app_post_key(bsp_btn_t button, bsp_btn_ev_t event);
