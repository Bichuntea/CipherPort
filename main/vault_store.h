#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define VAULT_MAX_ACCOUNTS 16U
#define VAULT_PLATFORM_MAX 32U
#define VAULT_URL_MAX 96U
#define VAULT_USERNAME_MAX 64U
#define VAULT_PASSWORD_MAX 96U
#define VAULT_NOTE_MAX 96U

typedef struct {
    uint32_t id;
    char platform[VAULT_PLATFORM_MAX + 1U];
    char url[VAULT_URL_MAX + 1U];
    char username[VAULT_USERNAME_MAX + 1U];
    char password[VAULT_PASSWORD_MAX + 1U];
    char note[VAULT_NOTE_MAX + 1U];
} vault_account_t;

esp_err_t vault_store_init(void);
esp_err_t vault_store_unlock(void);
void vault_store_lock(void);
bool vault_store_is_unlocked(void);
size_t vault_store_count(void);
bool vault_store_get(size_t index, vault_account_t *account);
esp_err_t vault_store_add(const vault_account_t *account);
esp_err_t vault_store_update(size_t index, const vault_account_t *account);
esp_err_t vault_store_delete(size_t index);
esp_err_t vault_store_erase(void);
