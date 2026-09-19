/****************************************************************************
 * contest2026_243_qiqimiaomiao/app/vocavibe/include/vocavibe_ui.h
 *
 * VocaVibe 屏幕全触控交互 UI 声明 (LVGL 9 on SF32LB52 LCD + Touch)
 ****************************************************************************/

#ifndef __VOCAVIBE_UI_H__
#define __VOCAVIBE_UI_H__

#include <stdbool.h>
#include <stdint.h>
#include "vocavibe_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* UI 事件回调函数指针 */
typedef void (*vocavibe_ui_card_answer_cb_t)(anki_rating_t rating);
typedef void (*vocavibe_ui_ai_query_cb_t)(const char *query);
typedef void (*vocavibe_ui_sync_cb_t)(void);
typedef void (*vocavibe_ui_bt_scan_cb_t)(void);
typedef void (*vocavibe_ui_bt_connect_cb_t)(const char *mac);
typedef void (*vocavibe_ui_net_connect_cb_t)(void);

typedef struct {
    vocavibe_ui_card_answer_cb_t on_card_answer;
    vocavibe_ui_ai_query_cb_t    on_ai_query;
    vocavibe_ui_sync_cb_t        on_sync;
    vocavibe_ui_bt_scan_cb_t     on_bt_scan;
    vocavibe_ui_bt_connect_cb_t  on_bt_connect;
    vocavibe_ui_net_connect_cb_t on_net_connect;
} vocavibe_ui_callbacks_t;

/* AI 助教动效状态枚举 */
typedef enum {
    AI_STATE_IDLE = 0,
    AI_STATE_LISTENING,
    AI_STATE_THINKING,
    AI_STATE_SPEAKING
} vocavibe_ai_state_t;

/* 初始化 UI (创建 4 页面 Tileview、小智声波、Anki 4 档按键、蓝牙代理列表等) */
int  vocavibe_ui_init(const vocavibe_ui_callbacks_t *cbs);

/* UI 核心主循环轮询（必须在初始化 LVGL 的主线程中调用，以共享 TLS 和触摸驱动） */
void vocavibe_ui_poll(void);

void vocavibe_ui_switch_page(int page_idx);
int  vocavibe_ui_get_current_page(void);
void vocavibe_ui_dump_layout(void);

/* 页面 0: 仪表盘数据刷新 */
void vocavibe_ui_update_dashboard(int total, int due, int reviewed);

/* 页面 1: Anki 卡片显示与翻转刷新 */
void vocavibe_ui_show_card(const anki_card_t *card, bool show_back, int cur_idx, int total_cnt);

/* 页面 2: AI 助教状态、流式输出与对话气泡 */
void vocavibe_ui_set_ai_state(vocavibe_ai_state_t state);
void vocavibe_ui_append_ai_stream(const char *delta);
void vocavibe_ui_set_ai_chat(const char *user_query, const char *ai_reply);

/* 页面 3: 蓝牙代理与同步设置 */
void vocavibe_ui_set_net_status(bool connected, const char *status_str);
void vocavibe_ui_add_bt_device(const char *name, const char *mac, const char *status_str, int rssi, bool is_connected);
void vocavibe_ui_clear_bt_devices(void);
void vocavibe_ui_update_device_status(const char *mac, const char *name, bool connected);
void vocavibe_ui_set_bt_status(const char *status_str, bool connected);
void vocavibe_ui_set_sync_status(const char *status_str);

#ifdef __cplusplus
}
#endif

#endif /* __VOCAVIBE_UI_H__ */
