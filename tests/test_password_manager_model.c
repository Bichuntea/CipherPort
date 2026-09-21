#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "password_manager_model.h"

typedef struct {
    uint32_t next;
} test_rng_t;

static uint32_t test_random(void *context)
{
    test_rng_t *rng = context;

    rng->next = rng->next * 1664525U + 1013904223U;
    return rng->next;
}

static void assert_permutation(const uint8_t order[PM_DIGIT_COUNT])
{
    bool seen[PM_DIGIT_COUNT] = { false };
    size_t i;

    for (i = 0; i < PM_DIGIT_COUNT; i++) {
        assert(order[i] < PM_DIGIT_COUNT);
        assert(!seen[order[i]]);
        seen[order[i]] = true;
    }
}

static void test_boot_and_pin_lockout(void)
{
    pm_model_t model;
    uint32_t actions;
    unsigned int i;

    pm_model_init(&model, true, 0, 0);
    assert(model.state == PM_STATE_LOCKED);
    pm_model_dispatch(&model, PM_EVENT_BEGIN_PIN_ENTRY);
    assert(model.state == PM_STATE_PIN_ENTRY);

    for (i = 0; i < PM_PIN_FAILURE_LIMIT; i++) {
        actions = pm_model_dispatch(&model, PM_EVENT_PIN_REJECTED);
        assert(actions & PM_ACTION_PERSIST_SECURITY);
        assert(actions & PM_ACTION_WIPE_SECRETS);
    }
    assert(model.state == PM_STATE_PIN_LOCKED);
    assert(model.pin_failures == PM_PIN_FAILURE_LIMIT);
    pm_model_dispatch(&model, PM_EVENT_LOCK);
    assert(model.state == PM_STATE_PIN_LOCKED);

    pm_model_init(&model, true, PM_PIN_FAILURE_LIMIT, 0);
    assert(model.state == PM_STATE_PIN_LOCKED);
}

static void test_recovery_limit_and_rotation(void)
{
    pm_model_t model;
    uint32_t actions;
    unsigned int i;

    pm_model_init(&model, true, PM_PIN_FAILURE_LIMIT, 0);
    for (i = 0; i < PM_RECOVERY_FAILURE_LIMIT; i++) {
        pm_model_dispatch(&model, PM_EVENT_BEGIN_RECOVERY);
        actions = pm_model_dispatch(&model, PM_EVENT_RECOVERY_REJECTED);
        assert(actions & PM_ACTION_PERSIST_SECURITY);
    }
    assert(model.state == PM_STATE_RECOVERY_DISABLED);
    pm_model_dispatch(&model, PM_EVENT_LOCK);
    assert(model.state == PM_STATE_RECOVERY_DISABLED);

    pm_model_init(&model, true, PM_PIN_FAILURE_LIMIT, 0);
    pm_model_dispatch(&model, PM_EVENT_BEGIN_RECOVERY);
    actions = pm_model_dispatch(&model, PM_EVENT_RECOVERY_ACCEPTED);
    assert(actions & PM_ACTION_WIPE_SECRETS);
    assert(model.state == PM_STATE_PIN_SETUP);
    pm_model_dispatch(&model, PM_EVENT_PIN_ENROLLED);
    actions = pm_model_dispatch(&model, PM_EVENT_RECOVERY_CONFIRMED);
    assert(model.state == PM_STATE_LOCKED);
    assert(actions & PM_ACTION_ROTATE_RECOVERY);
}

static void test_web_approval_and_safe_exit(void)
{
    pm_model_t model;
    uint32_t actions;

    pm_model_init(&model, true, 0, 0);
    pm_model_dispatch(&model, PM_EVENT_BEGIN_PIN_ENTRY);
    pm_model_dispatch(&model, PM_EVENT_PIN_ACCEPTED);
    pm_model_dispatch(&model, PM_EVENT_BEGIN_WEB_AUTH);
    pm_model_dispatch(&model, PM_EVENT_PIN_ACCEPTED);
    assert(model.state == PM_STATE_WEB_ACTIVE);
    pm_model_dispatch(&model, PM_EVENT_WEB_MUTATION_RECEIVED);
    actions = pm_model_dispatch(&model, PM_EVENT_APPROVE_MUTATION);
    assert(model.state == PM_STATE_WEB_ACTIVE);
    assert(actions == PM_ACTION_COMMIT_MUTATION);

    actions = pm_model_dispatch(&model, PM_EVENT_LOCK);
    assert(model.state == PM_STATE_LOCKED);
    assert(actions & PM_ACTION_WIPE_SECRETS);
    assert(actions & PM_ACTION_STOP_RADIO);
}

