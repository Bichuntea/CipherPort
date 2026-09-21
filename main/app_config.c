#include "app_config.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_mac.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "mbedtls/gcm.h"
#include "vault_store.h"
#include "nvs.h"
#include "nvs_flash.h"

#define CONFIG_NAMESPACE "ciphercfg"
#define CONFIG_LANGUAGE_KEY "language"
#define CONFIG_TIMEZONE_KEY "tz_offset"
#define CONFIG_TIME_EPOCH_KEY "time_epoch"
#define CONFIG_AUTO_LOCK_KEY "auto_lock"
#define CONFIG_REVEAL_KEY "reveal_sec"
#define CONFIG_KEY_SOUND_KEY "key_sound"
#define CONFIG_WELCOME_EN_KEY "welcome_en"
#define CONFIG_WELCOME_ZH_KEY "welcome_zh"
#define CONFIG_ENROLLED_KEY "enrolled"
#define CONFIG_PIN_SALT_KEY "pin_salt"
#define CONFIG_PIN_HASH_KEY "pin_hash"
#define CONFIG_RECOVERY_SALT_KEY "rec_salt"
#define CONFIG_RECOVERY_HASH_KEY "rec_hash"
#define CONFIG_PIN_FAILURES_KEY "pin_fail"
#define CONFIG_RECOVERY_FAILURES_KEY "rec_fail"
#define CONFIG_PIN_WRAP_KEY "pin_wrap"
#define CONFIG_PIN_NONCE_KEY "pin_nonce"
#define CONFIG_PIN_TAG_KEY "pin_tag"
#define CONFIG_REC_WRAP_KEY "rec_wrap"
#define CONFIG_REC_NONCE_KEY "rec_nonce"
#define CONFIG_REC_TAG_KEY "rec_tag"
#define SECRET_SALT_BYTES 16U
#define SECRET_HASH_BYTES 32U
#define SECRET_MAX_DIGITS 8U
#define SECRET_KDF_ROUNDS 4096U
#define VAULT_KEY_BYTES 32U
#define WRAP_NONCE_BYTES 12U
#define WRAP_TAG_BYTES 16U
#define DEFAULT_WELCOME_EN "YOUR KEYS.\nYOUR CONTROL."

typedef struct {
    uint32_t request_id;
    app_security_operation_t operation;
    uint8_t digits[SECRET_MAX_DIGITS];
    uint8_t length;
    uint8_t pin_failures;
    uint8_t recovery_failures;
} security_job_t;

typedef struct {
    bool enrolled;
    bool has_pin;
    bool has_recovery;
    uint8_t pin_failures;
    uint8_t recovery_failures;
    uint8_t pin_salt[SECRET_SALT_BYTES];
    uint8_t pin_hash[SECRET_HASH_BYTES];
    uint8_t recovery_salt[SECRET_SALT_BYTES];
    uint8_t recovery_hash[SECRET_HASH_BYTES];
    uint8_t pin_wrap[VAULT_KEY_BYTES];
    uint8_t pin_nonce[WRAP_NONCE_BYTES];
    uint8_t pin_tag[WRAP_TAG_BYTES];
    uint8_t recovery_wrap[VAULT_KEY_BYTES];
    uint8_t recovery_nonce[WRAP_NONCE_BYTES];
    uint8_t recovery_tag[WRAP_TAG_BYTES];
    uint8_t vault_key[VAULT_KEY_BYTES];
    bool vault_key_valid;
} security_cache_t;

typedef struct {
    uint32_t request_id;
    char message[APP_WELCOME_MAX_BYTES + 1U];
} welcome_job_t;

typedef struct {
    uint32_t request_id;
    esp_err_t error;
} welcome_result_t;

typedef struct {
    uint32_t request_id;
    app_language_t language;
} language_job_t;

typedef struct {
    uint32_t request_id;
    esp_err_t error;
} language_result_t;

typedef struct {
    uint32_t request_id;
    int16_t minutes;
    int64_t epoch_seconds;
    bool save_epoch;
} timezone_job_t;

typedef struct {
    uint32_t request_id;
    esp_err_t error;
} timezone_result_t;

typedef struct {
    app_display_settings_t settings;
} display_job_t;

static welcome_job_t s_welcome = {
    .message = DEFAULT_WELCOME_EN,
};
static uint32_t s_welcome_revision;
static app_language_t s_language = APP_LANGUAGE_ENGLISH;
static bool s_language_configured;
static int16_t s_timezone_offset_minutes;
static int64_t s_saved_time_epoch;
static uint32_t s_time_revision;
static app_display_settings_t s_display_settings = {
    .auto_lock_seconds = 120U,
    .reveal_seconds = 15U,
    .key_sound_enabled = true,
};
static security_cache_t s_security;
static portMUX_TYPE s_cache_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_welcome_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_language_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_timezone_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_display_settings_lock = portMUX_INITIALIZER_UNLOCKED;
static QueueHandle_t s_welcome_queue;
static QueueHandle_t s_welcome_results;
static SemaphoreHandle_t s_welcome_submit_lock;
static uint32_t s_next_welcome_request_id;
static QueueHandle_t s_language_queue;
static QueueHandle_t s_language_results;
static SemaphoreHandle_t s_language_submit_lock;
static uint32_t s_next_language_request_id;
static QueueHandle_t s_timezone_queue;
static QueueHandle_t s_timezone_results;
static SemaphoreHandle_t s_timezone_submit_lock;
static uint32_t s_next_timezone_request_id;
static QueueHandle_t s_display_settings_queue;
static QueueHandle_t s_security_jobs;
static QueueHandle_t s_security_results;
static uint32_t s_next_request_id;

