#include "vault_store.h"

#include <string.h>

#include "app_config.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mbedtls/gcm.h"

#define VAULT_PARTITION_SUBTYPE 0x40
#define VAULT_MAGIC 0x43505654U
#define VAULT_VERSION 1U
#define VAULT_SLOT_BYTES 8192U
#define VAULT_NONCE_BYTES 12U
#define VAULT_TAG_BYTES 16U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t generation;
    uint8_t nonce[VAULT_NONCE_BYTES];
    uint8_t tag[VAULT_TAG_BYTES];
    uint8_t reserved[24];
} vault_header_t;

typedef struct {
    uint32_t count;
    uint32_t next_id;
    vault_account_t accounts[VAULT_MAX_ACCOUNTS];
    uint8_t reserved[8];
} vault_payload_t;

_Static_assert(sizeof(vault_header_t) == 64U, "vault header must be aligned");
_Static_assert((sizeof(vault_payload_t) % 16U) == 0U, "vault payload must be encrypted-write aligned");

static const esp_partition_t *s_partition;
static SemaphoreHandle_t s_lock;
static vault_payload_t s_payload;
static vault_payload_t s_scratch[2];
static uint8_t s_ciphertext[sizeof(vault_payload_t)];
static uint32_t s_generation;
static int s_active_slot = -1;
static bool s_unlocked;

static void secure_wipe(void *memory, size_t size)
{
    volatile uint8_t *p = memory;
    while (size-- > 0U) *p++ = 0U;
}

static bool bounded_string(const char *text, size_t capacity)
{
    return text && memchr(text, '\0', capacity) != NULL;
}

static bool valid_account(const vault_account_t *account)
{
    return account && bounded_string(account->platform, sizeof(account->platform)) &&
           bounded_string(account->url, sizeof(account->url)) &&
           bounded_string(account->username, sizeof(account->username)) &&
           bounded_string(account->password, sizeof(account->password)) &&
           bounded_string(account->note, sizeof(account->note)) &&
           account->platform[0] != '\0';
}

static esp_err_t crypt(bool encrypt, const uint8_t key[32],
                       vault_header_t *header, const uint8_t *input,
                       uint8_t *output)
{
    mbedtls_gcm_context context;
    mbedtls_gcm_init(&context);
    int rc = mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (rc == 0 && encrypt) {
        rc = mbedtls_gcm_crypt_and_tag(&context, MBEDTLS_GCM_ENCRYPT,
                                      sizeof(vault_payload_t), header->nonce,
                                      sizeof(header->nonce),
                                      (const uint8_t *)header, 12U, input,
                                      output, sizeof(header->tag),
                                      (uint8_t *)header->tag);
    } else if (rc == 0) {
        rc = mbedtls_gcm_auth_decrypt(&context, sizeof(vault_payload_t),
                                      header->nonce, sizeof(header->nonce),
                                      (const uint8_t *)header, 12U,
                                      header->tag, sizeof(header->tag),
                                      input, output);
    }
    mbedtls_gcm_free(&context);
    return rc == 0 ? ESP_OK : ESP_ERR_INVALID_CRC;
}

static bool load_slot(int slot, const uint8_t key[32], vault_header_t *header,
                      vault_payload_t *payload)
{
    const size_t base = (size_t)slot * VAULT_SLOT_BYTES;
    if (esp_partition_read(s_partition, base, header, sizeof(*header)) != ESP_OK ||
        header->magic != VAULT_MAGIC || header->version != VAULT_VERSION ||
        header->payload_size != sizeof(*payload)) return false;
    if (esp_partition_read(s_partition, base + sizeof(*header), s_ciphertext,
                           sizeof(s_ciphertext)) != ESP_OK ||
        crypt(false, key, header, s_ciphertext, (uint8_t *)payload) != ESP_OK) {
        secure_wipe(s_ciphertext, sizeof(s_ciphertext));
        secure_wipe(payload, sizeof(*payload));
        return false;
    }
    secure_wipe(s_ciphertext, sizeof(s_ciphertext));
    if (payload->count > VAULT_MAX_ACCOUNTS) {
        secure_wipe(payload, sizeof(*payload));
        return false;
    }
    for (size_t i = 0; i < payload->count; ++i) {
        if (!valid_account(&payload->accounts[i])) {
            secure_wipe(payload, sizeof(*payload));
            return false;
        }
    }
    return true;
}

static bool slot_is_erased(int slot)
{
    vault_header_t header;
    const size_t base = (size_t)slot * VAULT_SLOT_BYTES;
    if (esp_partition_read(s_partition, base, &header, sizeof(header)) != ESP_OK) {
        return false;
    }
    const uint8_t *bytes = (const uint8_t *)&header;
    bool erased = true;
    for (size_t i = 0U; i < sizeof(header); ++i) erased = erased && bytes[i] == 0xFFU;
    secure_wipe(&header, sizeof(header));
    return erased;
}

