/****************************************************************************
 * contest2026_243_qiqimiaomiao/app/vocavibe/src/vocavibe_ui.c
 *
 * VocaVibe (随声记) - 羊皮纸经典纸质手账 UI 与 Anki 原生交互引擎
 * LVGL 9 on SF32LB52-DevKit-LCD (390x450 AMOLED + FT6146 Touch)
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <math.h>
#include <nuttx/lcd/lcd_dev.h>

#include <lvgl/lvgl.h>
#include "vocavibe_ui.h"
#include "vocavibe_core.h"
#include "vocavibe_touch.h"
#include "vocavibe_html.h"

LV_FONT_DECLARE(lv_font_simsun_16_cjk);

/* -------------------------------------------------------------------------
 * 羊皮纸经典手账美学调色盘
 * ------------------------------------------------------------------------- */
#define COLOR_PARCHMENT_BG     0xF5EEDB  /* 温润浅米黄底色 */
#define COLOR_PAPER_CARD       0xFDFBF7  /* 柔白微暖卡片纸质表面 */
#define COLOR_PAPER_BORDER     0xD8CCA3  /* 细腻素雅纸张边框 */
#define COLOR_INK_MAIN         0x2A2621  /* 古法深炭墨汁色（主标题、主要文字） */
#define COLOR_INK_MUTED        0x706658  /* 次级墨水灰（次要说明、音标、例句） */
#define COLOR_INK_HIGHLIGHT    0x9C4128  /* 赭石/赤金（强调与重要状态） */

/* 复古印章质感 4 档评分色 */
#define COLOR_SEAL_AGAIN       0xC84B31  /* 传统朱砂红泥 (1. 重来) */
#define COLOR_SEAL_HARD        0xD9822B  /* 暖琥珀金 (2. 困难) */
#define COLOR_SEAL_GOOD        0x3D7B50  /* 竹青墨绿 (3. 良好) */
#define COLOR_SEAL_EASY        0x2D6898  /* 霁蓝墨印 (4. 容易) */

/* 导航栏与功能键色调 */
#define COLOR_NAV_ACTIVE       0x8B4513  /* 暖棕褐（当前激活 Tab） */
#define COLOR_NAV_INACTIVE     0xE5DBCE  /* 浅羊皮底（未激活 Tab） */
#define COLOR_INK_WAVE         0x1A4B6E  /* 复古墨水蓝（声波动效） */
#define COLOR_BTN_BROWN        0x795548  /* 经典熟褐 */
#define COLOR_BTN_SLATE        0x546E7A  /* 灰蓝纸感 */

#define SCREEN_W 390
#define SCREEN_H 450
#define NAV_H    46
#define TV_H     SCREEN_H
#define SAFE_CARD_W 336
#define DOCK_W      340

static vocavibe_ui_callbacks_t s_cbs;

/* 全局 CJK 中文字体样式 */
static lv_style_t s_cjk_style;
static bool s_cjk_style_inited = false;

static void apply_cjk_font(lv_obj_t *obj)
{
    if (!s_cjk_style_inited) {
        lv_style_init(&s_cjk_style);
        lv_style_set_text_font(&s_cjk_style, &lv_font_simsun_16_cjk);
        s_cjk_style_inited = true;
    }
    if (obj) {
        lv_obj_add_style(obj, &s_cjk_style, 0);
    }
}

/* Tileview 与主对象 */
static lv_obj_t *s_tv = NULL;
static lv_obj_t *s_tiles[4];
static lv_obj_t *s_nav_btns[4];

/* --- Page 0: 仪表盘控件 --- */
static lv_obj_t *s_p0_due_val = NULL;
static lv_obj_t *s_p0_done_val = NULL;
static lv_obj_t *s_p0_total_val = NULL;

/* --- Page 1: Anki 卡片控件 --- */
static lv_obj_t *s_p1_progress_lbl = NULL;
static lv_obj_t *s_p1_word_lbl = NULL;
static lv_obj_t *s_p1_phonetic_lbl = NULL;
static lv_obj_t *s_p1_meaning_lbl = NULL;
static lv_obj_t *s_p1_example_lbl = NULL;
static lv_obj_t *s_p1_flip_btn = NULL;
static lv_obj_t *s_p1_ratings_cont = NULL;
static bool s_card_back_visible = false;

/* --- Page 2: AI 助教控件 --- */
#define NUM_WAVE_BARS 7
static lv_obj_t *s_wave_bars[NUM_WAVE_BARS];
static lv_obj_t *s_ai_status_lbl = NULL;
static lv_obj_t *s_chat_user_lbl = NULL;
static lv_obj_t *s_chat_ai_lbl = NULL;
static vocavibe_ai_state_t s_ai_state = AI_STATE_IDLE;
static float s_anim_phase = 0.0f;

/* --- Page 3: 设置与中转代理控件 --- */
static lv_obj_t *s_net_status_lbl = NULL;
static lv_obj_t *s_btn_net_conn = NULL;
static lv_obj_t *s_lbl_net_conn = NULL;
static lv_obj_t *s_sync_status_lbl = NULL;
static lv_obj_t *s_bt_status_lbl = NULL;
static lv_obj_t *s_bt_list = NULL;

/* 线程安全数据中转缓存 */
static int s_stat_total = 20, s_stat_due = 20, s_stat_reviewed = 0;
static volatile bool s_stat_dirty = true;

static char s_cur_word[64] = "openvela";
static char s_cur_phonetic[64] = "/open-vela/";
static char s_cur_meaning[256] = "嵌入式微控制器专属的下一代端侧实时 AI 操作系统";
static char s_cur_example[384] = "OpenVela OS powers intelligent edge hardware with microsecond latency.";
static int s_cur_idx = 1, s_total_cnt = 20;
static volatile bool s_card_dirty = true;
static volatile bool s_card_req_back = false;

static char s_chat_user_buf[128] = "用户: 你好 openvela!";
static char s_chat_ai_buf[512] = "AI: 你好！我是随声记 AI 助教，随时为你答疑解惑。";
static volatile bool s_chat_dirty = true;

static char s_net_status_buf[96] = "网络代理: 未连接";
static bool s_net_connected = false;
static volatile bool s_net_dirty = true;

static char s_sync_status_buf[96] = "AnkiConnect 服务: 127.0.0.1:8765 (就绪)";
static volatile bool s_sync_dirty = true;

static char s_bt_status_buf[96] = "耳机: 未连接";
static volatile bool s_bt_dirty = true;

#define MAX_BT_DEVICES 6
typedef struct {
    char name[32];
    char mac[20];
    char status[16];
    int rssi;
    bool is_connected;
} bt_dev_entry_t;
static bt_dev_entry_t s_bt_entries[MAX_BT_DEVICES];
static int s_bt_entry_count = 0;
static volatile bool s_bt_list_dirty = false;
static volatile int s_switch_to_page = -1;
static int s_current_page = 0;

int vocavibe_ui_get_current_page(void)
{
    return s_current_page;
}

/* -------------------------------------------------------------------------
 * 事件回调函数
 * ------------------------------------------------------------------------- */
static void update_nav_buttons_style(int active_idx)
{
    s_current_page = active_idx;
    for (int i = 0; i < 4; i++) {
        if (s_nav_btns[i]) {
            lv_obj_t *lbl = lv_obj_get_child(s_nav_btns[i], 0);
            if (i == active_idx) {
                lv_obj_set_style_bg_color(s_nav_btns[i], lv_color_hex(COLOR_NAV_ACTIVE), 0);
                lv_obj_set_style_border_color(s_nav_btns[i], lv_color_hex(0x5D2E0C), 0);
                if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
            } else {
                lv_obj_set_style_bg_color(s_nav_btns[i], lv_color_hex(COLOR_NAV_INACTIVE), 0);
                lv_obj_set_style_border_color(s_nav_btns[i], lv_color_hex(COLOR_PAPER_BORDER), 0);
                if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_INK_MAIN), 0);
            }
        }
    }
}

