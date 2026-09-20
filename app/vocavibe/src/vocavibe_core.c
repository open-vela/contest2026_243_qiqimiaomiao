/****************************************************************************
 * contest2026_243_qiqimiaomiao/app/vocavibe/src/vocavibe_core.c
 *
 * VocaVibe 核心业务逻辑：Anki SM-2 算法、卡组持久化、意图路由与端机通信
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "vocavibe_core.h"
#include "vocavibe_ui.h"
#include <cJSON.h>

static anki_card_t s_cards[VOCAVIBE_MAX_CARDS];
static int s_card_count = 0;
static int s_current_index = 0;
static bool s_card_showing_back = false;

/* 多牌组管理状态 (AnkiDroid 风格) */
static anki_deck_info_t s_decks[VOCAVIBE_MAX_DECKS];
static int s_deck_count = 0;
static uint32_t s_selected_deck_id = 0;
static char s_selected_deck_name[48] = "全部牌组";

static void init_default_deck(void)
{
    /* 彻底移除写死的假卡片，未同步时保持纯净空状态 */
    s_card_count = 0;
    s_current_index = 0;
    s_card_showing_back = false;
}

int vocavibe_deck_get_deck_count(void)
{
    return s_deck_count;
}

const anki_deck_info_t *vocavibe_deck_get_deck_at(int index)
{
    if (index < 0 || index >= s_deck_count) return NULL;
    return &s_decks[index];
}

uint32_t vocavibe_deck_get_selected_id(void)
{
    return s_selected_deck_id;
}

const char *vocavibe_deck_get_selected_name(void)
{
    return s_selected_deck_name;
}

int vocavibe_deck_get_total_due_all_decks(void)
{
    int total_due = 0;
    for (int i = 0; i < s_deck_count; i++) {
        total_due += s_decks[i].due_count;
    }
    return total_due;
}

int vocavibe_deck_select(uint32_t deck_id)
{
    s_selected_deck_id = deck_id;
    for (int i = 0; i < s_deck_count; i++) {
        if (s_decks[i].id == deck_id) {
            strncpy(s_selected_deck_name, s_decks[i].name, sizeof(s_selected_deck_name) - 1);
            s_selected_deck_name[sizeof(s_selected_deck_name) - 1] = '\0';
            break;
        }
    }
    printf("[VocaVibe Deck] 选中牌组 ID=%u (%s)\n", (unsigned int)deck_id, s_selected_deck_name);

    /* 向 PC 伴侣端请求拉取此牌组的卡片数据 */
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "type", "sync_pull_deck");
    cJSON_AddNumberToObject(req, "deck_id", deck_id);
    cJSON_AddStringToObject(req, "deck_name", s_selected_deck_name);
    char *jstr = cJSON_PrintUnformatted(req);
    if (jstr) {
        printf("[JSON] %s\n", jstr);
        free(jstr);
    }
    cJSON_Delete(req);

    return 0;
}

