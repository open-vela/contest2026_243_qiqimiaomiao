/****************************************************************************
 * contest2026_243_qiqimiaomiao/app/vocavibe/src/vocavibe_ui.c
 *
 * VocaVibe 屏幕全触控交互 UI (LVGL 9 on SF32LB52 390x450 AMOLED + FT6146 Touch)
 * 4 页面 Tileview 架构：
 *   Page 0: 仪表盘 Dashboard (统计、快捷入口)
 *   Page 1: Anki 卡片复习 (SM-2 4 档评分、正反面翻转)
 *   Page 2: VocaVibe AI 助教 (小智声波动效、打字机气泡、快捷问答)
 *   Page 3: 同步与蓝牙耳机代理设置 (AnkiConnect 同步、蓝牙耳机列表遥控)
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>

#include <lvgl/lvgl.h>
#include "vocavibe_ui.h"
#include "vocavibe_core.h"

#define SCREEN_W 390
#define SCREEN_H 450

static pthread_t s_ui_tid;
static volatile bool s_ui_running = false;
static vocavibe_ui_callbacks_t s_cbs;

/* Tileview 与主对象 */
static lv_obj_t *s_tv = NULL;
static lv_obj_t *s_tiles[4];

/* --- Page 0: 仪表盘控件 --- */
static lv_obj_t *s_p0_due_val = NULL;
static lv_obj_t *s_p0_done_val = NULL;
static lv_obj_t *s_p0_total_val = NULL;

/* --- Page 1: Anki 卡片控件 --- */
static lv_obj_t *s_p1_progress_lbl = NULL;
static lv_obj_t *s_p1_card_box = NULL;
static lv_obj_t *s_p1_word_lbl = NULL;
static lv_obj_t *s_p1_phonetic_lbl = NULL;
static lv_obj_t *s_p1_meaning_box = NULL;
static lv_obj_t *s_p1_meaning_lbl = NULL;
static lv_obj_t *s_p1_example_lbl = NULL;
static lv_obj_t *s_p1_btn_flip = NULL;
static lv_obj_t *s_p1_flip_lbl = NULL;
static lv_obj_t *s_p1_ratings_cont = NULL;
static bool s_card_back_visible = false;

/* --- Page 2: AI 助教与声波动效控件 --- */
#define NUM_WAVE_BARS 7
static lv_obj_t *s_wave_bars[NUM_WAVE_BARS];
static lv_obj_t *s_ai_status_lbl = NULL;
static lv_obj_t *s_chat_user_lbl = NULL;
static lv_obj_t *s_chat_ai_lbl = NULL;
static vocavibe_ai_state_t s_ai_state = AI_STATE_IDLE;
static float s_anim_phase = 0.0f;

/* --- Page 3: 同步与蓝牙代理控件 --- */
static lv_obj_t *s_sync_status_lbl = NULL;
static lv_obj_t *s_bt_status_lbl = NULL;
static lv_obj_t *s_bt_list = NULL;

/* 线程安全数据传递缓冲区 */
static int s_stat_total = 20, s_stat_due = 20, s_stat_reviewed = 0;
static volatile bool s_stat_dirty = true;

static char s_cur_word[48] = "openvela";
static char s_cur_phonetic[48] = "/ˈoʊpən ˈvɛlə/";
static char s_cur_meaning[128] = "面向端侧 AI 与嵌入式微控制器的开源实时操作系统";
static char s_cur_example[192] = "OpenVela OS powers intelligent edge hardware.";
static int s_cur_idx = 1, s_total_cnt = 20;
static volatile bool s_card_dirty = true;
static volatile bool s_card_req_back = false;

static char s_chat_user_buf[128] = "User: 你好，openvela！";
static char s_chat_ai_buf[512] = "AI: 你好！我是 VocaVibe 智能助教，随时为你解答词汇与例句用法。";
static volatile bool s_chat_dirty = true;

static char s_sync_status_buf[64] = "AnkiConnect: 待同步 (127.0.0.1:8765)";
static volatile bool s_sync_dirty = true;

static char s_bt_status_buf[64] = "蓝牙耳机: 未连接 (伴侣代理)";
static volatile bool s_bt_dirty = true;

#define MAX_BT_DEVICES 6
typedef struct {
    char name[32];
    char mac[20];
    int rssi;
} bt_dev_entry_t;
static bt_dev_entry_t s_bt_entries[MAX_BT_DEVICES];
static int s_bt_entry_count = 0;
static volatile bool s_bt_list_dirty = false;
static volatile int s_switch_to_page = -1;

/* -------------------------------------------------------------------------
 * 事件回调函数
 * ------------------------------------------------------------------------- */
static void on_dash_start_study_clicked(lv_event_t *e)
{
    (void)e;
    vocavibe_ui_switch_page(1);
}

static void on_dash_start_ai_clicked(lv_event_t *e)
{
    (void)e;
    vocavibe_ui_switch_page(2);
}

