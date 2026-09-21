#include "password_manager_model.h"

#include <string.h>

bool pm_time_epoch_valid(int64_t epoch_seconds)
{
    return epoch_seconds >= PM_TIME_MIN_EPOCH_UTC &&
           epoch_seconds <= PM_TIME_MAX_EPOCH_UTC;
}

int64_t pm_time_default_epoch(int16_t timezone_offset_minutes)
{
    if (timezone_offset_minutes < -840 || timezone_offset_minutes > 840) {
        timezone_offset_minutes = 0;
    }
    return PM_TIME_BASE_EPOCH_UTC + (int64_t)timezone_offset_minutes * 60;
}

static bool pm_state_has_secrets(pm_state_t state)
{
    return state == PM_STATE_PIN_SETUP ||
           state == PM_STATE_RECOVERY_CONFIRM ||
           state == PM_STATE_PIN_ENTRY ||
           state == PM_STATE_UNLOCKED ||
           state == PM_STATE_RECOVERY_ENTRY ||
           state == PM_STATE_WEB_AUTH_PENDING ||
           state == PM_STATE_WEB_ACTIVE ||
           state == PM_STATE_DEVICE_APPROVAL;
}

static bool pm_state_has_radio(pm_state_t state)
{
    return state == PM_STATE_WEB_AUTH_PENDING ||
           state == PM_STATE_WEB_ACTIVE ||
           state == PM_STATE_DEVICE_APPROVAL;
}

static uint32_t pm_enter_safe_state(pm_model_t *model, pm_state_t state)
{
    uint32_t actions = PM_ACTION_NONE;

    if (pm_state_has_secrets(model->state)) {
        actions |= PM_ACTION_WIPE_SECRETS;
    }
    if (pm_state_has_radio(model->state)) {
        actions |= PM_ACTION_STOP_RADIO;
    }
    model->state = state;
    return actions;
}

void pm_model_init(pm_model_t *model, bool enrolled, uint8_t pin_failures,
                   uint8_t recovery_failures)
{
    if (model == NULL) {
        return;
    }

    model->pin_failures = pin_failures;
    model->recovery_failures = recovery_failures;
    model->enrolled = enrolled;
    model->recovering = false;

    if (!enrolled) {
        model->state = PM_STATE_UNENROLLED;
    } else if (recovery_failures >= PM_RECOVERY_FAILURE_LIMIT) {
        model->state = PM_STATE_RECOVERY_DISABLED;
    } else if (pin_failures >= PM_PIN_FAILURE_LIMIT) {
        model->state = PM_STATE_PIN_LOCKED;
    } else {
        model->state = PM_STATE_LOCKED;
    }
}

static uint32_t pm_reject_pin(pm_model_t *model)
{
    uint32_t actions = PM_ACTION_PERSIST_SECURITY | PM_ACTION_WIPE_SECRETS;

    if (model->pin_failures < UINT8_MAX) {
        model->pin_failures++;
    }
    if (model->pin_failures >= PM_PIN_FAILURE_LIMIT) {
        if (pm_state_has_radio(model->state)) {
            actions |= PM_ACTION_STOP_RADIO;
        }
        model->state = PM_STATE_PIN_LOCKED;
    }
    return actions;
}

