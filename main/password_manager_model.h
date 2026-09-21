#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PM_PIN_DIGITS 4U
#define PM_RECOVERY_DIGITS 8U
#define PM_SECRET_MAX_DIGITS PM_RECOVERY_DIGITS
#define PM_PIN_FAILURE_LIMIT 5U
#define PM_RECOVERY_FAILURE_LIMIT 3U
#define PM_DIGIT_COUNT 10U
#define PM_TIME_BASE_EPOCH_UTC INT64_C(946684800)
#define PM_TIME_MIN_EPOCH_UTC INT64_C(946634400)
#define PM_TIME_MAX_EPOCH_UTC INT64_C(4102495200)

typedef enum {
    PM_STATE_UNENROLLED = 0,
    PM_STATE_PIN_SETUP,
    PM_STATE_RECOVERY_CONFIRM,
    PM_STATE_LOCKED,
    PM_STATE_PIN_ENTRY,
    PM_STATE_UNLOCKED,
    PM_STATE_PIN_LOCKED,
    PM_STATE_RECOVERY_ENTRY,
    PM_STATE_RECOVERY_DISABLED,
    PM_STATE_WEB_AUTH_PENDING,
    PM_STATE_WEB_ACTIVE,
    PM_STATE_DEVICE_APPROVAL,
    PM_STATE_SLEEP_PREP,
} pm_state_t;

typedef enum {
    PM_EVENT_BEGIN_ENROLLMENT = 0,
    PM_EVENT_PIN_ENROLLED,
    PM_EVENT_RECOVERY_CONFIRMED,
    PM_EVENT_BEGIN_PIN_ENTRY,
    PM_EVENT_PIN_ACCEPTED,
    PM_EVENT_PIN_REJECTED,
    PM_EVENT_BEGIN_RECOVERY,
    PM_EVENT_RECOVERY_ACCEPTED,
    PM_EVENT_RECOVERY_REJECTED,
    PM_EVENT_BEGIN_WEB_AUTH,
    PM_EVENT_WEB_MUTATION_RECEIVED,
    PM_EVENT_APPROVE_MUTATION,
    PM_EVENT_CANCEL_MUTATION,
    PM_EVENT_LOCK,
    PM_EVENT_SLEEP,
} pm_event_t;

typedef enum {
    PM_ACTION_NONE = 0,
    PM_ACTION_PERSIST_SECURITY = 1U << 0,
    PM_ACTION_WIPE_SECRETS = 1U << 1,
    PM_ACTION_STOP_RADIO = 1U << 2,
    PM_ACTION_COMMIT_MUTATION = 1U << 3,
    PM_ACTION_ROTATE_RECOVERY = 1U << 4,
} pm_action_t;

typedef struct {
    pm_state_t state;
    uint8_t pin_failures;
    uint8_t recovery_failures;
    bool enrolled;
    bool recovering;
} pm_model_t;

typedef uint32_t (*pm_random_fn_t)(void *context);

typedef struct {
    uint8_t order[PM_DIGIT_COUNT];
    uint8_t digits[PM_SECRET_MAX_DIGITS];
    uint8_t length;
    uint8_t position;
    uint8_t selected;
    pm_random_fn_t random;
    void *random_context;
} pm_secret_entry_t;

typedef struct {
    uint16_t panel_height;
    uint16_t viewport_height;
    uint16_t content_height;
    uint16_t offset_y;
    uint16_t max_offset_y;
} pm_scroll_t;

typedef struct {
    uint32_t started_ms;
    uint32_t duration_ms;
    bool active;
} pm_deadline_t;

void pm_model_init(pm_model_t *model, bool enrolled, uint8_t pin_failures,
                   uint8_t recovery_failures);
uint32_t pm_model_dispatch(pm_model_t *model, pm_event_t event);

bool pm_pin_entry_init(pm_secret_entry_t *entry, pm_random_fn_t random,
                       void *random_context);
bool pm_recovery_entry_init(pm_secret_entry_t *entry, pm_random_fn_t random,
                            void *random_context);
void pm_secret_entry_move(pm_secret_entry_t *entry, int direction);
bool pm_secret_entry_confirm(pm_secret_entry_t *entry);
bool pm_secret_entry_back(pm_secret_entry_t *entry);
void pm_secret_entry_wipe(pm_secret_entry_t *entry);

void pm_scroll_init(pm_scroll_t *scroll, uint16_t content_height,
                    uint16_t minimum_panel_height,
                    uint16_t maximum_panel_height,
                    uint16_t reserved_height);
bool pm_scroll_move(pm_scroll_t *scroll, int direction, uint16_t step_px);

void pm_deadline_start(pm_deadline_t *deadline, uint32_t now_ms,
                       uint32_t duration_seconds);
void pm_deadline_clear(pm_deadline_t *deadline);
uint32_t pm_deadline_remaining_ms(const pm_deadline_t *deadline,
                                  uint32_t now_ms);
uint16_t pm_deadline_remaining_seconds(const pm_deadline_t *deadline,
                                       uint32_t now_ms);
uint16_t pm_deadline_bar_width(const pm_deadline_t *deadline,
                               uint32_t now_ms, uint16_t track_width);
bool pm_deadline_expired(const pm_deadline_t *deadline, uint32_t now_ms);

/* Browser getTimezoneOffset() convention: minutes west of UTC. */
bool pm_time_epoch_valid(int64_t epoch_seconds);
int64_t pm_time_default_epoch(int16_t timezone_offset_minutes);
