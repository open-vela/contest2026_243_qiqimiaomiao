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

/* AI 呼吸灵动光球色彩 (古雅暖琥珀金 / 主动自驱柔和翠绿) */
#define COLOR_ORB_CORE         0xC87D38  /* 内核实体暖金球 */
#define COLOR_ORB_GLOW         0xE8A858  /* 外层半透明呼吸光晕 */
#define COLOR_ORB_PROACTIVE_CORE 0x2E7D32 /* 主动自驱内核翠绿 */
#define COLOR_ORB_PROACTIVE_GLOW 0x66BB6A /* 主动自驱外层翠绿光晕 */

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

/* --- Page 0: AnkiDroid 牌组列表控件 --- */
static lv_obj_t *s_p0_total_due_lbl = NULL;
static lv_obj_t *s_p0_deck_list = NULL;
static volatile bool s_p0_decks_dirty = true;

/* --- Page 1: Anki 卡片控件 --- */
static lv_obj_t *s_p1_progress_lbl = NULL;
static lv_obj_t *s_p1_word_lbl = NULL;
static lv_obj_t *s_p1_phonetic_lbl = NULL;
static lv_obj_t *s_p1_meaning_lbl = NULL;
static lv_obj_t *s_p1_example_lbl = NULL;
static lv_obj_t *s_p1_flip_btn = NULL;
static lv_obj_t *s_p1_ratings_cont = NULL;
static lv_obj_t *s_p1_rating_lbls[4] = {NULL, NULL, NULL, NULL};
static char s_cur_next_times[4][16] = {"<1m", "10m", "1d", "4d"};
static bool s_card_back_visible = false;

/* --- Page 2: AI 助教控件 --- */
static lv_obj_t *s_ai_orb_glow = NULL;
static lv_obj_t *s_ai_orb_core = NULL;
static lv_obj_t *s_ai_status_lbl = NULL;
static lv_obj_t *s_chat_cont = NULL;
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
static vocavibe_audio_mode_t s_audio_mode = VOCAVIBE_AUDIO_MODE_HEADSET;

/* 主动自驱任务弹窗组件与缓存 */
static lv_obj_t *s_proactive_modal = NULL;
static volatile bool s_proactive_modal_req = false;
static int s_proactive_due_count = 0;
static char s_proactive_msg[128] = {0};

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
static char s_chat_ai_buf[1024] = "AI: 你好！我是随声记 AI 助教，随时为你答疑解惑。";
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
static int s_net_conn_timeout_ms = 0;
static int s_bt_scan_timeout_ms = 0;

int vocavibe_ui_get_current_page(void)
{
    return s_current_page;
}

/* -------------------------------------------------------------------------
 * 事件回调函数
 * ------------------------------------------------------------------------- */
static void create_proactive_modal(void);
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

static void on_deck_item_clicked(lv_event_t *e)
{
    uint32_t deck_id = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    printf("[VocaVibe UI] 触控点选牌组 ID=%u，切入复习\n", (unsigned int)deck_id);
    if (s_cbs.on_deck_select) {
        s_cbs.on_deck_select(deck_id);
    }
    vocavibe_ui_switch_page(1);
}

