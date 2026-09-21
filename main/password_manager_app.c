#include "password_manager_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_lcd_panel_ops.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"
#include "password_manager_model.h"
#include "web_manager.h"
#include "vault_store.h"

extern const lv_font_t lv_font_cipherport_cjk_12;
extern const lv_font_t lv_font_cipherport_14;
extern const lv_font_t lv_font_cipherport_19;
extern const lv_font_t lv_font_source_han_sans_sc_14_cjk;

typedef enum {
    VIEW_LANGUAGE_SETUP = 0,
    VIEW_WELCOME,
    VIEW_CREATE_PIN, VIEW_CONFIRM_PIN, VIEW_PIN_MISMATCH,
    VIEW_RECOVERY_ONCE, VIEW_RECOVERY_SAVED, VIEW_RECOVERY_VERIFY,
    VIEW_SETUP_COMPLETE, VIEW_ENTER_PIN, VIEW_PIN_ERROR, VIEW_PIN_LOCKED,
    VIEW_RECOVERY_ENTRY, VIEW_RECOVERY_ERROR, VIEW_RECOVERY_DISABLED,
    VIEW_RECOVERY_SUCCESS, VIEW_RECOVERY_NEW_PIN, VIEW_RECOVERY_CONFIRM_PIN,
    VIEW_NEW_RECOVERY_ONCE, VIEW_NEW_RECOVERY_SAVED, VIEW_NEW_RECOVERY_VERIFY,
    VIEW_RECOVERY_COMPLETE,
    VIEW_ACCOUNTS, VIEW_ACCOUNT, VIEW_REVEAL_CONFIRM, VIEW_PASSWORD,
    VIEW_SETTINGS,
    VIEW_CHANGE_CURRENT, VIEW_CHANGE_ERROR, VIEW_CHANGE_NEW,
    VIEW_CHANGE_CONFIRM, VIEW_CHANGE_MISMATCH, VIEW_CHANGE_SAVED,
    VIEW_LOCK_CONFIRM,
    VIEW_WEB_OFF, VIEW_WEB_STARTING, VIEW_WEB_WAITING,
    VIEW_WEB_BROWSER_CONNECTED, VIEW_WEB_DEVICE_PIN, VIEW_WEB_DEVICE_PIN_ERROR,
    VIEW_WEB_ACTIVE, VIEW_WEB_APPROVAL_ADD, VIEW_WEB_APPROVAL_EDIT,
    VIEW_WEB_APPROVAL_DELETE, VIEW_WEB_WRITING, VIEW_WEB_WRITE_SUCCESS,
    VIEW_WEB_WRITE_FAILED, VIEW_WEB_EXPIRED, VIEW_WEB_DISCONNECTED,
    VIEW_ERASE_HOLD, VIEW_ERASE_CONFIRM, VIEW_ERASING,
    VIEW_ERASE_COMPLETE, VIEW_ERASE_FAILED, VIEW_ABOUT,
} view_t;

typedef struct { bsp_btn_t button; bsp_btn_ev_t event; } key_message_t;

#define COLOR_BG       0x000000U
#define COLOR_TEXT     0xE8EDE8U
#define COLOR_MUTED    0x709C7DU
#define COLOR_GREEN    0x1AC46BU
#define COLOR_AMBER    0xF2B240U
#define COLOR_RED      0xFF5C61U
#define PIN_LIMIT      5U
#define RECOVERY_LIMIT 3U

static lv_obj_t *s_screen;
static lv_timer_t *s_timer;
static QueueHandle_t s_keys;
static view_t s_view;
static int s_selection;
static uint8_t s_digits[8];
static uint8_t s_first_pin[4];
static uint8_t s_recovery[8];
static uint8_t s_digit_count;
static uint8_t s_digit_target;
static uint8_t s_digit_cursor;
static bool s_digit_incomplete;
static uint8_t s_digit_order[10];
static uint8_t s_pin_failures;
static uint8_t s_recovery_failures;
static uint32_t s_security_request;
static uint32_t s_hold_started;
static pm_deadline_t s_reveal_deadline;
static pm_scroll_t s_welcome_scroll;
static size_t s_account_index;
static size_t s_password_offset;
static char s_web_platform[VAULT_PLATFORM_MAX + 1U];
static uint16_t s_rendered_reveal_seconds;
static unsigned s_rendered_web_seconds = UINT32_MAX;
static bool s_first_account_flow;
static bool s_screen_off;
static app_language_t s_language = APP_LANGUAGE_ENGLISH;
static lv_font_t s_chinese_fallback_font;
static lv_font_t s_chinese_body_font;
static lv_font_t s_chinese_title_font;
static uint32_t s_welcome_revision;
static app_display_settings_t s_display_settings;
static int64_t s_last_activity_us;
static web_manager_state_t s_rendered_web_state = (web_manager_state_t)-1;
static const uint16_t s_button_mv[BSP_BTN_COUNT][2] = BSP_BTN_MV_TABLE;

static const char *ui_text(const char *english, const char *chinese)
{
    return s_language == APP_LANGUAGE_CHINESE ? chinese : english;
}

static const lv_font_t *chinese_font(void)
{
    return &s_chinese_body_font;
}

static const lv_font_t *body_font(void)
{
    return s_language == APP_LANGUAGE_CHINESE
        ? chinese_font() : &lv_font_montserrat_12;
}

static const lv_font_t *title_font(void)
{
    if (s_language == APP_LANGUAGE_CHINESE) return &s_chinese_title_font;
    return (s_view == VIEW_WELCOME || s_view == VIEW_ACCOUNTS)
        ? &lv_font_montserrat_22 : &lv_font_montserrat_20;
}

static bool contains_non_ascii(const char *text)
{
    if (text == NULL) return false;
    while (*text != '\0') {
        if ((unsigned char)*text++ >= 0x80U) return true;
    }
    return false;
}

static const lv_font_t *content_font(const char *text,
                                     const lv_font_t *latin_font)
{
    return s_language == APP_LANGUAGE_CHINESE || contains_non_ascii(text)
        ? chinese_font() : latin_font;
}

static const lv_font_t *small_font(void)
{
    return s_language == APP_LANGUAGE_CHINESE
        ? &lv_font_cipherport_cjk_12 : &lv_font_montserrat_10;
}

static const char *ui_hint(const char *english)
{
    if (s_language != APP_LANGUAGE_CHINESE || !english) return english;
    if (strcmp(english, "UP/DN") == 0) return "上下选择";
    if (strcmp(english, "UP/DN/HOLD") == 0) return "上下/长按";
    if (strcmp(english, "UP/DN SELECT") == 0) return "上下选择";
    if (strcmp(english, "UP/DN SCROLL") == 0) return "上下滚动";
    if (strcmp(english, "UP CANCEL") == 0) return "上键取消";
    if (strcmp(english, "HOLD EXIT") == 0) return "长按返回";
    if (strcmp(english, "HOLD BACK") == 0) return "长按返回";
    if (strcmp(english, "HOLD OK BACK") == 0) return "长按OK返回";
    if (strcmp(english, "HOLD OK LOCK") == 0) return "长按OK锁屏";
    if (strcmp(english, "HOLD OK 5 SEC") == 0) return "长按OK五秒";
    if (strcmp(english, "OK CHOOSE") == 0) return "OK确认";
    if (strcmp(english, "OK OPEN") == 0) return "OK打开";
    if (strcmp(english, "OK VIEW") == 0) return "OK查看";
    if (strcmp(english, "OK REVEAL") == 0) return "OK显示";
    if (strcmp(english, "OK HIDE") == 0) return "OK隐藏";
    if (strcmp(english, "OK SAVED") == 0) return "OK已保存";
    if (strcmp(english, "OK START") == 0) return "OK启动";
    if (strcmp(english, "OK STOP") == 0) return "OK停止";
    if (strcmp(english, "OK SELECT") == 0) return "OK选择";
    if (strcmp(english, "OK CONFIRM") == 0) return "OK确认";
    if (strcmp(english, "OK/HOLD BACK") == 0) return "OK/长按返回";
    if (strcmp(english, "OK / 2x SET") == 0) return "OK/双击设置";
    return english;
}

static void wipe(void *memory, size_t length)
{
    volatile uint8_t *p = memory;
    while (length-- > 0U) *p++ = 0U;
}

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int width, int height,
                     uint32_t border, uint32_t background)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(background), 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(border), 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y,
                       int width, int height, const lv_font_t *font,
                       uint32_t color, lv_text_align_t align)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(object, align, 0);
    lv_obj_set_style_text_line_space(object, 2, 0);
    lv_label_set_long_mode(object, LV_LABEL_LONG_CLIP);
    lv_label_set_text(object, text);
    return object;
}

static void clear_screen(void)
{
    if (!s_screen) {
        s_screen = lv_obj_create(NULL);
        lv_obj_remove_style_all(s_screen);
        lv_obj_set_size(s_screen, 240, 320);
        lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_screen, lv_color_hex(COLOR_BG), 0);
        lv_scr_load(s_screen);
    } else {
        lv_obj_clean(s_screen);
    }
}

static void add_status(void)
{
    lv_obj_t *status = box(s_screen, 12, 10, 216, 18, COLOR_GREEN, COLOR_BG);

    web_manager_status_t web;
    web_manager_get_status(&web);
    if (web.state == WEB_MANAGER_ACTIVE) {
        label(status, "WIFI", 75, 2, 42, 12, &lv_font_montserrat_10,
              COLOR_GREEN, LV_TEXT_ALIGN_CENTER);
    }

    int soc = bsp_battery_soc();
    uint32_t color = COLOR_MUTED;
    int fill = 0;
    if (soc >= 0) {
        if (soc > 100) soc = 100;
        color = soc >= 50 ? COLOR_GREEN : (soc >= 20 ? COLOR_AMBER : COLOR_RED);
        fill = soc >= 50 ? 11 : (soc >= 20 ? 7 : 3);
    }
    (void)box(status, 191, 3, 18, 10, color, COLOR_BG);
    if (fill > 0) {
        const int inset_fill = fill >= 11 ? 10 : (fill >= 7 ? 6 : 2);
        lv_obj_t *bar = box(status, 194, 6, inset_fill, 4, color, color);
        lv_obj_set_style_border_width(bar, 0, 0);
    }
    lv_obj_t *cap = box(status, 209, 6, 2, 4, color, color);
    lv_obj_set_style_border_width(cap, 0, 0);
}