static void on_card_flip_clicked(lv_event_t *e)
{
    (void)e;
    s_card_back_visible = !s_card_back_visible;
    if (s_p1_meaning_box) {
        if (s_card_back_visible) {
            lv_obj_clear_flag(s_p1_meaning_box, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_p1_ratings_cont, LV_OBJ_FLAG_HIDDEN);
            if (s_p1_flip_lbl) lv_label_set_text(s_p1_flip_lbl, "隐藏答案");
        } else {
            lv_obj_add_flag(s_p1_meaning_box, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_p1_ratings_cont, LV_OBJ_FLAG_HIDDEN);
            if (s_p1_flip_lbl) lv_label_set_text(s_p1_flip_lbl, "显示答案");
        }
    }
}

static void on_card_speak_clicked(lv_event_t *e)
{
    (void)e;
    vocavibe_core_request_tts(s_cur_word);
}

static void on_rating_clicked(lv_event_t *e)
{
    uintptr_t rating_val = (uintptr_t)lv_event_get_user_data(e);
    anki_rating_t rating = (anki_rating_t)rating_val;
    if (s_cbs.on_card_answer) {
        s_cbs.on_card_answer(rating);
    }
}

static void on_ai_prompt_clicked(lv_event_t *e)
{
    const char *prompt_type = (const char *)lv_event_get_user_data(e);
    char query[128];
    if (strcmp(prompt_type, "example") == 0) {
        snprintf(query, sizeof(query), "请为单词 %s 造两个地道的英文例句并附带中文释义", s_cur_word);
    } else if (strcmp(prompt_type, "synonym") == 0) {
        snprintf(query, sizeof(query), "请讲解单词 %s 的近义词辨析与使用语境", s_cur_word);
    } else {
        snprintf(query, sizeof(query), "请详细拆解单词 %s 的词根词缀与高效记忆法", s_cur_word);
    }

    vocavibe_ui_set_ai_state(AI_STATE_THINKING);
    vocavibe_ui_set_ai_chat(query, "AI 思考中...");
    if (s_cbs.on_ai_query) {
        s_cbs.on_ai_query(query);
    }
}

static void on_sync_pull_clicked(lv_event_t *e)
{
    (void)e;
    vocavibe_ui_set_sync_status("AnkiConnect: 同步请求中...");
    if (s_cbs.on_sync) {
        s_cbs.on_sync();
    }
}

static void on_bt_scan_clicked(lv_event_t *e)
{
    (void)e;
    vocavibe_ui_set_bt_status("正在扫描周围蓝牙耳机...", false);
    if (s_cbs.on_bt_scan) {
        s_cbs.on_bt_scan();
    }
}

static void on_bt_dev_item_clicked(lv_event_t *e)
{
    const char *mac = (const char *)lv_event_get_user_data(e);
    if (mac && s_cbs.on_bt_connect) {
        s_cbs.on_bt_connect(mac);
    }
}

/* -------------------------------------------------------------------------
 * UI 视图创建
 * ------------------------------------------------------------------------- */