static void secure_wipe(void *memory, size_t size)
{
    volatile uint8_t *p = memory;
    while (size-- > 0U) *p++ = 0U;
}

static bool valid_timezone_offset(int16_t minutes)
{
    return minutes >= -840 && minutes <= 840;
}

static bool apply_timezone_offset(int16_t minutes)
{
    char timezone[20];
    int magnitude = minutes < 0 ? -(int)minutes : (int)minutes;
    snprintf(timezone, sizeof(timezone), "UTC%c%d:%02d",
             minutes < 0 ? '-' : '+', magnitude / 60, magnitude % 60);
    if (setenv("TZ", timezone, 1) != 0) return false;
    tzset();
    return true;
}

static bool read_blob(nvs_handle_t handle, const char *key, uint8_t *value,
                      size_t expected)
{
    size_t size = expected;
    return nvs_get_blob(handle, key, value, &size) == ESP_OK && size == expected;
}

static bool read_string(nvs_handle_t handle, const char *key, char *value,
                        size_t capacity)
{
    size_t size = capacity;
    return nvs_get_str(handle, key, value, &size) == ESP_OK &&
           size > 1U && size <= capacity;
}

static bool valid_welcome_text(const char *text)
{
    if (text == NULL) return false;
    size_t length = strnlen(text, APP_WELCOME_MAX_BYTES + 1U);
    if (length == 0U || length > APP_WELCOME_MAX_BYTES) return false;
    for (size_t i = 0U; i < length;) {
        uint32_t codepoint;
        unsigned char first = (unsigned char)text[i++];
        if (first < 0x80U) {
            if (first != '\n' && (first < 0x20U || first == 0x7fU)) return false;
            continue;
        }
        size_t continuation;
        if (first >= 0xc2U && first <= 0xdfU) {
            codepoint = first & 0x1fU; continuation = 1U;
        } else if (first >= 0xe0U && first <= 0xefU) {
            codepoint = first & 0x0fU; continuation = 2U;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            codepoint = first & 0x07U; continuation = 3U;
        } else {
            return false;
        }
        if (i + continuation > length) return false;
        for (size_t n = 0U; n < continuation; ++n) {
            unsigned char next = (unsigned char)text[i++];
            if ((next & 0xc0U) != 0x80U) return false;
            codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
        if ((continuation == 2U && codepoint < 0x800U) ||
            (continuation == 3U && codepoint < 0x10000U) ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU) ||
            codepoint > 0x10ffffU ||
            (codepoint >= 0x80U && codepoint <= 0x9fU)) return false;
    }
    return true;
}

static bool valid_display_settings(const app_display_settings_t *settings)
{
    if (settings == NULL) return false;
    const uint16_t lock = settings->auto_lock_seconds;
    return (lock == 30U || lock == 60U || lock == 120U || lock == 300U) &&
           settings->reveal_seconds >= 5U && settings->reveal_seconds <= 30U &&
           settings->reveal_seconds % 5U == 0U;
}

/*
 * A deliberately bounded, device-bound iterative SHA-256 verifier. It keeps
 * plaintext PIN/recovery digits out of flash. Production still requires NVS
 * encryption, Flash Encryption, Secure Boot, and an approved eFuse policy.
 */
static void derive_secret(const uint8_t *digits, size_t length,
                          const uint8_t salt[SECRET_SALT_BYTES],
                          app_security_operation_t domain,
                          uint8_t output[SECRET_HASH_BYTES])
{
    uint8_t mac[6] = {0};
    uint8_t block[SECRET_HASH_BYTES + SECRET_SALT_BYTES + 6U +
                  SECRET_MAX_DIGITS + 5U];
    size_t used = 0U;
    (void)esp_efuse_mac_get_default(mac);
    memcpy(block + used, salt, SECRET_SALT_BYTES); used += SECRET_SALT_BYTES;
    memcpy(block + used, mac, sizeof(mac)); used += sizeof(mac);
    block[used++] = (domain == APP_SECURITY_STORE_RECOVERY ||
                     domain == APP_SECURITY_VERIFY_RECOVERY) ? 0x52U : 0x50U;
    block[used++] = (uint8_t)length;
    memcpy(block + used, digits, length); used += length;
    (void)mbedtls_sha256(block, used, output, 0);

    for (uint32_t round = 1U; round < SECRET_KDF_ROUNDS; ++round) {
        memcpy(block, output, SECRET_HASH_BYTES);
        memcpy(block + SECRET_HASH_BYTES, salt, SECRET_SALT_BYTES);
        memcpy(block + SECRET_HASH_BYTES + SECRET_SALT_BYTES, digits, length);
        size_t offset = SECRET_HASH_BYTES + SECRET_SALT_BYTES + length;
        block[offset + 0U] = (uint8_t)round;
        block[offset + 1U] = (uint8_t)(round >> 8);
        block[offset + 2U] = (uint8_t)(round >> 16);
        block[offset + 3U] = (uint8_t)(round >> 24);
        (void)mbedtls_sha256(block, offset + 4U, output, 0);
    }
    secure_wipe(block, sizeof(block));
    secure_wipe(mac, sizeof(mac));
}