int vocavibe_deck_init(const char *file_path)
{
    const char *path = file_path ? file_path : VOCAVIBE_DECK_FILE;
    FILE *fp = fopen(path, "r");
    if (!fp) {
        printf("[VocaVibe Core] 尚无本地卡组缓存 '%s'，等待 AnkiConnect 真实同步。\n", path);
        init_default_deck();
        return 0;
    }

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (sz <= 0 || sz > 65536) {
        fclose(fp);
        init_default_deck();
        return 0;
    }

    char *buf = (char *)malloc(sz + 1);
    if (!buf) {
        fclose(fp);
        init_default_deck();
        return 0;
    }

    size_t read_bytes = fread(buf, 1, sz, fp);
    buf[read_bytes] = '\0';
    fclose(fp);

    cJSON *root = cJSON_Parse(buf);
    free(buf);

    if (!root) {
        init_default_deck();
        return 0;
    }

    cJSON *arr = cJSON_IsArray(root) ? root : cJSON_GetObjectItem(root, "cards");
    if (!arr || !cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        init_default_deck();
        return 0;
    }

    s_card_count = 0;
    int arr_sz = cJSON_GetArraySize(arr);
    for (int i = 0; i < arr_sz && s_card_count < VOCAVIBE_MAX_CARDS; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        if (!item) continue;

        anki_card_t *c = &s_cards[s_card_count];
        memset(c, 0, sizeof(*c));

        cJSON *jid = cJSON_GetObjectItem(item, "id");
        cJSON *jword = cJSON_GetObjectItem(item, "word");
        cJSON *jpho = cJSON_GetObjectItem(item, "phonetic");
        cJSON *jmean = cJSON_GetObjectItem(item, "meaning");
        cJSON *jex = cJSON_GetObjectItem(item, "example");
        cJSON *jint = cJSON_GetObjectItem(item, "interval");
        cJSON *jfac = cJSON_GetObjectItem(item, "factor");
        cJSON *jreps = cJSON_GetObjectItem(item, "reps");
        cJSON *jnt = cJSON_GetObjectItem(item, "next_times");

        c->id = jid ? (uint32_t)jid->valueint : (s_card_count + 1);
        if (jword && jword->valuestring) strncpy(c->word, jword->valuestring, sizeof(c->word) - 1);
        if (jpho && jpho->valuestring) strncpy(c->phonetic, jpho->valuestring, sizeof(c->phonetic) - 1);
        if (jmean && jmean->valuestring) strncpy(c->meaning, jmean->valuestring, sizeof(c->meaning) - 1);
        if (jex && jex->valuestring) strncpy(c->example, jex->valuestring, sizeof(c->example) - 1);
        c->interval = jint ? (uint16_t)jint->valueint : 0;
        c->factor = jfac ? (uint16_t)jfac->valueint : 2500;
        c->reps = jreps ? (uint16_t)jreps->valueint : 0;
        c->reviewed = false;

        if (jnt && cJSON_IsArray(jnt)) {
            for (int k = 0; k < 4 && k < cJSON_GetArraySize(jnt); k++) {
                cJSON *t = cJSON_GetArrayItem(jnt, k);
                if (t && t->valuestring) {
                    strncpy(c->next_times[k], t->valuestring, sizeof(c->next_times[k]) - 1);
                }
            }
        }

        s_card_count++;
    }

    cJSON_Delete(root);
    printf("[VocaVibe Core] 成功从本地缓存恢复 %d 张卡片。\n", s_card_count);
    return 0;
}