static esp_err_t commit_locked(void)
{
    uint8_t key[32];
    if (!app_config_copy_vault_key(key)) return ESP_ERR_INVALID_STATE;
    int next_slot = s_active_slot == 0 ? 1 : 0;
    const size_t base = (size_t)next_slot * VAULT_SLOT_BYTES;
    vault_header_t header = {
        .magic = VAULT_MAGIC,
        .version = VAULT_VERSION,
        .payload_size = sizeof(s_payload),
        .generation = s_generation + 1U,
    };
    esp_fill_random(header.nonce, sizeof(header.nonce));
    esp_err_t error = crypt(true, key, &header, (const uint8_t *)&s_payload,
                            s_ciphertext);
    if (error == ESP_OK) error = esp_partition_erase_range(s_partition, base, VAULT_SLOT_BYTES);
    if (error == ESP_OK) error = esp_partition_write(s_partition, base + sizeof(header), s_ciphertext, sizeof(s_ciphertext));
    if (error == ESP_OK) error = esp_partition_write(s_partition, base, &header, sizeof(header));
    if (error == ESP_OK) {
        vault_header_t verify_header;
        if (!load_slot(next_slot, key, &verify_header, &s_scratch[0]) ||
            memcmp(&s_scratch[0], &s_payload, sizeof(s_payload)) != 0) error = ESP_FAIL;
        secure_wipe(&s_scratch[0], sizeof(s_scratch[0]));
    }
    if (error == ESP_OK) {
        s_generation = header.generation;
        s_active_slot = next_slot;
    }
    secure_wipe(key, sizeof(key));
    secure_wipe(s_ciphertext, sizeof(s_ciphertext));
    secure_wipe(&header, sizeof(header));
    return error;
}

esp_err_t vault_store_init(void)
{
    s_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                           VAULT_PARTITION_SUBTYPE, "vault");
    if (!s_partition || s_partition->size < VAULT_SLOT_BYTES * 2U) return ESP_ERR_NOT_FOUND;
    s_lock = xSemaphoreCreateMutex();
    return s_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t vault_store_unlock(void)
{
    if (!s_partition || !s_lock) return ESP_ERR_INVALID_STATE;
    uint8_t key[32];
    if (!app_config_copy_vault_key(key)) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    vault_header_t headers[2];
    bool valid[2] = {load_slot(0, key, &headers[0], &s_scratch[0]),
                     load_slot(1, key, &headers[1], &s_scratch[1])};
    int selected = valid[0] && valid[1]
        ? (headers[1].generation > headers[0].generation ? 1 : 0)
        : (valid[0] ? 0 : (valid[1] ? 1 : -1));
    secure_wipe(&s_payload, sizeof(s_payload));
    if (selected >= 0) {
        s_payload = s_scratch[selected];
        s_generation = headers[selected].generation;
        s_active_slot = selected;
    } else if (slot_is_erased(0) && slot_is_erased(1)) {
        s_payload.next_id = 1U;
        s_generation = 0U;
        s_active_slot = -1;
    } else {
        secure_wipe(headers, sizeof(headers));
        secure_wipe(s_scratch, sizeof(s_scratch));
        xSemaphoreGive(s_lock);
        secure_wipe(key, sizeof(key));
        return ESP_ERR_INVALID_CRC;
    }
    s_unlocked = true;
    secure_wipe(headers, sizeof(headers));
    secure_wipe(s_scratch, sizeof(s_scratch));
    xSemaphoreGive(s_lock);
    secure_wipe(key, sizeof(key));
    return ESP_OK;
}

void vault_store_lock(void)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    secure_wipe(&s_payload, sizeof(s_payload));
    s_unlocked = false;
    s_active_slot = -1;
    s_generation = 0U;
    xSemaphoreGive(s_lock);
}

bool vault_store_is_unlocked(void)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool unlocked = s_unlocked;
    xSemaphoreGive(s_lock);
    return unlocked;
}

size_t vault_store_count(void)
{
    if (!s_lock) return 0U;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t count = s_unlocked ? s_payload.count : 0U;
    xSemaphoreGive(s_lock);
    return count;
}

bool vault_store_get(size_t index, vault_account_t *account)
{
    if (!s_lock || !account) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool valid = s_unlocked && index < s_payload.count;
    if (valid) *account = s_payload.accounts[index];
    xSemaphoreGive(s_lock);
    return valid;
}

static esp_err_t mutate(size_t index, const vault_account_t *account, int operation)
{
    if (!s_lock || (operation != -1 && !valid_account(account))) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_unlocked) { xSemaphoreGive(s_lock); return ESP_ERR_INVALID_STATE; }
    vault_payload_t previous = s_payload;
    esp_err_t error = ESP_OK;
    if (operation == 1) {
        if (s_payload.count >= VAULT_MAX_ACCOUNTS) error = ESP_ERR_NO_MEM;
        else {
            s_payload.accounts[s_payload.count] = *account;
            s_payload.accounts[s_payload.count].id = s_payload.next_id++;
            ++s_payload.count;
        }
    } else if (operation == 0) {
        if (index >= s_payload.count) error = ESP_ERR_NOT_FOUND;
        else { uint32_t id=s_payload.accounts[index].id; s_payload.accounts[index]=*account; s_payload.accounts[index].id=id; }
    } else {
        if (index >= s_payload.count) error = ESP_ERR_NOT_FOUND;
        else { for (size_t i=index+1U;i<s_payload.count;++i) s_payload.accounts[i-1U]=s_payload.accounts[i]; --s_payload.count; secure_wipe(&s_payload.accounts[s_payload.count],sizeof(vault_account_t)); }
    }
    if (error == ESP_OK) error = commit_locked();
    if (error != ESP_OK) s_payload = previous;
    secure_wipe(&previous, sizeof(previous));
    xSemaphoreGive(s_lock);
    return error;
}

esp_err_t vault_store_add(const vault_account_t *account) { return mutate(0U, account, 1); }
esp_err_t vault_store_update(size_t index, const vault_account_t *account) { return mutate(index, account, 0); }
esp_err_t vault_store_delete(size_t index) { return mutate(index, NULL, -1); }

esp_err_t vault_store_erase(void)
{
    if (!s_partition || !s_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    secure_wipe(&s_payload, sizeof(s_payload));
    s_unlocked = false;
    s_active_slot = -1;
    s_generation = 0U;
    esp_err_t error = esp_partition_erase_range(s_partition, 0, s_partition->size);
    xSemaphoreGive(s_lock);
    return error;
}