static void add_shell(const char *english_title, const char *chinese_title,
                      const char *left_hint, const char *right_hint)
{
    add_status();
    label(s_screen, ui_text(english_title, chinese_title), 12, 34, 216, 28,
          title_font(), COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
    lv_obj_t *line = box(s_screen, 12, 66, 216, 1, COLOR_GREEN, COLOR_GREEN);
    lv_obj_set_style_border_width(line, 0, 0);
    const bool has_left = left_hint != NULL && left_hint[0] != '\0';
    const bool has_right = right_hint != NULL && right_hint[0] != '\0';
    if (has_left || has_right) {
        lv_obj_t *hint = box(s_screen, 12, 288, 216, 22, COLOR_MUTED, COLOR_BG);
        if (has_left && has_right) {
            label(hint, ui_hint(left_hint), 3, 3, 86, 16, body_font(),
                  COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
            label(hint, ui_hint(right_hint), 91, 3, 120, 16, body_font(),
                  COLOR_TEXT, LV_TEXT_ALIGN_RIGHT);
        } else {
            label(hint, ui_hint(has_left ? left_hint : right_hint), 4, 3, 206, 16,
                  body_font(), has_left ? COLOR_MUTED : COLOR_TEXT,
                  has_left ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT);
        }
    }
}

static void finish_screen(void)
{
    web_manager_status_t web;
    web_manager_get_status(&web);
    s_rendered_web_state = web.state;
    s_rendered_web_seconds = web.seconds_remaining;
    lv_obj_invalidate(s_screen);
}

static void add_account_list_hints(void)
{
    lv_obj_t *hint = box(s_screen, 12, 270, 216, 40, COLOR_MUTED, COLOR_BG);
    label(hint, ui_text("UP/DN    OK OPEN", "上下选择    OK打开"),
          4, 3, 206, 16, body_font(), COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
    label(hint, ui_text("2x OK SET    HOLD OK LOCK", "双击OK设置    长按OK锁屏"),
          4, 20, 206, 16, small_font(), COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
}

static void action_pair(const char *left_en, const char *left_zh,
                        const char *right_en, const char *right_zh,
                        int selected)
{
    lv_obj_t *left = box(s_screen, 12, 246, 104, 30,
                         selected == 0 ? COLOR_GREEN : COLOR_MUTED,
                         selected == 0 ? COLOR_GREEN : COLOR_BG);
    lv_obj_t *right = box(s_screen, 124, 246, 104, 30,
                          selected == 1 ? COLOR_GREEN : COLOR_MUTED,
                          selected == 1 ? COLOR_GREEN : COLOR_BG);
    const char *left_text = ui_text(left_en, left_zh);
    const char *right_text = ui_text(right_en, right_zh);
    label(left, left_text, 0, 7, 102, 16,
          content_font(left_text, body_font()),
          selected == 0 ? COLOR_BG : COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
    label(right, right_text, 0, 7, 102, 16,
          content_font(right_text, body_font()),
          selected == 1 ? COLOR_BG : COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
}

static void full_action(const char *english, const char *chinese,
                        uint32_t accent, bool filled)
{
    lv_obj_t *action = box(s_screen, 12, 246, 216, 30, accent,
                           filled ? accent : COLOR_BG);
    if (!filled) {
        lv_obj_t *rail = box(action, 0, 0, 4, 28, accent, accent);
        lv_obj_set_style_border_width(rail, 0, 0);
    }
    const char *action_text = ui_text(english, chinese);
    label(action, action_text, 8, 7, 198, 16,
          content_font(action_text, body_font()),
          filled ? COLOR_BG : accent, LV_TEXT_ALIGN_CENTER);
}

static void state_panel(const char *heading_en, const char *heading_zh,
                        const char *body_en, const char *body_zh,
                        const char *footer_en, const char *footer_zh,
                        uint32_t accent)
{
    const char *body = ui_text(body_en, body_zh);
    lv_obj_t *panel = box(s_screen, 12, 72, 216, 158, accent, COLOR_BG);
    const char *heading = ui_text(heading_en, heading_zh);
    const char *footer = ui_text(footer_en, footer_zh);
    label(panel, heading, 11, 11, 192, 20,
          content_font(heading, body_font()), accent, LV_TEXT_ALIGN_LEFT);
    label(panel, body, 11, 39, 192, 52,
          content_font(body, body_font()), COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
    label(panel, footer, 11, 108, 192, 36,
          content_font(footer, body_font()), COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
}

static void render_language_setup(void)
{
    clear_screen();
    add_shell("SELECT LANGUAGE", "选择语言", "UP/DN SELECT", "OK CONFIRM");
    state_panel("DEVICE LANGUAGE", "设备语言",
                "Choose the language used by\nthe device and Web manager.",
                "选择设备和网页管理使用的语言。",
                "SAVED FOR NEXT START", "选择会保存到设备",
                COLOR_GREEN);
    action_pair("ENGLISH", "ENGLISH", "中文", "中文", s_selection);
    finish_screen();
}

static void render_about(void)
{
    clear_screen();
    add_shell("ABOUT", "关于", "", "HOLD OK BACK");
    lv_obj_t *panel = box(s_screen, 12, 72, 216, 184, COLOR_GREEN, COLOR_BG);
    label(panel, "Cipherport", 11, 10, 192, 26, &lv_font_montserrat_20,
          COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
    label(panel, "Password Manager", 11, 39, 192, 19, &lv_font_montserrat_14,
          COLOR_GREEN, LV_TEXT_ALIGN_LEFT);
    lv_obj_t *line = box(panel, 11, 66, 192, 1, COLOR_MUTED, COLOR_MUTED);
    lv_obj_set_style_border_width(line, 0, 0);
    label(panel, "GITHUB REPOSITORY", 11, 76, 192, 14,
          &lv_font_montserrat_10, COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
    label(panel, "github.com/Bichuntea/CipherPort", 11, 94, 192, 16,
          &lv_font_montserrat_10, COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
    label(panel, "Author - Bichuntea", 11, 121, 192, 16,
          &lv_font_montserrat_12, COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
    label(panel, "Version - 1.0", 11, 147, 192, 16,
          &lv_font_montserrat_12, COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
    finish_screen();
}

static void shuffle_digits(void)
{
    for (uint8_t i = 0U; i < 10U; ++i) s_digit_order[i] = i;
    for (uint8_t i = 9U; i > 0U; --i) {
        uint32_t limit = UINT32_MAX - (UINT32_MAX % (i + 1U));
        uint32_t value;
        do value = esp_random(); while (value >= limit);
        uint8_t other = (uint8_t)(value % (i + 1U));
        uint8_t swap = s_digit_order[i]; s_digit_order[i] = s_digit_order[other]; s_digit_order[other] = swap;
    }
    s_digit_cursor = 0U;
}

static void begin_digits(uint8_t target)
{
    wipe(s_digits, sizeof(s_digits));
    s_digit_count = 0U;
    s_digit_target = target;
    s_digit_incomplete = false;
    shuffle_digits();
}

static void draw_selector(int top)
{
    lv_obj_t *selector = box(s_screen, 12, top, 216, 80, COLOR_MUTED, COLOR_BG);
    label(selector, ui_text("SELECT DIGIT OR ACTION", "选择数字或操作"), 5, 0, 204, 16,
          body_font(), COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
    for (int i = 0; i < 10; ++i) {
        int x = 5 + (i % 5) * 42;
        int y = 18 + (i / 5) * 32;
        bool selected = i == s_digit_cursor;
        lv_obj_t *cell = box(selector, x, y, 38, 26,
                             selected ? COLOR_GREEN : COLOR_MUTED,
                             selected ? COLOR_GREEN : COLOR_BG);
        if (selected) lv_obj_set_style_border_width(cell, 2, 0);
        char digit[2] = {(char)('0' + s_digit_order[i]), '\0'};
        label(cell, digit, 0, 3, 36, 18, &lv_font_montserrat_14,
              selected ? COLOR_BG : COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
    }
}

static void draw_entry_slots(int top)
{
    int width = s_digit_target == 4U ? 36 : 23;
    int gap = s_digit_target == 4U ? 10 : 4;
    int start = s_digit_target == 4U ? 33 : 12;
    for (uint8_t i = 0U; i < s_digit_target; ++i) {
        bool active = i == s_digit_count && s_digit_count < s_digit_target;
        char value[2] = {'_', '\0'};
        if (i < s_digit_count) value[0] = s_digit_target == 4U ? '*' : (char)('0' + s_digits[i]);
        label(s_screen, value, start + i * (width + gap), top + 7, width, 24,
              &lv_font_montserrat_18,
              i < s_digit_count ? COLOR_TEXT : (active ? COLOR_GREEN : COLOR_MUTED),
              LV_TEXT_ALIGN_CENTER);
    }
}

static bool is_digit_view(view_t view)
{
    switch (view) {
    case VIEW_CREATE_PIN: case VIEW_CONFIRM_PIN: case VIEW_RECOVERY_VERIFY:
    case VIEW_ENTER_PIN: case VIEW_RECOVERY_ENTRY: case VIEW_RECOVERY_NEW_PIN:
    case VIEW_RECOVERY_CONFIRM_PIN: case VIEW_NEW_RECOVERY_VERIFY:
    case VIEW_CHANGE_CURRENT: case VIEW_CHANGE_NEW: case VIEW_CHANGE_CONFIRM:
        return true;
    default: return false;
    }
}

static void render(void);
static void render_digit_view(void);
static void handle_key(key_message_t key);
static void handle_security_result(const app_security_result_t *result);

static bool is_web_view(view_t view)
{
    return view >= VIEW_WEB_OFF && view <= VIEW_WEB_DISCONNECTED;
}

static void leave_web_management(void)
{
    (void)web_manager_request_stop();
    s_selection = 0;
    s_view = s_first_account_flow ? VIEW_ACCOUNTS : VIEW_SETTINGS;
    render();
}

static void digit_copy(const char **title_en, const char **title_zh,
                       const char **prompt_en, const char **prompt_zh,
                       const char **step_en, const char **step_zh,
                       const char **submit_en, const char **submit_zh)
{
    *title_en = "ENTER PIN"; *title_zh = "输入PIN";
    *prompt_en = "ENTER YOUR 4-DIGIT PIN"; *prompt_zh = "输入4位PIN";
    *step_en = "5 ATTEMPTS"; *step_zh = "共5次机会";
    *submit_en = "CONFIRM"; *submit_zh = "确认";
    switch (s_view) {
    case VIEW_CREATE_PIN:
        *title_en="CREATE PIN"; *title_zh="创建PIN";
        *prompt_en="ENTER A NEW 4-DIGIT PIN"; *prompt_zh="输入新的4位PIN";
        *step_en="STEP 1 OF 2"; *step_zh="第1步/共2步";
        *submit_en="CONTINUE"; *submit_zh="继续"; break;
    case VIEW_CONFIRM_PIN:
        *title_en="CONFIRM PIN"; *title_zh="确认PIN";
        *prompt_en="RE-ENTER YOUR NEW PIN"; *prompt_zh="再次输入新PIN";
        *step_en="STEP 2 OF 2"; *step_zh="第2步/共2步"; break;
    case VIEW_RECOVERY_VERIFY: case VIEW_NEW_RECOVERY_VERIFY:
        *title_en="VERIFY CODE"; *title_zh="验证恢复码";
        *prompt_en="ENTER ALL 8 DIGITS"; *prompt_zh="输入完整8位数字";
        *step_en="LEADING ZEROES COUNT"; *step_zh="开头的0也要输入";
        *submit_en="VERIFY"; *submit_zh="验证"; break;
    case VIEW_RECOVERY_ENTRY:
        *title_en="RECOVERY"; *title_zh="恢复";
        *prompt_en="ENTER RECOVERY CODE"; *prompt_zh="输入恢复码";
        *step_en="3 ATTEMPTS"; *step_zh="共3次机会";
        *submit_en="VERIFY"; *submit_zh="验证"; break;
    case VIEW_RECOVERY_NEW_PIN:
        *title_en="NEW PIN"; *title_zh="新PIN";
        *prompt_en="CREATE A NEW 4-DIGIT PIN"; *prompt_zh="创建新的4位PIN";
        *step_en="RECOVERY 1 OF 2"; *step_zh="恢复第1步/共2步";
        *submit_en="CONTINUE"; *submit_zh="继续"; break;
    case VIEW_RECOVERY_CONFIRM_PIN:
        *title_en="CONFIRM PIN"; *title_zh="确认PIN";
        *prompt_en="RE-ENTER THE NEW PIN"; *prompt_zh="再次输入新PIN";
        *step_en="RECOVERY 2 OF 2"; *step_zh="恢复第2步/共2步"; break;
    case VIEW_CHANGE_CURRENT:
        *title_en="CURRENT PIN"; *title_zh="当前PIN";
        *prompt_en="VERIFY CURRENT PIN"; *prompt_zh="验证当前PIN";
        *step_en="STEP 1 OF 3"; *step_zh="第1步/共3步"; break;
    case VIEW_CHANGE_NEW:
        *title_en="NEW PIN"; *title_zh="新PIN";
        *prompt_en="ENTER A NEW 4-DIGIT PIN"; *prompt_zh="输入新的4位PIN";
        *step_en="STEP 2 OF 3"; *step_zh="第2步/共3步";
        *submit_en="CONTINUE"; *submit_zh="继续"; break;
    case VIEW_CHANGE_CONFIRM:
        *title_en="CONFIRM PIN"; *title_zh="确认PIN";
        *prompt_en="RE-ENTER THE NEW PIN"; *prompt_zh="再次输入新PIN";
        *step_en="STEP 3 OF 3"; *step_zh="第3步/共3步";
        *submit_en="SAVE"; *submit_zh="保存"; break;
    case VIEW_WEB_DEVICE_PIN:
        *title_en="DEVICE PIN"; *title_zh="设备PIN验证";
        *prompt_en="AUTHORIZE WEB SESSION"; *prompt_zh="授权本次网页会话";
        *step_en="PIN STAYS ON DEVICE"; *step_zh="PIN不会发送到网页";
        *submit_en="AUTHORIZE"; *submit_zh="授权"; break;
    default: break;
    }
}

static void render_digit_view(void)
{
    const char *title_en, *title_zh, *prompt_en, *prompt_zh;
    const char *step_en, *step_zh, *submit_en, *submit_zh;
    digit_copy(&title_en, &title_zh, &prompt_en, &prompt_zh,
               &step_en, &step_zh, &submit_en, &submit_zh);
    if (s_view == VIEW_ENTER_PIN) {
        step_en = "";
        step_zh = "";
    }
    clear_screen();
    add_shell(title_en, title_zh, "UP/DN/HOLD", "OK CHOOSE");
    label(s_screen, ui_text(prompt_en, prompt_zh), 12, 72, 216, 14,
          body_font(), COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
    draw_entry_slots(88);
    draw_selector(s_digit_target == 4U ? 136 : 132);
    char incomplete[48];
    if (s_language == APP_LANGUAGE_CHINESE) {
        snprintf(incomplete, sizeof(incomplete), "请完整输入%u位数字",
                 (unsigned)s_digit_target);
    } else {
        snprintf(incomplete, sizeof(incomplete), "ENTER ALL %u DIGITS",
                 (unsigned)s_digit_target);
    }
    label(s_screen, s_digit_incomplete ? incomplete : ui_text(step_en, step_zh), 12,
          s_digit_target == 4U ? 224 : 220, 216, 16,
          body_font(), s_digit_incomplete ? COLOR_RED : COLOR_MUTED,
          LV_TEXT_ALIGN_LEFT);
    action_pair("CLEAR", "清除", submit_en, submit_zh,
                s_digit_cursor == 10U ? 0 : (s_digit_cursor == 11U ? 1 : -1));
    finish_screen();
}

static void __attribute__((unused)) render_recovery_code_legacy(bool rotated)
{
    char code[9];
    for (size_t i = 0U; i < 8U; ++i) code[i] = (char)('0' + s_recovery[i]);
    code[8] = '\0';
    clear_screen();
    add_shell("SUPER ACCESS", "恢复码", "", "OK SAVED");
    state_panel(rotated ? "NEW 8-DIGIT CODE" : "SAVE THIS 8-DIGIT CODE",
                rotated ? "新恢复码" : "请抄写保存",
                "Shown once. Keep it offline.", "仅显示一次，请离线保存。",
                code, code, COLOR_GREEN);
    lv_obj_t *saved = box(s_screen, 12, 246, 216, 30, COLOR_GREEN, COLOR_GREEN);
    label(saved, "I SAVED IT", 0, 7, 214, 16, body_font(), COLOR_BG,
          LV_TEXT_ALIGN_CENTER);
    finish_screen();
}

static void render_recovery_code(bool rotated)
{
    char code[9];
    for (size_t i = 0U; i < 8U; ++i) code[i] = (char)('0' + s_recovery[i]);
    code[8] = '\0';
    clear_screen();
    add_shell("RECOVERY CODE", "恢复码", "", "");
    lv_obj_t *panel = box(s_screen, 12, 72, 216, 158, COLOR_GREEN, COLOR_BG);
    label(panel, ui_text(rotated ? "WRITE NEW CODE DOWN" : "WRITE THIS CODE DOWN",
                         rotated ? "请抄写新恢复码" : "请抄写恢复码"),
          11, 11, 192, 20, body_font(), COLOR_GREEN, LV_TEXT_ALIGN_LEFT);
    label(panel, ui_text("Shown once. Keep it offline and\nprivate.",
                         "仅显示一次，请离线保存。"), 11, 39, 192, 30,
          body_font(), COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
    label(panel, code, 11, 77, 192, 28, &lv_font_montserrat_22,
          COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
    full_action("I SAVED IT", "我已保存", COLOR_GREEN, true);
    finish_screen();
}

static void render_accounts(void)
{
    size_t count = vault_store_count();
    if (count > 0U) {
        s_first_account_flow = false;
        clear_screen();
        add_shell("SAVED ACCOUNTS", "已保存账户", "", "");
        if (s_account_index >= count) s_account_index = count - 1U;
        size_t first = s_account_index > 3U ? s_account_index - 3U : 0U;
        for (size_t row = 0U; row < 4U && first + row < count; ++row) {
            vault_account_t account;
            if (!vault_store_get(first + row, &account)) continue;
            bool selected = first + row == s_account_index;
            lv_obj_t *entry = box(s_screen, 8, 72 + (int)row * 50, 224, 44,
                                  selected ? COLOR_GREEN : COLOR_MUTED, COLOR_BG);
            if (selected) {
                lv_obj_t *rail = box(entry, 0, 0, 4, 42, COLOR_GREEN, COLOR_GREEN);
                lv_obj_set_style_border_width(rail, 0, 0);
            }
            label(entry, account.platform, 10, 3, 202, 18,
                  contains_non_ascii(account.platform)
                      ? &lv_font_cipherport_cjk_12 : &lv_font_montserrat_14,
                  selected ? COLOR_GREEN : COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
            if (account.note[0] != '\0') {
                label(entry, account.note, 10, 24, 202, 16,
                      contains_non_ascii(account.note)
                          ? &lv_font_cipherport_cjk_12 : &lv_font_montserrat_12,
                      COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
            }
            wipe(&account, sizeof(account));
        }
        add_account_list_hints();
        finish_screen();
        return;
    }
    s_first_account_flow = true;
    clear_screen();
    add_shell("SAVED ACCOUNTS", "已保存账号", "", "");
    state_panel("NO ACCOUNTS SAVED", "尚未保存账号",
                "Start Web Management to add\nyour first account.",
                "启动网页管理，录入第一个账号。",
                "FIRST ACCOUNT SETUP", "第一个账号设置", COLOR_GREEN);
    lv_obj_t *action = box(s_screen, 12, 246, 216, 30, COLOR_GREEN, COLOR_GREEN);
    label(action, ui_text("START WEB MANAGEMENT", "启动网页管理"), 0, 7, 214, 16,
          body_font(), COLOR_BG, LV_TEXT_ALIGN_CENTER);
    finish_screen();
    return;
    clear_screen();
    add_shell("SAVED ACCOUNTS", "已保存账号", "UP/DN", "OK OPEN");
    state_panel("NO ACCOUNTS", "暂无账号",
                "Use Web Management to add your first account.",
                "请通过网页管理添加第一个账号。",
                "2xOK SETTINGS", "双击OK进入设置", COLOR_GREEN);
    finish_screen();
}

static void render_account_real(void)
{
    vault_account_t account;
    if (!vault_store_get(s_account_index, &account)) {
        s_view = VIEW_ACCOUNTS;
        render_accounts();
        return;
    }
    clear_screen();
    add_shell("ACCOUNT", "账号详情", "HOLD EXIT", "OK VIEW");
    lv_obj_t *panel = box(s_screen, 12, 72, 216, 158, COLOR_GREEN, COLOR_BG);
    label(panel, ui_text("TITLE", "标题名称"), 10, 7, 194, 16,
          body_font(), COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
    label(panel, account.platform, 10, 25, 194, 20,
          content_font(account.platform, body_font()), COLOR_GREEN, LV_TEXT_ALIGN_LEFT);
    label(panel, ui_text("ACCOUNT", "账号"), 10, 49, 194, 16,
          body_font(), COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
    label(panel, account.username, 10, 67, 194, 20,
          content_font(account.username, body_font()), COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
    if (account.note[0] != '\0') {
        label(panel, ui_text("NOTE", "备注"), 10, 91, 194, 16,
              body_font(), COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
        lv_obj_t *note = label(panel, account.note, 10, 109, 194, 38,
                               content_font(account.note, body_font()), COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
        lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);
    }
    lv_obj_t *action = box(s_screen, 12, 246, 216, 30, COLOR_GREEN, COLOR_GREEN);
    label(action, ui_text("VIEW PASSWORD", "查看密码"), 0, 7, 214, 16,
          body_font(), COLOR_BG, LV_TEXT_ALIGN_CENTER);
    wipe(&account, sizeof(account));
    finish_screen();
}

static void draw_countdown(int top, unsigned remaining_seconds,
                           unsigned total_seconds)
{
    uint32_t accent = COLOR_GREEN;
    if (remaining_seconds * 20U <= total_seconds * 3U) accent = COLOR_RED;
    else if (remaining_seconds * 5U <= total_seconds * 2U) accent = COLOR_AMBER;
    char countdown[24];
    if (s_language == APP_LANGUAGE_CHINESE) {
        snprintf(countdown, sizeof(countdown), "剩余%u秒", remaining_seconds);
    } else {
        snprintf(countdown, sizeof(countdown), "T-%u SEC", remaining_seconds);
    }
    label(s_screen, countdown, 12, top, 216, 16, body_font(),
          accent, LV_TEXT_ALIGN_LEFT);
    lv_obj_t *track = box(s_screen, 12, top + 22, 216, 14, COLOR_MUTED, COLOR_BG);
    lv_obj_set_style_border_width(track, 2, 0);
    unsigned width = total_seconds == 0U ? 0U
        : (remaining_seconds * 212U + total_seconds - 1U) / total_seconds;
    if (width > 212U) width = 212U;
    if (width > 0U) {
        lv_obj_t *bar = box(track, 0, 0, (int)width, 10, accent, accent);
        lv_obj_set_style_border_width(bar, 0, 0);
    }
}

static void render_password_real(void)
{
    vault_account_t account;
    if (!vault_store_get(s_account_index, &account)) {
        s_view = VIEW_ACCOUNTS;
        render_accounts();
        return;
    }
    size_t length = strnlen(account.password, sizeof(account.password));
    const size_t visible = 22U;
    size_t maximum = length > visible ? length - visible : 0U;
    if (s_password_offset > maximum) s_password_offset = maximum;
    char shown[23];
    size_t shown_len = length - s_password_offset;
    if (shown_len > visible) shown_len = visible;
    memcpy(shown, account.password + s_password_offset, shown_len);
    shown[shown_len] = '\0';
    clear_screen();
    add_shell("PASSWORD", "密码", "UP/DN SCROLL", "OK HIDE");
    lv_obj_t *window = box(s_screen, 12, 72, 216, 92, COLOR_MUTED, COLOR_BG);
    label(window, ui_text("PASSWORD", "密码"), 11, 9, 192, 16, body_font(), COLOR_GREEN, LV_TEXT_ALIGN_LEFT);
    label(window, shown, 11, 31, 192, 22, &lv_font_montserrat_18, COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
    char range[28];
    snprintf(range, sizeof(range), "%u-%u / %u", (unsigned)s_password_offset + 1U,
             (unsigned)(s_password_offset + shown_len), (unsigned)length);
    label(window, range, 11, 61, 192, 14, body_font(), COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
    draw_countdown(176, pm_deadline_remaining_seconds(&s_reveal_deadline,
                                                      lv_tick_get()),
                   s_display_settings.reveal_seconds);
    lv_obj_t *hide = box(s_screen, 12, 246, 216, 30, COLOR_GREEN, COLOR_GREEN);
    label(hide, ui_text("HIDE NOW", "立即隐藏"), 0, 7, 214, 16, body_font(), COLOR_BG, LV_TEXT_ALIGN_CENTER);
    wipe(shown, sizeof(shown));
    wipe(&account, sizeof(account));
    finish_screen();
}

static void render_settings(void)
{
    static const char *const en[] = {"WEB MANAGEMENT", "CHANGE PIN", "LOCK NOW", "CLEAR ALL DATA", "ABOUT", "LANGUAGE: ENGLISH/中文"};
    static const char *const zh[] = {"网页管理", "修改PIN", "立即锁定", "清除全部数据", "关于", "语言：中文/English"};
    clear_screen();
    add_shell("SETTINGS", "设置", "UP/DN/HOLD", "OK OPEN");
    for (int row = 0; row < 6; ++row) {
        int item = row;
        lv_obj_t *entry = box(s_screen, 12, 72 + row * 32, 216, 27,
                              item == s_selection ? COLOR_GREEN : COLOR_MUTED,
                              item == s_selection ? COLOR_GREEN : COLOR_BG);
        const char *entry_text = ui_text(en[item], zh[item]);
        label(entry, entry_text, 10, 5, 194, 18,
              content_font(entry_text, body_font()),
              item == s_selection ? COLOR_BG : COLOR_TEXT,
              LV_TEXT_ALIGN_LEFT);
    }
    finish_screen();
}

static void __attribute__((unused)) render_web_legacy(void)
{
    web_manager_status_t web;
    web_manager_get_status(&web);
    if (web.state == WEB_MANAGER_ACTIVE) {
        clear_screen();
        add_shell(web.authorized ? "WEB ACTIVE" : "WEB MANAGEMENT",
                  web.authorized ? "网页管理中" : "网页管理",
                  "HOLD BACK", "");
        lv_obj_t *panel = box(s_screen, 12, 72, 216, 92, COLOR_GREEN, COLOR_BG);
        if (web.authorized) {
            label(panel, ui_text("CONNECTED", "浏览器已连接"), 11, 9,
                  192, 18, body_font(), COLOR_GREEN, LV_TEXT_ALIGN_LEFT);
            label(panel, ui_text("Web management is active.", "账号管理连接已启用。"),
                  11, 35, 192, 18, body_font(), COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
            label(panel, ui_text("ONE DEVICE ONLY", "一个设备已授权"), 11, 61,
                  192, 18, body_font(), COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
        } else {
            char credentials[128];
            snprintf(credentials, sizeof(credentials), "SSID %s\nPASS %s\nOPEN %s",
                     web.ssid, web.password, web.address);
            label(panel, ui_text("DEVICE WI-FI DETAILS", "连接设备 Wi-Fi"), 11, 7,
                  192, 16, body_font(), COLOR_GREEN, LV_TEXT_ALIGN_LEFT);
            label(panel, credentials, 11, 27, 192, 42, &lv_font_montserrat_10,
                  COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
            label(panel, "JOIN WIFI, THEN OPEN ADDRESS", 11, 72, 192, 12,
                  &lv_font_montserrat_10, COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
            wipe(credentials, sizeof(credentials));
        }
        if (!web.authorized) {
            draw_countdown(176, web.seconds_remaining,
                           WEB_MANAGER_CONNECT_SECONDS);
        } else {
            lv_obj_t *connected = box(s_screen, 12, 176, 216, 42,
                                      COLOR_GREEN, COLOR_BG);
            label(connected, "CONNECTED", 10, 3, 194, 12,
                  &lv_font_montserrat_10, COLOR_GREEN, LV_TEXT_ALIGN_CENTER);
            label(connected, "ONE DEVICE ONLY", 10, 15, 194, 12,
                  &lv_font_montserrat_10, COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
            label(connected, "EXIT CLOSES WI-FI", 10, 27, 194, 12,
                  &lv_font_montserrat_10, COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
        }
        lv_obj_t *stop = box(s_screen, 12, 246, 216, 30, COLOR_GREEN, COLOR_BG);
        lv_obj_t *rail = box(stop, 0, 0, 4, 28, COLOR_GREEN, COLOR_GREEN);
        lv_obj_set_style_border_width(rail, 0, 0);
        label(stop, ui_text("STOP", "停止"), 10, 7, 194, 16,
              body_font(), COLOR_GREEN, LV_TEXT_ALIGN_CENTER);
        s_rendered_web_seconds = web.seconds_remaining;
        finish_screen();
        return;
    }
    const char *heading_en = "WEB ACCESS OFF", *heading_zh = "网页访问已关闭";
    const char *body_en = "Wi-Fi stays off after boot.\nStart only when needed.";
    const char *body_zh = "设备启动后Wi-Fi默认关闭。\n仅在需要时开启。";
    const char *footer_en = "OK START", *footer_zh = "按OK启动";
    if (s_view == VIEW_WEB_STARTING || web.state == WEB_MANAGER_STARTING) {
        heading_en="STARTING"; heading_zh="正在启动网页管理";
        body_en="Creating a protected network."; body_zh="正在创建受保护的网络。";
        footer_en="PLEASE WAIT"; footer_zh="请稍候";
    } else if (web.state == WEB_MANAGER_ACTIVE && web.authorized) {
        clear_screen();
        add_shell("WEB ACTIVE", "网页管理已连接", "HOLD BACK", "OK SELECT");
        state_panel("BROWSER CONNECTED", "浏览器已连接",
                    "Account management is active\nover the local service.",
                    "账户管理已通过本机启用。",
                    "ONE APPROVED CLIENT", "一个已授权设备", COLOR_GREEN);
        lv_obj_t *stop = box(s_screen, 12, 246, 216, 30, COLOR_GREEN, COLOR_BG);
        lv_obj_t *rail = box(stop, 0, 0, 4, 28, COLOR_GREEN, COLOR_GREEN);
        lv_obj_set_style_border_width(rail, 0, 0);
        label(stop, ui_text("STOP", "停止"), 10, 7, 194, 16,
              body_font(), COLOR_GREEN, LV_TEXT_ALIGN_CENTER);
        finish_screen(); return;
    } else if (web.state == WEB_MANAGER_ACTIVE) {
        char body[128];
        snprintf(body, sizeof(body), "%s\n%s\n%s", web.address, web.ssid, web.password);
        clear_screen();
        add_shell("WEB MANAGEMENT", "网页管理", "HOLD BACK", "OK STOP");
        state_panel("CONNECT TO DEVICE WI-FI", "连接设备 Wi-Fi", body, body,
                    "OPEN ADDRESS IN BROWSER", "在浏览器中打开地址", COLOR_GREEN);
        lv_obj_t *stop = box(s_screen, 12, 246, 216, 30, COLOR_GREEN, COLOR_BG);
        label(stop, ui_text("STOP", "停止"), 0, 7, 214, 16,
              body_font(), COLOR_GREEN, LV_TEXT_ALIGN_CENTER);
        finish_screen(); return;
    }
    clear_screen(); add_shell("WEB MANAGEMENT", "网页管理", "UP/DN", "OK START");
    state_panel(heading_en, heading_zh, body_en, body_zh, footer_en, footer_zh, COLOR_GREEN);
    finish_screen();
}

static void render_web(void)
{
    web_manager_status_t web;
    web_manager_get_status(&web);
    const char *ok_hint = (s_view == VIEW_WEB_STARTING ||
                           web.state == WEB_MANAGER_STARTING ||
                           web.state == WEB_MANAGER_ACTIVE)
        ? "OK STOP" : "OK START";
    clear_screen();
    add_shell(web.authorized ? "WEB ACTIVE" : "WEB MANAGEMENT",
              web.authorized ? "网页管理已连接" : "网页管理",
              "HOLD BACK", ok_hint);

    if (s_view == VIEW_WEB_STARTING || web.state == WEB_MANAGER_STARTING) {
        state_panel("STARTING", "正在启动",
                    "Creating a protected Wi-Fi\nnetwork.",
                    "正在创建受保护的Wi-Fi网络。",
                    "ONE DEVICE ONLY", "仅允许一台设备", COLOR_GREEN);
        full_action("STOP", "停止", COLOR_GREEN, true);
        finish_screen();
        return;
    }

    if (web.state == WEB_MANAGER_ACTIVE && web.authorized) {
        state_panel("CONNECTED", "已连接",
                    "Web Management is active.",
                    "网页管理已启用。",
                    "ONE DEVICE ONLY", "仅允许一台设备", COLOR_GREEN);
        full_action("STOP", "停止", COLOR_GREEN, true);
        finish_screen();
        return;
    }

    if (web.state == WEB_MANAGER_ACTIVE && web.client_connected) {
        state_panel("WI-FI CONNECTED", "Wi-Fi已连接",
                    "Open in browser:\nhttp://192.168.4.1/",
                    "请在浏览器打开：\nhttp://192.168.4.1/",
                    "WAITING FOR BROWSER", "等待浏览器访问", COLOR_GREEN);
        full_action("STOP", "停止", COLOR_GREEN, true);
        finish_screen();
        return;
    }

    if (web.state == WEB_MANAGER_ACTIVE) {
        char details[128];
        if (s_language == APP_LANGUAGE_CHINESE) {
            snprintf(details, sizeof(details), "Wi-Fi %s\n密码 %s\n地址 %s",
                     web.ssid, web.password, web.address);
        } else {
            snprintf(details, sizeof(details), "SSID %s\nPASS %s\nOPEN %s",
                     web.ssid, web.password, web.address);
        }
        lv_obj_t *panel = box(s_screen, 12, 72, 216, 92,
                              COLOR_GREEN, COLOR_BG);
        label(panel, ui_text("CONNECT TO DEVICE WI-FI", "连接设备Wi-Fi"), 11, 7, 192, 16,
              body_font(), COLOR_GREEN, LV_TEXT_ALIGN_LEFT);
        label(panel, details, 11, 27, 192, 56, body_font(),
              COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
        draw_countdown(176, web.seconds_remaining,
                       WEB_MANAGER_CONNECT_SECONDS);
        full_action("STOP", "停止", COLOR_GREEN, true);
        wipe(details, sizeof(details));
        s_rendered_web_seconds = web.seconds_remaining;
        finish_screen();
        return;
    }

    state_panel("WEB ACCESS OFF", "网页访问已关闭",
                "Wi-Fi stays off after boot.\nStart only when needed.",
                "设备启动后Wi-Fi保持关闭，\n仅在需要时开启。",
                "LOCAL CONNECTION ONLY", "仅限本地连接",
                COLOR_GREEN);
    full_action("START", "启动", COLOR_GREEN, true);
    finish_screen();
}

static void render_message(const char *title_en, const char *title_zh,
                           const char *heading_en, const char *heading_zh,
                           const char *body_en, const char *body_zh,
                           const char *footer_en, const char *footer_zh,
                           uint32_t accent, const char *action_en,
                           const char *action_zh)
{
    clear_screen();
    add_shell(title_en, title_zh, "", "");
    state_panel(heading_en, heading_zh, body_en, body_zh,
                footer_en, footer_zh, accent);
    const char *visible_action = action_en;
    const char *visible_action_zh = action_zh;
    if (strcmp(title_en, "WRITE FAILED") == 0) {
        visible_action = "DONE";
        visible_action_zh = "DONE";
    }
    if (visible_action != NULL && visible_action[0] != '\0' &&
        strcmp(visible_action, "WAIT") != 0) {
        full_action(visible_action, visible_action_zh, accent, true);
    }
    finish_screen();
}

static void __attribute__((unused)) render_password(void)
{
    clear_screen();
    add_shell("PASSWORD", "密码", "UP/DN SCROLL", "OK HIDE");
    lv_obj_t *window = box(s_screen, 12, 72, 216, 92, COLOR_MUTED, COLOR_BG);
    label(window, "PASSWORD", 11, 9, 192, 14, body_font(), COLOR_GREEN, LV_TEXT_ALIGN_LEFT);
    label(window, "VAULT EMPTY", 11, 31, 192, 22, &lv_font_montserrat_18, COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
    label(window, "1-11 / 11", 11, 61, 192, 14, body_font(), COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
    draw_countdown(176, pm_deadline_remaining_seconds(&s_reveal_deadline,
                                                      lv_tick_get()), 15U);
    lv_obj_t *hide = box(s_screen, 12, 246, 216, 30, COLOR_GREEN, COLOR_GREEN);
    label(hide, ui_text("HIDE NOW", "立即隐藏"), 0, 7, 214, 16, body_font(), COLOR_BG, LV_TEXT_ALIGN_CENTER);
    finish_screen();
}

static void render_welcome(void)
{
    char english[APP_WELCOME_MAX_BYTES + 1U];
    lv_point_t measured = {0};
    app_config_copy_welcome(english);
    const char *message = english;
    const lv_font_t *message_font = content_font(message, &lv_font_montserrat_12);
    lv_text_get_size(&measured, message, message_font, 0, 2, 192,
                     LV_TEXT_FLAG_NONE);
    uint16_t previous_offset = s_welcome_scroll.offset_y;
    pm_scroll_init(&s_welcome_scroll, measured.y > 0 ? (uint16_t)measured.y : 16U,
                   82U, 142U, 50U);
    if (previous_offset > s_welcome_scroll.max_offset_y) {
        previous_offset = s_welcome_scroll.max_offset_y;
    }
    s_welcome_scroll.offset_y = previous_offset;
    clear_screen();
    add_shell("CIPHERPORT", "CIPHERPORT",
              s_welcome_scroll.max_offset_y > 0U ? "UP/DN SCROLL" : "",
              "HOLD OK LOCK");
    lv_obj_t *panel = box(s_screen, 12, 72, 216, s_welcome_scroll.panel_height,
                          COLOR_MUTED, COLOR_BG);
    label(panel, ui_text("WELCOME", "欢迎"), 11, 11, 192, 20,
          body_font(), COLOR_GREEN, LV_TEXT_ALIGN_LEFT);
    lv_obj_t *viewport = lv_obj_create(panel);
    lv_obj_remove_style_all(viewport);
    lv_obj_set_pos(viewport, 11, 39);
    lv_obj_set_size(viewport, 192, s_welcome_scroll.viewport_height);
    lv_obj_clear_flag(viewport, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *body = label(viewport, message, 0,
                           -(int)s_welcome_scroll.offset_y,
                           192, measured.y > 0 ? measured.y : 16,
                           message_font, COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(body, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_t *action = box(s_screen, 12, 246, 216, 34, COLOR_GREEN, COLOR_BG);
    lv_obj_t *rail = box(action, 0, 0, 4, 32, COLOR_GREEN, COLOR_GREEN);
    lv_obj_set_style_border_width(rail, 0, 0);
    label(action, ui_text("PRESS OK TO CONTINUE", "按OK继续"), 10, 8, 194, 18,
          body_font(), COLOR_GREEN, LV_TEXT_ALIGN_CENTER);
    wipe(english, sizeof(english));
    finish_screen();
}

static void render_pin_locked(void)
{
    clear_screen();
    add_shell("PIN LOCKED", "PIN已锁定", "UP/DN SELECT", "OK CHOOSE");
    state_panel("5 FAILED ATTEMPTS", "已连续失败5次",
                "Choose recovery or securely\nerase the vault.",
                "请选择恢复，或安全清除密码库。",
                "cardid IS PRESERVED", "保留cardid", COLOR_RED);
    action_pair("RECOVER", "恢复", "ERASE", "清除", s_selection);
    finish_screen();
}

static void render_pin_mismatch(bool changing)
{
    clear_screen();
    add_shell("PIN MISMATCH", "PIN不一致", "", "");
    state_panel("PINS DO NOT MATCH", "两次PIN不一致",
                changing ? "Both new-PIN entries\nwere cleared."
                         : "Both entries were cleared.",
                changing ? "两次新PIN输入均已清除。"
                         : "两次输入均已清除。",
                "", "", COLOR_RED);
    full_action("TRY AGAIN", "重试", COLOR_GREEN, true);
    finish_screen();
}

static void render_web_approval(bool adding, bool deleting)
{
    const char *title = adding ? "ADD ACCOUNT" :
                        (deleting ? "DELETE ACCOUNT" : "EDIT ACCOUNT");
    clear_screen();
    add_shell(title, adding ? "新增账号" : (deleting ? "删除账号" : "修改账号"),
              "UP/DN SELECT", "OK CHOOSE");
    state_panel("DEVICE APPROVAL REQUIRED", "需要设备确认",
                s_web_platform[0] ? s_web_platform : "ACCOUNT",
                s_web_platform[0] ? s_web_platform : "账号",
                deleting ? "THIS CANNOT BE UNDONE" : "REVIEW BEFORE WRITING",
                deleting ? "此操作无法撤销" : "写入前请检查",
                deleting ? COLOR_RED : COLOR_AMBER);
    action_pair(deleting ? "DELETE" : "APPROVE",
                deleting ? "删除" : "批准",
                "CANCEL", "取消", s_selection);
    finish_screen();
}

static void render(void)
{
    if (is_digit_view(s_view)) { render_digit_view(); return; }
    switch (s_view) {
    case VIEW_LANGUAGE_SETUP:
        render_language_setup(); break;
    case VIEW_WELCOME:
        render_welcome(); break;
    case VIEW_PIN_MISMATCH: render_pin_mismatch(false); break;
#if 0
    case VIEW_PIN_MISMATCH_LEGACY:
        render_message("PIN MISMATCH","PIN不一致","PINS DO NOT MATCH","两次PIN不一致",
                       "Both entries were cleared.","两次输入均已清除。","NO DATA WAS SAVED","尚未保存任何数据",
                       COLOR_AMBER,"TRY AGAIN","请重试"); break;
#endif
    case VIEW_RECOVERY_ONCE: render_recovery_code(false); break;
    case VIEW_NEW_RECOVERY_ONCE: render_recovery_code(true); break;
    case VIEW_RECOVERY_SAVED: case VIEW_NEW_RECOVERY_SAVED:
        render_message("SAVING CODE","保存恢复码","PLEASE WAIT","请稍候",
                       "Securing the 8-digit code.","正在安全保存8位恢复码。",
                       "DO NOT POWER OFF","请勿断电",COLOR_GREEN,"WAIT","等待"); break;
    case VIEW_SETUP_COMPLETE:
        render_message("SETUP COMPLETE","设置完成","VAULT READY","密码库已就绪",
                       "PIN and super code saved.","PIN和恢复码验证信息已保存。",
                       "SECURELY SAVED","安全保存",COLOR_GREEN,"LOCK","锁定"); break;
    case VIEW_PIN_ERROR: {
        char en[32], zh[32]; unsigned left=PIN_LIMIT-s_pin_failures;
        snprintf(en,sizeof(en),"%u TRIES LEFT",left); snprintf(zh,sizeof(zh),"剩余%u次",left);
        render_message("PIN ERROR","PIN错误","PIN NOT ACCEPTED","PIN不正确",
                       "CHECK PIN","请检查PIN。",en,zh,
                       COLOR_AMBER,"TRY AGAIN","重试"); break; }
    case VIEW_PIN_LOCKED: render_pin_locked(); break;
#if 0
    case VIEW_PIN_LOCKED_LEGACY:
        render_message("PIN LOCKED","PIN已锁定","5 FAILED ATTEMPTS","已连续失败5次",
                       "Recovery or erase is available.","可以使用恢复码或清除数据。",
                       "UP RECOVER/DN ERASE","上键恢复 · 下键清除",COLOR_RED,"RECOVER","恢复"); break;
#endif
    case VIEW_RECOVERY_ERROR: {
        char en[32], zh[32]; unsigned left=RECOVERY_LIMIT-s_recovery_failures;
        snprintf(en,sizeof(en),"%u ATTEMPTS LEFT",left); snprintf(zh,sizeof(zh),"剩余%u次",left);
        render_message("RECOVERY ERROR","恢复码错误","CODE REJECTED","恢复码不正确",
                       "Leading zeroes are significant.","开头的0也必须完全一致。",en,zh,
                       COLOR_AMBER,"TRY AGAIN","重试"); break; }
    case VIEW_RECOVERY_DISABLED:
        render_message("RECOVERY DISABLED","恢复已禁用","3 FAILED ATTEMPTS","已连续失败3次",
                       "Only device erase remains.","现在只能清除设备数据。","cardid WILL BE PRESERVED","将保留cardid",
                       COLOR_RED,"ERASE OPTIONS","清除选项"); break;
    case VIEW_RECOVERY_SUCCESS:
        render_message("RECOVERY SUCCESS","恢复成功","CREATE NEW CREDENTIALS","创建新的凭据",
                       "Old PIN will be replaced.","原PIN将被替换。","NEW SUPER CODE NEXT","随后生成新的恢复码",
                       COLOR_GREEN,"CONTINUE","继续"); break;
    case VIEW_RECOVERY_COMPLETE:
        render_message("RECOVERY COMPLETE","恢复完成","CREDENTIALS ROTATED","凭据已更新",
                       "Old PIN and recovery code are invalid.","旧PIN和旧恢复码已经失效。","DEVICE RETURNS TO LOCKED","设备将返回锁定状态",
                       COLOR_GREEN,"LOCK","锁定"); break;
    case VIEW_ACCOUNTS: render_accounts(); break;
    case VIEW_ACCOUNT:
        render_account_real(); break;
        clear_screen(); add_shell("ACCOUNT","账号","UP/DN SCROLL","OK REVEAL");
        state_panel("ACCOUNT 01","账号01","USERNAME\nNot configured\nPASSWORD\n********","用户名\n尚未配置\n密码\n********","WEB MANAGEMENT TO EDIT","请通过网页管理编辑",COLOR_GREEN);
        { lv_obj_t *a=box(s_screen,12,246,216,30,COLOR_GREEN,COLOR_GREEN); label(a,ui_text("REVEAL PASSWORD","显示密码"),0,7,214,16,body_font(),COLOR_BG,LV_TEXT_ALIGN_CENTER); }
        finish_screen(); break;
    case VIEW_REVEAL_CONFIRM:
        clear_screen(); add_shell("CONFIRM","确认","UP/DN", "OK/HOLD BACK");
        { char footer_en[40],footer_zh[40];
          snprintf(footer_en,sizeof(footer_en),"PLAINTEXT FOR %u SECONDS",(unsigned)s_display_settings.reveal_seconds);
          snprintf(footer_zh,sizeof(footer_zh),"明文显示%u秒",(unsigned)s_display_settings.reveal_seconds);
          state_panel("REVEAL PASSWORD?","显示密码？","Anyone nearby may see it.","附近的人可能看到密码。",footer_en,footer_zh,COLOR_AMBER); }
        action_pair("REVEAL","显示","CANCEL","取消",s_selection); finish_screen(); break;
    case VIEW_PASSWORD: render_password_real(); break;
    case VIEW_SETTINGS: render_settings(); break;
    case VIEW_CHANGE_ERROR:
        render_message("CURRENT PIN","当前PIN","WRONG PIN","PIN不正确","NOT CHANGED","尚未修改","VERIFY AGAIN","请重新验证",COLOR_AMBER,"TRY AGAIN","重试"); break;
    case VIEW_CHANGE_MISMATCH: render_pin_mismatch(true); break;
#if 0
    case VIEW_CHANGE_MISMATCH_LEGACY:
        render_message("PIN MISMATCH","PIN不一致","PINS DO NOT MATCH","两次PIN不一致","BOTH DRAFTS CLEARED","两次输入均已清除","NOT SAVED","尚未保存",COLOR_AMBER,"TRY AGAIN","重试"); break;
#endif
    case VIEW_CHANGE_SAVED:
        render_message("PIN SAVED","PIN已保存","WRAPPER UPDATED","保护信息已更新","OLD PIN IS INVALID","旧PIN已经失效","SECURITY DATA SAVED","安全信息已提交",COLOR_GREEN,"DONE","完成"); break;
    case VIEW_LOCK_CONFIRM:
        clear_screen(); add_shell("LOCK NOW","立即锁定","UP/DN","OK/HOLD BACK");
        state_panel("LOCK DEVICE?","锁定设备？","Web access and plaintext will close.","网页访问和明文显示将关闭。","PENDING REQUESTS DENIED","未保存的网页请求将被拒绝",COLOR_AMBER);
        action_pair("LOCK","锁定","CANCEL","取消",s_selection); finish_screen(); break;
case VIEW_WEB_OFF: case VIEW_WEB_STARTING: case VIEW_WEB_WAITING:
    case VIEW_WEB_ACTIVE: render_web(); break;
    case VIEW_WEB_BROWSER_CONNECTED:
        render_message("BROWSER FOUND","发现浏览器","BROWSER CONNECTED","浏览器已连接",
                       "One device connected.\nPress OK to authorize.","一台设备已连接。\n按OK授权网页管理。",
                       "","",COLOR_GREEN,"OK","OK"); break;
#if 0
    case VIEW_WEB_BROWSER_CONNECTED_LEGACY:
        render_message("BROWSER CONNECTED","浏览器已连接","DEVICE PIN REQUIRED","需要设备PIN","Authorize on this device only.","只能在本设备上授权。","PIN STAYS ON DEVICE","PIN不会进入浏览器",COLOR_GREEN,"ENTER PIN","输入PIN"); break;
    case VIEW_WEB_DEVICE_PIN_ERROR:
        render_message("DEVICE PIN","设备PIN验证","WRONG PIN","PIN不正确","WEB ACCESS DENIED","网页会话未获授权","NO TOKEN ISSUED","未签发会话令牌",COLOR_AMBER,"TRY AGAIN","重试"); break;
#endif
    case VIEW_WEB_APPROVAL_ADD: case VIEW_WEB_APPROVAL_EDIT: case VIEW_WEB_APPROVAL_DELETE: {
        const bool deleting=s_view==VIEW_WEB_APPROVAL_DELETE; const bool adding=s_view==VIEW_WEB_APPROVAL_ADD;
        render_web_approval(adding, deleting); break; }
#if 0
    case VIEW_WEB_APPROVAL_LEGACY: {
        const bool deleting=s_view==VIEW_WEB_APPROVAL_DELETE; const bool adding=s_view==VIEW_WEB_APPROVAL_ADD;
        render_message(adding?"ADD ACCOUNT":deleting?"DELETE ACCOUNT":"EDIT ACCOUNT",
                       adding?"新增账号":deleting?"删除账号":"修改账号",
                       "DEVICE APPROVAL REQUIRED","需要设备确认",s_web_platform[0]?s_web_platform:"ACCOUNT",s_web_platform[0]?s_web_platform:"账户",
                       deleting?"THIS CANNOT BE UNDONE":"REVIEW BEFORE WRITING",
                       deleting?"此操作无法撤销":"写入前请检查",deleting?COLOR_RED:COLOR_AMBER,
                       deleting?"DELETE":"APPROVE",deleting?"删除":"批准"); break; }
#endif
    case VIEW_WEB_WRITING:
        render_message("WRITING","正在写入","DO NOT POWER OFF","请勿断电","Atomic storage commit in progress.","正在进行原子存储提交。","INPUT TEMPORARILY LOCKED","输入暂时锁定",COLOR_AMBER,"WAIT","等待"); break;
    case VIEW_WEB_WRITE_SUCCESS:
        render_message("WRITE COMPLETE","写入完成","ACCOUNT DATA SAVED","账号数据已保存","Browser may continue.","浏览器可以继续操作。","ATOMIC COMMIT VERIFIED","原子提交已验证",COLOR_GREEN,"DONE","完成"); break;
    case VIEW_WEB_WRITE_FAILED:
        render_message("WRITE FAILED","写入失败","OLD DATA PRESERVED","旧数据已保留","Retry or deny the request.","可以重试或拒绝请求。","NO PARTIAL WRITE","没有启用不完整记录",COLOR_RED,"RETRY","重试"); break;
    case VIEW_WEB_EXPIRED:
        render_message("SESSION EXPIRED","会话已过期","WEB ACCESS CLOSED","网页访问已关闭","Start a new session if needed.","需要时请重新启动会话。","TOKENS WERE INVALIDATED","会话令牌已失效",COLOR_AMBER,"DONE","完成"); break;
    case VIEW_WEB_DISCONNECTED:
        render_message("DISCONNECTED","连接已断开","CLIENT LEFT","浏览器已断开","Pending changes were denied.","待处理修改已被拒绝。","WI-FI WILL STOP","Wi-Fi将关闭",COLOR_AMBER,"DONE","完成"); break;
    case VIEW_ERASE_HOLD:
        clear_screen(); add_shell("CLEAR ALL DATA","清除全部数据","UP CANCEL","HOLD OK 5 SEC");
        state_panel("DANGER","危险","All vault records and key wrappers will be removed.","密码库记录和密钥保护信息将被删除。","cardid IS PRESERVED","保留cardid",COLOR_RED);
        { lv_obj_t *a=box(s_screen,12,246,216,30,COLOR_RED,COLOR_RED); label(a,ui_text("HOLD OK 5 SEC","长按OK 5秒"),0,7,214,16,body_font(),COLOR_BG,LV_TEXT_ALIGN_CENTER); }
        finish_screen(); break;
    case VIEW_ERASE_CONFIRM:
        clear_screen(); add_shell("ERASE CONFIRM","确认清除","UP/DN","OK CONFIRM");
        state_panel("CANNOT BE UNDONE","无法撤销","Erase vault and security metadata?","清除密码库和安全信息？","cardid REMAINS UNCHANGED","cardid保持不变",COLOR_RED);
        action_pair("CANCEL","取消","ERASE","清除",s_selection); finish_screen(); break;
    case VIEW_ERASING:
        render_message("ERASING","正在清除","DO NOT POWER OFF","请勿断电","Destroying key wrappers first.","正在先销毁密钥保护信息。","cardid IS NOT TOUCHED","不会触碰cardid",COLOR_RED,"WAIT","等待"); break;
    case VIEW_ERASE_COMPLETE:
        render_message("ERASE COMPLETE","清除完成","DEVICE RESET","设备已重置","Create a new PIN to continue.","请创建新PIN后继续。","cardid PRESERVED","已保留cardid",COLOR_GREEN,"SET UP","开始设置"); break;
    case VIEW_ERASE_FAILED:
        render_message("ERASE FAILED","清除失败","DEVICE REMAINS LOCKED","设备仍保持锁定","Retry before using the device.","请重试后再使用设备。","ERASE NOT CONFIRMED","不要假定数据已经清除",COLOR_RED,"RETRY","重试"); break;
    case VIEW_ABOUT:
        render_about(); break;
    default: break;
    }
}

static void generate_recovery(void)
{
    for (size_t i = 0U; i < sizeof(s_recovery); ++i) {
        uint32_t value;
        const uint32_t limit = UINT32_MAX - (UINT32_MAX % 10U);
        do value = esp_random(); while (value >= limit);
        s_recovery[i] = (uint8_t)(value % 10U);
    }
}

static uint32_t submit_security(app_security_operation_t operation,
                                const uint8_t *digits, size_t length)
{
    uint32_t request = app_config_security_submit(operation, digits, length,
                                                   s_pin_failures,
                                                   s_recovery_failures);
    if (request != 0U) s_security_request = request;
    return request;
}

static void persist_failures(void)
{
    (void)app_config_security_submit(APP_SECURITY_SET_FAILURES, NULL, 0,
                                     s_pin_failures, s_recovery_failures);
}

static void submit_digits(void)
{
    if (s_security_request != 0U || s_digit_count != s_digit_target) return;
    switch (s_view) {
    case VIEW_CREATE_PIN:
        memcpy(s_first_pin, s_digits, sizeof(s_first_pin));
        begin_digits(4); s_view = VIEW_CONFIRM_PIN; render(); break;
    case VIEW_CONFIRM_PIN:
        if (memcmp(s_first_pin, s_digits, sizeof(s_first_pin)) != 0) {
            wipe(s_first_pin, sizeof(s_first_pin)); wipe(s_digits, sizeof(s_digits));
            s_view = VIEW_PIN_MISMATCH; render();
        } else if (submit_security(APP_SECURITY_STORE_PIN, s_digits, 4U) != 0U) {
            wipe(s_first_pin, sizeof(s_first_pin)); wipe(s_digits, sizeof(s_digits));
        }
        break;
    case VIEW_RECOVERY_VERIFY:
        if (memcmp(s_recovery, s_digits, sizeof(s_recovery)) != 0) {
            begin_digits(8); render();
        } else if (submit_security(APP_SECURITY_STORE_RECOVERY, s_digits, 8U) != 0U) {
            wipe(s_digits, sizeof(s_digits));
        }
        break;
    case VIEW_ENTER_PIN:
    case VIEW_CHANGE_CURRENT:
    case VIEW_WEB_DEVICE_PIN:
        if (submit_security(APP_SECURITY_VERIFY_PIN, s_digits, 4U) != 0U)
            wipe(s_digits, sizeof(s_digits));
        break;
    case VIEW_RECOVERY_ENTRY:
        if (submit_security(APP_SECURITY_VERIFY_RECOVERY, s_digits, 8U) != 0U)
            wipe(s_digits, sizeof(s_digits));
        break;
    case VIEW_RECOVERY_NEW_PIN:
    case VIEW_CHANGE_NEW:
        memcpy(s_first_pin, s_digits, sizeof(s_first_pin));
        begin_digits(4);
        s_view = s_view == VIEW_RECOVERY_NEW_PIN ? VIEW_RECOVERY_CONFIRM_PIN : VIEW_CHANGE_CONFIRM;
        render(); break;
    case VIEW_RECOVERY_CONFIRM_PIN:
    case VIEW_CHANGE_CONFIRM:
        if (memcmp(s_first_pin, s_digits, sizeof(s_first_pin)) != 0) {
            wipe(s_first_pin, sizeof(s_first_pin)); wipe(s_digits, sizeof(s_digits));
            s_view = s_view == VIEW_RECOVERY_CONFIRM_PIN ? VIEW_PIN_MISMATCH : VIEW_CHANGE_MISMATCH;
            render();
        } else if (submit_security(APP_SECURITY_STORE_PIN, s_digits, 4U) != 0U) {
            wipe(s_first_pin, sizeof(s_first_pin)); wipe(s_digits, sizeof(s_digits));
        }
        break;
    case VIEW_NEW_RECOVERY_VERIFY:
        if (memcmp(s_recovery, s_digits, sizeof(s_recovery)) != 0) {
            begin_digits(8); render();
        } else if (submit_security(APP_SECURITY_STORE_RECOVERY, s_digits, 8U) != 0U) {
            wipe(s_digits, sizeof(s_digits));
        }
        break;
    default: break;
    }
}

static void handle_security_result(const app_security_result_t *result)
{
    if (result->request_id != s_security_request) return;
    s_security_request = 0U;
    if (result->error != ESP_OK) {
        if (result->operation == APP_SECURITY_ERASE) s_view = VIEW_ERASE_FAILED;
        else if (result->operation == APP_SECURITY_STORE_PIN) s_view = VIEW_CHANGE_ERROR;
        else if (result->operation == APP_SECURITY_STORE_RECOVERY) {
            s_view = s_view == VIEW_NEW_RECOVERY_SAVED
                ? VIEW_NEW_RECOVERY_ONCE : VIEW_RECOVERY_ONCE;
        }
        else s_view = VIEW_PIN_ERROR;
        render(); return;
    }
    switch (result->operation) {
    case APP_SECURITY_STORE_PIN:
        if (s_view == VIEW_CONFIRM_PIN) {
            generate_recovery(); s_view = VIEW_RECOVERY_ONCE;
        } else if (s_view == VIEW_RECOVERY_CONFIRM_PIN) {
            generate_recovery(); s_view = VIEW_NEW_RECOVERY_ONCE;
        } else {
            s_view = VIEW_CHANGE_SAVED;
        }
        render(); break;
    case APP_SECURITY_STORE_RECOVERY:
        s_pin_failures = 0U; s_recovery_failures = 0U; persist_failures();
        (void)vault_store_unlock();
        wipe(s_recovery, sizeof(s_recovery));
        s_view = s_view == VIEW_NEW_RECOVERY_SAVED
            ? VIEW_RECOVERY_COMPLETE : VIEW_SETUP_COMPLETE;
        render(); break;
    case APP_SECURITY_VERIFY_PIN:
        if (s_view == VIEW_ENTER_PIN) {
            if (result->accepted) {
                s_pin_failures = 0U; persist_failures();
                if (vault_store_unlock() == ESP_OK) s_view = VIEW_ACCOUNTS;
                else s_view = VIEW_PIN_ERROR;
            } else {
                if (s_pin_failures < PIN_LIMIT) ++s_pin_failures;
                persist_failures();
                if (s_pin_failures >= PIN_LIMIT) { s_selection = 0; s_view = VIEW_PIN_LOCKED; }
                else s_view = VIEW_PIN_ERROR;
            }
        } else if (s_view == VIEW_CHANGE_CURRENT) {
            s_view = result->accepted ? VIEW_CHANGE_NEW : VIEW_CHANGE_ERROR;
            if (result->accepted) begin_digits(4);
        } else {
            bool authorized = result->accepted && vault_store_unlock() == ESP_OK;
            web_manager_set_authorized(authorized);
            if (authorized) { s_pin_failures=0U; persist_failures(); s_view=VIEW_WEB_ACTIVE; }
            else { if(s_pin_failures<PIN_LIMIT)++s_pin_failures;persist_failures();if(s_pin_failures>=PIN_LIMIT){(void)web_manager_request_stop();s_view=VIEW_PIN_LOCKED;}else s_view=VIEW_WEB_DEVICE_PIN_ERROR; }
        }
        render(); break;
    case APP_SECURITY_VERIFY_RECOVERY:
        if (result->accepted) {
            s_recovery_failures = 0U; persist_failures(); s_view = VIEW_RECOVERY_SUCCESS;
        } else {
            if (s_recovery_failures < RECOVERY_LIMIT) ++s_recovery_failures;
            persist_failures();
            s_view = s_recovery_failures >= RECOVERY_LIMIT ? VIEW_RECOVERY_DISABLED : VIEW_RECOVERY_ERROR;
        }
        render(); break;
    case APP_SECURITY_ERASE:
        wipe(s_first_pin, sizeof(s_first_pin)); wipe(s_recovery, sizeof(s_recovery));
        s_pin_failures = 0U; s_recovery_failures = 0U;
        if (result->accepted && vault_store_erase() != ESP_OK) s_view = VIEW_ERASE_FAILED;
        else s_view = result->accepted ? VIEW_ERASE_COMPLETE : VIEW_ERASE_FAILED;
        render(); break;
    default: break;
    }
}

static void handle_digit_key(key_message_t key)
{
    if (key.event == BSP_BTN_LONG && key.button == BSP_BTN_OK) {
        wipe(s_digits, sizeof(s_digits));
        if (s_security_request != 0U) return;
        if (s_view == VIEW_ENTER_PIN || s_view == VIEW_CREATE_PIN) s_view = VIEW_WELCOME;
        else if (s_view == VIEW_CONFIRM_PIN) {
            wipe(s_first_pin, sizeof(s_first_pin)); begin_digits(4); s_view = VIEW_CREATE_PIN;
        }
        else if (s_view == VIEW_RECOVERY_VERIFY) s_view = VIEW_RECOVERY_SAVED;
        else if (s_view == VIEW_NEW_RECOVERY_VERIFY) s_view = VIEW_NEW_RECOVERY_SAVED;
        else if (s_view == VIEW_RECOVERY_NEW_PIN) s_view = VIEW_RECOVERY_SUCCESS;
        else if (s_view == VIEW_RECOVERY_CONFIRM_PIN) {
            wipe(s_first_pin, sizeof(s_first_pin)); begin_digits(4); s_view = VIEW_RECOVERY_NEW_PIN;
        }
        else if (s_view == VIEW_CHANGE_CURRENT || s_view == VIEW_CHANGE_NEW || s_view == VIEW_CHANGE_CONFIRM) s_view = VIEW_SETTINGS;
        else if (s_view == VIEW_WEB_DEVICE_PIN) {
            (void)web_manager_request_stop();
            s_view = s_first_account_flow ? VIEW_ACCOUNTS : VIEW_SETTINGS;
        }
        else if (s_view == VIEW_RECOVERY_ENTRY) s_view = VIEW_PIN_LOCKED;
        render(); return;
    }
    if (key.event != BSP_BTN_CLICK || s_security_request != 0U) return;
    if (key.button == BSP_BTN_UP) {
        s_digit_cursor = (uint8_t)((s_digit_cursor + 11U) % 12U);
        s_digit_incomplete = false;
    } else if (key.button == BSP_BTN_DOWN) {
        s_digit_cursor = (uint8_t)((s_digit_cursor + 1U) % 12U);
        s_digit_incomplete = false;
    } else if (s_digit_cursor < 10U) {
        if (s_digit_count < s_digit_target) {
            s_digits[s_digit_count++] = s_digit_order[s_digit_cursor];
            s_digit_incomplete = false;
            if (s_digit_count < s_digit_target) {
                shuffle_digits();
            } else {
                s_digit_cursor = 11U;
            }
        }
    } else if (s_digit_cursor == 10U) {
        begin_digits(s_digit_target);
    } else if (s_digit_count != s_digit_target) {
        s_digit_incomplete = true;
    } else {
        submit_digits();
    }
    render();
}

static void go_locked(void)
{
    (void)web_manager_request_stop();
    pm_deadline_clear(&s_reveal_deadline);
    vault_store_lock();
    app_config_lock_vault();
    wipe(s_digits, sizeof(s_digits)); wipe(s_first_pin, sizeof(s_first_pin));
    s_view = VIEW_WELCOME; s_selection = 0; render();
}

static void enter_screen_off(void)
{
    (void)web_manager_request_stop();
    pm_deadline_clear(&s_reveal_deadline);
    vault_store_lock();
    app_config_lock_vault();
    wipe(s_digits, sizeof(s_digits));
    wipe(s_first_pin, sizeof(s_first_pin));
    wipe(s_recovery, sizeof(s_recovery));
    bsp_display_backlight(0);
    esp_lcd_panel_handle_t panel = bsp_display_panel();
    if (panel != NULL) (void)esp_lcd_panel_disp_on_off(panel, false);
    s_view = app_config_language_configured()
        ? VIEW_WELCOME : VIEW_LANGUAGE_SETUP;
    s_selection = 0;
    s_welcome_scroll.offset_y = 0U;
    s_screen_off = true;
}

static void wake_screen(void)
{
    /* Keep the panel and backlight off until the home screen has replaced the
       previous frame in display RAM.  Otherwise the ST7789 briefly exposes
       the page that was visible when the display was turned off. */
    s_screen_off = false;
    s_last_activity_us = esp_timer_get_time();
    render();
    lv_refr_now(NULL);

    esp_lcd_panel_handle_t panel = bsp_display_panel();
    if (panel != NULL) (void)esp_lcd_panel_disp_on_off(panel, true);
    bsp_display_backlight(100);
}

static void handle_key(key_message_t key)
{
    if (is_digit_view(s_view)) { handle_digit_key(key); return; }
    if (s_view == VIEW_ERASE_HOLD && key.button == BSP_BTN_OK && key.event == BSP_BTN_PRESS) {
        s_hold_started = lv_tick_get(); return;
    }
    if (key.event == BSP_BTN_LONG && key.button == BSP_BTN_OK &&
        (s_view == VIEW_WELCOME || s_view == VIEW_ACCOUNTS)) {
        enter_screen_off();
        return;
    }
    if (key.event == BSP_BTN_LONG && key.button == BSP_BTN_OK &&
        s_view != VIEW_ERASE_HOLD) {
        if (is_web_view(s_view)) {
            leave_web_management();
            return;
        } else if (s_view == VIEW_ACCOUNT || s_view == VIEW_REVEAL_CONFIRM) {
            s_view = VIEW_ACCOUNTS;
        } else if (s_view == VIEW_SETTINGS) {
            s_selection = 0;
            s_view = VIEW_ACCOUNTS;
        } else if (s_view == VIEW_ABOUT || s_view == VIEW_LOCK_CONFIRM) {
            s_view = VIEW_SETTINGS;
        } else {
            return;
        }
        render();
        return;
    }
    if (key.event == BSP_BTN_DOUBLE) {
        if (s_view == VIEW_ACCOUNTS && key.button == BSP_BTN_OK) {
            s_selection = 0;
            s_view = VIEW_SETTINGS;
            render();
        }
        return;
    }
    if (key.event != BSP_BTN_CLICK) return;
    switch (s_view) {
    case VIEW_LANGUAGE_SETUP:
        if (key.button == BSP_BTN_UP || key.button == BSP_BTN_DOWN) {
            s_selection = 1 - s_selection;
        } else if (key.button == BSP_BTN_OK) {
            app_language_t selected = s_selection == 0
                ? APP_LANGUAGE_ENGLISH : APP_LANGUAGE_CHINESE;
            if (app_config_request_language(selected)) {
                s_language = selected;
                s_selection = 0;
                s_view = VIEW_WELCOME;
            }
        }
        render();
        break;
    case VIEW_WELCOME:
        if (key.event != BSP_BTN_CLICK) break;
        if (key.button == BSP_BTN_UP) {
            if (pm_scroll_move(&s_welcome_scroll, -1, 16U)) render();
        } else if (key.button == BSP_BTN_DOWN) {
            if (pm_scroll_move(&s_welcome_scroll, 1, 16U)) render();
        } else if (key.button == BSP_BTN_OK) {
            begin_digits(4);
            s_view = app_config_is_enrolled() ? VIEW_ENTER_PIN : VIEW_CREATE_PIN;
            render();
        }
        break;
    case VIEW_PIN_MISMATCH:
        if (key.button == BSP_BTN_OK) {
            begin_digits(4); s_view = VIEW_CREATE_PIN; render();
        }
        break;
    case VIEW_RECOVERY_ONCE:
        if (key.button == BSP_BTN_OK) {
            s_view = VIEW_RECOVERY_SAVED;
            render();
            if (submit_security(APP_SECURITY_STORE_RECOVERY, s_recovery, 8U) == 0U) {
                s_view = VIEW_RECOVERY_ONCE;
                render();
            }
        }
        break;
    case VIEW_NEW_RECOVERY_ONCE:
        if (key.button == BSP_BTN_OK) {
            s_view = VIEW_NEW_RECOVERY_SAVED;
            render();
            if (submit_security(APP_SECURITY_STORE_RECOVERY, s_recovery, 8U) == 0U) {
                s_view = VIEW_NEW_RECOVERY_ONCE;
                render();
            }
        }
        break;
    case VIEW_RECOVERY_SAVED:
    case VIEW_NEW_RECOVERY_SAVED:
        break;
    case VIEW_SETUP_COMPLETE: case VIEW_RECOVERY_COMPLETE:
        if (key.button == BSP_BTN_OK) go_locked();
        break;
    case VIEW_PIN_ERROR:
        if (key.button == BSP_BTN_OK) { begin_digits(4); s_view = VIEW_ENTER_PIN; render(); }
        break;
    case VIEW_PIN_LOCKED:
        if (key.button == BSP_BTN_UP || key.button == BSP_BTN_DOWN) {
            s_selection = 1 - s_selection; render();
        } else if (key.button == BSP_BTN_OK) {
            if (s_selection == 0) { begin_digits(8); s_view=VIEW_RECOVERY_ENTRY; }
            else s_view=VIEW_ERASE_HOLD;
            render();
        }
        break;
    case VIEW_RECOVERY_ERROR:
        if (key.button == BSP_BTN_OK) { begin_digits(8); s_view=VIEW_RECOVERY_ENTRY; render(); }
        break;
    case VIEW_RECOVERY_DISABLED:
        if (key.button == BSP_BTN_OK) { s_view=VIEW_ERASE_HOLD; render(); }
        break;
    case VIEW_RECOVERY_SUCCESS:
        if (key.button == BSP_BTN_OK) { begin_digits(4); s_view=VIEW_RECOVERY_NEW_PIN; render(); }
        break;
    case VIEW_ACCOUNTS:
        if (vault_store_count() > 0U) {
            if (key.button==BSP_BTN_UP) { if (s_account_index>0U) --s_account_index; }
            else if (key.button==BSP_BTN_DOWN) { if (s_account_index+1U<vault_store_count()) ++s_account_index; }
            else if (key.button==BSP_BTN_OK) s_view=VIEW_ACCOUNT;
            render();
        } else if (key.button==BSP_BTN_OK) {
            s_first_account_flow=true;
            s_view=VIEW_WEB_OFF;
            render();
        }
        break;
    case VIEW_ACCOUNT:
        if (key.button==BSP_BTN_OK) {
            s_selection=0;
            s_view=VIEW_REVEAL_CONFIRM;
            render();
        }
        break;
    case VIEW_REVEAL_CONFIRM:
        if (key.button == BSP_BTN_UP || key.button == BSP_BTN_DOWN) s_selection=1-s_selection;
        else if (s_selection==0) {
            pm_deadline_start(&s_reveal_deadline, lv_tick_get(),
                              s_display_settings.reveal_seconds);
            s_rendered_reveal_seconds=s_display_settings.reveal_seconds;
            s_view=VIEW_PASSWORD;
        }
        else s_view=VIEW_ACCOUNT;
        render(); break;
    case VIEW_PASSWORD:
        if (key.button==BSP_BTN_UP && s_password_offset>0U) --s_password_offset;
        else if (key.button==BSP_BTN_DOWN) ++s_password_offset;
        else {pm_deadline_clear(&s_reveal_deadline);s_password_offset=0U;s_view=VIEW_ACCOUNT;}
        render(); break;
    case VIEW_SETTINGS:
        if (key.button==BSP_BTN_UP) s_selection=(s_selection+5)%6;
        else if (key.button==BSP_BTN_DOWN) s_selection=(s_selection+1)%6;
        else {
            if (s_selection==0) { web_manager_status_t w; web_manager_get_status(&w); s_view=w.state==WEB_MANAGER_ACTIVE?VIEW_WEB_ACTIVE:VIEW_WEB_OFF; }
            else if (s_selection==1) { begin_digits(4); s_view=VIEW_CHANGE_CURRENT; }
            else if (s_selection==2) { s_selection=0; s_view=VIEW_LOCK_CONFIRM; }
            else if (s_selection==3) s_view=VIEW_ERASE_HOLD;
            else if (s_selection==4) s_view=VIEW_ABOUT;
            else {
                s_language = s_language == APP_LANGUAGE_CHINESE
                    ? APP_LANGUAGE_ENGLISH : APP_LANGUAGE_CHINESE;
                (void)app_config_request_language(s_language);
            }
        }
        render(); break;
    case VIEW_CHANGE_ERROR:
        if (key.button == BSP_BTN_OK) { begin_digits(4); s_view=VIEW_CHANGE_CURRENT; render(); }
        break;
    case VIEW_CHANGE_MISMATCH:
        if (key.button == BSP_BTN_OK) { begin_digits(4); s_view=VIEW_CHANGE_NEW; render(); }
        break;
    case VIEW_CHANGE_SAVED:
        if (key.button == BSP_BTN_OK) { s_selection=1; s_view=VIEW_SETTINGS; render(); }
        break;
    case VIEW_LOCK_CONFIRM:
        if (key.button==BSP_BTN_UP || key.button==BSP_BTN_DOWN) s_selection=1-s_selection;
        else if (s_selection==0) go_locked(); else {s_view=VIEW_SETTINGS; render();}
        break;
    case VIEW_WEB_OFF:
        if (key.button == BSP_BTN_OK) {
            (void)web_manager_request_start(); s_view=VIEW_WEB_STARTING; render();
        }
        break;
    case VIEW_WEB_STARTING:
        if (key.button == BSP_BTN_OK) {
            leave_web_management();
        }
        break;
    case VIEW_WEB_EXPIRED: case VIEW_WEB_DISCONNECTED:
        if (key.button == BSP_BTN_OK) leave_web_management();
        break;
    case VIEW_WEB_WAITING:
        if (key.button == BSP_BTN_OK) {
            leave_web_management();
        }
        break;
    case VIEW_WEB_BROWSER_CONNECTED:
        if (key.button == BSP_BTN_OK) {
            web_manager_set_authorized(true);
            s_view=VIEW_WEB_ACTIVE;
            render();
        }
        break;
    case VIEW_WEB_ACTIVE:
        if (key.button == BSP_BTN_OK) {
            leave_web_management();
        }
        break;
    case VIEW_WEB_APPROVAL_ADD: case VIEW_WEB_APPROVAL_EDIT: case VIEW_WEB_APPROVAL_DELETE:
        if (key.button == BSP_BTN_UP || key.button == BSP_BTN_DOWN) {
            s_selection=1-s_selection; render();
        } else if (key.button == BSP_BTN_OK) {
            if (s_selection==0) { (void)web_manager_resolve_mutation(true); s_view=VIEW_WEB_WRITING; }
            else { (void)web_manager_resolve_mutation(false); s_view=VIEW_WEB_ACTIVE; }
            render();
        }
        break;
    case VIEW_WEB_WRITE_SUCCESS: case VIEW_WEB_WRITE_FAILED:
        if (key.button == BSP_BTN_OK) { s_view=VIEW_WEB_ACTIVE; render(); }
        break;
    case VIEW_ERASE_HOLD:
        if (key.button==BSP_BTN_UP) {s_hold_started=0;s_view=VIEW_SETTINGS;render();}
        else s_hold_started=0;
        break;
    case VIEW_ERASE_CONFIRM:
        if (key.button==BSP_BTN_UP || key.button==BSP_BTN_DOWN) {s_selection=1-s_selection;render();}
        else if (s_selection==0) {s_view=VIEW_SETTINGS;render();}
        else {s_view=VIEW_ERASING;render();submit_security(APP_SECURITY_ERASE,NULL,0);}
        break;
    case VIEW_ERASE_COMPLETE:
        if (key.button == BSP_BTN_OK) { begin_digits(4); s_view=VIEW_CREATE_PIN; render(); }
        break;
    case VIEW_ERASE_FAILED:
        if (key.button == BSP_BTN_OK) { s_view=VIEW_ERASING; render(); submit_security(APP_SECURITY_ERASE,NULL,0); }
        break;
    default: break;
    }
}

static void poll(lv_timer_t *timer)
{
    (void)timer;
    app_config_copy_display_settings(&s_display_settings);
    app_language_t configured_language = app_config_language();
    if (configured_language != s_language) {
        s_language = configured_language;
        render();
    }
    uint32_t welcome_revision = app_config_welcome_revision();
    if (welcome_revision != s_welcome_revision) {
        s_welcome_revision = welcome_revision;
        if (s_view == VIEW_WELCOME) render();
    }
    key_message_t key;
    while (xQueueReceive(s_keys, &key, 0) == pdTRUE) {
        if (s_screen_off) {
            if (key.event != BSP_BTN_PRESS) {
                wake_screen();
                while (xQueueReceive(s_keys, &key, 0) == pdTRUE) { }
            }
            continue;
        }
        s_last_activity_us = esp_timer_get_time();
        handle_key(key);
    }
    if (s_screen_off) return;
    app_security_result_t result;
    while (app_config_security_poll(&result)) handle_security_result(&result);

    if (s_view == VIEW_PASSWORD) {
        uint32_t now = lv_tick_get();
        uint16_t remaining = pm_deadline_remaining_seconds(&s_reveal_deadline, now);
        if (pm_deadline_expired(&s_reveal_deadline, now)) {
            pm_deadline_clear(&s_reveal_deadline);
            s_password_offset=0U;
            s_view=VIEW_ACCOUNT;
            render();
        } else if (remaining != s_rendered_reveal_seconds) {
            s_rendered_reveal_seconds=remaining;
            render();
        }
    }
    if (s_view == VIEW_ERASE_HOLD && s_hold_started != 0U) {
        int mv = bsp_button_read_mv();
        if (mv < s_button_mv[BSP_BTN_OK][0] || mv > s_button_mv[BSP_BTN_OK][1]) s_hold_started = 0U;
        else if (lv_tick_elaps(s_hold_started) >= 5000U) {
            s_hold_started=0U; s_selection=0; s_view=VIEW_ERASE_CONFIRM; render();
        }
    }

    web_manager_status_t web;
    web_manager_get_status(&web);
    web_manager_event_t web_event;
    while (web_manager_poll_event(&web_event)) {
        if (web_event.type==WEB_EVENT_BROWSER_CONNECTED) s_view=VIEW_WEB_BROWSER_CONNECTED;
        else if (web_event.type==WEB_EVENT_MUTATION_ADD) {s_selection=0;snprintf(s_web_platform,sizeof(s_web_platform),"%s",web_event.platform);s_view=VIEW_WEB_APPROVAL_ADD;}
        else if (web_event.type==WEB_EVENT_MUTATION_EDIT) {s_selection=0;snprintf(s_web_platform,sizeof(s_web_platform),"%s",web_event.platform);s_view=VIEW_WEB_APPROVAL_EDIT;}
        else if (web_event.type==WEB_EVENT_MUTATION_DELETE) {s_selection=0;snprintf(s_web_platform,sizeof(s_web_platform),"%s",web_event.platform);s_view=VIEW_WEB_APPROVAL_DELETE;}
        else if (web_event.type==WEB_EVENT_WRITE_OK) s_view=VIEW_WEB_WRITE_SUCCESS;
        else if (web_event.type==WEB_EVENT_WRITE_FAILED) s_view=VIEW_WEB_WRITE_FAILED;
        else if (web_event.type==WEB_EVENT_CHANGE_PIN) {
            (void)web_manager_request_stop();
            begin_digits(4);
            s_view=VIEW_CHANGE_CURRENT;
        }
        else if (web_event.type==WEB_EVENT_CLEAR_DATA) {
            (void)web_manager_request_stop();
            s_hold_started=0U;
            s_view=VIEW_ERASE_HOLD;
        }
        else if (web_event.type==WEB_EVENT_DISCONNECTED) s_view=VIEW_WEB_DISCONNECTED;
        else if (web_event.type==WEB_EVENT_EXPIRED) s_view=VIEW_WEB_EXPIRED;
        render();
    }
    if (s_view == VIEW_WEB_STARTING && web.state == WEB_MANAGER_ACTIVE) {s_view=VIEW_WEB_WAITING;render();}
    else if ((s_view==VIEW_WEB_WAITING || s_view==VIEW_WEB_ACTIVE) &&
             web.state==WEB_MANAGER_ACTIVE &&
             s_rendered_web_seconds != web.seconds_remaining) render();
    else if ((s_view==VIEW_SETTINGS || s_view==VIEW_WEB_OFF || s_view==VIEW_WEB_ACTIVE) &&
             s_rendered_web_state != web.state) render();

    int64_t now_us = esp_timer_get_time();
    int64_t web_activity_us = web_manager_last_activity_us();
    int64_t recent_activity_us = web_activity_us > s_last_activity_us
        ? web_activity_us : s_last_activity_us;
    int64_t idle_limit_us = (int64_t)s_display_settings.auto_lock_seconds * 1000000LL;
    if (s_display_settings.auto_lock_seconds > 0U &&
        s_security_request == 0U && s_view != VIEW_ERASING &&
        !(web.state == WEB_MANAGER_ACTIVE && web.authorized) &&
        now_us - recent_activity_us >= idle_limit_us) {
        enter_screen_off();
    }
}

esp_err_t password_manager_app_start(void)
{
    s_chinese_fallback_font = lv_font_source_han_sans_sc_14_cjk;
    s_chinese_fallback_font.fallback = &lv_font_cipherport_cjk_12;
    s_chinese_body_font = lv_font_cipherport_14;
    s_chinese_body_font.fallback = &s_chinese_fallback_font;
    s_chinese_title_font = lv_font_cipherport_19;
    s_chinese_title_font.fallback = &s_chinese_body_font;
    s_language = app_config_language();
    s_welcome_revision = app_config_welcome_revision();
    app_config_copy_display_settings(&s_display_settings);
    s_last_activity_us = esp_timer_get_time();
    s_keys = xQueueCreate(12, sizeof(key_message_t));
    if (!s_keys) return ESP_ERR_NO_MEM;
    s_pin_failures = app_config_pin_failures();
    s_recovery_failures = app_config_recovery_failures();
    if (!app_config_language_configured()) {
        s_selection = 0;
        s_view = VIEW_LANGUAGE_SETUP;
    }
    else if (!app_config_is_enrolled()) s_view=VIEW_WELCOME;
    else if (s_recovery_failures >= RECOVERY_LIMIT) s_view=VIEW_RECOVERY_DISABLED;
    else if (s_pin_failures >= PIN_LIMIT) s_view=VIEW_PIN_LOCKED;
    else s_view=VIEW_WELCOME;
    render();
    s_timer = lv_timer_create(poll, 50, NULL);
    return s_timer ? ESP_OK : ESP_ERR_NO_MEM;
}

bool password_manager_app_post_key(bsp_btn_t button, bsp_btn_ev_t event)
{
    if (!s_keys) return false;
    key_message_t key = {.button=button,.event=event};
    return xQueueSend(s_keys, &key, 0) == pdTRUE;
}