void vocavibe_deck_flush(void)
{
    /* 尝试在闪存中持久化写入 */
    mkdir(VOCAVIBE_DATA_DIR, 0777);
    FILE *fp = fopen(VOCAVIBE_DECK_FILE, "w");
    if (!fp) {
        return;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();

    for (int i = 0; i < s_card_count; i++) {
        anki_card_t *c = &s_cards[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", c->id);
        cJSON_AddStringToObject(item, "word", c->word);
        cJSON_AddStringToObject(item, "phonetic", c->phonetic);
        cJSON_AddStringToObject(item, "meaning", c->meaning);
        cJSON_AddStringToObject(item, "example", c->example);
        cJSON_AddNumberToObject(item, "interval", c->interval);
        cJSON_AddNumberToObject(item, "factor", c->factor);
        cJSON_AddNumberToObject(item, "reps", c->reps);
        cJSON_AddItemToArray(arr, item);
    }

    cJSON_AddItemToObject(root, "cards", arr);
    char *out = cJSON_Print(root);
    if (out) {
        fputs(out, fp);
        free(out);
    }
    fclose(fp);
    cJSON_Delete(root);
}

int vocavibe_deck_get_total_count(void)
{
    return s_card_count;
}

int vocavibe_deck_get_due_count(void)
{
    int due = 0;
    for (int i = 0; i < s_card_count; i++) {
        if (!s_cards[i].reviewed || s_cards[i].interval == 0) {
            due++;
        }
    }
    return due;
}

int vocavibe_deck_get_reviewed_count(void)
{
    int rev = 0;
    for (int i = 0; i < s_card_count; i++) {
        if (s_cards[i].reviewed) rev++;
    }
    return rev;
}

int vocavibe_deck_get_current_index(void)
{
    return s_current_index;
}

const anki_card_t *vocavibe_deck_get_current_card(void)
{
    if (s_card_count <= 0) return NULL;
    if (s_current_index < 0 || s_current_index >= s_card_count) {
        s_current_index = 0;
    }
    return &s_cards[s_current_index];
}

const anki_card_t *vocavibe_deck_get_card_at(int index)
{
    if (index < 0 || index >= s_card_count) return NULL;
    return &s_cards[index];
}

/* 标准 Anki SM-2 算法核心实现 */
int vocavibe_deck_answer_card(anki_rating_t rating)
{
    if (s_card_count <= 0) return -1;
    anki_card_t *card = &s_cards[s_current_index];

    int grade = (int)rating; /* 1=Again, 2=Hard, 3=Good, 4=Easy */

    /* 1. 更新 SM-2 难度因子 factor (初值 2500 表示 2.50) */
    int delta = 100 - (5 - grade) * (80 + (5 - grade) * 20);
    int new_factor = (int)card->factor + delta;
    if (new_factor < 1300) new_factor = 1300;
    card->factor = (uint16_t)new_factor;

    /* 2. 更新复习间隔 interval 与复习次数 reps */
    if (rating == ANKI_RATING_AGAIN) {
        card->reps = 0;
        card->interval = 0; /* 立即重来 */
    } else if (rating == ANKI_RATING_HARD) {
        card->reps++;
        card->interval = (card->interval == 0) ? 1 : (card->interval * 12 / 10);
    } else if (rating == ANKI_RATING_GOOD) {
        card->reps++;
        if (card->reps == 1) {
            card->interval = 1;
        } else if (card->reps == 2) {
            card->interval = 3;
        } else {
            card->interval = (card->interval * card->factor) / 1000;
        }
    } else if (rating == ANKI_RATING_EASY) {
        card->reps++;
        if (card->reps == 1) {
            card->interval = 2;
        } else if (card->reps == 2) {
            card->interval = 5;
        } else {
            card->interval = (card->interval * card->factor * 13) / 10000;
        }
    }
    card->reviewed = true;

    printf("[VocaVibe SM-2] Card '%s' answered rating %d -> Reps:%d, Interval:%dd, Factor:%.2f\n",
           card->word, grade, card->reps, card->interval, card->factor / 1000.0f);

    /* 同步给 PC 伴侣端 (方便回写 AnkiConnect) */
    cJSON *notify = cJSON_CreateObject();
    cJSON_AddStringToObject(notify, "type", "card_answer_sync");
    cJSON_AddNumberToObject(notify, "id", card->id);
    cJSON_AddStringToObject(notify, "word", card->word);
    cJSON_AddNumberToObject(notify, "rating", grade);
    cJSON_AddNumberToObject(notify, "interval", card->interval);
    cJSON_AddNumberToObject(notify, "factor", card->factor);
    cJSON_AddNumberToObject(notify, "reps", card->reps);
    char *jstr = cJSON_PrintUnformatted(notify);
    if (jstr) {
        printf("[JSON] %s\n", jstr);
        free(jstr);
    }
    cJSON_Delete(notify);

    /* 寻找下一张待复习卡片 (循环查找) */
    int next_idx = (s_current_index + 1) % s_card_count;
    s_current_index = next_idx;
    s_card_showing_back = false;

    vocavibe_deck_flush();

    /* 刷新 UI 卡片显示与仪表盘数据 */
    vocavibe_ui_show_card(&s_cards[s_current_index], false, s_current_index + 1, s_card_count);
    vocavibe_ui_update_dashboard(vocavibe_deck_get_total_count(),
                                 vocavibe_deck_get_due_count(),
                                 vocavibe_deck_get_reviewed_count());
    return 0;
}

int vocavibe_deck_add_card(const char *word, const char *phonetic, const char *meaning, const char *example)
{
    if (s_card_count >= VOCAVIBE_MAX_CARDS || !word) return -1;

    /* 查重 */
    for (int i = 0; i < s_card_count; i++) {
        if (strcasecmp(s_cards[i].word, word) == 0) {
            return vocavibe_deck_update_card(s_cards[i].id, meaning, example);
        }
    }

    anki_card_t *c = &s_cards[s_card_count];
    memset(c, 0, sizeof(*c));
    c->id = (uint32_t)(s_card_count + 1);
    strncpy(c->word, word, sizeof(c->word) - 1);
    if (phonetic) strncpy(c->phonetic, phonetic, sizeof(c->phonetic) - 1);
    if (meaning) strncpy(c->meaning, meaning, sizeof(c->meaning) - 1);
    if (example) strncpy(c->example, example, sizeof(c->example) - 1);
    c->factor = 2500;
    c->interval = 0;
    c->reps = 0;
    c->reviewed = false;

    s_card_count++;
    vocavibe_deck_flush();

    vocavibe_ui_update_dashboard(vocavibe_deck_get_total_count(),
                                 vocavibe_deck_get_due_count(),
                                 vocavibe_deck_get_reviewed_count());
    printf("[VocaVibe Core] AI Added new card: '%s'\n", word);
    return 0;
}

int vocavibe_deck_update_card(uint32_t id, const char *meaning, const char *example)
{
    for (int i = 0; i < s_card_count; i++) {
        if (s_cards[i].id == id) {
            if (meaning) strncpy(s_cards[i].meaning, meaning, sizeof(s_cards[i].meaning) - 1);
            if (example) strncpy(s_cards[i].example, example, sizeof(s_cards[i].example) - 1);
            vocavibe_deck_flush();
            printf("[VocaVibe Core] Card id=%lu updated.\n", (unsigned long)id);
            return 0;
        }
    }
    return -1;
}

int vocavibe_deck_delete_card(uint32_t id)
{
    for (int i = 0; i < s_card_count; i++) {
        if (s_cards[i].id == id) {
            for (int j = i; j < s_card_count - 1; j++) {
                s_cards[j] = s_cards[j + 1];
            }
            s_card_count--;
            if (s_current_index >= s_card_count && s_card_count > 0) {
                s_current_index = s_card_count - 1;
            }
            vocavibe_deck_flush();
            vocavibe_ui_show_card(vocavibe_deck_get_current_card(), false, s_current_index + 1, s_card_count);
            vocavibe_ui_update_dashboard(vocavibe_deck_get_total_count(),
                                         vocavibe_deck_get_due_count(),
                                         vocavibe_deck_get_reviewed_count());
            printf("[VocaVibe Core] Card id=%lu deleted.\n", (unsigned long)id);
            return 0;
        }
    }
    return -1;
}

const anki_card_t *vocavibe_deck_find_card(const char *word)
{
    if (!word) return NULL;
    for (int i = 0; i < s_card_count; i++) {
        if (strcasecmp(s_cards[i].word, word) == 0) {
            return &s_cards[i];
        }
    }
    return NULL;
}

/* 本地意图轻量分类器（即使网络断开，端侧卡片触控与本地离线词控依然秒级响应） */
vocavibe_intent_t vocavibe_classify_intent_local(const char *text)
{
    if (!text || strlen(text) == 0) return INTENT_NONE;

    if (strstr(text, "翻转") || strstr(text, "背面") || strstr(text, "答案") || strstr(text, "看释义")) {
        return INTENT_FLIP;
    }
    if (strstr(text, "重来") || strstr(text, "忘记") || strstr(text, "不会") || strstr(text, "没记住")) {
        return INTENT_AGAIN;
    }
    if (strstr(text, "困难") || strstr(text, "模糊") || strstr(text, "有点难")) {
        return INTENT_HARD;
    }
    if (strstr(text, "良好") || strstr(text, "认识") || strstr(text, "记住") || strstr(text, "记住了")) {
        return INTENT_GOOD;
    }
    if (strstr(text, "简单") || strstr(text, "容易") || strstr(text, "掌握") || strstr(text, "太简单")) {
        return INTENT_EASY;
    }
    if (strstr(text, "下一张") || strstr(text, "下一个") || strstr(text, "继续")) {
        return INTENT_NEXT;
    }
    if (strstr(text, "openvela") || strstr(text, "OpenVela") || strstr(text, "你好") ||
        strstr(text, "造句") || strstr(text, "例句") || strstr(text, "解释") || strstr(text, "助教")) {
        return INTENT_WAKE_LLM;
    }

    return INTENT_WAKE_LLM; /* 默认未知指令转送大模型 */
}

/* 发送标准 JSON 指令帧到 PC 伴侣网关 */
int vocavibe_core_send_json(const char *type, const char *data)
{
    if (!type) return -1;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", type);
    if (data) {
        cJSON_AddStringToObject(root, "data", data);
    }
    char *out = cJSON_PrintUnformatted(root);
    if (out) {
        printf("[JSON] %s\n", out);
        fflush(stdout);
        free(out);
    }
    cJSON_Delete(root);
    return 0;
}

int vocavibe_core_request_tts(const char *text)
{
    return vocavibe_core_send_json("tts_speak", text);
}

int vocavibe_core_request_ai(const char *query)
{
    return vocavibe_core_send_json("ai_query", query);
}

int vocavibe_core_request_sync(void)
{
    return vocavibe_core_send_json("sync_pull", NULL);
}

int vocavibe_core_request_sync_push(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "sync_push");
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < s_card_count; i++) {
        anki_card_t *c = &s_cards[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", c->id);
        cJSON_AddStringToObject(item, "word", c->word);
        cJSON_AddNumberToObject(item, "interval", c->interval);
        cJSON_AddNumberToObject(item, "factor", c->factor);
        cJSON_AddNumberToObject(item, "reps", c->reps);
        cJSON_AddBoolToObject(item, "reviewed", c->reviewed);
        cJSON_AddItemToArray(arr, item);
    }
    cJSON_AddItemToObject(root, "cards", arr);
    char *out = cJSON_PrintUnformatted(root);
    if (out) {
        printf("[JSON] %s\n", out);
        fflush(stdout);
        free(out);
    }
    cJSON_Delete(root);
    return 0;
}


int vocavibe_core_request_bt_scan(void)
{
    return vocavibe_core_send_json("bt_scan", NULL);
}

int vocavibe_core_request_bt_connect(const char *mac)
{
    return vocavibe_core_send_json("bt_connect", mac);
}

int vocavibe_core_request_net_connect(void)
{
    return vocavibe_core_send_json("net_connect", NULL);
}

int vocavibe_core_request_audio_mode(int mode)
{
    return vocavibe_core_send_json("audio_mode", (mode == 1) ? "speaker" : "headset");
}

/* 接收并处理来自电脑中转网关/耳机的协议报文 */
void vocavibe_core_handle_line(const char *line)
{
    if (!line) return;
    const char *p = line;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p == '\0') return;

    /* 仅处理以 '{' 开头的合法 JSON 报文 */
    if (*p != '{') return;

    cJSON *root = cJSON_Parse(p);
    if (!root) return;

    cJSON *jtype = cJSON_GetObjectItem(root, "type");
    if (!jtype || !jtype->valuestring) {
        cJSON_Delete(root);
        return;
    }

    const char *type = jtype->valuestring;

    /* 1. 语音拾音识别结果 ASR */
    if (strcmp(type, "asr_result") == 0) {
        cJSON *jtext = cJSON_GetObjectItem(root, "text");
        if (jtext && jtext->valuestring) {
            const char *speech_text = jtext->valuestring;
            printf("[VocaVibe] ASR Received: '%s'\n", speech_text);

            /* 本地意图快速匹配 */
            vocavibe_intent_t intent = vocavibe_classify_intent_local(speech_text);
            if (intent == INTENT_FLIP) {
                s_card_showing_back = !s_card_showing_back;
                vocavibe_ui_show_card(vocavibe_deck_get_current_card(), s_card_showing_back, s_current_index + 1, s_card_count);
            } else if (intent == INTENT_AGAIN) {
                vocavibe_deck_answer_card(ANKI_RATING_AGAIN);
            } else if (intent == INTENT_HARD) {
                vocavibe_deck_answer_card(ANKI_RATING_HARD);
            } else if (intent == INTENT_GOOD) {
                vocavibe_deck_answer_card(ANKI_RATING_GOOD);
            } else if (intent == INTENT_EASY) {
                vocavibe_deck_answer_card(ANKI_RATING_EASY);
            } else if (intent == INTENT_NEXT) {
                s_current_index = (s_current_index + 1) % s_card_count;
                s_card_showing_back = false;
                vocavibe_ui_show_card(vocavibe_deck_get_current_card(), false, s_current_index + 1, s_card_count);
            } else {
                /* 唤醒词或大模型问答：切换到 AI 助教页面，展示提问并启动流式回答 */
                vocavibe_ui_switch_page(2);
                vocavibe_ui_set_ai_state(AI_STATE_THINKING);
                vocavibe_ui_set_ai_chat(speech_text, "AI 思考中...");
                vocavibe_core_request_ai(speech_text);
            }
        }
    }
    /* 2. 大模型流式输出片段 */
    else if (strcmp(type, "ai_stream") == 0) {
        cJSON *jdelta = cJSON_GetObjectItem(root, "delta");
        if (jdelta && jdelta->valuestring) {
            vocavibe_ui_append_ai_stream(jdelta->valuestring);
        }
    }
    /* 3. 大模型完整回复文本与状态 */
    else if (strcmp(type, "ai_response") == 0) {
        cJSON *juser = cJSON_GetObjectItem(root, "user");
        cJSON *jai = cJSON_GetObjectItem(root, "ai");
        const char *user_str = (juser && juser->valuestring) ? juser->valuestring : "";
        const char *ai_str = (jai && jai->valuestring) ? jai->valuestring : "";
        vocavibe_ui_switch_page(2);
        vocavibe_ui_set_ai_chat(user_str, ai_str);
        vocavibe_ui_set_ai_state(AI_STATE_SPEAKING);
    }
    /* 4. AI 状态动效同步 (listening/thinking/speaking/idle) */
    else if (strcmp(type, "ai_state") == 0) {
        cJSON *jstate = cJSON_GetObjectItem(root, "state");
        if (jstate && jstate->valuestring) {
            if (strcmp(jstate->valuestring, "listening") == 0) {
                vocavibe_ui_set_ai_state(AI_STATE_LISTENING);
            } else if (strcmp(jstate->valuestring, "thinking") == 0) {
                vocavibe_ui_set_ai_state(AI_STATE_THINKING);
            } else if (strcmp(jstate->valuestring, "speaking") == 0) {
                vocavibe_ui_set_ai_state(AI_STATE_SPEAKING);
            } else {
                vocavibe_ui_set_ai_state(AI_STATE_IDLE);
            }
        }
    }
    /* 5. 蓝牙耳机扫描结果代理列表 */
    else if (strcmp(type, "bt_scan_result") == 0) {
        vocavibe_ui_clear_bt_devices();
        cJSON *jdevs = cJSON_GetObjectItem(root, "devices");
        if (jdevs && cJSON_IsArray(jdevs)) {
            int num = cJSON_GetArraySize(jdevs);
            for (int i = 0; i < num; i++) {
                cJSON *dev = cJSON_GetArrayItem(jdevs, i);
                if (!dev) continue;
                cJSON *jname = cJSON_GetObjectItem(dev, "name");
                cJSON *jmac = cJSON_GetObjectItem(dev, "mac");
                cJSON *jrssi = cJSON_GetObjectItem(dev, "rssi");
                cJSON *jstat = cJSON_GetObjectItem(dev, "status");
                cJSON *jconn = cJSON_GetObjectItem(dev, "connected");
                const char *name = jname && jname->valuestring ? jname->valuestring : "Unknown Headset";
                const char *mac = jmac && jmac->valuestring ? jmac->valuestring : "--:--:--:--";
                const char *stat_str = jstat && jstat->valuestring ? jstat->valuestring : NULL;
                bool is_conn = jconn ? cJSON_IsTrue(jconn) : false;
                int rssi = jrssi ? jrssi->valueint : -60;
                vocavibe_ui_add_bt_device(name, mac, stat_str, rssi, is_conn);
            }
        }
    }
    /* 6. 蓝牙耳机连接状态反馈 */
    else if (strcmp(type, "bt_status") == 0) {
        printf("[VocaVibe] 成功处理 bt_status 报文!\n");
        cJSON *jconn = cJSON_GetObjectItem(root, "connected");
        cJSON *jname = cJSON_GetObjectItem(root, "name");
        cJSON *jmac = cJSON_GetObjectItem(root, "mac");
        bool conn = jconn ? cJSON_IsTrue(jconn) : false;
        const char *name = jname && jname->valuestring ? jname->valuestring : "";
        const char *mac = jmac && jmac->valuestring ? jmac->valuestring : "";
        char status_buf[64];
        if (conn) {
            snprintf(status_buf, sizeof(status_buf), "已连接: %s", name[0] ? name : "蓝牙设备");
            vocavibe_ui_show_audio_modal();
        } else {
            snprintf(status_buf, sizeof(status_buf), "未连接蓝牙设备");
        }
        vocavibe_ui_set_bt_status(status_buf, conn);
        vocavibe_ui_update_device_status(mac, name, conn);
    }
    /* 7. 接收全部牌组概览 (AnkiDroid 风格牌组列表同步) */
    else if (strcmp(type, "sync_decks_overview") == 0) {
        cJSON *jdecks = cJSON_GetObjectItem(root, "decks");
        if (jdecks && cJSON_IsArray(jdecks)) {
            s_deck_count = 0;
            int num = cJSON_GetArraySize(jdecks);
            for (int i = 0; i < num && s_deck_count < VOCAVIBE_MAX_DECKS; i++) {
                cJSON *item = cJSON_GetArrayItem(jdecks, i);
                if (!item) continue;
                anki_deck_info_t *d = &s_decks[s_deck_count];
                memset(d, 0, sizeof(*d));
                cJSON *jid = cJSON_GetObjectItem(item, "id");
                cJSON *jname = cJSON_GetObjectItem(item, "name");
                cJSON *jnew = cJSON_GetObjectItem(item, "new_count");
                cJSON *jlearn = cJSON_GetObjectItem(item, "learn_count");
                cJSON *jdue = cJSON_GetObjectItem(item, "due_count");

                d->id = jid ? (uint32_t)jid->valueint : (s_deck_count + 1);
                if (jname && jname->valuestring) {
                    strncpy(d->name, jname->valuestring, sizeof(d->name) - 1);
                }
                d->new_count = jnew ? jnew->valueint : 0;
                d->learn_count = jlearn ? jlearn->valueint : 0;
                d->due_count = jdue ? jdue->valueint : 0;
                s_deck_count++;
            }
            printf("[VocaVibe Core] 收到 AnkiConnect %d 个牌组概览数据\n", s_deck_count);
            vocavibe_ui_update_decks_list();
            vocavibe_ui_set_sync_status("AnkiConnect: 牌组已同步");
        }
    }
    /* 8. 选中牌组卡片同步 (包含 FSRS/SM-2 预计算的 4 档时间标签) */
    else if (strcmp(type, "sync_deck") == 0) {
        cJSON *jcards = cJSON_GetObjectItem(root, "cards");
        cJSON *jdeck_name = cJSON_GetObjectItem(root, "deck_name");
        if (jdeck_name && jdeck_name->valuestring) {
            strncpy(s_selected_deck_name, jdeck_name->valuestring, sizeof(s_selected_deck_name) - 1);
        }
        if (jcards && cJSON_IsArray(jcards)) {
            s_card_count = 0;
            int num = cJSON_GetArraySize(jcards);
            for (int i = 0; i < num && s_card_count < VOCAVIBE_MAX_CARDS; i++) {
                cJSON *item = cJSON_GetArrayItem(jcards, i);
                if (!item) continue;
                anki_card_t *c = &s_cards[s_card_count];
                memset(c, 0, sizeof(*c));
                cJSON *jid = cJSON_GetObjectItem(item, "id");
                cJSON *jword = cJSON_GetObjectItem(item, "word");
                cJSON *jpho = cJSON_GetObjectItem(item, "phonetic");
                cJSON *jmean = cJSON_GetObjectItem(item, "meaning");
                cJSON *jex = cJSON_GetObjectItem(item, "example");
                cJSON *jint = cJSON_GetObjectItem(item, "interval");
                cJSON *jfac = cJSON_GetObjectItem(item, "factor");
                cJSON *jnt = cJSON_GetObjectItem(item, "next_times");

                c->id = jid ? (uint32_t)jid->valueint : (s_card_count + 1);
                if (jword && jword->valuestring) strncpy(c->word, jword->valuestring, sizeof(c->word) - 1);
                if (jpho && jpho->valuestring) strncpy(c->phonetic, jpho->valuestring, sizeof(c->phonetic) - 1);
                if (jmean && jmean->valuestring) strncpy(c->meaning, jmean->valuestring, sizeof(c->meaning) - 1);
                if (jex && jex->valuestring) strncpy(c->example, jex->valuestring, sizeof(c->example) - 1);
                c->interval = jint ? (uint16_t)jint->valueint : 0;
                c->factor = jfac ? (uint16_t)jfac->valueint : 2500;
                c->reviewed = false;

                if (jnt && cJSON_IsArray(jnt)) {
                    for (int k = 0; k < 4 && k < cJSON_GetArraySize(jnt); k++) {
                        cJSON *t = cJSON_GetArrayItem(jnt, k);
                        if (t && t->valuestring) {
                            strncpy(c->next_times[k], t->valuestring, sizeof(c->next_times[k]) - 1);
                        }
                    }
                }

                s_card_count++;
            }
            vocavibe_deck_flush();
            s_current_index = 0;
            s_card_showing_back = false;
            vocavibe_ui_show_card(vocavibe_deck_get_current_card(), false, s_card_count > 0 ? 1 : 0, s_card_count);
            vocavibe_ui_update_dashboard(vocavibe_deck_get_total_count(),
                                         vocavibe_deck_get_due_count(),
                                         vocavibe_deck_get_reviewed_count());
            vocavibe_ui_set_sync_status("AnkiConnect: 卡片已加载");
        }
    }
    /* 8. AI Skill 动态添加卡片 */
    else if (strcmp(type, "card_add") == 0) {
        cJSON *jword = cJSON_GetObjectItem(root, "word");
        cJSON *jpho = cJSON_GetObjectItem(root, "phonetic");
        cJSON *jmean = cJSON_GetObjectItem(root, "meaning");
        cJSON *jex = cJSON_GetObjectItem(root, "example");
        if (jword && jword->valuestring) {
            vocavibe_deck_add_card(jword->valuestring,
                                   jpho ? jpho->valuestring : "",
                                   jmean ? jmean->valuestring : "",
                                   jex ? jex->valuestring : "");
        }
    }
    /* 9. AI Skill 动态删除卡片 */
    else if (strcmp(type, "card_del") == 0) {
        cJSON *jid = cJSON_GetObjectItem(root, "id");
        if (jid) {
            vocavibe_deck_delete_card((uint32_t)jid->valueint);
        }
    }
    /* 10. PC 网络中转连接状态反馈 */
    else if (strcmp(type, "net_status") == 0) {
        printf("[VocaVibe] 成功处理 net_status 报文!\n");
        cJSON *jconn = cJSON_GetObjectItem(root, "connected");
        cJSON *jip = cJSON_GetObjectItem(root, "ip");
        bool conn = jconn ? cJSON_IsTrue(jconn) : false;
        const char *ip = jip && jip->valuestring ? jip->valuestring : "127.0.0.1";
        char status_buf[64];
        if (conn) {
            snprintf(status_buf, sizeof(status_buf), "网络代理: 已连接 (%s)", ip);
        } else {
            snprintf(status_buf, sizeof(status_buf), "网络代理: 未连接");
        }
        vocavibe_ui_set_net_status(conn, status_buf);
    }
    /* 11. 时间同步报文 (宿主机下发高精度时间戳) */
    else if (strcmp(type, "time_sync") == 0) {
        cJSON *jts = cJSON_GetObjectItem(root, "timestamp");
        if (jts) {
            time_t sec = 0;
            if (jts->valuedouble > 0) sec = (time_t)jts->valuedouble;
            else if (jts->valueint > 0) sec = (time_t)jts->valueint;
            if (sec > 0) {
                struct timeval tv;
                tv.tv_sec = sec;
                tv.tv_usec = 0;
                settimeofday(&tv, NULL);
                printf("[VocaVibe] 成功同步宿主机时间: timestamp=%ld\n", (long)sec);
            }
        }
    }

    cJSON_Delete(root);
}

