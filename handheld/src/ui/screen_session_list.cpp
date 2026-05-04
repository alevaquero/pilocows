#include "screen_session_list.h"
#include "ui_manager.h"
#include "lvgl.h"
#include "../i18n/i18n.h"
#include "../i18n/strings_en.h"
#include "../storage/session_storage.h"
#include "esp_log.h"
#include <stdio.h>
#include <inttypes.h>

static const char *TAG = "scr_sess_list";

#define LIST_MAX 50

// ── Static label refs ─────────────────────────────────────────────────────────
static lv_obj_t *s_hdr;
static lv_obj_t *s_lbl_title;
static lv_obj_t *s_lbl_back;
static lv_obj_t *s_lbl_empty;

// ── List widget ───────────────────────────────────────────────────────────────
static lv_obj_t *s_list;

// ── Detail panel (full-screen overlay when a session is tapped) ───────────────
static lv_obj_t *s_detail_panel;
static lv_obj_t *s_lbl_detail_name;
static lv_obj_t *s_lbl_detail_info;
static lv_obj_t *s_btn_reopen;
static lv_obj_t *s_lbl_btn_reopen;
static lv_obj_t *s_btn_set_current;
static lv_obj_t *s_lbl_btn_set_current;
static lv_obj_t *s_btn_close_session;
static lv_obj_t *s_lbl_btn_close_session;
static lv_obj_t *s_btn_delete;
static lv_obj_t *s_lbl_btn_delete;
static lv_obj_t *s_btn_detail_cancel;
static lv_obj_t *s_lbl_btn_detail_cancel;

// Currently selected session in the detail panel
static uint32_t s_selected_id = 0;
static uint8_t  s_selected_status = SESSION_STATUS_OPEN;

static lv_obj_t *s_scr = NULL;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static const char *type_en_str(uint8_t type)
{
    switch ((session_type_t)type) {
        case SESSION_TYPE_WEIGHING:    return "Weighing";
        case SESSION_TYPE_VACCINATION: return "Vaccination";
        case SESSION_TYPE_PREGNANCY:   return "Pregnancy Check";
        case SESSION_TYPE_TB_TEST:     return "TB Test";
        case SESSION_TYPE_REMOVAL:     return "Removal";
        default:                       return "General";
    }
}