uint32_t pm_model_dispatch(pm_model_t *model, pm_event_t event)
{
    uint32_t actions = PM_ACTION_NONE;

    if (model == NULL) {
        return PM_ACTION_NONE;
    }

    if (event == PM_EVENT_LOCK) {
        pm_state_t destination = PM_STATE_LOCKED;

        if (!model->enrolled) {
            destination = PM_STATE_UNENROLLED;
        } else if (model->recovery_failures >= PM_RECOVERY_FAILURE_LIMIT) {
            destination = PM_STATE_RECOVERY_DISABLED;
        } else if (model->pin_failures >= PM_PIN_FAILURE_LIMIT) {
            destination = PM_STATE_PIN_LOCKED;
        }
        model->recovering = false;
        return pm_enter_safe_state(model, destination);
    }
    if (event == PM_EVENT_SLEEP) {
        model->recovering = false;
        return pm_enter_safe_state(model, PM_STATE_SLEEP_PREP);
    }

    switch (model->state) {
    case PM_STATE_UNENROLLED:
        if (event == PM_EVENT_BEGIN_ENROLLMENT) {
            model->state = PM_STATE_PIN_SETUP;
        }
        break;
    case PM_STATE_PIN_SETUP:
        if (event == PM_EVENT_PIN_ENROLLED) {
            model->state = PM_STATE_RECOVERY_CONFIRM;
        }
        break;
    case PM_STATE_RECOVERY_CONFIRM:
        if (event == PM_EVENT_RECOVERY_CONFIRMED) {
            model->enrolled = true;
            model->pin_failures = 0;
            model->recovery_failures = 0;
            actions = PM_ACTION_PERSIST_SECURITY | PM_ACTION_WIPE_SECRETS;
            if (model->recovering) {
                actions |= PM_ACTION_ROTATE_RECOVERY;
            }
            model->recovering = false;
            model->state = PM_STATE_LOCKED;
        }
        break;
    case PM_STATE_LOCKED:
        if (event == PM_EVENT_BEGIN_PIN_ENTRY) {
            model->state = PM_STATE_PIN_ENTRY;
        }
        break;
    case PM_STATE_PIN_ENTRY:
        if (event == PM_EVENT_PIN_ACCEPTED) {
            model->pin_failures = 0;
            model->state = PM_STATE_UNLOCKED;
            actions = PM_ACTION_PERSIST_SECURITY | PM_ACTION_WIPE_SECRETS;
        } else if (event == PM_EVENT_PIN_REJECTED) {
            actions = pm_reject_pin(model);
        }
        break;
    case PM_STATE_UNLOCKED:
        if (event == PM_EVENT_BEGIN_WEB_AUTH) {
            model->state = PM_STATE_WEB_AUTH_PENDING;
        }
        break;
    case PM_STATE_PIN_LOCKED:
        if (event == PM_EVENT_BEGIN_RECOVERY &&
            model->recovery_failures < PM_RECOVERY_FAILURE_LIMIT) {
            model->state = PM_STATE_RECOVERY_ENTRY;
        }
        break;
    case PM_STATE_RECOVERY_ENTRY:
        if (event == PM_EVENT_RECOVERY_ACCEPTED) {
            model->recovering = true;
            model->state = PM_STATE_PIN_SETUP;
            actions = PM_ACTION_WIPE_SECRETS;
        } else if (event == PM_EVENT_RECOVERY_REJECTED) {
            if (model->recovery_failures < UINT8_MAX) {
                model->recovery_failures++;
            }
            actions = PM_ACTION_PERSIST_SECURITY | PM_ACTION_WIPE_SECRETS;
            if (model->recovery_failures >= PM_RECOVERY_FAILURE_LIMIT) {
                model->state = PM_STATE_RECOVERY_DISABLED;
            }
        }
        break;
    case PM_STATE_WEB_AUTH_PENDING:
        if (event == PM_EVENT_PIN_ACCEPTED) {
            model->pin_failures = 0;
            model->state = PM_STATE_WEB_ACTIVE;
            actions = PM_ACTION_PERSIST_SECURITY | PM_ACTION_WIPE_SECRETS;
        } else if (event == PM_EVENT_PIN_REJECTED) {
            actions = pm_reject_pin(model);
        }
        break;
    case PM_STATE_WEB_ACTIVE:
        if (event == PM_EVENT_WEB_MUTATION_RECEIVED) {
            model->state = PM_STATE_DEVICE_APPROVAL;
        }
        break;
    case PM_STATE_DEVICE_APPROVAL:
        if (event == PM_EVENT_APPROVE_MUTATION) {
            model->state = PM_STATE_WEB_ACTIVE;
            actions = PM_ACTION_COMMIT_MUTATION;
        } else if (event == PM_EVENT_CANCEL_MUTATION) {
            model->state = PM_STATE_WEB_ACTIVE;
            actions = PM_ACTION_WIPE_SECRETS;
        }
        break;
    case PM_STATE_RECOVERY_DISABLED:
    case PM_STATE_SLEEP_PREP:
        break;
    }

    return actions;
}

/* Rejection sampling prevents modulo bias when mapping a CSPRNG word. */
static uint32_t pm_random_below(pm_secret_entry_t *entry, uint32_t limit)
{
    uint32_t value;
    uint32_t threshold = (uint32_t)(-limit) % limit;

    do {
        value = entry->random(entry->random_context);
    } while (value < threshold);
    return value % limit;
}

static void pm_secret_entry_shuffle(pm_secret_entry_t *entry)
{
    size_t i;

    for (i = 0; i < PM_DIGIT_COUNT; i++) {
        entry->order[i] = (uint8_t)i;
    }
    for (i = PM_DIGIT_COUNT - 1U; i > 0U; i--) {
        size_t swap = pm_random_below(entry, (uint32_t)i + 1U);
        uint8_t value = entry->order[i];

        entry->order[i] = entry->order[swap];
        entry->order[swap] = value;
    }
    entry->selected = 0;
}

static bool pm_secret_entry_init(pm_secret_entry_t *entry, uint8_t length,
                                 pm_random_fn_t random, void *random_context)
{
    if (entry == NULL || random == NULL ||
        (length != PM_PIN_DIGITS && length != PM_RECOVERY_DIGITS)) {
        return false;
    }

    memset(entry, 0, sizeof(*entry));
    entry->length = length;
    entry->random = random;
    entry->random_context = random_context;
    pm_secret_entry_shuffle(entry);
    return true;
}

bool pm_pin_entry_init(pm_secret_entry_t *entry, pm_random_fn_t random,
                       void *random_context)
{
    return pm_secret_entry_init(entry, PM_PIN_DIGITS, random, random_context);
}

bool pm_recovery_entry_init(pm_secret_entry_t *entry, pm_random_fn_t random,
                            void *random_context)
{
    return pm_secret_entry_init(entry, PM_RECOVERY_DIGITS, random,
                                random_context);
}