static bool constant_equal(const uint8_t *left, const uint8_t *right, size_t size)
{
    uint8_t diff = 0U;
    for (size_t i = 0U; i < size; ++i) diff |= (uint8_t)(left[i] ^ right[i]);
    return diff == 0U;
}

static esp_err_t wrap_key(const uint8_t kek[SECRET_HASH_BYTES],
                          const uint8_t key[VAULT_KEY_BYTES],
                          uint8_t nonce[WRAP_NONCE_BYTES],
                          uint8_t wrapped[VAULT_KEY_BYTES],
                          uint8_t tag[WRAP_TAG_BYTES])
{
    mbedtls_gcm_context context;
    mbedtls_gcm_init(&context);
    esp_fill_random(nonce, WRAP_NONCE_BYTES);
    int rc = mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, kek, 256);
    if (rc == 0) rc = mbedtls_gcm_crypt_and_tag(&context, MBEDTLS_GCM_ENCRYPT,
                                                VAULT_KEY_BYTES, nonce,
                                                WRAP_NONCE_BYTES, NULL, 0,
                                                key, wrapped, WRAP_TAG_BYTES, tag);
    mbedtls_gcm_free(&context);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

static bool unwrap_key(const uint8_t kek[SECRET_HASH_BYTES],
                       const uint8_t nonce[WRAP_NONCE_BYTES],
                       const uint8_t wrapped[VAULT_KEY_BYTES],
                       const uint8_t tag[WRAP_TAG_BYTES],
                       uint8_t key[VAULT_KEY_BYTES])
{
    mbedtls_gcm_context context;
    mbedtls_gcm_init(&context);
    int rc = mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, kek, 256);
    if (rc == 0) rc = mbedtls_gcm_auth_decrypt(&context, VAULT_KEY_BYTES,
                                               nonce, WRAP_NONCE_BYTES,
                                               NULL, 0, tag, WRAP_TAG_BYTES,
                                               wrapped, key);
    mbedtls_gcm_free(&context);
    if (rc != 0) secure_wipe(key, VAULT_KEY_BYTES);
    return rc == 0;
}

static esp_err_t persist_security(const security_cache_t *cache)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    error = nvs_set_u8(handle, CONFIG_ENROLLED_KEY, cache->enrolled ? 1U : 0U);
    if (error == ESP_OK) error = nvs_set_u8(handle, CONFIG_PIN_FAILURES_KEY, cache->pin_failures);
    if (error == ESP_OK) error = nvs_set_u8(handle, CONFIG_RECOVERY_FAILURES_KEY, cache->recovery_failures);
    if (error == ESP_OK && cache->has_pin) error = nvs_set_blob(handle, CONFIG_PIN_SALT_KEY, cache->pin_salt, sizeof(cache->pin_salt));
    if (error == ESP_OK && cache->has_pin) error = nvs_set_blob(handle, CONFIG_PIN_HASH_KEY, cache->pin_hash, sizeof(cache->pin_hash));
    if (error == ESP_OK && cache->has_pin) error = nvs_set_blob(handle, CONFIG_PIN_WRAP_KEY, cache->pin_wrap, sizeof(cache->pin_wrap));
    if (error == ESP_OK && cache->has_pin) error = nvs_set_blob(handle, CONFIG_PIN_NONCE_KEY, cache->pin_nonce, sizeof(cache->pin_nonce));
    if (error == ESP_OK && cache->has_pin) error = nvs_set_blob(handle, CONFIG_PIN_TAG_KEY, cache->pin_tag, sizeof(cache->pin_tag));
    if (error == ESP_OK && cache->has_recovery) error = nvs_set_blob(handle, CONFIG_RECOVERY_SALT_KEY, cache->recovery_salt, sizeof(cache->recovery_salt));
    if (error == ESP_OK && cache->has_recovery) error = nvs_set_blob(handle, CONFIG_RECOVERY_HASH_KEY, cache->recovery_hash, sizeof(cache->recovery_hash));
    if (error == ESP_OK && cache->has_recovery) error = nvs_set_blob(handle, CONFIG_REC_WRAP_KEY, cache->recovery_wrap, sizeof(cache->recovery_wrap));
    if (error == ESP_OK && cache->has_recovery) error = nvs_set_blob(handle, CONFIG_REC_NONCE_KEY, cache->recovery_nonce, sizeof(cache->recovery_nonce));
    if (error == ESP_OK && cache->has_recovery) error = nvs_set_blob(handle, CONFIG_REC_TAG_KEY, cache->recovery_tag, sizeof(cache->recovery_tag));
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}