static void on_tileview_value_changed(lv_event_t *e)
{
    lv_obj_t *tv = lv_event_get_target(e);
    if (!tv) return;
    lv_obj_t *cur_tile = lv_tileview_get_tile_active(tv);
    if (!cur_tile) return;
    for (int i = 0; i < 4; i++) {
        if (s_tiles[i] == cur_tile) {
            if (s_current_page != i) {
                printf("[VocaVibe UI] 屏幕轻扫手势触发: 切换到页面 %d\n", i);
                update_nav_buttons_style(i);
            }
            break;
        }
    }
}

static void on_nav_btn_clicked(lv_event_t *e)
{
    uintptr_t target_idx = (uintptr_t)lv_event_get_user_data(e);
    printf("[VocaVibe UI] 触控点击导航栏: 切换到页面 %d\n", (int)target_idx);
    if (s_tv) {
        lv_tileview_set_tile_by_index(s_tv, (uint32_t)target_idx, 0, LV_ANIM_OFF);
        update_nav_buttons_style((int)target_idx);
    }
}

static void on_dash_start_study_clicked(lv_event_t *e)
{
    (void)e;
    printf("[VocaVibe UI] 触控点击: 开始 Anki 记忆复习\n");
    vocavibe_ui_switch_page(1);
}

static void on_dash_start_ai_clicked(lv_event_t *e)
{
    (void)e;
    printf("[VocaVibe UI] 触控点击: 进入 AI 助教交互\n");
    vocavibe_ui_switch_page(2);
}

static void on_card_flip_clicked(lv_event_t *e)
{
    (void)e;
    printf("[VocaVibe UI] 触控点击: 查看答案 / 翻转卡片背面\n");
    s_card_req_back = true;
    s_card_dirty = true;
}

static void on_card_speak_clicked(lv_event_t *e)
{
    (void)e;
    printf("[VocaVibe UI] 触控点击: 发音朗读单词 '%s'\n", s_cur_word);
    if (s_cbs.on_ai_query) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "朗读发音并解析: %s", s_cur_word);
        s_cbs.on_ai_query(cmd);
    }
}

static void on_rating_clicked(lv_event_t *e)
{
    anki_rating_t rating = (anki_rating_t)(uintptr_t)lv_event_get_user_data(e);
    printf("[VocaVibe UI] 触控点击: Anki 记忆打分 Q=%d\n", (int)rating);
    if (s_cbs.on_card_answer) {
        s_cbs.on_card_answer(rating);
    }
}

static void on_ai_prompt_clicked(lv_event_t *e)
{
    const char *prompt_type = (const char *)lv_event_get_user_data(e);
    char query[128];
    if (strcmp(prompt_type, "example") == 0) {
        snprintf(query, sizeof(query), "请为单词 %s 造两个实用的中英双语例句", s_cur_word);
    } else if (strcmp(prompt_type, "synonym") == 0) {
        snprintf(query, sizeof(query), "请提供 %s 的近义词辨析及用法差异", s_cur_word);
    } else {
        snprintf(query, sizeof(query), "请解析 %s 的词根词缀及记忆窍门", s_cur_word);
    }
    printf("[VocaVibe UI] 触控点击 AI 胶囊: %s\n", query);
    if (s_cbs.on_ai_query) {
        s_cbs.on_ai_query(query);
    }
}



static void on_bt_scan_clicked(lv_event_t *e)
{
    (void)e;
    printf("[VocaVibe UI] 触控点击: 扫描周围蓝牙设备\n");
    vocavibe_ui_clear_bt_devices();
    if (s_bt_list) {
        lv_obj_clean(s_bt_list);
        lv_obj_t *placeholder = lv_label_create(s_bt_list);
        apply_cjk_font(placeholder);
        lv_label_set_text(placeholder, "正在扫描周围设备...");
        lv_obj_set_style_text_color(placeholder, lv_color_hex(COLOR_SEAL_HARD), 0);
        lv_obj_center(placeholder);
    }
    if (s_cbs.on_bt_scan) {
        s_cbs.on_bt_scan();
    }
}

static void on_bt_dev_item_clicked(lv_event_t *e)
{
    const char *mac = (const char *)lv_event_get_user_data(e);
    printf("[VocaVibe UI] 触控点击: 连接设备 MAC %s\n", mac ? mac : "unknown");
    if (mac) {
        for (int i = 0; i < s_bt_entry_count; i++) {
            if (strcmp(s_bt_entries[i].mac, mac) == 0) {
                strncpy(s_bt_entries[i].status, "连接中...", sizeof(s_bt_entries[i].status) - 1);
                s_bt_list_dirty = true;
                break;
            }
        }
    }
    if (s_cbs.on_bt_connect && mac) {
        s_cbs.on_bt_connect(mac);
    }
}

/* -------------------------------------------------------------------------
 * UI 页面创建（全中文 + 羊皮纸手账质感）
 * ------------------------------------------------------------------------- */