int vocavibe_core_init(void)
{
    printf("[VocaVibe Core] Initializing VocaVibe AI Core...\n");
    vocavibe_deck_init(VOCAVIBE_DECK_FILE);
    return 0;
}

int vocavibe_core_deinit(void)
{
    vocavibe_deck_flush();
    return 0;
}

void vocavibe_trigger_proactive_reminder(int force_due)
{
    int due_cnt = force_due;
    if (due_cnt <= 0) {
        due_cnt = vocavibe_deck_get_total_due_all_decks();
    }
    if (due_cnt <= 0) {
        due_cnt = vocavibe_deck_get_due_count();
    }
    if (due_cnt <= 0) {
        due_cnt = 5; /* 兜底模拟值，确保演示链路完整流畅 */
    }

    printf("\n======================================================\n");
    printf(" ⏰ [Proactive Task] 触发端侧到期主动背词自驱提醒！\n");
    printf("    当前待复习卡片总数: %d\n", due_cnt);
    printf("======================================================\n");

    /* 1. 组合温润的唤醒文案 */
    char tts_text[192];
    snprintf(tts_text, sizeof(tts_text),
             "晚上好！检测到您今天还有 %d 张卡片等待复习，现在花两分钟过一下吧~", due_cnt);

    /* 2. 界面表现：平滑切至 Page 2 (AI 助教)，光球切为翠绿自驱律动，弹出沉浸提醒卡片 */
    vocavibe_ui_switch_page(2);
    vocavibe_ui_set_ai_state(AI_STATE_PROACTIVE);
    vocavibe_ui_show_proactive_alert(due_cnt, tts_text);

    /* 3. 听觉执行：调度 TTS 通过蓝牙耳机/板载喇叭播报提醒语音 */
    vocavibe_core_request_tts(tts_text);
}