static esp_err_t erase_security_namespace(void)
{
    nvs_handle_t handle;
    esp_err_t error = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (error != ESP_OK) return error;
    const char *keys[] = {
        CONFIG_ENROLLED_KEY, CONFIG_PIN_SALT_KEY, CONFIG_PIN_HASH_KEY,
        CONFIG_RECOVERY_SALT_KEY, CONFIG_RECOVERY_HASH_KEY,
        CONFIG_PIN_FAILURES_KEY, CONFIG_RECOVERY_FAILURES_KEY,
        CONFIG_PIN_WRAP_KEY, CONFIG_PIN_NONCE_KEY, CONFIG_PIN_TAG_KEY,
        CONFIG_REC_WRAP_KEY, CONFIG_REC_NONCE_KEY, CONFIG_REC_TAG_KEY,
        CONFIG_WELCOME_EN_KEY, CONFIG_WELCOME_ZH_KEY, CONFIG_LANGUAGE_KEY,
        CONFIG_AUTO_LOCK_KEY, CONFIG_REVEAL_KEY, CONFIG_KEY_SOUND_KEY,
    };
    for (size_t i = 0U; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        esp_err_t item_error = nvs_erase_key(handle, keys[i]);
        if (item_error != ESP_OK && item_error != ESP_ERR_NVS_NOT_FOUND) error = item_error;
    }
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}

static void process_security_job(security_job_t *job, app_security_result_t *result)
{
    security_cache_t cache;
    uint8_t candidate[SECRET_HASH_BYTES] = {0};
    taskENTER_CRITICAL(&s_cache_lock);
    cache = s_security;
    taskEXIT_CRITICAL(&s_cache_lock);

    *result = (app_security_result_t) {
        .request_id = job->request_id,
        .operation = job->operation,
        .error = ESP_OK,
        .accepted = false,
    };
    switch (job->operation) {
    case APP_SECURITY_STORE_PIN:
        if (job->length != 4U) { result->error = ESP_ERR_INVALID_SIZE; break; }
        /* A merged image written at 0x0 intentionally resets NVS while the
         * protected vault partition remains outside the image. A first-time
         * enrollment therefore has to discard any orphaned ciphertext before
         * creating a new vault key. This runs in the storage worker, never in
         * the LVGL/button path. PIN changes and recovery keep the vault. */
        if (!cache.enrolled) {
            result->error = vault_store_erase();
            if (result->error != ESP_OK) break;
            secure_wipe(cache.vault_key, sizeof(cache.vault_key));
            cache.vault_key_valid = false;
        }
        esp_fill_random(cache.pin_salt, sizeof(cache.pin_salt));
        derive_secret(job->digits, job->length, cache.pin_salt, job->operation, cache.pin_hash);
        if (!cache.vault_key_valid) {
            esp_fill_random(cache.vault_key, sizeof(cache.vault_key));
            cache.vault_key_valid = true;
        }
        result->error = wrap_key(cache.pin_hash, cache.vault_key,
                                 cache.pin_nonce, cache.pin_wrap, cache.pin_tag);
        cache.has_pin = true;
        if (result->error == ESP_OK) result->error = persist_security(&cache);
        result->accepted = result->error == ESP_OK;
        break;
    case APP_SECURITY_VERIFY_PIN:
        if (job->length != 4U || !cache.has_pin) break;
        derive_secret(job->digits, job->length, cache.pin_salt, job->operation, candidate);
        result->accepted = constant_equal(candidate, cache.pin_hash, sizeof(candidate));
        if (result->accepted) {
            result->accepted = unwrap_key(candidate, cache.pin_nonce,
                                          cache.pin_wrap, cache.pin_tag,
                                          cache.vault_key);
            cache.vault_key_valid = result->accepted;
        }
        break;
    case APP_SECURITY_STORE_RECOVERY:
        if (job->length != 8U) { result->error = ESP_ERR_INVALID_SIZE; break; }
        esp_fill_random(cache.recovery_salt, sizeof(cache.recovery_salt));
        derive_secret(job->digits, job->length, cache.recovery_salt, job->operation, cache.recovery_hash);
        if (!cache.vault_key_valid) { result->error = ESP_ERR_INVALID_STATE; break; }
        result->error = wrap_key(cache.recovery_hash, cache.vault_key,
                                 cache.recovery_nonce, cache.recovery_wrap,
                                 cache.recovery_tag);
        cache.has_recovery = true;
        cache.enrolled = cache.has_pin;
        if (result->error == ESP_OK) result->error = persist_security(&cache);
        result->accepted = result->error == ESP_OK;
        break;
    case APP_SECURITY_VERIFY_RECOVERY:
        if (job->length != 8U || !cache.has_recovery) break;
        derive_secret(job->digits, job->length, cache.recovery_salt, job->operation, candidate);
        result->accepted = constant_equal(candidate, cache.recovery_hash, sizeof(candidate));
        if (result->accepted) {
            result->accepted = unwrap_key(candidate, cache.recovery_nonce,
                                          cache.recovery_wrap,
                                          cache.recovery_tag, cache.vault_key);
            cache.vault_key_valid = result->accepted;
        }
        break;
    case APP_SECURITY_SET_FAILURES:
        cache.pin_failures = job->pin_failures;
        cache.recovery_failures = job->recovery_failures;
        result->error = persist_security(&cache);
        result->accepted = result->error == ESP_OK;
        break;
    case APP_SECURITY_ERASE:
        result->error = erase_security_namespace();
        if (result->error == ESP_OK) {
            secure_wipe(&cache, sizeof(cache));
            taskENTER_CRITICAL(&s_welcome_lock);
            snprintf(s_welcome.message, sizeof(s_welcome.message), "%s", DEFAULT_WELCOME_EN);
            taskEXIT_CRITICAL(&s_welcome_lock);
            taskENTER_CRITICAL(&s_language_lock);
            s_language = APP_LANGUAGE_ENGLISH;
            taskEXIT_CRITICAL(&s_language_lock);
            taskENTER_CRITICAL(&s_display_settings_lock);
            s_display_settings = (app_display_settings_t) {
                .auto_lock_seconds = 120U,
                .reveal_seconds = 15U,
                .key_sound_enabled = true,
            };
            taskEXIT_CRITICAL(&s_display_settings_lock);
            result->accepted = true;
        }
        break;
    default:
        result->error = ESP_ERR_INVALID_ARG;
        break;
    }

    if (result->error == ESP_OK &&
        (job->operation == APP_SECURITY_STORE_PIN ||
         job->operation == APP_SECURITY_STORE_RECOVERY ||
         job->operation == APP_SECURITY_VERIFY_PIN ||
         job->operation == APP_SECURITY_VERIFY_RECOVERY ||
         job->operation == APP_SECURITY_SET_FAILURES ||
         job->operation == APP_SECURITY_ERASE)) {
        taskENTER_CRITICAL(&s_cache_lock);
        s_security = cache;
        taskEXIT_CRITICAL(&s_cache_lock);
    }
    secure_wipe(candidate, sizeof(candidate));
    secure_wipe(&cache, sizeof(cache));
}