static void create_page_0_dashboard(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0c1017), 0);

    /* 顶部标题区 */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "VocaVibe 随声记");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00e5ff), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t *sub = lv_label_create(parent);
    lv_label_set_text(sub, "OpenVela 2026 AI 硬件大赛 | Team 243");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x78909c), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 42);

    /* 学习进度统计卡片 */
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 360, 110);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 75);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x161d2b), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2d3a54), 0);
    lv_obj_set_style_radius(card, 12, 0);

    /* 三列统计：待复习 / 已掌握 / 总词库 */
    s_p0_due_val = lv_label_create(card);
    lv_label_set_text(s_p0_due_val, "20");
    lv_obj_set_style_text_color(s_p0_due_val, lv_color_hex(0xff7043), 0);
    lv_obj_align(s_p0_due_val, LV_ALIGN_TOP_LEFT, 25, 12);
    lv_obj_t *due_sub = lv_label_create(card);
    lv_label_set_text(due_sub, "今日待学");
    lv_obj_set_style_text_color(due_sub, lv_color_hex(0x90a4ae), 0);
    lv_obj_align(due_sub, LV_ALIGN_TOP_LEFT, 15, 45);

    s_p0_done_val = lv_label_create(card);
    lv_label_set_text(s_p0_done_val, "0");
    lv_obj_set_style_text_color(s_p0_done_val, lv_color_hex(0x66bb6a), 0);
    lv_obj_align(s_p0_done_val, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_t *done_sub = lv_label_create(card);
    lv_label_set_text(done_sub, "今日已练");
    lv_obj_set_style_text_color(done_sub, lv_color_hex(0x90a4ae), 0);
    lv_obj_align(done_sub, LV_ALIGN_TOP_MID, 0, 45);

    s_p0_total_val = lv_label_create(card);
    lv_label_set_text(s_p0_total_val, "20");
    lv_obj_set_style_text_color(s_p0_total_val, lv_color_hex(0x29b6f6), 0);
    lv_obj_align(s_p0_total_val, LV_ALIGN_TOP_RIGHT, -25, 12);
    lv_obj_t *tot_sub = lv_label_create(card);
    lv_label_set_text(tot_sub, "词库总数");
    lv_obj_set_style_text_color(tot_sub, lv_color_hex(0x90a4ae), 0);
    lv_obj_align(tot_sub, LV_ALIGN_TOP_RIGHT, -15, 45);

    /* 快捷大按钮 1: 进入 Anki 复习 */
    lv_obj_t *btn_study = lv_button_create(parent);
    lv_obj_set_size(btn_study, 360, 68);
    lv_obj_align(btn_study, LV_ALIGN_TOP_MID, 0, 205);
    lv_obj_set_style_bg_color(btn_study, lv_color_hex(0x0288d1), 0);
    lv_obj_set_style_radius(btn_study, 14, 0);
    lv_obj_add_event_cb(btn_study, on_dash_start_study_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_study_lbl = lv_label_create(btn_study);
    lv_label_set_text(btn_study_lbl, "📖 开始 Anki 记忆复习 ➔");
    lv_obj_set_style_text_color(btn_study_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_center(btn_study_lbl);

    /* 快捷大按钮 2: AI 助教实时问答 */
    lv_obj_t *btn_ai = lv_button_create(parent);
    lv_obj_set_size(btn_ai, 360, 68);
    lv_obj_align(btn_ai, LV_ALIGN_TOP_MID, 0, 288);
    lv_obj_set_style_bg_color(btn_ai, lv_color_hex(0x5e35b1), 0);
    lv_obj_set_style_radius(btn_ai, 14, 0);
    lv_obj_add_event_cb(btn_ai, on_dash_start_ai_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_ai_lbl = lv_label_create(btn_ai);
    lv_label_set_text(btn_ai_lbl, "🤖 唤醒 VocaVibe AI 助教 ➔");
    lv_obj_set_style_text_color(btn_ai_lbl, lv_color_hex(0xffffff), 0);
    lv_obj_center(btn_ai_lbl);

    /* 底部滑动指引 */
    lv_obj_t *guide_lbl = lv_label_create(parent);
    lv_label_set_text(guide_lbl, "👈 左右滑动屏幕切换功能页面 👉");
    lv_obj_set_style_text_color(guide_lbl, lv_color_hex(0x546e7a), 0);
    lv_obj_align(guide_lbl, LV_ALIGN_BOTTOM_MID, 0, -18);
}

static void create_page_1_anki_study(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0c1017), 0);

    /* 顶部指示栏 */
    s_p1_progress_lbl = lv_label_create(parent);
    lv_label_set_text(s_p1_progress_lbl, "Anki 复习卡片 (1 / 20)");
    lv_obj_set_style_text_color(s_p1_progress_lbl, lv_color_hex(0x80deea), 0);
    lv_obj_align(s_p1_progress_lbl, LV_ALIGN_TOP_LEFT, 20, 14);

    /* 朗读发音小按钮 */
    lv_obj_t *btn_speak = lv_button_create(parent);
    lv_obj_set_size(btn_speak, 75, 30);
    lv_obj_align(btn_speak, LV_ALIGN_TOP_RIGHT, -20, 10);
    lv_obj_set_style_bg_color(btn_speak, lv_color_hex(0x263238), 0);
    lv_obj_set_style_radius(btn_speak, 6, 0);
    lv_obj_add_event_cb(btn_speak, on_card_speak_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *spk_lbl = lv_label_create(btn_speak);
    lv_label_set_text(spk_lbl, "🔊 朗读");
    lv_obj_center(spk_lbl);

    /* 主卡片区域 */
    s_p1_card_box = lv_obj_create(parent);
    lv_obj_set_size(s_p1_card_box, 360, 260);
    lv_obj_align(s_p1_card_box, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_color(s_p1_card_box, lv_color_hex(0x161d2b), 0);
    lv_obj_set_style_border_color(s_p1_card_box, lv_color_hex(0x2e3c54), 0);
    lv_obj_set_style_radius(s_p1_card_box, 14, 0);
    lv_obj_set_scrollbar_mode(s_p1_card_box, LV_SCROLLBAR_MODE_AUTO);

    /* 正面：单词与音标 */
    s_p1_word_lbl = lv_label_create(s_p1_card_box);
    lv_label_set_text(s_p1_word_lbl, "openvela");
    lv_obj_set_style_text_color(s_p1_word_lbl, lv_color_hex(0x00e5ff), 0);
    lv_obj_align(s_p1_word_lbl, LV_ALIGN_TOP_LEFT, 10, 8);

    s_p1_phonetic_lbl = lv_label_create(s_p1_card_box);
    lv_label_set_text(s_p1_phonetic_lbl, "/ˈoʊpən ˈvɛlə/");
    lv_obj_set_style_text_color(s_p1_phonetic_lbl, lv_color_hex(0xffb74d), 0);
    lv_obj_align(s_p1_phonetic_lbl, LV_ALIGN_TOP_LEFT, 10, 36);

    /* 翻转显示背面按键 */
    s_p1_btn_flip = lv_button_create(s_p1_card_box);
    lv_obj_set_size(s_p1_btn_flip, 120, 32);
    lv_obj_align(s_p1_btn_flip, LV_ALIGN_TOP_RIGHT, -5, 12);
    lv_obj_set_style_bg_color(s_p1_btn_flip, lv_color_hex(0x37474f), 0);
    lv_obj_set_style_radius(s_p1_btn_flip, 6, 0);
    lv_obj_add_event_cb(s_p1_btn_flip, on_card_flip_clicked, LV_EVENT_CLICKED, NULL);

    s_p1_flip_lbl = lv_label_create(s_p1_btn_flip);
    lv_label_set_text(s_p1_flip_lbl, "显示答案");
    lv_obj_center(s_p1_flip_lbl);

    /* 背面释义与例句容器 */
    s_p1_meaning_box = lv_obj_create(s_p1_card_box);
    lv_obj_set_size(s_p1_meaning_box, 330, 165);
    lv_obj_align(s_p1_meaning_box, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_p1_meaning_box, lv_color_hex(0x101622), 0);
    lv_obj_set_style_border_color(s_p1_meaning_box, lv_color_hex(0x222e44), 0);
    lv_obj_set_style_radius(s_p1_meaning_box, 8, 0);
    lv_obj_add_flag(s_p1_meaning_box, LV_OBJ_FLAG_HIDDEN); /* 默认隐藏正面 */

    s_p1_meaning_lbl = lv_label_create(s_p1_meaning_box);
    lv_obj_set_size(s_p1_meaning_lbl, 305, 55);
    lv_label_set_long_mode(s_p1_meaning_lbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_p1_meaning_lbl, "面向端侧 AI 与嵌入式微控制器的开源实时操作系统");
    lv_obj_set_style_text_color(s_p1_meaning_lbl, lv_color_hex(0x81c784), 0);
    lv_obj_align(s_p1_meaning_lbl, LV_ALIGN_TOP_LEFT, 5, 5);

    s_p1_example_lbl = lv_label_create(s_p1_meaning_box);
    lv_obj_set_size(s_p1_example_lbl, 305, 80);
    lv_label_set_long_mode(s_p1_example_lbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_p1_example_lbl, "例: OpenVela OS powers intelligent edge hardware with microsecond latency.");
    lv_obj_set_style_text_color(s_p1_example_lbl, lv_color_hex(0xb0bec5), 0);
    lv_obj_align(s_p1_example_lbl, LV_ALIGN_TOP_LEFT, 5, 65);

    /* 底部标准 Anki 4 档评分按键容器 */
    s_p1_ratings_cont = lv_obj_create(parent);
    lv_obj_set_size(s_p1_ratings_cont, 370, 110);
    lv_obj_align(s_p1_ratings_cont, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_opa(s_p1_ratings_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_p1_ratings_cont, 0, 0);
    lv_obj_set_style_pad_all(s_p1_ratings_cont, 0, 0);

    /* 4 档按键按 2x2 网格排列 */
    /* 1. Again (重来 1m) */
    lv_obj_t *b1 = lv_button_create(s_p1_ratings_cont);
    lv_obj_set_size(b1, 175, 48);
    lv_obj_align(b1, LV_ALIGN_TOP_LEFT, 5, 0);
    lv_obj_set_style_bg_color(b1, lv_color_hex(0xd32f2f), 0);
    lv_obj_set_style_radius(b1, 10, 0);
    lv_obj_add_event_cb(b1, on_rating_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)ANKI_RATING_AGAIN);
    lv_obj_t *t1 = lv_label_create(b1);
    lv_label_set_text(t1, "1. 重来 (1m)");
    lv_obj_center(t1);

    /* 2. Hard (困难 1d) */
    lv_obj_t *b2 = lv_button_create(s_p1_ratings_cont);
    lv_obj_set_size(b2, 175, 48);
    lv_obj_align(b2, LV_ALIGN_TOP_RIGHT, -5, 0);
    lv_obj_set_style_bg_color(b2, lv_color_hex(0xf57c00), 0);
    lv_obj_set_style_radius(b2, 10, 0);
    lv_obj_add_event_cb(b2, on_rating_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)ANKI_RATING_HARD);
    lv_obj_t *t2 = lv_label_create(b2);
    lv_label_set_text(t2, "2. 困难 (1d)");
    lv_obj_center(t2);

    /* 3. Good (良好 3d) */
    lv_obj_t *b3 = lv_button_create(s_p1_ratings_cont);
    lv_obj_set_size(b3, 175, 48);
    lv_obj_align(b3, LV_ALIGN_BOTTOM_LEFT, 5, 0);
    lv_obj_set_style_bg_color(b3, lv_color_hex(0x2e7d32), 0);
    lv_obj_set_style_radius(b3, 10, 0);
    lv_obj_add_event_cb(b3, on_rating_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)ANKI_RATING_GOOD);
    lv_obj_t *t3 = lv_label_create(b3);
    lv_label_set_text(t3, "3. 良好 (3d)");
    lv_obj_center(t3);

    /* 4. Easy (简单 7d) */
    lv_obj_t *b4 = lv_button_create(s_p1_ratings_cont);
    lv_obj_set_size(b4, 175, 48);
    lv_obj_align(b4, LV_ALIGN_BOTTOM_RIGHT, -5, 0);
    lv_obj_set_style_bg_color(b4, lv_color_hex(0x1565c0), 0);
    lv_obj_set_style_radius(b4, 10, 0);
    lv_obj_add_event_cb(b4, on_rating_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)ANKI_RATING_EASY);
    lv_obj_t *t4 = lv_label_create(b4);
    lv_label_set_text(t4, "4. 简单 (7d)");
    lv_obj_center(t4);
}