static void show_detail(const session_meta_t *m)
{
    s_selected_id     = m->id;
    s_selected_status = m->status;

    lv_label_set_text(s_lbl_detail_name, m->name);

    const char *type_str   = i18n_t(type_en_str(m->type));
    const char *status_str = (m->status == SESSION_STATUS_OPEN)
                             ? i18n_t("Open") : i18n_t("Closed");
    char info[64];
    snprintf(info, sizeof(info), "%s | %" PRIu32 " | %s",
             type_str, m->tag_count, status_str);
    lv_label_set_text(s_lbl_detail_info, info);

    // Button visibility depends on status
    if (m->status == SESSION_STATUS_CLOSED) {
        lv_obj_clear_flag(s_btn_reopen,        LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag  (s_btn_set_current,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag  (s_btn_close_session, LV_OBJ_FLAG_HIDDEN);
    } else {  // OPEN
        lv_obj_add_flag  (s_btn_reopen,        LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_btn_set_current,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_btn_close_session, LV_OBJ_FLAG_HIDDEN);
    }

    // Hide list and header — detail is full-screen
    lv_obj_add_flag(s_hdr,  LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_detail_panel, LV_OBJ_FLAG_HIDDEN);
}

static void hide_detail(void)
{
    s_selected_id = 0;
    lv_obj_add_flag(s_detail_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_hdr,  LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_list, LV_OBJ_FLAG_HIDDEN);
    // s_lbl_empty visibility managed by rebuild_list()
}

static void rebuild_list(void)
{
    lv_obj_clean(s_list);

    static session_meta_t sessions[LIST_MAX];
    int count = session_list_all(sessions, LIST_MAX);

    if (count == 0) {
        lv_obj_clear_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < count; i++) {
        const session_meta_t *m = &sessions[i];

        lv_obj_t *row = lv_list_add_btn(s_list, NULL, "");
        lv_obj_set_height(row, 44);
        lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
        lv_obj_set_layout(row, 0);  // disable flex (LV_LAYOUT_NONE=0) so our alignment works

        // Session name
        lv_obj_t *lbl_name = lv_label_create(row);
        lv_label_set_text(lbl_name, m->name);
        lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl_name, 230);
        lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 4, 0);

        // Tag count column (middle-right)
        char cnt_buf[12];
        snprintf(cnt_buf, sizeof(cnt_buf), "%" PRIu32, m->tag_count);
        lv_obj_t *lbl_cnt = lv_label_create(row);
        lv_label_set_text(lbl_cnt, cnt_buf);
        lv_obj_set_style_text_font(lbl_cnt, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl_cnt, lv_palette_main(LV_PALETTE_BLUE_GREY), LV_PART_MAIN);
        lv_obj_align(lbl_cnt, LV_ALIGN_RIGHT_MID, -82, 0);

        // Status badge (right side)
        const char *st = (m->status == SESSION_STATUS_OPEN)
                         ? i18n_t("Open") : i18n_t("Closed");
        lv_color_t st_color = (m->status == SESSION_STATUS_OPEN)
                              ? lv_color_hex(0x27AE60) : lv_color_hex(0x888899);
        lv_obj_t *lbl_st = lv_label_create(row);
        lv_label_set_text(lbl_st, st);
        lv_obj_set_style_text_color(lbl_st, st_color, LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_st, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_align(lbl_st, LV_ALIGN_RIGHT_MID, -8, 0);

        // Pass session id via user_data on click
        static session_meta_t copy_buf[LIST_MAX];
        copy_buf[i] = *m;
        lv_obj_add_event_cb(row, [](lv_event_t *ev) {
            // Retrieve meta from the list item's user_data
            session_meta_t *meta = (session_meta_t *)lv_event_get_user_data(ev);
            show_detail(meta);
        }, LV_EVENT_CLICKED, &copy_buf[i]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Callbacks
// ─────────────────────────────────────────────────────────────────────────────

static void on_back(lv_event_t *e)
{
    (void)e;
    hide_detail();
    ui_manager_show(SCREEN_SESSION_MENU);
}

static void on_detail_close(lv_event_t *e)
{
    (void)e;
    hide_detail();
}

static void on_reopen(lv_event_t *e)
{
    (void)e;
    if (s_selected_id == 0) return;
    session_set_status(s_selected_id, SESSION_STATUS_OPEN);
    session_set_active(s_selected_id);
    hide_detail();
    rebuild_list();
    ESP_LOGI(TAG, "Reopened session %" PRIu32, s_selected_id);
}

static void on_set_current(lv_event_t *e)
{
    (void)e;
    if (s_selected_id == 0) return;
    session_set_active(s_selected_id);
    ESP_LOGI(TAG, "Set current session %" PRIu32, s_selected_id);
    ui_manager_show(SCREEN_SCAN);
}

static void on_close_session(lv_event_t *e)
{
    (void)e;
    if (s_selected_id == 0) return;
    session_set_status(s_selected_id, SESSION_STATUS_CLOSED);
    ESP_LOGI(TAG, "Closed session %" PRIu32, s_selected_id);
    hide_detail();
    rebuild_list();
}

static void on_delete(lv_event_t *e)
{
    (void)e;
    if (s_selected_id == 0) return;
    esp_err_t err = session_delete(s_selected_id);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Deleted session %" PRIu32, s_selected_id);
    } else {
        ESP_LOGE(TAG, "Delete session %" PRIu32 " failed: %s",
                 s_selected_id, esp_err_to_name(err));
    }
    hide_detail();
    rebuild_list();
}

static void on_screen_loaded(lv_event_t *e)
{
    (void)e;
    hide_detail();
    rebuild_list();
}

// ─────────────────────────────────────────────────────────────────────────────
// Create
// ─────────────────────────────────────────────────────────────────────────────

void screen_session_list_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_scr, on_screen_loaded, LV_EVENT_SCREEN_LOADED, NULL);

    // ── Header ────────────────────────────────────────────────────────────────
    s_hdr = lv_obj_create(s_scr);
    lv_obj_set_size(s_hdr, 480, 40);
    lv_obj_set_pos(s_hdr, 0, 0);
    lv_obj_clear_flag(s_hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_hdr, 0, LV_PART_MAIN);

    lv_obj_t *btn_back = lv_btn_create(s_hdr);
    lv_obj_set_size(btn_back, 60, 32);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_border_width(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_back, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_back, 10);
    lv_obj_add_event_cb(btn_back, on_back, LV_EVENT_CLICKED, NULL);
    s_lbl_back = lv_label_create(btn_back);
    lv_label_set_text(s_lbl_back, i18n_t(STR_BTN_BACK));
    lv_obj_set_style_text_font(s_lbl_back, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_center(s_lbl_back);

    s_lbl_title = lv_label_create(s_hdr);
    lv_label_set_text(s_lbl_title, i18n_t(STR_SESSION_LIST));
    lv_obj_set_style_text_font(s_lbl_title, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(s_lbl_title, LV_ALIGN_CENTER, 0, 0);

    // ── Empty label ───────────────────────────────────────────────────────────
    s_lbl_empty = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_empty, i18n_t(STR_HISTORY_EMPTY));
    lv_obj_set_style_text_font(s_lbl_empty, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_empty, 0, 150);
    lv_obj_set_width(s_lbl_empty, 480);
    lv_obj_set_style_text_align(s_lbl_empty, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_add_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);

    // ── Scrollable list ───────────────────────────────────────────────────────
    s_list = lv_list_create(s_scr);
    lv_obj_set_size(s_list, 480, 280);
    lv_obj_set_pos(s_list, 0, 40);
    lv_obj_set_style_border_width(s_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_list, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_list, 4, LV_PART_MAIN);

    // ── Detail panel (full-screen — covers list + header) ─────────────────────
    //
    // Layout (480×320):
    //   y=  0, h=40  Mini-header: session name (title-sized)
    //   y= 48, h=26  Info row: type | count | status
    //   y= 90, h=50  Row 1: Set as Current  |  Close Session   (open sessions)
    //                       Reopen                             (closed sessions)
    //   y=150, h=50  Row 2: Delete (always)
    //   y=270, h=50  Cancel button (bottom)
    s_detail_panel = lv_obj_create(s_scr);
    lv_obj_set_size(s_detail_panel, 480, 320);
    lv_obj_set_pos(s_detail_panel, 0, 0);
    lv_obj_set_style_border_width(s_detail_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_detail_panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_detail_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_detail_panel, 16, LV_PART_MAIN);

    s_lbl_detail_name = lv_label_create(s_detail_panel);
    lv_label_set_text(s_lbl_detail_name, "");
    lv_label_set_long_mode(s_lbl_detail_name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_lbl_detail_name, 448);
    lv_obj_set_style_text_font(s_lbl_detail_name, &lv_font_montserrat_22, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_detail_name, 0, 0);

    s_lbl_detail_info = lv_label_create(s_detail_panel);
    lv_label_set_text(s_lbl_detail_info, "");
    lv_obj_set_style_text_font(s_lbl_detail_info, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_detail_info, 0, 32);

    // ── Reopen button (closed sessions only) ──────────────────────────────────
    s_btn_reopen = lv_btn_create(s_detail_panel);
    lv_obj_set_size(s_btn_reopen, 448, 50);
    lv_obj_set_pos(s_btn_reopen, 0, 74);
    lv_obj_set_style_bg_color(s_btn_reopen, lv_color_hex(0x27AE60), LV_PART_MAIN);
    lv_obj_set_style_radius(s_btn_reopen, 6, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_reopen, 10);
    lv_obj_add_event_cb(s_btn_reopen, on_reopen, LV_EVENT_CLICKED, NULL);
    s_lbl_btn_reopen = lv_label_create(s_btn_reopen);
    lv_label_set_text(s_lbl_btn_reopen, i18n_t(STR_SESSION_REOPEN));
    lv_obj_set_style_text_color(s_lbl_btn_reopen, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_btn_reopen, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_center(s_lbl_btn_reopen);

    // ── Set as Current button (open sessions, left half) ──────────────────────
    s_btn_set_current = lv_btn_create(s_detail_panel);
    lv_obj_set_size(s_btn_set_current, 220, 50);
    lv_obj_set_pos(s_btn_set_current, 0, 74);
    lv_obj_set_style_radius(s_btn_set_current, 6, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_set_current, 10);
    lv_obj_add_event_cb(s_btn_set_current, on_set_current, LV_EVENT_CLICKED, NULL);
    s_lbl_btn_set_current = lv_label_create(s_btn_set_current);
    lv_label_set_text(s_lbl_btn_set_current, i18n_t(STR_SESSION_SET_CURRENT));
    lv_obj_set_style_text_font(s_lbl_btn_set_current, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_center(s_lbl_btn_set_current);

    // ── Close Session button (open sessions, right half) ──────────────────────
    s_btn_close_session = lv_btn_create(s_detail_panel);
    lv_obj_set_size(s_btn_close_session, 220, 50);
    lv_obj_set_pos(s_btn_close_session, 228, 74);
    lv_obj_set_style_bg_color(s_btn_close_session, lv_color_hex(0xE67E22), LV_PART_MAIN);
    lv_obj_set_style_radius(s_btn_close_session, 6, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_close_session, 10);
    lv_obj_add_event_cb(s_btn_close_session, on_close_session, LV_EVENT_CLICKED, NULL);
    s_lbl_btn_close_session = lv_label_create(s_btn_close_session);
    lv_label_set_text(s_lbl_btn_close_session, i18n_t(STR_SESSION_CLOSE));
    lv_obj_set_style_text_color(s_lbl_btn_close_session, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_btn_close_session, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_center(s_lbl_btn_close_session);

    // ── Delete button (always visible) ────────────────────────────────────────
    s_btn_delete = lv_btn_create(s_detail_panel);
    lv_obj_set_size(s_btn_delete, 448, 50);
    lv_obj_set_pos(s_btn_delete, 0, 134);
    lv_obj_set_style_bg_color(s_btn_delete, lv_color_hex(0xC0392B), LV_PART_MAIN);
    lv_obj_set_style_radius(s_btn_delete, 6, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_delete, 10);
    lv_obj_add_event_cb(s_btn_delete, on_delete, LV_EVENT_CLICKED, NULL);
    s_lbl_btn_delete = lv_label_create(s_btn_delete);
    lv_label_set_text(s_lbl_btn_delete, i18n_t(STR_SESSION_DELETE));
    lv_obj_set_style_text_color(s_lbl_btn_delete, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_btn_delete, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_center(s_lbl_btn_delete);

    // ── Cancel button (bottom) ────────────────────────────────────────────────
    s_btn_detail_cancel = lv_btn_create(s_detail_panel);
    lv_obj_set_size(s_btn_detail_cancel, 448, 50);
    lv_obj_set_pos(s_btn_detail_cancel, 0, 238);
    lv_obj_set_style_border_width(s_btn_detail_cancel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_btn_detail_cancel, 6, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_detail_cancel, 10);
    lv_obj_add_event_cb(s_btn_detail_cancel, on_detail_close, LV_EVENT_CLICKED, NULL);
    s_lbl_btn_detail_cancel = lv_label_create(s_btn_detail_cancel);
    lv_label_set_text(s_lbl_btn_detail_cancel, i18n_t(STR_BTN_CANCEL));
    lv_obj_set_style_text_font(s_lbl_btn_detail_cancel, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_center(s_lbl_btn_detail_cancel);

    lv_obj_add_flag(s_detail_panel, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "Session list screen created");
}

// ─────────────────────────────────────────────────────────────────────────────
// Load / refresh
// ─────────────────────────────────────────────────────────────────────────────

void screen_session_list_load(void)
{
    lv_scr_load(s_scr);
    // rebuild_list() and hide_detail() run via on_screen_loaded
}

void screen_session_list_refresh_language(void)
{
    lv_label_set_text(s_lbl_title,                i18n_t(STR_SESSION_LIST));
    lv_label_set_text(s_lbl_back,                 i18n_t(STR_BTN_BACK));
    lv_label_set_text(s_lbl_btn_reopen,           i18n_t(STR_SESSION_REOPEN));
    lv_label_set_text(s_lbl_btn_set_current,      i18n_t(STR_SESSION_SET_CURRENT));
    lv_label_set_text(s_lbl_btn_close_session,    i18n_t(STR_SESSION_CLOSE));
    lv_label_set_text(s_lbl_btn_delete,           i18n_t(STR_SESSION_DELETE));
    lv_label_set_text(s_lbl_btn_detail_cancel,    i18n_t(STR_BTN_CANCEL));
    // Rebuild list rows so type/status strings update
    rebuild_list();
}