static void storage_task(void *argument)
{
    (void)argument;
    security_job_t job;
    welcome_job_t welcome;
    language_job_t language;
    timezone_job_t timezone;
    display_job_t display;
    for (;;) {
        if (xQueueReceive(s_security_jobs, &job, pdMS_TO_TICKS(25)) == pdTRUE) {
            app_security_result_t result;
            process_security_job(&job, &result);
            secure_wipe(&job, sizeof(job));
            (void)xQueueSend(s_security_results, &result, portMAX_DELAY);
        }
        if (xQueueReceive(s_welcome_queue, &welcome, 0) == pdTRUE) {
            nvs_handle_t handle;
            welcome_result_t result = {.request_id = welcome.request_id};
            result.error = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle);
            if (result.error == ESP_OK) {
                result.error = nvs_set_str(handle, CONFIG_WELCOME_EN_KEY, welcome.message);
                if (result.error == ESP_OK) result.error = nvs_commit(handle);
                nvs_close(handle);
                if (result.error == ESP_OK) {
                    taskENTER_CRITICAL(&s_welcome_lock);
                    s_welcome = welcome;
                    ++s_welcome_revision;
                    taskEXIT_CRITICAL(&s_welcome_lock);
                }
            }
            secure_wipe(&welcome, sizeof(welcome));
            (void)xQueueOverwrite(s_welcome_results, &result);
        }
        if (xQueueReceive(s_language_queue, &language, 0) == pdTRUE) {
            nvs_handle_t handle;
            language_result_t result = {.request_id = language.request_id};
            result.error = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle);
            if (result.error == ESP_OK) {
                result.error = nvs_set_u8(handle, CONFIG_LANGUAGE_KEY,
                                             (uint8_t)language.language);
                if (result.error == ESP_OK) result.error = nvs_commit(handle);
                nvs_close(handle);
                if (result.error == ESP_OK) {
                    taskENTER_CRITICAL(&s_language_lock);
                    s_language = language.language;
                    s_language_configured = true;
                    taskEXIT_CRITICAL(&s_language_lock);
                }
            }
            if (language.request_id != 0U) (void)xQueueOverwrite(s_language_results, &result);
            secure_wipe(&language, sizeof(language));
        }
        if (xQueueReceive(s_timezone_queue, &timezone, 0) == pdTRUE) {
            nvs_handle_t handle;
            timezone_result_t result = {.request_id = timezone.request_id};
            result.error = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle);
            if (result.error == ESP_OK) {
                result.error = nvs_set_i16(handle, CONFIG_TIMEZONE_KEY,
                                           timezone.minutes);
                if (result.error == ESP_OK && timezone.save_epoch) {
                    result.error = nvs_set_i64(handle, CONFIG_TIME_EPOCH_KEY,
                                               timezone.epoch_seconds);
                }
                if (result.error == ESP_OK) result.error = nvs_commit(handle);
                nvs_close(handle);
            }
            if (result.error == ESP_OK && !apply_timezone_offset(timezone.minutes)) {
                result.error = ESP_FAIL;
            }
            if (result.error == ESP_OK) {
                taskENTER_CRITICAL(&s_timezone_lock);
                s_timezone_offset_minutes = timezone.minutes;
                if (timezone.save_epoch) {
                    s_saved_time_epoch = timezone.epoch_seconds;
                }
                ++s_time_revision;
                taskEXIT_CRITICAL(&s_timezone_lock);
            }
            secure_wipe(&timezone, sizeof(timezone));
            (void)xQueueOverwrite(s_timezone_results, &result);
        }
        if (xQueueReceive(s_display_settings_queue, &display, 0) == pdTRUE) {
            nvs_handle_t handle;
            if (nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
                esp_err_t error = nvs_set_u16(handle, CONFIG_AUTO_LOCK_KEY,
                                               display.settings.auto_lock_seconds);
                if (error == ESP_OK) error = nvs_set_u8(handle, CONFIG_REVEAL_KEY,
                                                        display.settings.reveal_seconds);
                if (error == ESP_OK) error = nvs_set_u8(handle, CONFIG_KEY_SOUND_KEY,
                                                        display.settings.key_sound_enabled ? 1U : 0U);
                if (error == ESP_OK) error = nvs_commit(handle);
                nvs_close(handle);
            }
            secure_wipe(&display, sizeof(display));
        }
    }
}