static void test_secret_entry(void)
{
    pm_secret_entry_t entry;
    test_rng_t rng = { .next = 7U };
    size_t i;

    assert(pm_pin_entry_init(&entry, test_random, &rng));
    assert(entry.length == PM_PIN_DIGITS);
    assert_permutation(entry.order);

    pm_secret_entry_move(&entry, 1);
    assert(entry.selected == 1);
    pm_secret_entry_move(&entry, -1);
    assert(entry.selected == 0);

    for (i = 0; i < entry.length; i++) {
        bool completed = pm_secret_entry_confirm(&entry);
        assert(completed == (i + 1U == entry.length));
        if (!completed) {
            assert_permutation(entry.order);
        }
    }
    assert(pm_secret_entry_back(&entry));
    assert(entry.position == PM_PIN_DIGITS - 1U);
    assert_permutation(entry.order);

    pm_secret_entry_wipe(&entry);
    for (i = 0; i < sizeof(entry); i++) {
        assert(((const uint8_t *)&entry)[i] == 0);
    }

    assert(pm_recovery_entry_init(&entry, test_random, &rng));
    assert(entry.length == PM_RECOVERY_DIGITS);
    pm_secret_entry_wipe(&entry);
}

static void test_welcome_scroll_layout(void)
{
    pm_scroll_t scroll;

    pm_scroll_init(&scroll, 32U, 82U, 142U, 50U);
    assert(scroll.panel_height == 82U);
    assert(scroll.viewport_height == 32U);
    assert(scroll.max_offset_y == 0U);
    assert(!pm_scroll_move(&scroll, 1, 16U));

    pm_scroll_init(&scroll, 160U, 82U, 142U, 50U);
    assert(scroll.panel_height == 142U);
    assert(scroll.viewport_height == 92U);
    assert(scroll.max_offset_y == 68U);
    assert(pm_scroll_move(&scroll, 1, 16U));
    assert(scroll.offset_y == 16U);
    assert(pm_scroll_move(&scroll, 1, 100U));
    assert(scroll.offset_y == 68U);
    assert(!pm_scroll_move(&scroll, 1, 16U));
    assert(pm_scroll_move(&scroll, -1, 16U));
    assert(scroll.offset_y == 52U);
    assert(pm_scroll_move(&scroll, -1, 100U));
    assert(scroll.offset_y == 0U);
}

static void test_absolute_deadline(void)
{
    pm_deadline_t deadline;

    pm_deadline_start(&deadline, 100U, 15U);
    assert(pm_deadline_remaining_seconds(&deadline, 100U) == 15U);
    assert(pm_deadline_remaining_seconds(&deadline, 1099U) == 15U);
    assert(pm_deadline_remaining_seconds(&deadline, 1100U) == 14U);
    assert(pm_deadline_remaining_seconds(&deadline, 2600U) == 13U);
    assert(pm_deadline_remaining_seconds(&deadline, 15099U) == 1U);
    assert(!pm_deadline_expired(&deadline, 15099U));
    assert(pm_deadline_expired(&deadline, 15100U));
    assert(pm_deadline_bar_width(&deadline, 15100U, 212U) == 0U);

    pm_deadline_start(&deadline, UINT32_MAX - 499U, 2U);
    assert(pm_deadline_remaining_ms(&deadline, 500U) == 1000U);
    assert(pm_deadline_remaining_seconds(&deadline, 500U) == 1U);
    pm_deadline_clear(&deadline);
    assert(!deadline.active);
    assert(pm_deadline_remaining_seconds(&deadline, 0U) == 0U);
}

static void test_clock_defaults_and_bounds(void)
{
    assert(pm_time_default_epoch(0) == PM_TIME_BASE_EPOCH_UTC);
    assert(pm_time_default_epoch(-480) == PM_TIME_BASE_EPOCH_UTC - 28800);
    assert(pm_time_default_epoch(330) == PM_TIME_BASE_EPOCH_UTC + 19800);
    assert(pm_time_default_epoch(900) == PM_TIME_BASE_EPOCH_UTC);
    assert(pm_time_epoch_valid(PM_TIME_MIN_EPOCH_UTC));
    assert(pm_time_epoch_valid(PM_TIME_MAX_EPOCH_UTC));
    assert(!pm_time_epoch_valid(PM_TIME_MIN_EPOCH_UTC - 1));
    assert(!pm_time_epoch_valid(PM_TIME_MAX_EPOCH_UTC + 1));
}

int main(void)
{
    test_boot_and_pin_lockout();
    test_recovery_limit_and_rotation();
    test_web_approval_and_safe_exit();
    test_secret_entry();
    test_welcome_scroll_layout();
    test_absolute_deadline();
    test_clock_defaults_and_bounds();
    return 0;
}