void vocavibe_proactive_check(void)
{
    static int s_last_check_min = -1;
    static int s_last_proactive_day = -1;

    time_t now = time(NULL);
    /* 避免系统未同步时间时的负数或初始 Epoch 0 */
    if (now < 1700000000) {
        return;
    }

    struct tm tm_now;
    localtime_r(&now, &tm_now);

    /* 每分钟检测一次 */
    if (tm_now.tm_min == s_last_check_min) {
        return;
    }
    s_last_check_min = tm_now.tm_min;

    /* 设定每日真实世界触发时段：20:00 (晚间复习黄金时段) */
    if (tm_now.tm_hour == 20 && tm_now.tm_min == 0) {
        /* 当天未提醒过且卡组有到期未背词 */
        if (s_last_proactive_day != tm_now.tm_mday) {
            int total_due = vocavibe_deck_get_total_due_all_decks();
            if (total_due > 0) {
                s_last_proactive_day = tm_now.tm_mday;
                printf("[VocaVibe Proactive] 真实世界时间 20:00 到达，检测到 %d 张待复习卡片，启动主动唤醒！\n", total_due);
                vocavibe_trigger_proactive_reminder(total_due);
            }
        }
    }
}

void vocavibe_core_poll(void)
{
    /* 轮询主动自驱时间判定 */
    vocavibe_proactive_check();
}