esp_err_t app_config_init(void)
{
    esp_err_t error = nvs_flash_init();
    if (error != ESP_OK) return error;
    nvs_handle_t handle;
    if (nvs_open(CONFIG_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        uint8_t value = 0U;
        welcome_job_t welcome = s_welcome;
        if (!read_string(handle, CONFIG_WELCOME_EN_KEY, welcome.message, sizeof(welcome.message)) ||
            !valid_welcome_text(welcome.message)) {
            snprintf(welcome.message, sizeof(welcome.message), "%s", DEFAULT_WELCOME_EN);
        }
        taskENTER_CRITICAL(&s_welcome_lock);
        s_welcome = welcome;
        taskEXIT_CRITICAL(&s_welcome_lock);
        if (nvs_get_u8(handle, CONFIG_LANGUAGE_KEY, &value) == ESP_OK &&
            value <= (uint8_t)APP_LANGUAGE_CHINESE) {
            s_language = (app_language_t)value;
            s_language_configured = true;
        }
        int16_t timezone_offset = 0;
        if (nvs_get_i16(handle, CONFIG_TIMEZONE_KEY, &timezone_offset) == ESP_OK &&
            valid_timezone_offset(timezone_offset)) {
            s_timezone_offset_minutes = timezone_offset;
        }
        (void)nvs_get_i64(handle, CONFIG_TIME_EPOCH_KEY, &s_saved_time_epoch);
        app_display_settings_t display = s_display_settings;
        (void)nvs_get_u16(handle, CONFIG_AUTO_LOCK_KEY, &display.auto_lock_seconds);
        (void)nvs_get_u8(handle, CONFIG_REVEAL_KEY, &display.reveal_seconds);
        if (nvs_get_u8(handle, CONFIG_KEY_SOUND_KEY, &value) == ESP_OK) {
            display.key_sound_enabled = value != 0U;
        }
        if (valid_display_settings(&display)) s_display_settings = display;
        if (nvs_get_u8(handle, CONFIG_ENROLLED_KEY, &value) == ESP_OK) s_security.enrolled = value == 1U;
        (void)nvs_get_u8(handle, CONFIG_PIN_FAILURES_KEY, &s_security.pin_failures);
        (void)nvs_get_u8(handle, CONFIG_RECOVERY_FAILURES_KEY, &s_security.recovery_failures);
        s_security.has_pin = read_blob(handle, CONFIG_PIN_SALT_KEY, s_security.pin_salt, sizeof(s_security.pin_salt)) &&
                             read_blob(handle, CONFIG_PIN_HASH_KEY, s_security.pin_hash, sizeof(s_security.pin_hash)) &&
                             read_blob(handle, CONFIG_PIN_WRAP_KEY, s_security.pin_wrap, sizeof(s_security.pin_wrap)) &&
                             read_blob(handle, CONFIG_PIN_NONCE_KEY, s_security.pin_nonce, sizeof(s_security.pin_nonce)) &&
                             read_blob(handle, CONFIG_PIN_TAG_KEY, s_security.pin_tag, sizeof(s_security.pin_tag));
        s_security.has_recovery = read_blob(handle, CONFIG_RECOVERY_SALT_KEY, s_security.recovery_salt, sizeof(s_security.recovery_salt)) &&
                                  read_blob(handle, CONFIG_RECOVERY_HASH_KEY, s_security.recovery_hash, sizeof(s_security.recovery_hash)) &&
                                  read_blob(handle, CONFIG_REC_WRAP_KEY, s_security.recovery_wrap, sizeof(s_security.recovery_wrap)) &&
                                  read_blob(handle, CONFIG_REC_NONCE_KEY, s_security.recovery_nonce, sizeof(s_security.recovery_nonce)) &&
                                  read_blob(handle, CONFIG_REC_TAG_KEY, s_security.recovery_tag, sizeof(s_security.recovery_tag));
        s_security.enrolled = s_security.enrolled && s_security.has_pin && s_security.has_recovery;
        nvs_close(handle);
    }
    if (!apply_timezone_offset(s_timezone_offset_minutes)) return ESP_FAIL;
    s_welcome_queue = xQueueCreate(1, sizeof(welcome_job_t));
    s_welcome_results = xQueueCreate(1, sizeof(welcome_result_t));
    s_welcome_submit_lock = xSemaphoreCreateMutex();
    s_language_queue = xQueueCreate(4, sizeof(language_job_t));
    s_language_results = xQueueCreate(1, sizeof(language_result_t));
    s_language_submit_lock = xSemaphoreCreateMutex();
    s_timezone_queue = xQueueCreate(1, sizeof(timezone_job_t));
    s_timezone_results = xQueueCreate(1, sizeof(timezone_result_t));
    s_timezone_submit_lock = xSemaphoreCreateMutex();
    s_display_settings_queue = xQueueCreate(1, sizeof(display_job_t));
    s_security_jobs = xQueueCreate(4, sizeof(security_job_t));
    s_security_results = xQueueCreate(4, sizeof(app_security_result_t));
    if (!s_welcome_queue || !s_welcome_results || !s_welcome_submit_lock ||
        !s_language_queue || !s_language_results || !s_language_submit_lock ||
        !s_timezone_queue || !s_timezone_results || !s_timezone_submit_lock ||
        !s_display_settings_queue || !s_security_jobs ||
        !s_security_results) return ESP_ERR_NO_MEM;
    if (xTaskCreate(storage_task, "secure_store", 4096, NULL, 3, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

void app_config_copy_welcome(char message[APP_WELCOME_MAX_BYTES + 1U])
{
    if (message == NULL) return;
    taskENTER_CRITICAL(&s_welcome_lock);
    memcpy(message, s_welcome.message, sizeof(s_welcome.message));
    taskEXIT_CRITICAL(&s_welcome_lock);
}

esp_err_t app_config_save_welcome(const char *message)
{
    welcome_job_t job;
    if (!s_welcome_queue || !s_welcome_results || !s_welcome_submit_lock) return ESP_ERR_INVALID_STATE;
    if (!valid_welcome_text(message)) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_welcome_submit_lock, pdMS_TO_TICKS(5000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    job.request_id = ++s_next_welcome_request_id;
    snprintf(job.message, sizeof(job.message), "%s", message);
    esp_err_t error = ESP_ERR_TIMEOUT;
    if (xQueueSend(s_welcome_queue, &job, pdMS_TO_TICKS(5000)) == pdTRUE) {
        welcome_result_t result;
        while (xQueueReceive(s_welcome_results, &result, pdMS_TO_TICKS(5000)) == pdTRUE) {
            if (result.request_id == job.request_id) {
                error = result.error;
                break;
            }
        }
    }
    secure_wipe(&job, sizeof(job));
    xSemaphoreGive(s_welcome_submit_lock);
    return error;
}

uint32_t app_config_welcome_revision(void)
{
    uint32_t revision;
    taskENTER_CRITICAL(&s_welcome_lock);
    revision = s_welcome_revision;
    taskEXIT_CRITICAL(&s_welcome_lock);
    return revision;
}

app_language_t app_config_language(void)
{
    app_language_t language;
    taskENTER_CRITICAL(&s_language_lock);
    language = s_language;
    taskEXIT_CRITICAL(&s_language_lock);
    return language;
}

bool app_config_language_configured(void)
{
    bool configured;
    taskENTER_CRITICAL(&s_language_lock);
    configured = s_language_configured;
    taskEXIT_CRITICAL(&s_language_lock);
    return configured;
}

bool app_config_request_language(app_language_t language)
{
    if (!s_language_queue ||
        (language != APP_LANGUAGE_ENGLISH &&
         language != APP_LANGUAGE_CHINESE)) return false;
    language_job_t job = {.language = language};
    bool queued = xQueueSend(s_language_queue, &job, 0) == pdTRUE;
    if (queued) {
        taskENTER_CRITICAL(&s_language_lock);
        s_language = language;
        s_language_configured = true;
        taskEXIT_CRITICAL(&s_language_lock);
    }
    secure_wipe(&job, sizeof(job));
    return queued;
}

esp_err_t app_config_save_language(app_language_t language)
{
    if (language != APP_LANGUAGE_ENGLISH && language != APP_LANGUAGE_CHINESE) return ESP_ERR_INVALID_ARG;
    if (!s_language_queue || !s_language_results || !s_language_submit_lock) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_language_submit_lock, pdMS_TO_TICKS(5000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    language_job_t job = {.request_id = ++s_next_language_request_id, .language = language};
    esp_err_t error = ESP_ERR_TIMEOUT;
    if (xQueueSend(s_language_queue, &job, pdMS_TO_TICKS(5000)) == pdTRUE) {
        language_result_t result;
        while (xQueueReceive(s_language_results, &result, pdMS_TO_TICKS(5000)) == pdTRUE) {
            if (result.request_id == job.request_id) {
                error = result.error;
                break;
            }
        }
    }
    secure_wipe(&job, sizeof(job));
    xSemaphoreGive(s_language_submit_lock);
    return error;
}

esp_err_t app_config_save_timezone_offset(int16_t minutes)
{
    if (!valid_timezone_offset(minutes)) return ESP_ERR_INVALID_ARG;
    if (minutes == app_config_timezone_offset()) return ESP_OK;
    if (!s_timezone_queue || !s_timezone_results || !s_timezone_submit_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_timezone_submit_lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    timezone_job_t job = {
        .request_id = ++s_next_timezone_request_id,
        .minutes = minutes,
    };
    esp_err_t error = ESP_ERR_TIMEOUT;
    if (xQueueSend(s_timezone_queue, &job, pdMS_TO_TICKS(5000)) == pdTRUE) {
        timezone_result_t result;
        while (xQueueReceive(s_timezone_results, &result,
                             pdMS_TO_TICKS(5000)) == pdTRUE) {
            if (result.request_id == job.request_id) {
                error = result.error;
                break;
            }
        }
    }
    secure_wipe(&job, sizeof(job));
    xSemaphoreGive(s_timezone_submit_lock);
    return error;
}

esp_err_t app_config_save_clock(int64_t epoch_seconds, int16_t minutes)
{
    if (!valid_timezone_offset(minutes) || epoch_seconds <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_timezone_queue || !s_timezone_results || !s_timezone_submit_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_timezone_submit_lock, pdMS_TO_TICKS(5000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    timezone_job_t job = {
        .request_id = ++s_next_timezone_request_id,
        .minutes = minutes,
        .epoch_seconds = epoch_seconds,
        .save_epoch = true,
    };
    esp_err_t error = ESP_ERR_TIMEOUT;
    if (xQueueSend(s_timezone_queue, &job, pdMS_TO_TICKS(5000)) == pdTRUE) {
        timezone_result_t result;
        while (xQueueReceive(s_timezone_results, &result,
                             pdMS_TO_TICKS(5000)) == pdTRUE) {
            if (result.request_id == job.request_id) {
                error = result.error;
                break;
            }
        }
    }
    secure_wipe(&job, sizeof(job));
    xSemaphoreGive(s_timezone_submit_lock);
    return error;
}

int16_t app_config_timezone_offset(void)
{
    int16_t minutes;
    taskENTER_CRITICAL(&s_timezone_lock);
    minutes = s_timezone_offset_minutes;
    taskEXIT_CRITICAL(&s_timezone_lock);
    return minutes;
}

int64_t app_config_saved_time_epoch(void)
{
    int64_t epoch_seconds;
    taskENTER_CRITICAL(&s_timezone_lock);
    epoch_seconds = s_saved_time_epoch;
    taskEXIT_CRITICAL(&s_timezone_lock);
    return epoch_seconds;
}

void app_config_notify_time_set(void)
{
    taskENTER_CRITICAL(&s_timezone_lock);
    ++s_time_revision;
    taskEXIT_CRITICAL(&s_timezone_lock);
}

uint32_t app_config_time_revision(void)
{
    uint32_t revision;
    taskENTER_CRITICAL(&s_timezone_lock);
    revision = s_time_revision;
    taskEXIT_CRITICAL(&s_timezone_lock);
    return revision;
}

void app_config_copy_display_settings(app_display_settings_t *settings)
{
    if (settings == NULL) return;
    taskENTER_CRITICAL(&s_display_settings_lock);
    *settings = s_display_settings;
    taskEXIT_CRITICAL(&s_display_settings_lock);
}

bool app_config_request_display_settings(const app_display_settings_t *settings)
{
    if (!s_display_settings_queue || !valid_display_settings(settings)) return false;
    display_job_t job = {.settings = *settings};
    taskENTER_CRITICAL(&s_display_settings_lock);
    s_display_settings = *settings;
    taskEXIT_CRITICAL(&s_display_settings_lock);
    bool queued = xQueueOverwrite(s_display_settings_queue, &job) == pdTRUE;
    secure_wipe(&job, sizeof(job));
    return queued;
}

bool app_config_is_enrolled(void) { bool v; taskENTER_CRITICAL(&s_cache_lock); v=s_security.enrolled; taskEXIT_CRITICAL(&s_cache_lock); return v; }
bool app_config_has_recovery(void) { bool v; taskENTER_CRITICAL(&s_cache_lock); v=s_security.has_recovery; taskEXIT_CRITICAL(&s_cache_lock); return v; }
uint8_t app_config_pin_failures(void) { uint8_t v; taskENTER_CRITICAL(&s_cache_lock); v=s_security.pin_failures; taskEXIT_CRITICAL(&s_cache_lock); return v; }
uint8_t app_config_recovery_failures(void) { uint8_t v; taskENTER_CRITICAL(&s_cache_lock); v=s_security.recovery_failures; taskEXIT_CRITICAL(&s_cache_lock); return v; }

uint32_t app_config_security_submit(app_security_operation_t operation,
                                    const uint8_t *digits, size_t length,
                                    uint8_t pin_failures,
                                    uint8_t recovery_failures)
{
    if (!s_security_jobs || length > SECRET_MAX_DIGITS || (length > 0U && !digits)) return 0U;
    security_job_t job = {
        .request_id = ++s_next_request_id,
        .operation = operation,
        .length = (uint8_t)length,
        .pin_failures = pin_failures,
        .recovery_failures = recovery_failures,
    };
    if (length > 0U) memcpy(job.digits, digits, length);
    if (xQueueSend(s_security_jobs, &job, 0) != pdTRUE) {
        secure_wipe(&job, sizeof(job));
        return 0U;
    }
    uint32_t id = job.request_id;
    secure_wipe(&job, sizeof(job));
    return id;
}

bool app_config_security_poll(app_security_result_t *result)
{
    return result && s_security_results && xQueueReceive(s_security_results, result, 0) == pdTRUE;
}

bool app_config_copy_vault_key(uint8_t output[32])
{
    if (!output) return false;
    bool valid;
    taskENTER_CRITICAL(&s_cache_lock);
    valid = s_security.vault_key_valid;
    if (valid) memcpy(output, s_security.vault_key, VAULT_KEY_BYTES);
    taskEXIT_CRITICAL(&s_cache_lock);
    return valid;
}

void app_config_lock_vault(void)
{
    taskENTER_CRITICAL(&s_cache_lock);
    secure_wipe(s_security.vault_key, sizeof(s_security.vault_key));
    s_security.vault_key_valid = false;
    taskEXIT_CRITICAL(&s_cache_lock);
}