static void create_page_2_ai_assistant(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0c1017), 0);

    /* 顶部标题与唤醒词提示 */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "VocaVibe AI 助教");
    lv_obj_set_style_text_color(title, lv_color_hex(0xab47bc), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    s_ai_status_lbl = lv_label_create(parent);
    lv_label_set_text(s_ai_status_lbl, "唤醒词: 你好，openvela / Hello，openvela");
    lv_obj_set_style_text_color(s_ai_status_lbl, lv_color_hex(0x80cbc4), 0);
    lv_obj_align(s_ai_status_lbl, LV_ALIGN_TOP_MID, 0, 32);

    /* 小智声波动效容器 (Sonic Waveform) */
    lv_obj_t *wave_box = lv_obj_create(parent);
    lv_obj_set_size(wave_box, 360, 65);
    lv_obj_align(wave_box, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_color(wave_box, lv_color_hex(0x131a26), 0);
    lv_obj_set_style_border_color(wave_box, lv_color_hex(0x27354d), 0);
    lv_obj_set_style_radius(wave_box, 12, 0);

    /* 7 根声波律动柱状条 */
    int bar_w = 12;
    int spacing = 40;
    for (int i = 0; i < NUM_WAVE_BARS; i++) {
        s_wave_bars[i] = lv_obj_create(wave_box);
        lv_obj_set_size(s_wave_bars[i], bar_w, 14);
        lv_obj_align(s_wave_bars[i], LV_ALIGN_CENTER, (i - 3) * spacing, 0);
        lv_obj_set_style_radius(s_wave_bars[i], 6, 0);
        lv_obj_set_style_bg_color(s_wave_bars[i], lv_color_hex(0x26c6da), 0);
        lv_obj_set_style_border_width(s_wave_bars[i], 0, 0);
    }

    /* 对话气泡容器 */
    lv_obj_t *chat_cont = lv_obj_create(parent);
    lv_obj_set_size(chat_cont, 360, 230);
    lv_obj_align(chat_cont, LV_ALIGN_TOP_MID, 0, 130);
    lv_obj_set_style_bg_color(chat_cont, lv_color_hex(0x161d2b), 0);
    lv_obj_set_style_border_color(chat_cont, lv_color_hex(0x2d3a54), 0);
    lv_obj_set_style_radius(chat_cont, 12, 0);

    s_chat_user_lbl = lv_label_create(chat_cont);
    lv_obj_set_size(s_chat_user_lbl, 330, 45);
    lv_label_set_long_mode(s_chat_user_lbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_chat_user_lbl, "User: 你好，openvela！");
    lv_obj_set_style_text_color(s_chat_user_lbl, lv_color_hex(0xffca28), 0);
    lv_obj_align(s_chat_user_lbl, LV_ALIGN_TOP_LEFT, 5, 5);

    s_chat_ai_lbl = lv_label_create(chat_cont);
    lv_obj_set_size(s_chat_ai_lbl, 330, 160);
    lv_label_set_long_mode(s_chat_ai_lbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_chat_ai_lbl, "AI: 你好！我是 VocaVibe 智能助教，随时为你解答词汇与例句用法。");
    lv_obj_set_style_text_color(s_chat_ai_lbl, lv_color_hex(0xe0e0e0), 0);
    lv_obj_align(s_chat_ai_lbl, LV_ALIGN_TOP_LEFT, 5, 55);

    /* 底部 3 个快捷问答胶囊按钮 */
    lv_obj_t *b_ex = lv_button_create(parent);
    lv_obj_set_size(b_ex, 110, 40);
    lv_obj_align(b_ex, LV_ALIGN_BOTTOM_LEFT, 15, -20);
    lv_obj_set_style_bg_color(b_ex, lv_color_hex(0x3949ab), 0);
    lv_obj_set_style_radius(b_ex, 8, 0);
    lv_obj_add_event_cb(b_ex, on_ai_prompt_clicked, LV_EVENT_CLICKED, (void *)"example");
    lv_obj_t *lbl_ex = lv_label_create(b_ex);
    lv_label_set_text(lbl_ex, "词汇造句");
    lv_obj_center(lbl_ex);

    lv_obj_t *b_syn = lv_button_create(parent);
    lv_obj_set_size(b_syn, 110, 40);
    lv_obj_align(b_syn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(b_syn, lv_color_hex(0x5e35b1), 0);
    lv_obj_set_style_radius(b_syn, 8, 0);
    lv_obj_add_event_cb(b_syn, on_ai_prompt_clicked, LV_EVENT_CLICKED, (void *)"synonym");
    lv_obj_t *lbl_syn = lv_label_create(b_syn);
    lv_label_set_text(lbl_syn, "近义辨析");
    lv_obj_center(lbl_syn);

    lv_obj_t *b_root = lv_button_create(parent);
    lv_obj_set_size(b_root, 110, 40);
    lv_obj_align(b_root, LV_ALIGN_BOTTOM_RIGHT, -15, -20);
    lv_obj_set_style_bg_color(b_root, lv_color_hex(0x00897b), 0);
    lv_obj_set_style_radius(b_root, 8, 0);
    lv_obj_add_event_cb(b_root, on_ai_prompt_clicked, LV_EVENT_CLICKED, (void *)"root");
    lv_obj_t *lbl_root = lv_label_create(b_root);
    lv_label_set_text(lbl_root, "词根助记");
    lv_obj_center(lbl_root);
}

static void create_page_3_settings(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x0c1017), 0);

    /* 顶部标题 */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "同步与蓝牙代理设置");
    lv_obj_set_style_text_color(title, lv_color_hex(0x26a69a), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    /* 1. AnkiConnect 桥接卡片 */
    lv_obj_t *sync_card = lv_obj_create(parent);
    lv_obj_set_size(sync_card, 360, 110);
    lv_obj_align(sync_card, LV_ALIGN_TOP_MID, 0, 45);
    lv_obj_set_style_bg_color(sync_card, lv_color_hex(0x161d2b), 0);
    lv_obj_set_style_border_color(sync_card, lv_color_hex(0x2d3a54), 0);
    lv_obj_set_style_radius(sync_card, 12, 0);

    s_sync_status_lbl = lv_label_create(sync_card);
    lv_label_set_text(s_sync_status_lbl, "AnkiConnect: 待同步 (127.0.0.1:8765)");
    lv_obj_set_style_text_color(s_sync_status_lbl, lv_color_hex(0xffb74d), 0);
    lv_obj_align(s_sync_status_lbl, LV_ALIGN_TOP_LEFT, 10, 8);

    lv_obj_t *btn_pull = lv_button_create(sync_card);
    lv_obj_set_size(btn_pull, 150, 42);
    lv_obj_align(btn_pull, LV_ALIGN_BOTTOM_LEFT, 10, -8);
    lv_obj_set_style_bg_color(btn_pull, lv_color_hex(0x0288d1), 0);
    lv_obj_set_style_radius(btn_pull, 8, 0);
    lv_obj_add_event_cb(btn_pull, on_sync_pull_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_pull = lv_label_create(btn_pull);
    lv_label_set_text(lbl_pull, "🔄 从 Anki 拉取");
    lv_obj_center(lbl_pull);

    lv_obj_t *btn_push = lv_button_create(sync_card);
    lv_obj_set_size(btn_push, 150, 42);
    lv_obj_align(btn_push, LV_ALIGN_BOTTOM_RIGHT, -10, -8);
    lv_obj_set_style_bg_color(btn_push, lv_color_hex(0x00897b), 0);
    lv_obj_set_style_radius(btn_push, 8, 0);
    lv_obj_add_event_cb(btn_push, on_sync_pull_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_push = lv_label_create(btn_push);
    lv_label_set_text(lbl_push, "⬆️ 备份到 Anki");
    lv_obj_center(lbl_push);

    /* 2. 蓝牙耳机代理遥控卡片 */
    lv_obj_t *bt_card = lv_obj_create(parent);
    lv_obj_set_size(bt_card, 360, 240);
    lv_obj_align(bt_card, LV_ALIGN_TOP_MID, 0, 165);
    lv_obj_set_style_bg_color(bt_card, lv_color_hex(0x161d2b), 0);
    lv_obj_set_style_border_color(bt_card, lv_color_hex(0x2d3a54), 0);
    lv_obj_set_style_radius(bt_card, 12, 0);

    s_bt_status_lbl = lv_label_create(bt_card);
    lv_label_set_text(s_bt_status_lbl, "蓝牙耳机: 未连接 (伴侣代理)");
    lv_obj_set_style_text_color(s_bt_status_lbl, lv_color_hex(0x81c784), 0);
    lv_obj_align(s_bt_status_lbl, LV_ALIGN_TOP_LEFT, 10, 8);

    lv_obj_t *btn_scan = lv_button_create(bt_card);
    lv_obj_set_size(btn_scan, 110, 32);
    lv_obj_align(btn_scan, LV_ALIGN_TOP_RIGHT, -10, 4);
    lv_obj_set_style_bg_color(btn_scan, lv_color_hex(0x455a64), 0);
    lv_obj_set_style_radius(btn_scan, 6, 0);
    lv_obj_add_event_cb(btn_scan, on_bt_scan_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_scan = lv_label_create(btn_scan);
    lv_label_set_text(lbl_scan, "🔍 扫描耳机");
    lv_obj_center(lbl_scan);

    /* 蓝牙耳机滚动列表 */
    s_bt_list = lv_list_create(bt_card);
    lv_obj_set_size(s_bt_list, 335, 160);
    lv_obj_align(s_bt_list, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_color(s_bt_list, lv_color_hex(0x101622), 0);
    lv_obj_set_style_border_color(s_bt_list, lv_color_hex(0x222e44), 0);
    lv_obj_set_style_radius(s_bt_list, 8, 0);

    /* 默认填充 2 个提示占位 */
    lv_obj_t *item1 = lv_list_add_button(s_bt_list, NULL, "AirPods Pro (-52dBm) [点击连接]");
    lv_obj_add_event_cb(item1, on_bt_dev_item_clicked, LV_EVENT_CLICKED, (void *)"AA:BB:CC:11:22:33");
    lv_obj_t *item2 = lv_list_add_button(s_bt_list, NULL, "HUAWEI FreeBuds (-65dBm) [点击连接]");
    lv_obj_add_event_cb(item2, on_bt_dev_item_clicked, LV_EVENT_CLICKED, (void *)"DD:EE:FF:44:55:66");

    /* 底部系统标识 */
    lv_obj_t *sys_lbl = lv_label_create(parent);
    lv_label_set_text(sys_lbl, "OpenVela OS | SF32LB52 AMOLED | Team 243");
    lv_obj_set_style_text_color(sys_lbl, lv_color_hex(0x455a64), 0);
    lv_obj_align(sys_lbl, LV_ALIGN_BOTTOM_MID, 0, -10);
}

/* -------------------------------------------------------------------------
 * UI 刷新线程
 * ------------------------------------------------------------------------- */
static void *ui_thread_func(void *arg)
{
    (void)arg;
    while (s_ui_running) {
        /* 1. 切换页面请求 */
        if (s_switch_to_page >= 0 && s_switch_to_page < 4 && s_tv) {
            lv_tileview_set_tile_by_index(s_tv, (uint32_t)s_switch_to_page, 0, LV_ANIM_ON);
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

        /* 3. 卡片数据与正反面刷新 */
        if (s_card_dirty) {
            s_card_dirty = false;
            if (s_p1_word_lbl) lv_label_set_text(s_p1_word_lbl, s_cur_word);
            if (s_p1_phonetic_lbl) lv_label_set_text(s_p1_phonetic_lbl, s_cur_phonetic);
            if (s_p1_meaning_lbl) lv_label_set_text(s_p1_meaning_lbl, s_cur_meaning);
            if (s_p1_example_lbl) lv_label_set_text(s_p1_example_lbl, s_cur_example);

            char prog_buf[48];
            snprintf(prog_buf, sizeof(prog_buf), "Anki 复习卡片 (%d / %d)", s_cur_idx, s_total_cnt);
            if (s_p1_progress_lbl) lv_label_set_text(s_p1_progress_lbl, prog_buf);

            s_card_back_visible = s_card_req_back;
            if (s_p1_meaning_box) {
                if (s_card_back_visible) {
                    lv_obj_clear_flag(s_p1_meaning_box, LV_OBJ_FLAG_HIDDEN);
                    if (s_p1_flip_lbl) lv_label_set_text(s_p1_flip_lbl, "隐藏答案");
                } else {
                    lv_obj_add_flag(s_p1_meaning_box, LV_OBJ_FLAG_HIDDEN);
                    if (s_p1_flip_lbl) lv_label_set_text(s_p1_flip_lbl, "显示答案");
                }
            }
        }

        /* 4. AI 助教对话气泡刷新 */
        if (s_chat_dirty) {
            s_chat_dirty = false;
            if (s_chat_user_lbl) lv_label_set_text(s_chat_user_lbl, s_chat_user_buf);
            if (s_chat_ai_lbl) lv_label_set_text(s_chat_ai_lbl, s_chat_ai_buf);
        }

        /* 5. 小智声波律动动效 (Sonic Waveform) */
        s_anim_phase += 0.15f;
        if (s_anim_phase > 6.28318f) s_anim_phase -= 6.28318f;

        float amp = 10.0f;
        if (s_ai_state == AI_STATE_LISTENING) {
            amp = 30.0f;
            if (s_ai_status_lbl) lv_label_set_text(s_ai_status_lbl, "🎙️ 正在倾听您的发音与提问...");
        } else if (s_ai_state == AI_STATE_THINKING) {
            amp = 20.0f;
            if (s_ai_status_lbl) lv_label_set_text(s_ai_status_lbl, "🧠 Xiaomi MiMo 2.5 正在思考中...");
        } else if (s_ai_state == AI_STATE_SPEAKING) {
            amp = 35.0f;
            if (s_ai_status_lbl) lv_label_set_text(s_ai_status_lbl, "🔊 正在通过蓝牙耳机语音解答...");
        } else {
            amp = 8.0f;
            if (s_ai_status_lbl) lv_label_set_text(s_ai_status_lbl, "唤醒词: 你好，openvela / Hello，openvela");
        }

        for (int i = 0; i < NUM_WAVE_BARS; i++) {
            if (s_wave_bars[i]) {
                float h = 14.0f + fabsf(sinf(s_anim_phase + (float)i * 0.9f)) * amp;
                lv_obj_set_height(s_wave_bars[i], (int32_t)h);
            }
        }

        /* 6. 同步与蓝牙状态 */
        if (s_sync_dirty) {
            s_sync_dirty = false;
            if (s_sync_status_lbl) lv_label_set_text(s_sync_status_lbl, s_sync_status_buf);
        }
        if (s_bt_dirty) {
            s_bt_dirty = false;
            if (s_bt_status_lbl) lv_label_set_text(s_bt_status_lbl, s_bt_status_buf);
        }

        /* 7. 蓝牙设备列表刷新 */
        if (s_bt_list_dirty && s_bt_list) {
            s_bt_list_dirty = false;
            lv_obj_clean(s_bt_list);
            for (int i = 0; i < s_bt_entry_count; i++) {
                char item_text[64];
                snprintf(item_text, sizeof(item_text), "%s (%ddBm)", s_bt_entries[i].name, s_bt_entries[i].rssi);
                lv_obj_t *it = lv_list_add_button(s_bt_list, NULL, item_text);
                lv_obj_add_event_cb(it, on_bt_dev_item_clicked, LV_EVENT_CLICKED, (void *)s_bt_entries[i].mac);
            }
        }

        uint32_t idle = lv_timer_handler();
        idle = (idle > 0 && idle <= 40) ? idle : 20;
        usleep(idle * 1000);
    }
    return NULL;
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
        for (int retry = 0; retry < 50; retry++) {
            if (access("/dev/lcd0", F_OK) == 0 && access("/dev/input0", F_OK) == 0) {
                break;
            }
            usleep(100000);
        }

        lv_init();

        lv_nuttx_dsc_t info;
        lv_nuttx_result_t result;
        lv_nuttx_dsc_init(&info);

        info.fb_path = "/dev/lcd0";
        if (access("/dev/input0", F_OK) == 0) {
            info.input_path = "/dev/input0";
        }

        lv_nuttx_init(&info, &result);
        if (result.disp == NULL) {
            printf("[VocaVibe UI] Warning: lv_nuttx_init disp is NULL\n");
            return -1;
        }
    }

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0c1017), 0);

    /* 创建 4 页面 Tileview */
    s_tv = lv_tileview_create(scr);
    lv_obj_set_size(s_tv, SCREEN_W, SCREEN_H);
    lv_obj_center(s_tv);
    lv_obj_set_style_bg_color(s_tv, lv_color_hex(0x0c1017), 0);

    /* Page 0: 仪表盘 (可向右滑入 Page 1) */
    s_tiles[0] = lv_tileview_add_tile(s_tv, 0, 0, LV_DIR_RIGHT);
    create_page_0_dashboard(s_tiles[0]);

    /* Page 1: Anki 卡片复习 (可向左返回 Page 0，向右进入 Page 2) */
    s_tiles[1] = lv_tileview_add_tile(s_tv, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    create_page_1_anki_study(s_tiles[1]);

    /* Page 2: VocaVibe AI 助教 (可向左返回 Page 1，向右进入 Page 3) */
    s_tiles[2] = lv_tileview_add_tile(s_tv, 2, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    create_page_2_ai_assistant(s_tiles[2]);

    /* Page 3: 同步与设置 (可向左返回 Page 2) */
    s_tiles[3] = lv_tileview_add_tile(s_tv, 3, 0, LV_DIR_LEFT);
    create_page_3_settings(s_tiles[3]);

    /* 启动 UI 定时器与刷新渲染线程 */
    s_ui_running = true;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 24576);
    int ret = pthread_create(&s_ui_tid, &attr, ui_thread_func, NULL);
    pthread_attr_destroy(&attr);
    if (ret != 0) {
        printf("[VocaVibe UI] pthread_create failed: %d\n", ret);
        return ret;
    }

    printf("[VocaVibe UI] 4-Page Tileview UI started successfully.\n");
    return 0;
}

void vocavibe_ui_switch_page(int page_idx)
{
    s_switch_to_page = page_idx;
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
        snprintf(s_chat_user_buf, sizeof(s_chat_user_buf), "User: %s", user_query);
    }
    if (ai_reply) {
        snprintf(s_chat_ai_buf, sizeof(s_chat_ai_buf), "AI: %s", ai_reply);
    }
    s_chat_dirty = true;
}

void vocavibe_ui_add_bt_device(const char *name, const char *mac, int rssi)
{
    if (s_bt_entry_count < MAX_BT_DEVICES) {
        strncpy(s_bt_entries[s_bt_entry_count].name, name ? name : "Unknown", sizeof(s_bt_entries[0].name) - 1);
        strncpy(s_bt_entries[s_bt_entry_count].mac, mac ? mac : "--:--", sizeof(s_bt_entries[0].mac) - 1);
        s_bt_entries[s_bt_entry_count].rssi = rssi;
        s_bt_entry_count++;
        s_bt_list_dirty = true;
    }
}

void vocavibe_ui_clear_bt_devices(void)
{
    s_bt_entry_count = 0;
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