void pm_secret_entry_move(pm_secret_entry_t *entry, int direction)
{
    if (entry == NULL || entry->position >= entry->length) {
        return;
    }
    if (direction < 0) {
        entry->selected = (uint8_t)((entry->selected + PM_DIGIT_COUNT - 1U) %
                                    PM_DIGIT_COUNT);
    } else if (direction > 0) {
        entry->selected = (uint8_t)((entry->selected + 1U) % PM_DIGIT_COUNT);
    }
}

bool pm_secret_entry_confirm(pm_secret_entry_t *entry)
{
    if (entry == NULL || entry->position >= entry->length) {
        return false;
    }

    entry->digits[entry->position++] = entry->order[entry->selected];
    if (entry->position < entry->length) {
        pm_secret_entry_shuffle(entry);
    }
    return entry->position == entry->length;
}

bool pm_secret_entry_back(pm_secret_entry_t *entry)
{
    if (entry == NULL || entry->position == 0) {
        return false;
    }

    entry->position--;
    entry->digits[entry->position] = 0;
    pm_secret_entry_shuffle(entry);
    return true;
}

void pm_secret_entry_wipe(pm_secret_entry_t *entry)
{
    volatile uint8_t *bytes;
    size_t i;

    if (entry == NULL) {
        return;
    }
    bytes = (volatile uint8_t *)entry;
    for (i = 0; i < sizeof(*entry); i++) {
        bytes[i] = 0;
    }
}

void pm_scroll_init(pm_scroll_t *scroll, uint16_t content_height,
                    uint16_t minimum_panel_height,
                    uint16_t maximum_panel_height,
                    uint16_t reserved_height)
{
    uint32_t desired;

    if (scroll == NULL) {
        return;
    }
    if (maximum_panel_height < minimum_panel_height) {
        maximum_panel_height = minimum_panel_height;
    }
    desired = (uint32_t)content_height + reserved_height;
    scroll->panel_height = desired < minimum_panel_height
        ? minimum_panel_height
        : desired > maximum_panel_height ? maximum_panel_height
                                         : (uint16_t)desired;
    scroll->viewport_height = scroll->panel_height > reserved_height
        ? (uint16_t)(scroll->panel_height - reserved_height) : 0U;
    scroll->content_height = content_height;
    scroll->offset_y = 0U;
    scroll->max_offset_y = content_height > scroll->viewport_height
        ? (uint16_t)(content_height - scroll->viewport_height) : 0U;
}

bool pm_scroll_move(pm_scroll_t *scroll, int direction, uint16_t step_px)
{
    uint16_t previous;

    if (scroll == NULL || step_px == 0U) {
        return false;
    }
    previous = scroll->offset_y;
    if (direction < 0) {
        scroll->offset_y = scroll->offset_y > step_px
            ? (uint16_t)(scroll->offset_y - step_px) : 0U;
    } else if (direction > 0) {
        uint32_t next = (uint32_t)scroll->offset_y + step_px;
        scroll->offset_y = next < scroll->max_offset_y
            ? (uint16_t)next : scroll->max_offset_y;
    }
    return previous != scroll->offset_y;
}

void pm_deadline_start(pm_deadline_t *deadline, uint32_t now_ms,
                       uint32_t duration_seconds)
{
    if (deadline == NULL) {
        return;
    }
    deadline->started_ms = now_ms;
    deadline->duration_ms = duration_seconds * 1000U;
    deadline->active = duration_seconds > 0U;
}

void pm_deadline_clear(pm_deadline_t *deadline)
{
    if (deadline != NULL) {
        memset(deadline, 0, sizeof(*deadline));
    }
}

uint32_t pm_deadline_remaining_ms(const pm_deadline_t *deadline,
                                  uint32_t now_ms)
{
    uint32_t elapsed;

    if (deadline == NULL || !deadline->active) {
        return 0U;
    }
    elapsed = now_ms - deadline->started_ms;
    return elapsed >= deadline->duration_ms
        ? 0U : deadline->duration_ms - elapsed;
}

uint16_t pm_deadline_remaining_seconds(const pm_deadline_t *deadline,
                                       uint32_t now_ms)
{
    uint32_t remaining = pm_deadline_remaining_ms(deadline, now_ms);
    return (uint16_t)((remaining + 999U) / 1000U);
}

uint16_t pm_deadline_bar_width(const pm_deadline_t *deadline,
                               uint32_t now_ms, uint16_t track_width)
{
    uint32_t remaining;

    if (deadline == NULL || deadline->duration_ms == 0U) {
        return 0U;
    }
    remaining = pm_deadline_remaining_ms(deadline, now_ms);
    return (uint16_t)(((uint64_t)remaining * track_width +
                       deadline->duration_ms - 1U) /
                      deadline->duration_ms);
}

bool pm_deadline_expired(const pm_deadline_t *deadline, uint32_t now_ms)
{
    return deadline != NULL && deadline->active &&
           pm_deadline_remaining_ms(deadline, now_ms) == 0U;
}