static void create_page_0_dashboard(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(COLOR_PARCHMENT_BG), 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶部标题区 */
    lv_obj_t *title = lv_label_create(parent);
    apply_cjk_font(title);
    lv_label_set_text(title, "随声记 VocaVibe");
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_INK_MAIN), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    lv_obj_t *sub = lv_label_create(parent);
    apply_cjk_font(sub);
    lv_label_set_text(sub, "2026 OpenVela AI 硬件终端 (Team 243)");
    lv_obj_set_style_text_color(sub, lv_color_hex(COLOR_INK_MUTED), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 36);

    /* 学习进度统计便签卡片 */
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 360, 95);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 65);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PAPER_CARD), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_PAPER_BORDER), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* 三列统计：今日待学 / 今日已学 / 词库总数 */
    s_p0_due_val = lv_label_create(card);
    apply_cjk_font(s_p0_due_val);
    lv_label_set_text(s_p0_due_val, "20");
    lv_obj_set_style_text_color(s_p0_due_val, lv_color_hex(COLOR_SEAL_AGAIN), 0);
    lv_obj_align(s_p0_due_val, LV_ALIGN_TOP_LEFT, 25, 8);
    lv_obj_t *due_sub = lv_label_create(card);
    apply_cjk_font(due_sub);
    lv_label_set_text(due_sub, "今日待学");
    lv_obj_set_style_text_color(due_sub, lv_color_hex(COLOR_INK_MUTED), 0);
    lv_obj_align(due_sub, LV_ALIGN_TOP_LEFT, 15, 38);

    s_p0_done_val = lv_label_create(card);
    apply_cjk_font(s_p0_done_val);
    lv_label_set_text(s_p0_done_val, "0");
    lv_obj_set_style_text_color(s_p0_done_val, lv_color_hex(COLOR_SEAL_GOOD), 0);
    lv_obj_align(s_p0_done_val, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t *done_sub = lv_label_create(card);
    apply_cjk_font(done_sub);
    lv_label_set_text(done_sub, "今日已学");
    lv_obj_set_style_text_color(done_sub, lv_color_hex(COLOR_INK_MUTED), 0);
    lv_obj_align(done_sub, LV_ALIGN_TOP_MID, 0, 38);

    s_p0_total_val = lv_label_create(card);
    apply_cjk_font(s_p0_total_val);
    lv_label_set_text(s_p0_total_val, "20");
    lv_obj_set_style_text_color(s_p0_total_val, lv_color_hex(COLOR_SEAL_EASY), 0);
    lv_obj_align(s_p0_total_val, LV_ALIGN_TOP_RIGHT, -25, 8);
    lv_obj_t *tot_sub = lv_label_create(card);
    apply_cjk_font(tot_sub);
    lv_label_set_text(tot_sub, "词库总数");
    lv_obj_set_style_text_color(tot_sub, lv_color_hex(COLOR_INK_MUTED), 0);
    lv_obj_align(tot_sub, LV_ALIGN_TOP_RIGHT, -15, 38);

    /* 快捷手账大按钮 1: 进入 Anki 复习 */
    lv_obj_t *btn_study = lv_button_create(parent);
    lv_obj_set_ext_click_area(btn_study, 12);
    lv_obj_set_size(btn_study, SAFE_CARD_W, 64);
    lv_obj_align(btn_study, LV_ALIGN_TOP_MID, 0, 175);
    lv_obj_set_style_bg_color(btn_study, lv_color_hex(COLOR_BTN_BROWN), 0);
    lv_obj_set_style_border_color(btn_study, lv_color_hex(0x5D3A20), 0);
    lv_obj_set_style_border_width(btn_study, 2, 0);
    lv_obj_set_style_radius(btn_study, 14, 0);
    lv_obj_clear_flag(btn_study, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_study, on_dash_start_study_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_study, on_dash_start_study_clicked, LV_EVENT_SHORT_CLICKED, NULL);

    lv_obj_t *btn_study_lbl = lv_label_create(btn_study);
    apply_cjk_font(btn_study_lbl);
    lv_label_set_text(btn_study_lbl, "📖 开始 Anki 记忆复习 >");
    lv_obj_set_style_text_color(btn_study_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(btn_study_lbl);

    /* 快捷手账大按钮 2: AI 助教实时问答 */
    lv_obj_t *btn_ai = lv_button_create(parent);
    lv_obj_set_ext_click_area(btn_ai, 12);
    lv_obj_set_size(btn_ai, SAFE_CARD_W, 64);
    lv_obj_align(btn_ai, LV_ALIGN_TOP_MID, 0, 255);
    lv_obj_set_style_bg_color(btn_ai, lv_color_hex(COLOR_BTN_SLATE), 0);
    lv_obj_set_style_border_color(btn_ai, lv_color_hex(0x37474F), 0);
    lv_obj_set_style_border_width(btn_ai, 2, 0);
    lv_obj_set_style_radius(btn_ai, 14, 0);
    lv_obj_clear_flag(btn_ai, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_ai, on_dash_start_ai_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_ai, on_dash_start_ai_clicked, LV_EVENT_SHORT_CLICKED, NULL);

    lv_obj_t *btn_ai_lbl = lv_label_create(btn_ai);
    apply_cjk_font(btn_ai_lbl);
    lv_label_set_text(btn_ai_lbl, "🎙️ 进入 AI 助教交互 >");
    lv_obj_set_style_text_color(btn_ai_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(btn_ai_lbl);

    /* 底部滑动指引 */
    lv_obj_t *guide_lbl = lv_label_create(parent);
    apply_cjk_font(guide_lbl);
    lv_label_set_text(guide_lbl, "左右轻扫屏幕或点击下方标签切换");
    lv_obj_set_style_text_color(guide_lbl, lv_color_hex(COLOR_INK_MUTED), 0);
    lv_obj_align(guide_lbl, LV_ALIGN_BOTTOM_MID, 0, -10);
}

static void create_page_1_anki_study(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(COLOR_PARCHMENT_BG), 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶部安全指示栏：宽 336px 居中避让 AMOLED 屏幕左右大圆角 */
    lv_obj_t *top_bar = lv_obj_create(parent);
    lv_obj_set_size(top_bar, SAFE_CARD_W, 28);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_pad_all(top_bar, 0, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    s_p1_progress_lbl = lv_label_create(top_bar);
    apply_cjk_font(s_p1_progress_lbl);
    lv_label_set_text(s_p1_progress_lbl, "卡片进度 (1 / 20)");
    lv_obj_set_style_text_color(s_p1_progress_lbl, lv_color_hex(COLOR_INK_MAIN), 0);
    lv_obj_align(s_p1_progress_lbl, LV_ALIGN_LEFT_MID, 8, 0);

    /* 朗读发音小印章按钮 (右侧内缩 8px 避让物理右上圆角) */
    lv_obj_t *btn_speak = lv_button_create(top_bar);
    lv_obj_set_ext_click_area(btn_speak, 12);
    lv_obj_set_size(btn_speak, 68, 26);
    lv_obj_align(btn_speak, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_bg_color(btn_speak, lv_color_hex(COLOR_PAPER_CARD), 0);
    lv_obj_set_style_border_color(btn_speak, lv_color_hex(COLOR_PAPER_BORDER), 0);
    lv_obj_set_style_border_width(btn_speak, 1, 0);
    lv_obj_set_style_radius(btn_speak, 6, 0);
    lv_obj_clear_flag(btn_speak, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_speak, on_card_speak_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_speak, on_card_speak_clicked, LV_EVENT_SHORT_CLICKED, NULL);

    lv_obj_t *spk_lbl = lv_label_create(btn_speak);
    apply_cjk_font(spk_lbl);
    lv_label_set_text(spk_lbl, "发音");
    lv_obj_set_style_text_color(spk_lbl, lv_color_hex(COLOR_INK_MAIN), 0);
    lv_obj_center(spk_lbl);

    /* 主卡片区域（柔白纸质感便签） */
    lv_obj_t *card_box = lv_obj_create(parent);
    lv_obj_set_size(card_box, SAFE_CARD_W, 220);
    lv_obj_align(card_box, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_color(card_box, lv_color_hex(COLOR_PAPER_CARD), 0);
    lv_obj_set_style_border_color(card_box, lv_color_hex(COLOR_PAPER_BORDER), 0);
    lv_obj_set_style_border_width(card_box, 2, 0);
    lv_obj_set_style_radius(card_box, 14, 0);
    lv_obj_clear_flag(card_box, LV_OBJ_FLAG_SCROLLABLE);

    /* 正面：单词与音标 */
    s_p1_word_lbl = lv_label_create(card_box);
    apply_cjk_font(s_p1_word_lbl);
    lv_label_set_text(s_p1_word_lbl, "openvela");
    lv_obj_set_style_text_color(s_p1_word_lbl, lv_color_hex(COLOR_INK_MAIN), 0);
    lv_obj_align(s_p1_word_lbl, LV_ALIGN_TOP_LEFT, 10, 8);

    s_p1_phonetic_lbl = lv_label_create(card_box);
    apply_cjk_font(s_p1_phonetic_lbl);
    lv_label_set_text(s_p1_phonetic_lbl, "/open-vela/");
    lv_obj_set_style_text_color(s_p1_phonetic_lbl, lv_color_hex(COLOR_SEAL_HARD), 0);
    lv_obj_align(s_p1_phonetic_lbl, LV_ALIGN_TOP_LEFT, 10, 34);

    /* 背面内容（中文释义与例句，翻面前默认隐藏） */
    s_p1_meaning_lbl = lv_label_create(card_box);
    apply_cjk_font(s_p1_meaning_lbl);
    lv_obj_set_size(s_p1_meaning_lbl, 306, 60);
    lv_label_set_long_mode(s_p1_meaning_lbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_p1_meaning_lbl, "嵌入式微控制器专属的下一代端侧实时 AI 操作系统");
    lv_obj_set_style_text_color(s_p1_meaning_lbl, lv_color_hex(COLOR_SEAL_GOOD), 0);
    lv_obj_align(s_p1_meaning_lbl, LV_ALIGN_TOP_LEFT, 10, 65);
    lv_obj_add_flag(s_p1_meaning_lbl, LV_OBJ_FLAG_HIDDEN);

    s_p1_example_lbl = lv_label_create(card_box);
    apply_cjk_font(s_p1_example_lbl);
    lv_obj_set_size(s_p1_example_lbl, 306, 75);
    lv_label_set_long_mode(s_p1_example_lbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_p1_example_lbl, "例句: OpenVela OS powers intelligent edge hardware with microsecond latency.");
    lv_obj_set_style_text_color(s_p1_example_lbl, lv_color_hex(COLOR_INK_MUTED), 0);
    lv_obj_align(s_p1_example_lbl, LV_ALIGN_TOP_LEFT, 10, 130);
    lv_obj_add_flag(s_p1_example_lbl, LV_OBJ_FLAG_HIDDEN);

    /* 正面正中央显目的“查看答案”翻面按键 */
    s_p1_flip_btn = lv_button_create(card_box);
    lv_obj_set_ext_click_area(s_p1_flip_btn, 12);
    lv_obj_set_size(s_p1_flip_btn, 220, 48);
    lv_obj_align(s_p1_flip_btn, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_bg_color(s_p1_flip_btn, lv_color_hex(COLOR_BTN_BROWN), 0);
    lv_obj_set_style_radius(s_p1_flip_btn, 10, 0);
    lv_obj_clear_flag(s_p1_flip_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_p1_flip_btn, on_card_flip_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_p1_flip_btn, on_card_flip_clicked, LV_EVENT_SHORT_CLICKED, NULL);

    lv_obj_t *flip_lbl = lv_label_create(s_p1_flip_btn);
    apply_cjk_font(flip_lbl);
    lv_label_set_text(flip_lbl, "查看答案 / 翻转背面");
    lv_obj_set_style_text_color(flip_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(flip_lbl);

    /* 底部标准 Anki 4 档印章评分按键容器（翻面后显示） */
    s_p1_ratings_cont = lv_obj_create(parent);
    lv_obj_set_size(s_p1_ratings_cont, SAFE_CARD_W, 102);
    lv_obj_align(s_p1_ratings_cont, LV_ALIGN_BOTTOM_MID, 0, -68);
    lv_obj_set_style_bg_opa(s_p1_ratings_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_p1_ratings_cont, 0, 0);
    lv_obj_set_style_pad_all(s_p1_ratings_cont, 0, 0);
    lv_obj_clear_flag(s_p1_ratings_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_p1_ratings_cont, LV_OBJ_FLAG_HIDDEN);

    /* 4 档按键按 2x2 网格排列 */
    /* 1. 重来 (1m) */
    lv_obj_t *b1 = lv_button_create(s_p1_ratings_cont);
    lv_obj_set_ext_click_area(b1, 12);
    lv_obj_set_size(b1, 162, 46);
    lv_obj_align(b1, LV_ALIGN_TOP_LEFT, 5, 0);
    lv_obj_set_style_bg_color(b1, lv_color_hex(COLOR_SEAL_AGAIN), 0);
    lv_obj_set_style_radius(b1, 10, 0);
    lv_obj_clear_flag(b1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(b1, on_rating_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)ANKI_RATING_AGAIN);
    lv_obj_add_event_cb(b1, on_rating_clicked, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)ANKI_RATING_AGAIN);
    lv_obj_t *t1 = lv_label_create(b1);
    apply_cjk_font(t1);
    lv_label_set_text(t1, "1. 重来 (1m)");
    lv_obj_set_style_text_color(t1, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(t1);

    /* 2. 困难 (1d) */
    lv_obj_t *b2 = lv_button_create(s_p1_ratings_cont);
    lv_obj_set_ext_click_area(b2, 12);
    lv_obj_set_size(b2, 162, 46);
    lv_obj_align(b2, LV_ALIGN_TOP_RIGHT, -5, 0);
    lv_obj_set_style_bg_color(b2, lv_color_hex(COLOR_SEAL_HARD), 0);
    lv_obj_set_style_radius(b2, 10, 0);
    lv_obj_clear_flag(b2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(b2, on_rating_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)ANKI_RATING_HARD);
    lv_obj_add_event_cb(b2, on_rating_clicked, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)ANKI_RATING_HARD);
    lv_obj_t *t2 = lv_label_create(b2);
    apply_cjk_font(t2);
    lv_label_set_text(t2, "2. 困难 (1d)");
    lv_obj_set_style_text_color(t2, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(t2);

    /* 3. 良好 (3d) */
    lv_obj_t *b3 = lv_button_create(s_p1_ratings_cont);
    lv_obj_set_ext_click_area(b3, 12);
    lv_obj_set_size(b3, 162, 46);
    lv_obj_align(b3, LV_ALIGN_BOTTOM_LEFT, 5, 0);
    lv_obj_set_style_bg_color(b3, lv_color_hex(COLOR_SEAL_GOOD), 0);
    lv_obj_set_style_radius(b3, 10, 0);
    lv_obj_clear_flag(b3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(b3, on_rating_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)ANKI_RATING_GOOD);
    lv_obj_add_event_cb(b3, on_rating_clicked, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)ANKI_RATING_GOOD);
    lv_obj_t *t3 = lv_label_create(b3);
    apply_cjk_font(t3);
    lv_label_set_text(t3, "3. 良好 (3d)");
    lv_obj_set_style_text_color(t3, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(t3);

    /* 4. 容易 (7d) */
    lv_obj_t *b4 = lv_button_create(s_p1_ratings_cont);
    lv_obj_set_ext_click_area(b4, 12);
    lv_obj_set_size(b4, 162, 46);
    lv_obj_align(b4, LV_ALIGN_BOTTOM_RIGHT, -5, 0);
    lv_obj_set_style_bg_color(b4, lv_color_hex(COLOR_SEAL_EASY), 0);
    lv_obj_set_style_radius(b4, 10, 0);
    lv_obj_clear_flag(b4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(b4, on_rating_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)ANKI_RATING_EASY);
    lv_obj_add_event_cb(b4, on_rating_clicked, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)ANKI_RATING_EASY);
    lv_obj_t *t4 = lv_label_create(b4);
    apply_cjk_font(t4);
    lv_label_set_text(t4, "4. 容易 (7d)");
    lv_obj_set_style_text_color(t4, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(t4);
}

static void create_page_2_ai_assistant(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(COLOR_PARCHMENT_BG), 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶部标题与状态 */
    lv_obj_t *title = lv_label_create(parent);
    apply_cjk_font(title);
    lv_label_set_text(title, "随声记 AI 助教");
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_INK_MAIN), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    s_ai_status_lbl = lv_label_create(parent);
    apply_cjk_font(s_ai_status_lbl);
    lv_label_set_text(s_ai_status_lbl, "唤醒词：你好 openvela");
    lv_obj_set_style_text_color(s_ai_status_lbl, lv_color_hex(COLOR_INK_MUTED), 0);
    lv_obj_align(s_ai_status_lbl, LV_ALIGN_TOP_MID, 0, 32);

    /* 7 柱复古墨水跳动声波条 */
    lv_obj_t *wave_cont = lv_obj_create(parent);
    lv_obj_set_size(wave_cont, 240, 48);
    lv_obj_align(wave_cont, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_opa(wave_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wave_cont, 0, 0);
    lv_obj_set_style_pad_all(wave_cont, 0, 0);
    lv_obj_clear_flag(wave_cont, LV_OBJ_FLAG_SCROLLABLE);

    int start_x = 18;
    int step_x = 30;
    for (int i = 0; i < NUM_WAVE_BARS; i++) {
        s_wave_bars[i] = lv_obj_create(wave_cont);
        lv_obj_set_size(s_wave_bars[i], 12, 14);
        lv_obj_align(s_wave_bars[i], LV_ALIGN_BOTTOM_LEFT, start_x + i * step_x, 0);
        lv_obj_set_style_bg_color(s_wave_bars[i], lv_color_hex(COLOR_INK_WAVE), 0);
        lv_obj_set_style_radius(s_wave_bars[i], 5, 0);
        lv_obj_set_style_border_width(s_wave_bars[i], 0, 0);
        lv_obj_clear_flag(s_wave_bars[i], LV_OBJ_FLAG_SCROLLABLE);
    }

    /* 便签式对话消息区（柔白卡片质感） */
    lv_obj_t *chat_cont = lv_obj_create(parent);
    lv_obj_set_size(chat_cont, SAFE_CARD_W, 195);
    lv_obj_align(chat_cont, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_set_style_bg_color(chat_cont, lv_color_hex(COLOR_PAPER_CARD), 0);
    lv_obj_set_style_border_color(chat_cont, lv_color_hex(COLOR_PAPER_BORDER), 0);
    lv_obj_set_style_border_width(chat_cont, 2, 0);
    lv_obj_set_style_radius(chat_cont, 12, 0);
    lv_obj_clear_flag(chat_cont, LV_OBJ_FLAG_SCROLLABLE);

    s_chat_user_lbl = lv_label_create(chat_cont);
    apply_cjk_font(s_chat_user_lbl);
    lv_obj_set_size(s_chat_user_lbl, 316, 42);
    lv_label_set_long_mode(s_chat_user_lbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_chat_user_lbl, "用户: 你好 openvela!");
    lv_obj_set_style_text_color(s_chat_user_lbl, lv_color_hex(COLOR_SEAL_HARD), 0);
    lv_obj_align(s_chat_user_lbl, LV_ALIGN_TOP_LEFT, 5, 5);

    s_chat_ai_lbl = lv_label_create(chat_cont);
    apply_cjk_font(s_chat_ai_lbl);
    lv_obj_set_size(s_chat_ai_lbl, 316, 135);
    lv_label_set_long_mode(s_chat_ai_lbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_chat_ai_lbl, "AI: 你好！我是随声记 AI 助教，随时为你答疑解惑。");
    lv_obj_set_style_text_color(s_chat_ai_lbl, lv_color_hex(COLOR_INK_MAIN), 0);
    lv_obj_align(s_chat_ai_lbl, LV_ALIGN_TOP_LEFT, 5, 52);

    /* 底部 3 个快捷便签胶囊按钮 */
    lv_obj_t *b_ex = lv_button_create(parent);
    lv_obj_set_ext_click_area(b_ex, 12);
    lv_obj_set_size(b_ex, 104, 40);
    lv_obj_align(b_ex, LV_ALIGN_BOTTOM_LEFT, 27, -68);
    lv_obj_set_style_bg_color(b_ex, lv_color_hex(COLOR_BTN_SLATE), 0);
    lv_obj_set_style_radius(b_ex, 8, 0);
    lv_obj_clear_flag(b_ex, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(b_ex, on_ai_prompt_clicked, LV_EVENT_CLICKED, (void *)"example");
    lv_obj_add_event_cb(b_ex, on_ai_prompt_clicked, LV_EVENT_SHORT_CLICKED, (void *)"example");
    lv_obj_t *lbl_ex = lv_label_create(b_ex);
    apply_cjk_font(lbl_ex);
    lv_label_set_text(lbl_ex, "例句讲解");
    lv_obj_set_style_text_color(lbl_ex, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_ex);

    lv_obj_t *b_syn = lv_button_create(parent);
    lv_obj_set_ext_click_area(b_syn, 12);
    lv_obj_set_size(b_syn, 104, 40);
    lv_obj_align(b_syn, LV_ALIGN_BOTTOM_MID, 0, -68);
    lv_obj_set_style_bg_color(b_syn, lv_color_hex(COLOR_BTN_BROWN), 0);
    lv_obj_set_style_radius(b_syn, 8, 0);
    lv_obj_clear_flag(b_syn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(b_syn, on_ai_prompt_clicked, LV_EVENT_CLICKED, (void *)"synonym");
    lv_obj_add_event_cb(b_syn, on_ai_prompt_clicked, LV_EVENT_SHORT_CLICKED, (void *)"synonym");
    lv_obj_t *lbl_syn = lv_label_create(b_syn);
    apply_cjk_font(lbl_syn);
    lv_label_set_text(lbl_syn, "同义辨析");
    lv_obj_set_style_text_color(lbl_syn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_syn);

    lv_obj_t *b_root = lv_button_create(parent);
    lv_obj_set_ext_click_area(b_root, 12);
    lv_obj_set_size(b_root, 104, 40);
    lv_obj_align(b_root, LV_ALIGN_BOTTOM_RIGHT, -27, -68);
    lv_obj_set_style_bg_color(b_root, lv_color_hex(COLOR_SEAL_GOOD), 0);
    lv_obj_set_style_radius(b_root, 8, 0);
    lv_obj_clear_flag(b_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(b_root, on_ai_prompt_clicked, LV_EVENT_CLICKED, (void *)"root");
    lv_obj_add_event_cb(b_root, on_ai_prompt_clicked, LV_EVENT_SHORT_CLICKED, (void *)"root");
    lv_obj_t *lbl_root = lv_label_create(b_root);
    apply_cjk_font(lbl_root);
    lv_label_set_text(lbl_root, "词根拆解");
    lv_obj_set_style_text_color(lbl_root, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_root);
}

static void create_page_3_settings(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(COLOR_PARCHMENT_BG), 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶部标题栏：左侧 "设备" 大字标题，右侧 "刷新" 按钮 (100% 对齐系统设计图) */
    lv_obj_t *title = lv_label_create(parent);
    apply_cjk_font(title);
    lv_label_set_text(title, "设备");
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_INK_MAIN), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 24, 18);

    lv_obj_t *btn_refresh = lv_button_create(parent);
    lv_obj_set_ext_click_area(btn_refresh, 12);
    lv_obj_set_size(btn_refresh, 72, 34);
    lv_obj_align(btn_refresh, LV_ALIGN_TOP_RIGHT, -24, 12);
    lv_obj_set_style_bg_color(btn_refresh, lv_color_hex(COLOR_BTN_SLATE), 0);
    lv_obj_set_style_radius(btn_refresh, 6, 0);
    lv_obj_clear_flag(btn_refresh, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_refresh, on_bt_scan_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_refresh, on_bt_scan_clicked, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_t *lbl_refresh = lv_label_create(btn_refresh);
    apply_cjk_font(lbl_refresh);
    lv_label_set_text(lbl_refresh, "刷新");
    lv_obj_set_style_text_color(lbl_refresh, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_refresh);

    /* 主设备列表白底卡片容器 (342 x 320, 避让底部 46px 悬浮 Dock 栏) */
    s_bt_list = lv_obj_create(parent);
    lv_obj_set_size(s_bt_list, 342, 320);
    lv_obj_align(s_bt_list, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_color(s_bt_list, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(s_bt_list, lv_color_hex(0xE2DDD5), 0);
    lv_obj_set_style_border_width(s_bt_list, 1, 0);
    lv_obj_set_style_radius(s_bt_list, 8, 0);
    lv_obj_set_style_pad_all(s_bt_list, 0, 0);
    lv_obj_clear_flag(s_bt_list, LV_OBJ_FLAG_SCROLLABLE);

    /* 初始未扫描提示 */
    lv_obj_t *placeholder = lv_label_create(s_bt_list);
    apply_cjk_font(placeholder);
    lv_label_set_text(placeholder, "点击右上角刷新扫描周围设备");
    lv_obj_set_style_text_color(placeholder, lv_color_hex(COLOR_INK_MUTED), 0);
    lv_obj_center(placeholder);
}

static void create_bottom_nav_bar(lv_obj_t *scr)
{
    /* 悬浮胶囊式手账 Dock 栏：避让 AMOLED 屏幕大圆角，底部悬浮 12px */
    lv_obj_t *nav = lv_obj_create(scr);
    lv_obj_set_size(nav, DOCK_W, NAV_H);
    lv_obj_align(nav, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_color(nav, lv_color_hex(0xECE3D0), 0);
    lv_obj_set_style_border_color(nav, lv_color_hex(COLOR_PAPER_BORDER), 0);
    lv_obj_set_style_border_width(nav, 2, 0);
    lv_obj_set_style_radius(nav, 23, 0);
    lv_obj_set_style_pad_all(nav, 4, 0);
    lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);

    const char *tabs[] = {"仪表盘", "记忆卡", "AI助教", "设置"};
    int tab_w = 78;
    int gap = 5;
    for (int i = 0; i < 4; i++) {
        s_nav_btns[i] = lv_button_create(nav);
        lv_obj_set_ext_click_area(s_nav_btns[i], 12);
        lv_obj_set_size(s_nav_btns[i], tab_w, 36);
        lv_obj_align(s_nav_btns[i], LV_ALIGN_LEFT_MID, 4 + i * (tab_w + gap), 0);
        lv_obj_set_style_radius(s_nav_btns[i], 18, 0);
        lv_obj_set_style_border_width(s_nav_btns[i], 1, 0);
        lv_obj_clear_flag(s_nav_btns[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(s_nav_btns[i], on_nav_btn_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_add_event_cb(s_nav_btns[i], on_nav_btn_clicked, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)i);

        lv_obj_t *lbl = lv_label_create(s_nav_btns[i]);
        apply_cjk_font(lbl);
        lv_label_set_text(lbl, tabs[i]);
        lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_INK_MAIN), 0);
        lv_obj_center(lbl);
    }
    update_nav_buttons_style(0);
}

/* -------------------------------------------------------------------------
 * 对外公开接口实现
 * ------------------------------------------------------------------------- */
int vocavibe_ui_init(const vocavibe_ui_callbacks_t *cbs)
{
    if (cbs) {
        s_cbs = *cbs;
    }

    if (!lv_is_initialized()) {
        lv_init();
    }

    if (lv_display_get_default() == NULL) {
        /* 1. 开机等待底层 LCD 设备就绪（最多 3 秒） */
        for (int retry = 0; retry < 30; retry++) {
            if (access("/dev/lcd0", F_OK) == 0) {
                break;
            }
            usleep(100000);
        }

        static lv_nuttx_dsc_t info;
        static lv_nuttx_result_t result;
        lv_nuttx_dsc_init(&info);

        info.fb_path = "/dev/lcd0";
#ifdef CONFIG_INPUT_TOUCHSCREEN
        info.input_path = "/dev/input0";
#endif

        lv_nuttx_init(&info, &result);
        printf("[VocaVibe UI] lv_nuttx_init 完成: 屏幕 disp=%p, 触控 indev=%p\n",
               result.disp, result.indev);
        if (result.disp == NULL) {
            printf("[VocaVibe UI] 严重错误: 屏幕初始化失败！\n");
            return -1;
        }

        /* 2. 硬件就绪后强制发送 LCDDEVIO_SETPOWER 点亮 AMOLED 屏幕 (DisplayOn 0x29) */
        usleep(50000); /* 确保硬件线程完成 Init */
        int lcd_fd = open("/dev/lcd0", O_RDWR | O_CLOEXEC);
        if (lcd_fd >= 0) {
            int power = 100;
            for (int p_retry = 0; p_retry < 3; p_retry++) {
                ioctl(lcd_fd, LCDDEVIO_SETPOWER, (unsigned long)power);
                usleep(10000);
            }
            close(lcd_fd);
            printf("[VocaVibe UI] 已发送 LCDDEVIO_SETPOWER 点亮屏幕 (Power=100)\n");
        } else {
            printf("[VocaVibe UI] 警告: /dev/lcd0 暂无法打开 (errno=%d)\n", errno);
        }

        /* 挂接 FT6146 双模硬件直通触控引擎 */
        vocavibe_touch_init(result.disp);
    }

    lv_obj_t *scr = lv_screen_active();
    if (!scr) {
        printf("[VocaVibe UI] 严重错误: 无法获取活跃屏幕\n");
        return -1;
    }
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_PARCHMENT_BG), 0);

    /* 创建 4 页面 Tileview */
    s_tv = lv_tileview_create(scr);
    lv_obj_set_size(s_tv, SCREEN_W, TV_H);
    lv_obj_align(s_tv, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(s_tv, lv_color_hex(COLOR_PARCHMENT_BG), 0);
    lv_obj_add_event_cb(s_tv, on_tileview_value_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_tv, on_tileview_value_changed, LV_EVENT_SCROLL_END, NULL);

    /* Page 0: 仪表盘 */
    s_tiles[0] = lv_tileview_add_tile(s_tv, 0, 0, LV_DIR_RIGHT);
    create_page_0_dashboard(s_tiles[0]);

    /* Page 1: Anki 卡片复习 */
    s_tiles[1] = lv_tileview_add_tile(s_tv, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    create_page_1_anki_study(s_tiles[1]);

    /* Page 2: AI 助教 */
    s_tiles[2] = lv_tileview_add_tile(s_tv, 2, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    create_page_2_ai_assistant(s_tiles[2]);

    /* Page 3: 设置与代理 */
    s_tiles[3] = lv_tileview_add_tile(s_tv, 3, 0, LV_DIR_LEFT);
    create_page_3_settings(s_tiles[3]);

    /* 创建固定在屏幕底部的 4 按钮触控导航栏 */
    create_bottom_nav_bar(scr);

    /* 强制立即刷新首帧，确保开机上电显存立即呈现羊皮纸 UI */
    if (lv_display_get_default()) {
        lv_refr_now(lv_display_get_default());
    }

    printf("[VocaVibe UI] 羊皮纸纸质手账 UI 与 4 页面全触控 Tileview 初始化成功\n");
    return 0;
}

void vocavibe_ui_poll(void)
{
    /* 开机前几帧确保底层硬件线程与显示电源 100% 处于开启状态 */
    static int s_pwr_sync_cnt = 0;
    if (s_pwr_sync_cnt < 5) {
        s_pwr_sync_cnt++;
        int pfd = open("/dev/lcd0", O_RDWR | O_CLOEXEC);
        if (pfd >= 0) {
            ioctl(pfd, LCDDEVIO_SETPOWER, 100);
            close(pfd);
        }
    }

    /* 1. 切换页面请求 */
    if (s_switch_to_page >= 0 && s_switch_to_page < 4 && s_tv) {
        lv_tileview_set_tile_by_index(s_tv, (uint32_t)s_switch_to_page, 0, LV_ANIM_OFF);
        update_nav_buttons_style(s_switch_to_page);
        s_switch_to_page = -1;
    }

    /* 2. 仪表盘数据刷新 */
    if (s_stat_dirty) {
        s_stat_dirty = false;
        char buf[16];
        if (s_p0_due_val) {
            snprintf(buf, sizeof(buf), "%d", s_stat_due);
            lv_label_set_text(s_p0_due_val, buf);
        }
        if (s_p0_done_val) {
            snprintf(buf, sizeof(buf), "%d", s_stat_reviewed);
            lv_label_set_text(s_p0_done_val, buf);
        }
        if (s_p0_total_val) {
            snprintf(buf, sizeof(buf), "%d", s_stat_total);
            lv_label_set_text(s_p0_total_val, buf);
        }
    }

    /* 3. 卡片数据与正反面切换刷新 (支持 HTML 解析) */
    if (s_card_dirty) {
        s_card_dirty = false;
        char plain_buf[384];

        if (s_p1_word_lbl) {
            vocavibe_html_to_plain_text(s_cur_word, plain_buf, sizeof(plain_buf));
            lv_label_set_text(s_p1_word_lbl, plain_buf);
        }
        if (s_p1_phonetic_lbl) {
            vocavibe_html_to_plain_text(s_cur_phonetic, plain_buf, sizeof(plain_buf));
            lv_label_set_text(s_p1_phonetic_lbl, plain_buf);
        }
        if (s_p1_meaning_lbl) {
            vocavibe_html_to_plain_text(s_cur_meaning, plain_buf, sizeof(plain_buf));
            lv_label_set_text(s_p1_meaning_lbl, plain_buf);
        }
        if (s_p1_example_lbl) {
            vocavibe_html_to_plain_text(s_cur_example, plain_buf, sizeof(plain_buf));
            lv_label_set_text(s_p1_example_lbl, plain_buf);
        }

        char prog_buf[48];
        snprintf(prog_buf, sizeof(prog_buf), "卡片进度 (%d / %d)", s_cur_idx, s_total_cnt);
        if (s_p1_progress_lbl) lv_label_set_text(s_p1_progress_lbl, prog_buf);

        s_card_back_visible = s_card_req_back;

        /* Anki 原生交互：未翻开时隐藏答案和 4 档打分，显示“查看答案”；翻开后相反 */
        if (s_card_back_visible) {
            if (s_p1_meaning_lbl) lv_obj_clear_flag(s_p1_meaning_lbl, LV_OBJ_FLAG_HIDDEN);
            if (s_p1_example_lbl) lv_obj_clear_flag(s_p1_example_lbl, LV_OBJ_FLAG_HIDDEN);
            if (s_p1_flip_btn) lv_obj_add_flag(s_p1_flip_btn, LV_OBJ_FLAG_HIDDEN);
            if (s_p1_ratings_cont) lv_obj_clear_flag(s_p1_ratings_cont, LV_OBJ_FLAG_HIDDEN);
        } else {
            if (s_p1_meaning_lbl) lv_obj_add_flag(s_p1_meaning_lbl, LV_OBJ_FLAG_HIDDEN);
            if (s_p1_example_lbl) lv_obj_add_flag(s_p1_example_lbl, LV_OBJ_FLAG_HIDDEN);
            if (s_p1_flip_btn) lv_obj_clear_flag(s_p1_flip_btn, LV_OBJ_FLAG_HIDDEN);
            if (s_p1_ratings_cont) lv_obj_add_flag(s_p1_ratings_cont, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* 4. AI 助教对话气泡刷新 */
    if (s_chat_dirty) {
        s_chat_dirty = false;
        if (s_chat_user_lbl) lv_label_set_text(s_chat_user_lbl, s_chat_user_buf);
        if (s_chat_ai_lbl) lv_label_set_text(s_chat_ai_lbl, s_chat_ai_buf);
    }

    /* 5. 小智墨水律动声波动效 */
    s_anim_phase += 0.15f;
    if (s_anim_phase > 6.28318f) s_anim_phase -= 6.28318f;

    float amp = 6.0f;
    if (s_ai_state == AI_STATE_LISTENING) {
        amp = 26.0f;
        if (s_ai_status_lbl) lv_label_set_text(s_ai_status_lbl, "正在聆听您的提问...");
    } else if (s_ai_state == AI_STATE_THINKING) {
        amp = 18.0f;
        if (s_ai_status_lbl) lv_label_set_text(s_ai_status_lbl, "MiMo 2.5 正在流式思考中...");
    } else if (s_ai_state == AI_STATE_SPEAKING) {
        amp = 30.0f;
        if (s_ai_status_lbl) lv_label_set_text(s_ai_status_lbl, "正在通过蓝牙耳机播报语音...");
    } else {
        amp = 6.0f;
        if (s_ai_status_lbl) lv_label_set_text(s_ai_status_lbl, "唤醒词：你好 openvela");
    }

    for (int i = 0; i < NUM_WAVE_BARS; i++) {
        if (s_wave_bars[i]) {
            float h = 10.0f + fabsf(sinf(s_anim_phase + (float)i * 0.9f)) * amp;
            lv_obj_set_height(s_wave_bars[i], (int32_t)h);
        }
    }

    /* 6. 网络代理、Anki 同步与蓝牙状态 */
    if (s_net_dirty) {
        s_net_dirty = false;
        if (s_net_status_lbl) {
            lv_label_set_text(s_net_status_lbl, s_net_status_buf);
            if (s_net_connected) {
                lv_obj_set_style_text_color(s_net_status_lbl, lv_color_hex(COLOR_SEAL_EASY), 0);
            } else {
                lv_obj_set_style_text_color(s_net_status_lbl, lv_color_hex(COLOR_INK_MUTED), 0);
            }
        }
        if (s_lbl_net_conn) {
            lv_label_set_text(s_lbl_net_conn, s_net_connected ? "已连接" : "连接");
        }
        if (s_btn_net_conn) {
            lv_obj_set_style_bg_color(s_btn_net_conn, lv_color_hex(s_net_connected ? COLOR_SEAL_GOOD : COLOR_BTN_SLATE), 0);
        }
    }
    if (s_sync_dirty) {
        s_sync_dirty = false;
        if (s_sync_status_lbl) lv_label_set_text(s_sync_status_lbl, s_sync_status_buf);
    }
    if (s_bt_dirty) {
        s_bt_dirty = false;
        if (s_bt_status_lbl) lv_label_set_text(s_bt_status_lbl, s_bt_status_buf);
    }

    /* 7. 设备列表刷新：完全对应手机/电脑设备列表规范 (左侧名称，右侧状态，底部分割线) */
    if (s_bt_list_dirty && s_bt_list) {
        s_bt_list_dirty = false;
        lv_obj_clean(s_bt_list);
        if (s_bt_entry_count == 0) {
            lv_obj_t *placeholder = lv_label_create(s_bt_list);
            apply_cjk_font(placeholder);
            lv_label_set_text(placeholder, "点击右上角刷新扫描周围设备");
            lv_obj_set_style_text_color(placeholder, lv_color_hex(COLOR_INK_MUTED), 0);
            lv_obj_center(placeholder);
        } else {
            int row_h = 50;
            for (int i = 0; i < s_bt_entry_count; i++) {
                lv_obj_t *row = lv_button_create(s_bt_list);
                lv_obj_set_size(row, 342, row_h);
                lv_obj_align(row, LV_ALIGN_TOP_LEFT, 0, i * row_h);
                lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
                lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
                lv_obj_set_style_border_color(row, lv_color_hex(0xEDE7DE), 0);
                lv_obj_set_style_border_width(row, 1, 0);
                lv_obj_set_style_radius(row, 0, 0);
                lv_obj_set_style_pad_all(row, 0, 0);
                lv_obj_set_style_shadow_width(row, 0, 0);
                lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

                /* 左侧设备名 */
                lv_obj_t *name_lbl = lv_label_create(row);
                apply_cjk_font(name_lbl);
                lv_label_set_text(name_lbl, s_bt_entries[i].name);
                lv_obj_set_style_text_color(name_lbl, lv_color_hex(COLOR_INK_MAIN), 0);
                lv_obj_align(name_lbl, LV_ALIGN_LEFT_MID, 16, 0);

                /* 右侧状态标签 */
                lv_obj_t *status_lbl = lv_label_create(row);
                apply_cjk_font(status_lbl);
                const char *st = s_bt_entries[i].status[0] ? s_bt_entries[i].status : (s_bt_entries[i].is_connected ? "已连接" : "未设置");
                lv_label_set_text(status_lbl, st);
                if (s_bt_entries[i].is_connected || strcmp(st, "已连接") == 0) {
                    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x2E7D32), 0);
                } else if (strcmp(st, "已断开") == 0) {
                    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x5C5449), 0);
                } else {
                    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x9E9589), 0);
                }
                lv_obj_align(status_lbl, LV_ALIGN_RIGHT_MID, -16, 0);

                lv_obj_add_event_cb(row, on_bt_dev_item_clicked, LV_EVENT_CLICKED, (void *)s_bt_entries[i].mac);
                lv_obj_add_event_cb(row, on_bt_dev_item_clicked, LV_EVENT_SHORT_CLICKED, (void *)s_bt_entries[i].mac);
            }
        }
    }

    /* 8. 处理 LVGL 定时器和双模触控事件 (提高到 100Hz+ 采样率以提升触控灵敏度) */
    uint32_t idle = lv_timer_handler();
    idle = (idle > 0 && idle <= 10) ? idle : 8;
    usleep(idle * 1000);
}

void vocavibe_ui_switch_page(int page_idx)
{
    s_switch_to_page = page_idx;
}

static void dump_obj_tree(lv_obj_t *obj, int depth)
{
    if (!obj) return;
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    bool hidden = lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN);
    const char *txt = "";
    if (lv_obj_check_type(obj, &lv_label_class)) {
        txt = lv_label_get_text(obj);
    }
    for (int i = 0; i < depth; i++) printf("  ");
    printf("[%d,%d -> %d,%d] w=%d h=%d hidden=%d %s\n",
           (int)coords.x1, (int)coords.y1, (int)coords.x2, (int)coords.y2,
           (int)(coords.x2 - coords.x1 + 1), (int)(coords.y2 - coords.y1 + 1),
           hidden ? 1 : 0, txt && txt[0] ? txt : "");

    uint32_t cnt = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < cnt; i++) {
        dump_obj_tree(lv_obj_get_child(obj, i), depth + 1);
    }
}

void vocavibe_ui_dump_layout(void)
{
    printf("\n=== [VocaVibe UI 真机绝对坐标与组件树 Dump] ===\n");
    printf("当前活动页面: Page %d\n", s_current_page);
    for (int p = 0; p < 4; p++) {
        printf("\n--- Page %d (Tile %d) ---\n", p, p);
        if (s_tiles[p]) {
            dump_obj_tree(s_tiles[p], 1);
        }
    }
    printf("================================================\n\n");
}

void vocavibe_ui_update_dashboard(int total, int due, int reviewed)
{
    s_stat_total = total;
    s_stat_due = due;
    s_stat_reviewed = reviewed;
    s_stat_dirty = true;
}

void vocavibe_ui_show_card(const anki_card_t *card, bool show_back, int cur_idx, int total_cnt)
{
    if (card) {
        strncpy(s_cur_word, card->word, sizeof(s_cur_word) - 1);
        s_cur_word[sizeof(s_cur_word) - 1] = '\0';
        strncpy(s_cur_phonetic, card->phonetic, sizeof(s_cur_phonetic) - 1);
        s_cur_phonetic[sizeof(s_cur_phonetic) - 1] = '\0';
        strncpy(s_cur_meaning, card->meaning, sizeof(s_cur_meaning) - 1);
        s_cur_meaning[sizeof(s_cur_meaning) - 1] = '\0';
        strncpy(s_cur_example, card->example, sizeof(s_cur_example) - 1);
        s_cur_example[sizeof(s_cur_example) - 1] = '\0';
    }
    s_cur_idx = cur_idx;
    s_total_cnt = total_cnt;
    s_card_req_back = show_back;
    s_card_dirty = true;
}

void vocavibe_ui_set_ai_state(vocavibe_ai_state_t state)
{
    s_ai_state = state;
}

void vocavibe_ui_append_ai_stream(const char *delta)
{
    if (!delta) return;
    size_t cur_len = strlen(s_chat_ai_buf);
    size_t dlen = strlen(delta);
    if (cur_len + dlen < sizeof(s_chat_ai_buf) - 1) {
        strcat(s_chat_ai_buf, delta);
        s_chat_dirty = true;
    }
}

void vocavibe_ui_set_ai_chat(const char *user_query, const char *ai_reply)
{
    if (user_query) {
        snprintf(s_chat_user_buf, sizeof(s_chat_user_buf), "用户: %s", user_query);
    }
    if (ai_reply) {
        snprintf(s_chat_ai_buf, sizeof(s_chat_ai_buf), "AI: %s", ai_reply);
    }
    s_chat_dirty = true;
}

void vocavibe_ui_add_bt_device(const char *name, const char *mac, const char *status_str, int rssi, bool is_connected)
{
    if (s_bt_entry_count < MAX_BT_DEVICES) {
        strncpy(s_bt_entries[s_bt_entry_count].name, name ? name : "未知设备", sizeof(s_bt_entries[0].name) - 1);
        strncpy(s_bt_entries[s_bt_entry_count].mac, mac ? mac : "--:--", sizeof(s_bt_entries[0].mac) - 1);
        strncpy(s_bt_entries[s_bt_entry_count].status, status_str ? status_str : (is_connected ? "已连接" : "未设置"), sizeof(s_bt_entries[0].status) - 1);
        s_bt_entries[s_bt_entry_count].rssi = rssi;
        s_bt_entries[s_bt_entry_count].is_connected = is_connected;
        s_bt_entry_count++;
        s_bt_list_dirty = true;
    }
}

void vocavibe_ui_clear_bt_devices(void)
{
    s_bt_entry_count = 0;
    s_bt_list_dirty = true;
}

void vocavibe_ui_update_device_status(const char *mac, const char *name, bool connected)
{
    for (int i = 0; i < s_bt_entry_count; i++) {
        if ((mac && mac[0] && strcmp(s_bt_entries[i].mac, mac) == 0) ||
            (name && name[0] && strstr(s_bt_entries[i].name, name))) {
            s_bt_entries[i].is_connected = connected;
            strncpy(s_bt_entries[i].status, connected ? "已连接" : "已断开", sizeof(s_bt_entries[i].status) - 1);
        } else if (connected) {
            if (s_bt_entries[i].is_connected) {
                s_bt_entries[i].is_connected = false;
                strncpy(s_bt_entries[i].status, "已断开", sizeof(s_bt_entries[i].status) - 1);
            }
        }
    }
    s_bt_list_dirty = true;
}

void vocavibe_ui_set_bt_status(const char *status_str, bool connected)
{
    (void)connected;
    if (status_str) {
        strncpy(s_bt_status_buf, status_str, sizeof(s_bt_status_buf) - 1);
        s_bt_status_buf[sizeof(s_bt_status_buf) - 1] = '\0';
        s_bt_dirty = true;
    }
}

void vocavibe_ui_set_sync_status(const char *status_str)
{
    if (status_str) {
        strncpy(s_sync_status_buf, status_str, sizeof(s_sync_status_buf) - 1);
        s_sync_status_buf[sizeof(s_sync_status_buf) - 1] = '\0';
        s_sync_dirty = true;
    }
}

void vocavibe_ui_set_net_status(bool connected, const char *status_str)
{
    s_net_connected = connected;
    if (status_str) {
        strncpy(s_net_status_buf, status_str, sizeof(s_net_status_buf) - 1);
        s_net_status_buf[sizeof(s_net_status_buf) - 1] = '\0';
    } else {
        snprintf(s_net_status_buf, sizeof(s_net_status_buf), "网络代理: %s", connected ? "已连接" : "未连接");
    }
    s_net_dirty = true;
}