static void on_card_back_to_decks_clicked(lv_event_t *e)
{
    (void)e;
    printf("[VocaVibe UI] 点击返回按钮: 退出卡片复习，返回牌组列表\n");
    vocavibe_ui_switch_page(0);
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

static void __attribute__((unused)) on_ai_prompt_clicked(lv_event_t *e)
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

static void on_net_connect_clicked(lv_event_t *e)
{
    (void)e;
    printf("[VocaVibe UI] 触控点击: 请求连接网络代理\n");
    s_net_conn_timeout_ms = 3000;
    if (s_lbl_net_conn) lv_label_set_text(s_lbl_net_conn, "连接中...");
    if (s_btn_net_conn) lv_obj_set_style_bg_color(s_btn_net_conn, lv_color_hex(COLOR_SEAL_HARD), 0);
    if (s_net_status_lbl) {
        lv_label_set_text(s_net_status_lbl, "网络代理: 正在握手连接...");
        lv_obj_set_style_text_color(s_net_status_lbl, lv_color_hex(COLOR_SEAL_HARD), 0);
    }
    if (s_cbs.on_net_connect) {
        s_cbs.on_net_connect();
    }
}

static void on_sync_pull_clicked(lv_event_t *e)
{
    (void)e;
    printf("[VocaVibe UI] 触控点击: 拉取云端 Anki 卡组\n");
    if (s_cbs.on_sync) {
        s_cbs.on_sync();
    }
}

static void on_bt_scan_clicked(lv_event_t *e)
{
    (void)e;
    printf("[VocaVibe UI] 触控点击: 扫描周围蓝牙设备\n");
    s_bt_scan_timeout_ms = 3000;
    vocavibe_ui_clear_bt_devices();
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
 * Page 0: AnkiDroid 牌组列表动态渲染 (主线程安全执行)
 * ------------------------------------------------------------------------- */
static void render_decks_list_internal(void)
{
    if (!s_p0_deck_list) return;

    /* 1. 更新顶部总待复习卡片摘要 */
    int total_due = vocavibe_deck_get_total_due_all_decks();
    int deck_cnt = vocavibe_deck_get_deck_count();
    if (s_p0_total_due_lbl) {
        char buf[64];
        if (deck_cnt == 0) {
            snprintf(buf, sizeof(buf), "未同步词库 (请点右侧同步)");
        } else {
            snprintf(buf, sizeof(buf), "%d 个牌组，共 %d 张待复习", deck_cnt, total_due);
        }
        lv_label_set_text(s_p0_total_due_lbl, buf);
    }

    /* 2. 清空现有子项 */
    lv_obj_clean(s_p0_deck_list);

    /* 3. 如果尚未同步任何牌组，展示优雅空状态纸质卡片 */
    if (deck_cnt == 0) {
        lv_obj_t *empty_card = lv_obj_create(s_p0_deck_list);
        lv_obj_set_size(empty_card, 330, 140);
        lv_obj_set_style_bg_color(empty_card, lv_color_hex(COLOR_PAPER_CARD), 0);
        lv_obj_set_style_border_color(empty_card, lv_color_hex(COLOR_PAPER_BORDER), 0);
        lv_obj_set_style_border_width(empty_card, 1, 0);
        lv_obj_set_style_radius(empty_card, 10, 0);
        lv_obj_set_style_pad_all(empty_card, 12, 0);
        lv_obj_clear_flag(empty_card, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl_title = lv_label_create(empty_card);
        apply_cjk_font(lbl_title);
        lv_label_set_text(lbl_title, "暂无本地牌组");
        lv_obj_set_style_text_color(lbl_title, lv_color_hex(COLOR_INK_MAIN), 0);
        lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 8);

        lv_obj_t *lbl_desc = lv_label_create(empty_card);
        apply_cjk_font(lbl_desc);
        lv_obj_set_width(lbl_desc, 300);
        lv_label_set_long_mode(lbl_desc, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(lbl_desc, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(lbl_desc, "电脑端打开 Anki 与伴侣端后\n点击右上角「同步」拉取牌组");
        lv_obj_set_style_text_color(lbl_desc, lv_color_hex(COLOR_INK_MUTED), 0);
        lv_obj_align(lbl_desc, LV_ALIGN_CENTER, 0, 16);
        return;
    }

    /* 4. 遍历已同步的牌组，创建 1:1 AnkiDroid 风格卡片条目 */
    for (int i = 0; i < deck_cnt; i++) {
        const anki_deck_info_t *deck = vocavibe_deck_get_deck_at(i);
        if (!deck) continue;

        lv_obj_t *item_btn = lv_button_create(s_p0_deck_list);
        lv_obj_set_size(item_btn, 330, 52);
        lv_obj_set_style_bg_color(item_btn, lv_color_hex(COLOR_PAPER_CARD), 0);
        lv_obj_set_style_border_color(item_btn, lv_color_hex(COLOR_PAPER_BORDER), 0);
        lv_obj_set_style_border_width(item_btn, 1, 0);
        lv_obj_set_style_radius(item_btn, 10, 0);
        lv_obj_set_style_pad_all(item_btn, 6, 0);
        lv_obj_clear_flag(item_btn, LV_OBJ_FLAG_SCROLLABLE);

        /* 绑定触控点选：传入牌组 ID */
        lv_obj_add_event_cb(item_btn, on_deck_item_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)deck->id);
        lv_obj_add_event_cb(item_btn, on_deck_item_clicked, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)deck->id);

        /* 左侧：牌组名称 (支持长文本优雅截断) */
        lv_obj_t *name_lbl = lv_label_create(item_btn);
        apply_cjk_font(name_lbl);
        lv_obj_set_width(name_lbl, 175);
        lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
        lv_label_set_text(name_lbl, deck->name);
        lv_obj_set_style_text_color(name_lbl, lv_color_hex(COLOR_INK_MAIN), 0);
        lv_obj_align(name_lbl, LV_ALIGN_LEFT_MID, 6, 0);

        /* 右侧：经典 AnkiDroid 蓝/红/绿三色徽章计数容器 */
        lv_obj_t *badge_cont = lv_obj_create(item_btn);
        lv_obj_set_size(badge_cont, 130, 36);
        lv_obj_align(badge_cont, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_opa(badge_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(badge_cont, 0, 0);
        lv_obj_set_style_pad_all(badge_cont, 0, 0);
        lv_obj_clear_flag(badge_cont, LV_OBJ_FLAG_SCROLLABLE);

        char cnt_buf[16];

        /* 蓝色：新卡数 (New) */
        lv_obj_t *lbl_new = lv_label_create(badge_cont);
        apply_cjk_font(lbl_new);
        snprintf(cnt_buf, sizeof(cnt_buf), "%d", deck->new_count);
        lv_label_set_text(lbl_new, cnt_buf);
        lv_obj_set_style_text_color(lbl_new, lv_color_hex(COLOR_SEAL_EASY), 0);
        lv_obj_align(lbl_new, LV_ALIGN_LEFT_MID, 4, 0);

        /* 红色：学习中 (Learn) */
        lv_obj_t *lbl_learn = lv_label_create(badge_cont);
        apply_cjk_font(lbl_learn);
        snprintf(cnt_buf, sizeof(cnt_buf), "%d", deck->learn_count);
        lv_label_set_text(lbl_learn, cnt_buf);
        lv_obj_set_style_text_color(lbl_learn, lv_color_hex(COLOR_SEAL_AGAIN), 0);
        lv_obj_align(lbl_learn, LV_ALIGN_CENTER, 0, 0);

        /* 绿色：待复习 (Due) */
        lv_obj_t *lbl_due = lv_label_create(badge_cont);
        apply_cjk_font(lbl_due);
        snprintf(cnt_buf, sizeof(cnt_buf), "%d", deck->due_count);
        lv_label_set_text(lbl_due, cnt_buf);
        lv_obj_set_style_text_color(lbl_due, lv_color_hex(COLOR_SEAL_GOOD), 0);
        lv_obj_align(lbl_due, LV_ALIGN_RIGHT_MID, -4, 0);
    }
}

void vocavibe_ui_update_decks_list(void)
{
    /* 跨线程通知：置 dirty 标志，由主循环安全调度渲染 */
    s_p0_decks_dirty = true;
}

/* -------------------------------------------------------------------------
 * UI 页面创建（全中文 + 羊皮纸手账质感）
 * ------------------------------------------------------------------------- */
static void create_page_0_dashboard(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(COLOR_PARCHMENT_BG), 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶部 AnkiDroid 风格标题栏 (宽 346, 高 50, y=8) */
    lv_obj_t *top_card = lv_obj_create(parent);
    lv_obj_set_size(top_card, SAFE_CARD_W, 50);
    lv_obj_align(top_card, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_bg_color(top_card, lv_color_hex(COLOR_PAPER_CARD), 0);
    lv_obj_set_style_border_color(top_card, lv_color_hex(COLOR_PAPER_BORDER), 0);
    lv_obj_set_style_border_width(top_card, 1, 0);
    lv_obj_set_style_radius(top_card, 10, 0);
    lv_obj_set_style_pad_all(top_card, 6, 0);
    lv_obj_clear_flag(top_card, LV_OBJ_FLAG_SCROLLABLE);

    /* AnkiDroid 品牌标题 */
    lv_obj_t *title = lv_label_create(top_card);
    apply_cjk_font(title);
    lv_label_set_text(title, "AnkiDroid 牌组");
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_INK_MAIN), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 2);

    /* 总待复习卡片摘要 */
    s_p0_total_due_lbl = lv_label_create(top_card);
    apply_cjk_font(s_p0_total_due_lbl);
    lv_label_set_text(s_p0_total_due_lbl, "0 张卡片待复习");
    lv_obj_set_style_text_color(s_p0_total_due_lbl, lv_color_hex(COLOR_INK_MUTED), 0);
    lv_obj_align(s_p0_total_due_lbl, LV_ALIGN_TOP_LEFT, 8, 24);

    /* 右上角快捷同步按钮 */
    lv_obj_t *btn_sync_top = lv_button_create(top_card);
    lv_obj_set_ext_click_area(btn_sync_top, 10);
    lv_obj_set_size(btn_sync_top, 64, 34);
    lv_obj_align(btn_sync_top, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(btn_sync_top, lv_color_hex(COLOR_SEAL_EASY), 0);
    lv_obj_set_style_radius(btn_sync_top, 6, 0);
    lv_obj_clear_flag(btn_sync_top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_sync_top, on_sync_pull_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_sync_top, on_sync_pull_clicked, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_t *lbl_sync_top = lv_label_create(btn_sync_top);
    apply_cjk_font(lbl_sync_top);
    lv_label_set_text(lbl_sync_top, "同步");
    lv_obj_set_style_text_color(lbl_sync_top, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_sync_top);

    /* 牌组列表容器 (宽 346, 高 324, y=64, 纵向触控滚动) */
    s_p0_deck_list = lv_obj_create(parent);
    lv_obj_set_size(s_p0_deck_list, 346, 324);
    lv_obj_align(s_p0_deck_list, LV_ALIGN_TOP_MID, 0, 64);
    lv_obj_set_style_bg_opa(s_p0_deck_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_p0_deck_list, 0, 0);
    lv_obj_set_style_pad_all(s_p0_deck_list, 0, 0);
    lv_obj_set_style_pad_row(s_p0_deck_list, 6, 0);
    lv_obj_add_flag(s_p0_deck_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_p0_deck_list, LV_DIR_VER);

    /* 初始空状态刷新 */
    vocavibe_ui_update_decks_list();
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

    /* 返回牌组列表小按钮 */
    lv_obj_t *btn_back = lv_button_create(top_bar);
    lv_obj_set_ext_click_area(btn_back, 12);
    lv_obj_set_size(btn_back, 64, 26);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(COLOR_PAPER_CARD), 0);
    lv_obj_set_style_border_color(btn_back, lv_color_hex(COLOR_PAPER_BORDER), 0);
    lv_obj_set_style_border_width(btn_back, 1, 0);
    lv_obj_set_style_radius(btn_back, 6, 0);
    lv_obj_clear_flag(btn_back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_back, on_card_back_to_decks_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_back, on_card_back_to_decks_clicked, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    apply_cjk_font(lbl_back);
    lv_label_set_text(lbl_back, "< 牌组");
    lv_obj_set_style_text_color(lbl_back, lv_color_hex(COLOR_INK_MAIN), 0);
    lv_obj_center(lbl_back);

    /* 卡片进度显示 */
    s_p1_progress_lbl = lv_label_create(top_bar);
    apply_cjk_font(s_p1_progress_lbl);
    lv_label_set_text(s_p1_progress_lbl, "进度 (0 / 0)");
    lv_obj_set_style_text_color(s_p1_progress_lbl, lv_color_hex(COLOR_INK_MAIN), 0);
    lv_obj_align(s_p1_progress_lbl, LV_ALIGN_LEFT_MID, 72, 0);

    /* 朗读发音小印章按钮 (右侧内缩 8px 避让物理右上圆角) */
    lv_obj_t *btn_speak = lv_button_create(top_bar);
    lv_obj_set_ext_click_area(btn_speak, 12);
    lv_obj_set_size(btn_speak, 64, 26);
    lv_obj_align(btn_speak, LV_ALIGN_RIGHT_MID, -4, 0);
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
    lv_label_set_text(s_p1_word_lbl, "请选择牌组");
    lv_obj_set_style_text_color(s_p1_word_lbl, lv_color_hex(COLOR_INK_MAIN), 0);
    lv_obj_align(s_p1_word_lbl, LV_ALIGN_TOP_LEFT, 10, 8);

    s_p1_phonetic_lbl = lv_label_create(card_box);
    apply_cjk_font(s_p1_phonetic_lbl);
    lv_label_set_text(s_p1_phonetic_lbl, "--");
    lv_obj_set_style_text_color(s_p1_phonetic_lbl, lv_color_hex(COLOR_SEAL_HARD), 0);
    lv_obj_align(s_p1_phonetic_lbl, LV_ALIGN_TOP_LEFT, 10, 34);

    /* 背面内容（中文释义与例句，翻面前默认隐藏） */
    s_p1_meaning_lbl = lv_label_create(card_box);
    apply_cjk_font(s_p1_meaning_lbl);
    lv_obj_set_size(s_p1_meaning_lbl, 306, 60);
    lv_label_set_long_mode(s_p1_meaning_lbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_p1_meaning_lbl, "请先在牌组列表选择一个牌组开始学习");
    lv_obj_set_style_text_color(s_p1_meaning_lbl, lv_color_hex(COLOR_SEAL_GOOD), 0);
    lv_obj_align(s_p1_meaning_lbl, LV_ALIGN_TOP_LEFT, 10, 65);
    lv_obj_add_flag(s_p1_meaning_lbl, LV_OBJ_FLAG_HIDDEN);

    s_p1_example_lbl = lv_label_create(card_box);
    apply_cjk_font(s_p1_example_lbl);
    lv_obj_set_size(s_p1_example_lbl, 306, 75);
    lv_label_set_long_mode(s_p1_example_lbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_p1_example_lbl, "");
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
    /* 1. 重来 */
    lv_obj_t *b1 = lv_button_create(s_p1_ratings_cont);
    lv_obj_set_ext_click_area(b1, 12);
    lv_obj_set_size(b1, 162, 46);
    lv_obj_align(b1, LV_ALIGN_TOP_LEFT, 5, 0);
    lv_obj_set_style_bg_color(b1, lv_color_hex(COLOR_SEAL_AGAIN), 0);
    lv_obj_set_style_radius(b1, 10, 0);
    lv_obj_clear_flag(b1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(b1, on_rating_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)ANKI_RATING_AGAIN);
    lv_obj_add_event_cb(b1, on_rating_clicked, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)ANKI_RATING_AGAIN);
    s_p1_rating_lbls[0] = lv_label_create(b1);
    apply_cjk_font(s_p1_rating_lbls[0]);
    lv_label_set_text(s_p1_rating_lbls[0], "1. 重来 (<1m)");
    lv_obj_set_style_text_color(s_p1_rating_lbls[0], lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(s_p1_rating_lbls[0]);

    /* 2. 困难 */
    lv_obj_t *b2 = lv_button_create(s_p1_ratings_cont);
    lv_obj_set_ext_click_area(b2, 12);
    lv_obj_set_size(b2, 162, 46);
    lv_obj_align(b2, LV_ALIGN_TOP_RIGHT, -5, 0);
    lv_obj_set_style_bg_color(b2, lv_color_hex(COLOR_SEAL_HARD), 0);
    lv_obj_set_style_radius(b2, 10, 0);
    lv_obj_clear_flag(b2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(b2, on_rating_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)ANKI_RATING_HARD);
    lv_obj_add_event_cb(b2, on_rating_clicked, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)ANKI_RATING_HARD);
    s_p1_rating_lbls[1] = lv_label_create(b2);
    apply_cjk_font(s_p1_rating_lbls[1]);
    lv_label_set_text(s_p1_rating_lbls[1], "2. 困难 (10m)");
    lv_obj_set_style_text_color(s_p1_rating_lbls[1], lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(s_p1_rating_lbls[1]);

    /* 3. 良好 */
    lv_obj_t *b3 = lv_button_create(s_p1_ratings_cont);
    lv_obj_set_ext_click_area(b3, 12);
    lv_obj_set_size(b3, 162, 46);
    lv_obj_align(b3, LV_ALIGN_BOTTOM_LEFT, 5, 0);
    lv_obj_set_style_bg_color(b3, lv_color_hex(COLOR_SEAL_GOOD), 0);
    lv_obj_set_style_radius(b3, 10, 0);
    lv_obj_clear_flag(b3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(b3, on_rating_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)ANKI_RATING_GOOD);
    lv_obj_add_event_cb(b3, on_rating_clicked, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)ANKI_RATING_GOOD);
    s_p1_rating_lbls[2] = lv_label_create(b3);
    apply_cjk_font(s_p1_rating_lbls[2]);
    lv_label_set_text(s_p1_rating_lbls[2], "3. 良好 (1d)");
    lv_obj_set_style_text_color(s_p1_rating_lbls[2], lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(s_p1_rating_lbls[2]);

    /* 4. 容易 */
    lv_obj_t *b4 = lv_button_create(s_p1_ratings_cont);
    lv_obj_set_ext_click_area(b4, 12);
    lv_obj_set_size(b4, 162, 46);
    lv_obj_align(b4, LV_ALIGN_BOTTOM_RIGHT, -5, 0);
    lv_obj_set_style_bg_color(b4, lv_color_hex(COLOR_SEAL_EASY), 0);
    lv_obj_set_style_radius(b4, 10, 0);
    lv_obj_clear_flag(b4, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(b4, on_rating_clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)ANKI_RATING_EASY);
    lv_obj_add_event_cb(b4, on_rating_clicked, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)ANKI_RATING_EASY);
    s_p1_rating_lbls[3] = lv_label_create(b4);
    apply_cjk_font(s_p1_rating_lbls[3]);
    lv_label_set_text(s_p1_rating_lbls[3], "4. 容易 (4d)");
    lv_obj_set_style_text_color(s_p1_rating_lbls[3], lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(s_p1_rating_lbls[3]);
}

static void create_page_2_ai_assistant(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(COLOR_PARCHMENT_BG), 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* 1. 灵动呼吸光球容器 (宽 120, 高 48, 居中靠顶 y=6) */
    lv_obj_t *orb_cont = lv_obj_create(parent);
    lv_obj_set_size(orb_cont, 120, 48);
    lv_obj_align(orb_cont, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_bg_opa(orb_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(orb_cont, 0, 0);
    lv_obj_set_style_pad_all(orb_cont, 0, 0);
    lv_obj_clear_flag(orb_cont, LV_OBJ_FLAG_SCROLLABLE);

    /* 外层半透明呼吸晕圈 (初始 38x38) */
    s_ai_orb_glow = lv_obj_create(orb_cont);
    lv_obj_set_size(s_ai_orb_glow, 38, 38);
    lv_obj_center(s_ai_orb_glow);
    lv_obj_set_style_bg_color(s_ai_orb_glow, lv_color_hex(COLOR_ORB_GLOW), 0);
    lv_obj_set_style_bg_opa(s_ai_orb_glow, LV_OPA_40, 0);
    lv_obj_set_style_radius(s_ai_orb_glow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_ai_orb_glow, 0, 0);
    lv_obj_clear_flag(s_ai_orb_glow, LV_OBJ_FLAG_SCROLLABLE);

    /* 内层实体暖金光球 (初始 22x22) */
    s_ai_orb_core = lv_obj_create(orb_cont);
    lv_obj_set_size(s_ai_orb_core, 22, 22);
    lv_obj_center(s_ai_orb_core);
    lv_obj_set_style_bg_color(s_ai_orb_core, lv_color_hex(COLOR_ORB_CORE), 0);
    lv_obj_set_style_radius(s_ai_orb_core, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_ai_orb_core, 0, 0);
    lv_obj_clear_flag(s_ai_orb_core, LV_OBJ_FLAG_SCROLLABLE);

    /* 2. 状态指示单行标签 (y=56) */
    s_ai_status_lbl = lv_label_create(parent);
    apply_cjk_font(s_ai_status_lbl);
    lv_label_set_text(s_ai_status_lbl, "唤醒词：你好 openvela");
    lv_obj_set_style_text_color(s_ai_status_lbl, lv_color_hex(COLOR_INK_MUTED), 0);
    lv_obj_align(s_ai_status_lbl, LV_ALIGN_TOP_MID, 0, 56);

    /* 3. 沉浸式大对话卡片 (宽 346, 高 310, y=78, 避开底部 y=396 的 Dock 栏) */
    s_chat_cont = lv_obj_create(parent);
    lv_obj_set_size(s_chat_cont, 346, 310);
    lv_obj_align(s_chat_cont, LV_ALIGN_TOP_MID, 0, 78);
    lv_obj_set_style_bg_color(s_chat_cont, lv_color_hex(COLOR_PAPER_CARD), 0);
    lv_obj_set_style_border_color(s_chat_cont, lv_color_hex(COLOR_PAPER_BORDER), 0);
    lv_obj_set_style_border_width(s_chat_cont, 2, 0);
    lv_obj_set_style_radius(s_chat_cont, 14, 0);
    lv_obj_set_style_pad_all(s_chat_cont, 12, 0);
    /* 允许并优化纵向丝滑手势触控滚动 */
    lv_obj_add_flag(s_chat_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_chat_cont, LV_DIR_VER);

    /* 用户提问气泡 (顶部暖琥珀标签) */
    s_chat_user_lbl = lv_label_create(s_chat_cont);
    apply_cjk_font(s_chat_user_lbl);
    lv_obj_set_width(s_chat_user_lbl, 320);
    lv_label_set_long_mode(s_chat_user_lbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_chat_user_lbl, "用户: 你好 openvela!");
    lv_obj_set_style_text_color(s_chat_user_lbl, lv_color_hex(COLOR_SEAL_HARD), 0);
    lv_obj_align(s_chat_user_lbl, LV_ALIGN_TOP_LEFT, 0, 0);

    /* AI 助教答复文本 (大字体、支持长文本自动换行与滚动浏览) */
    s_chat_ai_lbl = lv_label_create(s_chat_cont);
    apply_cjk_font(s_chat_ai_lbl);
    lv_obj_set_width(s_chat_ai_lbl, 320);
    lv_label_set_long_mode(s_chat_ai_lbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_chat_ai_lbl, "AI: 你好！我是随声记 AI 助教，随时为你答疑解惑。");
    lv_obj_set_style_text_color(s_chat_ai_lbl, lv_color_hex(COLOR_INK_MAIN), 0);
    lv_obj_align(s_chat_ai_lbl, LV_ALIGN_TOP_LEFT, 0, 42);
}

static void create_page_3_settings(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(COLOR_PARCHMENT_BG), 0);
    /* 允许平滑垂直滚动浏览，底部预留 58px 避让悬浮 Dock 栏 */
    lv_obj_add_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_bottom(parent, 58, 0);

    /* -------------------------------------------------------------
     * 卡片 1: 电脑宿主机代理连接 (342 x 52, y=8)
     * ------------------------------------------------------------- */
    lv_obj_t *sync_card = lv_obj_create(parent);
    lv_obj_set_size(sync_card, 342, 52);
    lv_obj_align(sync_card, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_bg_color(sync_card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(sync_card, lv_color_hex(0xE2DDD5), 0);
    lv_obj_set_style_border_width(sync_card, 1, 0);
    lv_obj_set_style_radius(sync_card, 10, 0);
    lv_obj_set_style_pad_all(sync_card, 0, 0);
    lv_obj_clear_flag(sync_card, LV_OBJ_FLAG_SCROLLABLE);

    /* 网络代理状态与连接按钮 */
    s_net_status_lbl = lv_label_create(sync_card);
    apply_cjk_font(s_net_status_lbl);
    lv_label_set_text(s_net_status_lbl, "网络代理: 未连接");
    lv_obj_set_style_text_color(s_net_status_lbl, lv_color_hex(COLOR_INK_MUTED), 0);
    lv_obj_align(s_net_status_lbl, LV_ALIGN_LEFT_MID, 14, 0);

    s_btn_net_conn = lv_button_create(sync_card);
    lv_obj_set_ext_click_area(s_btn_net_conn, 12);
    lv_obj_set_size(s_btn_net_conn, 80, 32);
    lv_obj_align(s_btn_net_conn, LV_ALIGN_RIGHT_MID, -14, 0);
    lv_obj_set_style_bg_color(s_btn_net_conn, lv_color_hex(COLOR_BTN_SLATE), 0);
    lv_obj_set_style_radius(s_btn_net_conn, 6, 0);
    lv_obj_clear_flag(s_btn_net_conn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_btn_net_conn, on_net_connect_clicked, LV_EVENT_CLICKED, NULL);

    s_lbl_net_conn = lv_label_create(s_btn_net_conn);
    apply_cjk_font(s_lbl_net_conn);
    lv_label_set_text(s_lbl_net_conn, "连接");
    lv_obj_set_style_text_color(s_lbl_net_conn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(s_lbl_net_conn);

    /* -------------------------------------------------------------
     * 卡片 2: 蓝牙耳机精炼管理 (342 x 316, y=66)
     * ------------------------------------------------------------- */
    lv_obj_t *bt_card = lv_obj_create(parent);
    lv_obj_set_size(bt_card, 342, 316);
    lv_obj_align(bt_card, LV_ALIGN_TOP_MID, 0, 66);
    lv_obj_set_style_bg_color(bt_card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(bt_card, lv_color_hex(0xE2DDD5), 0);
    lv_obj_set_style_border_width(bt_card, 1, 0);
    lv_obj_set_style_radius(bt_card, 10, 0);
    lv_obj_set_style_pad_all(bt_card, 0, 0);
    lv_obj_clear_flag(bt_card, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶部标题栏 */
    lv_obj_t *title = lv_label_create(bt_card);
    apply_cjk_font(title);
    lv_label_set_text(title, "蓝牙耳机设备");
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_INK_MAIN), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 14, 12);

    lv_obj_t *btn_refresh = lv_button_create(bt_card);
    lv_obj_set_ext_click_area(btn_refresh, 12);
    lv_obj_set_size(btn_refresh, 70, 28);
    lv_obj_align(btn_refresh, LV_ALIGN_TOP_RIGHT, -14, 8);
    lv_obj_set_style_bg_color(btn_refresh, lv_color_hex(COLOR_BTN_SLATE), 0);
    lv_obj_set_style_radius(btn_refresh, 6, 0);
    lv_obj_clear_flag(btn_refresh, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_refresh, on_bt_scan_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn_refresh, on_bt_scan_clicked, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_t *lbl_refresh = lv_label_create(btn_refresh);
    apply_cjk_font(lbl_refresh);
    lv_label_set_text(lbl_refresh, "扫描");
    lv_obj_set_style_text_color(lbl_refresh, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_refresh);

    /* 设备列表容器 (342 x 268, y=42) */
    s_bt_list = lv_obj_create(bt_card);
    lv_obj_set_size(s_bt_list, 342, 268);
    lv_obj_align(s_bt_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_bt_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_bt_list, 0, 0);
    lv_obj_set_style_pad_all(s_bt_list, 0, 0);
    lv_obj_clear_flag(s_bt_list, LV_OBJ_FLAG_SCROLLABLE);

    /* 初始未扫描提示 */
    lv_obj_t *placeholder = lv_label_create(s_bt_list);
    apply_cjk_font(placeholder);
    lv_label_set_text(placeholder, "点击右上角扫描周围蓝牙耳机");
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

    const char *tabs[] = {"牌组", "背诵", "AI助教", "设置"};
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
        info.input_path = NULL; /* 由 vocavibe_touch 统一托管驱动，彻底杜绝双 indev 冲突与粘连 */

        lv_nuttx_init(&info, &result);
        printf("[VocaVibe UI] lv_nuttx_init 完成: 屏幕 disp=%p\n", result.disp);
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
    lv_obj_set_style_anim_duration(s_tv, 250, 0); /* 250ms 丝滑页面平滑滑动切换 */
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

    /* 1. 切换页面请求 (开启 250ms 丝滑平滑滑动动效) */
    if (s_switch_to_page >= 0 && s_switch_to_page < 4 && s_tv) {
        lv_tileview_set_tile_by_index(s_tv, (uint32_t)s_switch_to_page, 0, LV_ANIM_ON);
        update_nav_buttons_style(s_switch_to_page);
        s_switch_to_page = -1;
    }

    /* 2. AnkiDroid 牌组列表与待复习卡片摘要刷新 */
    if (s_p0_decks_dirty) {
        s_p0_decks_dirty = false;
        render_decks_list_internal();
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

        char prog_buf[64];
        const char *dname = vocavibe_deck_get_selected_name();
        snprintf(prog_buf, sizeof(prog_buf), "%s (%d/%d)", dname ? dname : "卡片", s_cur_idx, s_total_cnt);
        if (s_p1_progress_lbl) lv_label_set_text(s_p1_progress_lbl, prog_buf);

        s_card_back_visible = s_card_req_back;

        /* Anki 原生交互：未翻开时隐藏答案和 4 档打分，显示“查看答案”；翻开后相反 */
        if (s_card_back_visible) {
            if (s_p1_meaning_lbl) lv_obj_clear_flag(s_p1_meaning_lbl, LV_OBJ_FLAG_HIDDEN);
            if (s_p1_example_lbl) lv_obj_clear_flag(s_p1_example_lbl, LV_OBJ_FLAG_HIDDEN);
            if (s_p1_flip_btn) lv_obj_add_flag(s_p1_flip_btn, LV_OBJ_FLAG_HIDDEN);
            if (s_p1_ratings_cont) lv_obj_clear_flag(s_p1_ratings_cont, LV_OBJ_FLAG_HIDDEN);

            /* 动态更新 4 档评分按钮上的复习预测时间（由 AnkiConnect 下发） */
            char btn_txt[32];
            if (s_p1_rating_lbls[0]) {
                snprintf(btn_txt, sizeof(btn_txt), "1. 重来 (%s)", s_cur_next_times[0]);
                lv_label_set_text(s_p1_rating_lbls[0], btn_txt);
            }
            if (s_p1_rating_lbls[1]) {
                snprintf(btn_txt, sizeof(btn_txt), "2. 困难 (%s)", s_cur_next_times[1]);
                lv_label_set_text(s_p1_rating_lbls[1], btn_txt);
            }
            if (s_p1_rating_lbls[2]) {
                snprintf(btn_txt, sizeof(btn_txt), "3. 良好 (%s)", s_cur_next_times[2]);
                lv_label_set_text(s_p1_rating_lbls[2], btn_txt);
            }
            if (s_p1_rating_lbls[3]) {
                snprintf(btn_txt, sizeof(btn_txt), "4. 容易 (%s)", s_cur_next_times[3]);
                lv_label_set_text(s_p1_rating_lbls[3], btn_txt);
            }
        } else {
            if (s_p1_meaning_lbl) lv_obj_add_flag(s_p1_meaning_lbl, LV_OBJ_FLAG_HIDDEN);
            if (s_p1_example_lbl) lv_obj_add_flag(s_p1_example_lbl, LV_OBJ_FLAG_HIDDEN);
            if (s_p1_flip_btn) lv_obj_clear_flag(s_p1_flip_btn, LV_OBJ_FLAG_HIDDEN);
            if (s_p1_ratings_cont) lv_obj_add_flag(s_p1_ratings_cont, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* 4. AI 助教对话卡片内容刷新 */
    if (s_chat_dirty) {
        s_chat_dirty = false;
        if (s_chat_user_lbl) lv_label_set_text(s_chat_user_lbl, s_chat_user_buf);
        if (s_chat_ai_lbl) {
            lv_label_set_text(s_chat_ai_lbl, s_chat_ai_buf);
            if (s_chat_cont) {
                lv_obj_scroll_to_view(s_chat_ai_lbl, LV_ANIM_OFF);
            }
        }
    }

    /* 5. 灵动呼吸光球动效：仅在当前处于 Page 2 时以 30FPS 节流刷新，呈现呼吸起伏与光晕律动 */
    static uint32_t s_last_orb_tick = 0;
    uint32_t now_tick = lv_tick_get();
    if (s_current_page == 2 && (now_tick - s_last_orb_tick >= 33)) {
        s_last_orb_tick = now_tick;
        float speed = 0.08f;
        int core_base = 22, core_var = 3;
        int glow_base = 38, glow_var = 6;
        int glow_opa_base = LV_OPA_30, glow_opa_var = 25;

        uint32_t target_core_color = COLOR_ORB_CORE;
        uint32_t target_glow_color = COLOR_ORB_GLOW;

        if (s_ai_state == AI_STATE_PROACTIVE) {
            speed = 0.16f;
            core_base = 25; core_var = 5;
            glow_base = 45; glow_var = 12;
            glow_opa_base = LV_OPA_60; glow_opa_var = 35;
            target_core_color = COLOR_ORB_PROACTIVE_CORE;
            target_glow_color = COLOR_ORB_PROACTIVE_GLOW;
            if (s_ai_status_lbl) lv_label_set_text(s_ai_status_lbl, "⏰ 主动自驱：今日待复习提醒中...");
        } else if (s_ai_state == AI_STATE_LISTENING) {
            speed = 0.18f;
            core_base = 24; core_var = 5;
            glow_base = 42; glow_var = 10;
            glow_opa_base = LV_OPA_50; glow_opa_var = 40;
            if (s_ai_status_lbl) lv_label_set_text(s_ai_status_lbl, "正在聆听您的提问...");
        } else if (s_ai_state == AI_STATE_THINKING) {
            speed = 0.25f;
            core_base = 20; core_var = 4;
            glow_base = 40; glow_var = 8;
            glow_opa_base = LV_OPA_40; glow_opa_var = 35;
            if (s_ai_status_lbl) lv_label_set_text(s_ai_status_lbl, "MiMo 2.5 正在流式思考中...");
        } else if (s_ai_state == AI_STATE_SPEAKING) {
            speed = 0.14f;
            core_base = 25; core_var = 6;
            glow_base = 44; glow_var = 12;
            glow_opa_base = LV_OPA_60; glow_opa_var = 45;
            if (s_ai_status_lbl) lv_label_set_text(s_ai_status_lbl, "正在通过蓝牙耳机播报语音...");
        } else {
            speed = 0.08f;
            core_base = 22; core_var = 3;
            glow_base = 38; glow_var = 6;
            glow_opa_base = LV_OPA_30; glow_opa_var = 25;
            if (s_ai_status_lbl) lv_label_set_text(s_ai_status_lbl, "唤醒词：你好 openvela");
        }

        s_anim_phase += speed;
        if (s_anim_phase > 6.28318f) s_anim_phase -= 6.28318f;
        float wave = sinf(s_anim_phase); /* -1.0 ~ 1.0 */

        int core_sz = core_base + (int)(wave * core_var);
        int glow_sz = glow_base + (int)(wave * glow_var);
        int glow_opa = glow_opa_base + (int)(wave * glow_opa_var);
        if (glow_opa < LV_OPA_10) glow_opa = LV_OPA_10;
        if (glow_opa > LV_OPA_COVER) glow_opa = LV_OPA_COVER;

        if (s_ai_orb_core) {
            lv_obj_set_size(s_ai_orb_core, core_sz, core_sz);
            lv_obj_set_style_bg_color(s_ai_orb_core, lv_color_hex(target_core_color), 0);
        }
        if (s_ai_orb_glow) {
            lv_obj_set_size(s_ai_orb_glow, glow_sz, glow_sz);
            lv_obj_set_style_bg_color(s_ai_orb_glow, lv_color_hex(target_glow_color), 0);
            lv_obj_set_style_bg_opa(s_ai_orb_glow, (lv_opa_t)glow_opa, 0);
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
            if (s_bt_scan_timeout_ms > 0) {
                lv_label_set_text(placeholder, "正在扫描周围设备...");
                lv_obj_set_style_text_color(placeholder, lv_color_hex(COLOR_SEAL_HARD), 0);
            } else {
                lv_label_set_text(placeholder, "点击右上角刷新扫描周围设备");
                lv_obj_set_style_text_color(placeholder, lv_color_hex(COLOR_INK_MUTED), 0);
            }
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
            }
        }
    }

    /* 8. 连接握手与扫描超时倒计时检测 (防虚假连接与假死) */
    if (s_net_conn_timeout_ms > 0) {
        if (s_net_conn_timeout_ms <= 8) {
            s_net_conn_timeout_ms = 0;
            if (!s_net_connected) {
                if (s_net_status_lbl) {
                    lv_label_set_text(s_net_status_lbl, "网络代理: 未连接 (请启动PC伴侣)");
                    lv_obj_set_style_text_color(s_net_status_lbl, lv_color_hex(COLOR_INK_MUTED), 0);
                }
                if (s_lbl_net_conn) {
                    lv_label_set_text(s_lbl_net_conn, "连接");
                }
                if (s_btn_net_conn) {
                    lv_obj_set_style_bg_color(s_btn_net_conn, lv_color_hex(COLOR_BTN_SLATE), 0);
                }
            } else {
                if (s_lbl_net_conn) {
                    lv_label_set_text(s_lbl_net_conn, "已连接");
                }
                if (s_btn_net_conn) {
                    lv_obj_set_style_bg_color(s_btn_net_conn, lv_color_hex(COLOR_SEAL_GOOD), 0);
                }
            }
        } else {
            s_net_conn_timeout_ms -= 8;
        }
    }

    if (s_bt_scan_timeout_ms > 0) {
        if (s_bt_scan_timeout_ms <= 8) {
            s_bt_scan_timeout_ms = 0;
            if (s_bt_entry_count == 0 && s_bt_list) {
                lv_obj_clean(s_bt_list);
                lv_obj_t *placeholder = lv_label_create(s_bt_list);
                apply_cjk_font(placeholder);
                lv_label_set_text(placeholder, "未发现设备，请检查电脑端蓝牙");
                lv_obj_set_style_text_color(placeholder, lv_color_hex(COLOR_INK_MUTED), 0);
                lv_obj_center(placeholder);
            }
        } else {
            s_bt_scan_timeout_ms -= 8;
        }
    }


    /* 9.1 主动自驱任务弹窗安全调度 */
    if (s_proactive_modal_req) {
        s_proactive_modal_req = false;
        create_proactive_modal();
    }

    /* 10. 处理 LVGL 定时器和触控事件 (动态极速调度，跑满硬件刷屏极限) */
    uint32_t idle = lv_timer_handler();
    /* 当正在切页滑动或手势拖动时，idle 为 0，仅微延时 1ms 让出总线并跑满 60FPS 极速渲染；
     * 静态无操作时适度休眠降低功耗与总线发热 */
    if (idle == 0) {
        usleep(1000);
    } else if (idle < 8) {
        usleep(idle * 1000);
    } else {
        usleep(8000);
    }
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
        for (int k = 0; k < 4; k++) {
            if (card->next_times[k][0]) {
                strncpy(s_cur_next_times[k], card->next_times[k], sizeof(s_cur_next_times[k]) - 1);
                s_cur_next_times[k][sizeof(s_cur_next_times[k]) - 1] = '\0';
            }
        }
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
    s_bt_scan_timeout_ms = 0; /* 收到设备数据，立即取消超时状态 */
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
    s_net_conn_timeout_ms = 0; /* 收到连接响应，取消超时重试倒计时 */
    if (status_str) {
        strncpy(s_net_status_buf, status_str, sizeof(s_net_status_buf) - 1);
        s_net_status_buf[sizeof(s_net_status_buf) - 1] = '\0';
    } else {
        snprintf(s_net_status_buf, sizeof(s_net_status_buf), "网络代理: %s", connected ? "已连接" : "未连接");
    }
    s_net_dirty = true;
}

void vocavibe_ui_set_audio_mode(vocavibe_audio_mode_t mode)
{
    s_audio_mode = mode;
    if (s_cbs.on_audio_mode) {
        s_cbs.on_audio_mode(mode);
    }
}

vocavibe_audio_mode_t vocavibe_ui_get_audio_mode(void)
{
    return s_audio_mode;
}

void vocavibe_ui_show_audio_modal(void)
{
    /* 已取消板载扬声器支持，固定通过蓝牙耳机播放，无需弹出选择对话框 */
}

/* -------------------------------------------------------------------------
 * 主动自驱任务弹窗：晚间到期背词自驱提醒
 * ------------------------------------------------------------------------- */
static void on_proactive_start_clicked(lv_event_t *e)
{
    (void)e;
    printf("[VocaVibe UI] 点击[立即复习]: 关闭主动弹窗，一键切入卡片背诵 (Page 1)\n");
    if (s_proactive_modal) {
        lv_obj_delete(s_proactive_modal);
        s_proactive_modal = NULL;
    }
    vocavibe_ui_set_ai_state(AI_STATE_IDLE);
    vocavibe_ui_switch_page(1);
}

static void on_proactive_dismiss_clicked(lv_event_t *e)
{
    (void)e;
    printf("[VocaVibe UI] 点击[稍后提醒]: 关闭主动提醒弹窗\n");
    if (s_proactive_modal) {
        lv_obj_delete(s_proactive_modal);
        s_proactive_modal = NULL;
    }
    vocavibe_ui_set_ai_state(AI_STATE_IDLE);
}

static void create_proactive_modal(void)
{
    if (s_proactive_modal) {
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    if (!scr) return;

    /* 半透明背景遮罩 (390 x 450) */
    s_proactive_modal = lv_obj_create(scr);
    lv_obj_set_size(s_proactive_modal, 390, 450);
    lv_obj_center(s_proactive_modal);
    lv_obj_set_style_bg_color(s_proactive_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_proactive_modal, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_proactive_modal, 0, 0);
    lv_obj_set_style_radius(s_proactive_modal, 0, 0);
    lv_obj_clear_flag(s_proactive_modal, LV_OBJ_FLAG_SCROLLABLE);

    /* 居中手账风卡片 (330 x 195) */
    lv_obj_t *card = lv_obj_create(s_proactive_modal);
    lv_obj_set_size(card, 330, 195);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_PAPER_CARD), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COLOR_PAPER_BORDER), 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_shadow_width(card, 24, 0);
    lv_obj_set_style_shadow_opa(card, 90, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* 顶部标题提示 */
    lv_obj_t *title = lv_label_create(card);
    apply_cjk_font(title);
    lv_label_set_text(title, "⏰ 学习自驱提醒 (Proactive Task)");
    lv_obj_set_style_text_color(title, lv_color_hex(COLOR_SEAL_HARD), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    /* 详细提示语 */
    lv_obj_t *msg_lbl = lv_label_create(card);
    apply_cjk_font(msg_lbl);
    char buf[192];
    if (s_proactive_msg[0]) {
        snprintf(buf, sizeof(buf), "%s", s_proactive_msg);
    } else {
        snprintf(buf, sizeof(buf), "晚上好！今日尚有 %d 张卡片待复习，趁现在花两分钟过一下吧~", s_proactive_due_count);
    }
    lv_label_set_text(msg_lbl, buf);
    lv_label_set_long_mode(msg_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg_lbl, 290);
    lv_obj_set_style_text_align(msg_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(msg_lbl, lv_color_hex(COLOR_INK_MAIN), 0);
    lv_obj_align(msg_lbl, LV_ALIGN_TOP_MID, 0, 42);

    /* 按钮 1: [立即复习] (124 x 42) -> 点击一键切入复习 */
    lv_obj_t *btn_start = lv_button_create(card);
    lv_obj_set_ext_click_area(btn_start, 8);
    lv_obj_set_size(btn_start, 124, 42);
    lv_obj_align(btn_start, LV_ALIGN_BOTTOM_LEFT, 12, -14);
    lv_obj_set_style_bg_color(btn_start, lv_color_hex(COLOR_SEAL_GOOD), 0);
    lv_obj_set_style_radius(btn_start, 8, 0);
    lv_obj_clear_flag(btn_start, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_start, on_proactive_start_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_start = lv_label_create(btn_start);
    apply_cjk_font(lbl_start);
    lv_label_set_text(lbl_start, "立即复习");
    lv_obj_set_style_text_color(lbl_start, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_start);

    /* 按钮 2: [稍后提醒] (124 x 42) -> 关闭弹窗 */
    lv_obj_t *btn_dismiss = lv_button_create(card);
    lv_obj_set_ext_click_area(btn_dismiss, 8);
    lv_obj_set_size(btn_dismiss, 124, 42);
    lv_obj_align(btn_dismiss, LV_ALIGN_BOTTOM_RIGHT, -12, -14);
    lv_obj_set_style_bg_color(btn_dismiss, lv_color_hex(COLOR_BTN_SLATE), 0);
    lv_obj_set_style_radius(btn_dismiss, 8, 0);
    lv_obj_clear_flag(btn_dismiss, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn_dismiss, on_proactive_dismiss_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_dismiss = lv_label_create(btn_dismiss);
    apply_cjk_font(lbl_dismiss);
    lv_label_set_text(lbl_dismiss, "稍后");
    lv_obj_set_style_text_color(lbl_dismiss, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl_dismiss);
}

void vocavibe_ui_show_proactive_alert(int due_count, const char *tips_msg)
{
    s_proactive_due_count = due_count;
    if (tips_msg) {
        strncpy(s_proactive_msg, tips_msg, sizeof(s_proactive_msg) - 1);
        s_proactive_msg[sizeof(s_proactive_msg) - 1] = '\0';
    } else {
        s_proactive_msg[0] = '\0';
    }
    s_proactive_modal_req = true;
}
