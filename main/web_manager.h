#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define WEB_MANAGER_CONNECT_SECONDS 120U
#define WEB_MANAGER_IDLE_SECONDS 600U

typedef enum {
    WEB_MANAGER_OFF = 0,
    WEB_MANAGER_STARTING,
    WEB_MANAGER_ACTIVE,
    WEB_MANAGER_STOPPING,
    WEB_MANAGER_FAILED,
} web_manager_state_t;
typedef enum { WEB_EVENT_NONE=0, WEB_EVENT_BROWSER_CONNECTED, WEB_EVENT_MUTATION_ADD, WEB_EVENT_MUTATION_EDIT, WEB_EVENT_MUTATION_DELETE, WEB_EVENT_WRITE_OK, WEB_EVENT_WRITE_FAILED, WEB_EVENT_CHANGE_PIN, WEB_EVENT_CLEAR_DATA, WEB_EVENT_DISCONNECTED, WEB_EVENT_EXPIRED } web_manager_event_type_t;
typedef struct { web_manager_event_type_t type; char platform[33]; } web_manager_event_t;
typedef struct { web_manager_state_t state; char ssid[33]; char password[16]; char address[24]; bool client_connected; bool authorized; unsigned seconds_remaining; } web_manager_status_t;

esp_err_t web_manager_init(void);
bool web_manager_request_start(void);
bool web_manager_request_stop(void);
void web_manager_get_status(web_manager_status_t *status);
bool web_manager_poll_event(web_manager_event_t *event);
void web_manager_set_authorized(bool authorized);
bool web_manager_resolve_mutation(bool approve);
int64_t web_manager_last_activity_us(void);
