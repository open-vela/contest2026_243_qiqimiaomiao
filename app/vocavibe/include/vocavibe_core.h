/****************************************************************************
 * contest2026_243_qiqimiaomiao/app/vocavibe/include/vocavibe_core.h
 *
 * VocaVibe 核心业务逻辑：Anki SM-2 算法、卡组持久化、意图路由与端机通信
 ****************************************************************************/

#ifndef __INCLUDE_VOCAVIBE_CORE_H
#define __INCLUDE_VOCAVIBE_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VOCAVIBE_MAX_CARDS     64
#define VOCAVIBE_DATA_DIR      "/data/vocavibe"
#define VOCAVIBE_DECK_FILE     "/data/vocavibe/deck.json"

/* Anki 4 档标准评分 */
typedef enum {
    ANKI_RATING_AGAIN = 1, /* 重来: 1分钟内重试 */
    ANKI_RATING_HARD  = 2, /* 困难: 缩短复习间隔 */
    ANKI_RATING_GOOD  = 3, /* 良好: 正常推进间隔 */
    ANKI_RATING_EASY  = 4  /* 简单: 显著延长间隔 */
} anki_rating_t;

/* Anki 记忆卡片结构体 */
typedef struct {
    uint32_t id;
    char     word[48];
    char     phonetic[48];
    char     meaning[96];
    char     example[160];
    uint16_t interval;     /* 当前复习间隔 (天) */
    uint16_t factor;       /* SM-2 难度因子 (例如 2500 = 250%) */
    uint16_t reps;         /* 复习次数 */
    int64_t  due;          /* 下次到期时间戳 */
    bool     reviewed;     /* 今日是否已复习 */
} anki_card_t;

/* 本地意图识别分类 */
typedef enum {
    INTENT_NONE = 0,
    INTENT_FLIP,           /* 翻转卡片 */
    INTENT_AGAIN,          /* 重来 / 忘记 */
    INTENT_HARD,           /* 困难 / 模糊 */
    INTENT_GOOD,           /* 良好 / 认识 */
    INTENT_EASY,           /* 简单 / 掌握 */
    INTENT_NEXT,           /* 下一个 */
    INTENT_WAKE_LLM        /* 唤醒词 / 大模型问答 */
} vocavibe_intent_t;

/* --- 核心生命周期 --- */
int  vocavibe_core_init(void);
int  vocavibe_core_deinit(void);
void vocavibe_core_poll(void);

/* --- 卡组管理与 Anki SM-2 算法 --- */
int  vocavibe_deck_init(const char *file_path);
int  vocavibe_deck_get_total_count(void);
int  vocavibe_deck_get_due_count(void);
int  vocavibe_deck_get_reviewed_count(void);
int  vocavibe_deck_get_current_index(void);
const anki_card_t *vocavibe_deck_get_current_card(void);
const anki_card_t *vocavibe_deck_get_card_at(int index);

/* 对当前卡片执行评分，并推进到下一张待复习卡片 */
int  vocavibe_deck_answer_card(anki_rating_t rating);

/* 卡组增删改查接口（供 AI Agent Skill 动态调用） */
int  vocavibe_deck_add_card(const char *word, const char *phonetic, const char *meaning, const char *example);
int  vocavibe_deck_update_card(uint32_t id, const char *meaning, const char *example);
int  vocavibe_deck_delete_card(uint32_t id);
const anki_card_t *vocavibe_deck_find_card(const char *word);
void vocavibe_deck_flush(void);

/* --- 本地意图解析 Fallback --- */
vocavibe_intent_t vocavibe_classify_intent_local(const char *text);

/* --- 协议与通信 --- */
void vocavibe_core_handle_line(const char *line);
int  vocavibe_core_send_json(const char *type, const char *data);
int  vocavibe_core_request_tts(const char *text);
int  vocavibe_core_request_ai(const char *query);
int  vocavibe_core_request_sync(void);
int  vocavibe_core_request_bt_scan(void);
int  vocavibe_core_request_bt_connect(const char *mac);

#ifdef __cplusplus
}
#endif

#endif /* __INCLUDE_VOCAVIBE_CORE_H */

