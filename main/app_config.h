#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define APP_WELCOME_MAX_BYTES 256U

typedef struct {
    uint16_t auto_lock_seconds;
    uint8_t reveal_seconds;
    bool key_sound_enabled;
} app_display_settings_t;

typedef enum {
    APP_LANGUAGE_ENGLISH = 0,
    APP_LANGUAGE_CHINESE = 1,
} app_language_t;

typedef enum {
    APP_SECURITY_STORE_PIN = 1,
    APP_SECURITY_VERIFY_PIN,
    APP_SECURITY_STORE_RECOVERY,
    APP_SECURITY_VERIFY_RECOVERY,
    APP_SECURITY_SET_FAILURES,
    APP_SECURITY_ERASE,
} app_security_operation_t;

typedef struct {
    uint32_t request_id;
    app_security_operation_t operation;
    esp_err_t error;
    bool accepted;
} app_security_result_t;

esp_err_t app_config_init(void);
void app_config_copy_welcome(char message[APP_WELCOME_MAX_BYTES + 1U]);
/* Waits for the storage worker to commit; call only from a non-UI task. */
esp_err_t app_config_save_welcome(const char *message);
uint32_t app_config_welcome_revision(void);
app_language_t app_config_language(void);
bool app_config_language_configured(void);
bool app_config_request_language(app_language_t language);
/* Waits for NVS commit; call only from a non-UI task. */
esp_err_t app_config_save_language(app_language_t language);
/* Browser getTimezoneOffset() convention: minutes west of UTC. */
esp_err_t app_config_save_timezone_offset(int16_t minutes);
int16_t app_config_timezone_offset(void);
/* Atomically persists the last Web-synchronized epoch and its time zone. */
esp_err_t app_config_save_clock(int64_t epoch_seconds, int16_t minutes);
int64_t app_config_saved_time_epoch(void);
void app_config_notify_time_set(void);
uint32_t app_config_time_revision(void);
void app_config_copy_display_settings(app_display_settings_t *settings);
bool app_config_request_display_settings(const app_display_settings_t *settings);

bool app_config_is_enrolled(void);
bool app_config_has_recovery(void);
uint8_t app_config_pin_failures(void);
uint8_t app_config_recovery_failures(void);

/* Non-blocking: derivation, verification, and NVS writes run in a worker. */
uint32_t app_config_security_submit(app_security_operation_t operation,
                                    const uint8_t *digits, size_t length,
                                    uint8_t pin_failures,
                                    uint8_t recovery_failures);
bool app_config_security_poll(app_security_result_t *result);

/* Available only after successful enrollment or PIN/recovery verification. */
bool app_config_copy_vault_key(uint8_t output[32]);
void app_config_lock_vault(void);
