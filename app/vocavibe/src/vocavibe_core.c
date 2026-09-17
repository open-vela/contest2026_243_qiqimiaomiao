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

/* 默认比赛定制 20 个高频 AI 嵌入式核心词库 */
static const anki_card_t s_default_cards[] = {
    {1, "openvela", "/ˈoʊpən ˈvɛlə/", "面向端侧 AI 与嵌入式微控制器的下一代开源实时操作系统", "OpenVela OS powers intelligent edge hardware with microsecond latency.", 0, 2500, 0, 0, false},
    {2, "ecosystem", "/ˈiːkoʊˌsɪstəm/", "生态系统；多设备、开发者与模型协同演进的软件网络", "Developers collaborate to enrich the vibrant openvela ecosystem.", 0, 2500, 0, 0, false},
    {3, "embedded", "/ɪmˈbɛdɪd/", "嵌入式的；植入硬件芯片与微控制器内部的高效系统", "SF32LB52 is an advanced dual-core embedded IoT processor.", 0, 2500, 0, 0, false},
    {4, "latency", "/ˈleɪtənsi/", "延迟；端侧处理音频拾音到输出答复的时间间隔", "Ultra-low latency is crucial for real-time voice conversations.", 0, 2500, 0, 0, false},
    {5, "multimodal", "/ˌmʌltiˈmoʊdl/", "多模态的；融合屏幕触控、语音问答与声波视觉的交互", "VocaVibe delivers a seamless multimodal language learning interface.", 0, 2500, 0, 0, false},
    {6, "neural", "/ˈnʊrəl/", "神经的；端侧轻量神经网络与深度学习推理计算模型", "Edge neural networks optimize speech feature extraction.", 0, 2500, 0, 0, false},
    {7, "heuristic", "/hjʊˈrɪstɪk/", "启发式的；基于规则与认知遗忘曲线的动态记忆算法", "The Anki SM-2 heuristic algorithm optimizes spaced reviews.", 0, 2500, 0, 0, false},
    {8, "synthesize", "/ˈsɪnθəsaɪz/", "合成；利用音频神经网络引擎实时生成自然发音", "Cloud TTS engines synthesize crystal-clear pronunciation.", 0, 2500, 0, 0, false},
    {9, "cognitive", "/ˈkɑːɡnətɪv/", "认知的；学习者大脑对词汇记忆与语义理解的心智过程", "Spaced review significantly reduces cognitive overload.", 0, 2500, 0, 0, false},
    {10, "inference", "/ˈɪnfərəns/", "推理；大语言模型根据提问上下文生成连续释义的过程", "Xiaomi MiMo LLM performs high-speed streaming inference.", 0, 2500, 0, 0, false},
    {11, "agile", "/ˈædʒl/", "敏捷的；具备轻量低开销与快速响应的软硬件协同架构", "OpenVela facilitates agile iteration for smart edge devices.", 0, 2500, 0, 0, false},
    {12, "paradigm", "/ˈpærədaɪm/", "范式；端侧智能硬件与云端大模型协作的一致工程架构", "Distributed agents represent a new paradigm in embedded computing.", 0, 2500, 0, 0, false},
    {13, "telemetry", "/təˈlɛmətri/", "遥测数据；设备电池状态、网络连接与内存占用指标", "System telemetry reports real-time connection status to the UI.", 0, 2500, 0, 0, false},
    {14, "pervasive", "/pərˈveɪsɪv/", "泛在的；无处不在的分布式微智能协同感知网络", "Pervasive intelligence bridges wearable hardware and cloud agents.", 0, 2500, 0, 0, false},
    {15, "orchestrate", "/ˈɔːrkɪstreɪt/", "编排；多智能体流程调度、拾音、大模型与音频流同步", "The central coordinator orchestrates ASR, LLM, and UI updates.", 0, 2500, 0, 0, false},
    {16, "autonomous", "/ɔːˈtɑːnəməs/", "自主的；具备端侧本地解析决策与即时响应能力的代理", "Autonomous agent skills handle card CRUD events seamlessly.", 0, 2500, 0, 0, false},
    {17, "resonance", "/ˈrɛzənəns/", "共振/共鸣；小智声波律动与人声语调在视觉上的动态同步", "Sonic waveforms oscillate in visual resonance with speech.", 0, 2500, 0, 0, false},
    {18, "tangible", "/ˈtændʒəbl/", "有形的；触手可及的全触控硬件学习伴侣实体终端", "VocaVibe transforms cloud AI into a tangible desktop companion.", 0, 2500, 0, 0, false},
    {19, "fidelity", "/fɪˈdɛləti/", "保真度；高清语音采样与鲜艳 AMOLED 显示屏视觉呈现", "High fidelity audio ensures users grasp precise pronunciation.", 0, 2500, 0, 0, false},
    {20, "benchmark", "/ˈbɛntʃmɑːrk/", "标杆；2026 首届 openvela 软硬件开发者大赛示范水准", "VocaVibe sets a benchmark for edge AI hardware innovations.", 0, 2500, 0, 0, false}
};

static void init_default_deck(void)
{
    s_card_count = sizeof(s_default_cards) / sizeof(s_default_cards[0]);
    if (s_card_count > VOCAVIBE_MAX_CARDS) {
        s_card_count = VOCAVIBE_MAX_CARDS;
    }
    memcpy(s_cards, s_default_cards, sizeof(anki_card_t) * s_card_count);
    s_current_index = 0;
    s_card_showing_back = false;
}

int vocavibe_deck_init(const char *file_path)
{
    const char *path = file_path ? file_path : VOCAVIBE_DECK_FILE;
    FILE *fp = fopen(path, "r");
    if (!fp) {
        printf("[VocaVibe Core] Deck file '%s' not found. Initializing default 20 cards.\n", path);
        init_default_deck();
        vocavibe_deck_flush();
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
        printf("[VocaVibe Core] Corrupted JSON in deck file. Using defaults.\n");
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

        c->id = jid ? (uint32_t)jid->valueint : (s_card_count + 1);
        if (jword && jword->valuestring) strncpy(c->word, jword->valuestring, sizeof(c->word) - 1);
        if (jpho && jpho->valuestring) strncpy(c->phonetic, jpho->valuestring, sizeof(c->phonetic) - 1);
        if (jmean && jmean->valuestring) strncpy(c->meaning, jmean->valuestring, sizeof(c->meaning) - 1);
        if (jex && jex->valuestring) strncpy(c->example, jex->valuestring, sizeof(c->example) - 1);
        c->interval = jint ? (uint16_t)jint->valueint : 0;
        c->factor = jfac ? (uint16_t)jfac->valueint : 2500;
        c->reps = jreps ? (uint16_t)jreps->valueint : 0;
        c->reviewed = false;

        s_card_count++;
    }

    cJSON_Delete(root);

    if (s_card_count == 0) {
        init_default_deck();
    }

    printf("[VocaVibe Core] Successfully loaded %d Anki cards from flash.\n", s_card_count);
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

int vocavibe_core_request_bt_scan(void)
{
    return vocavibe_core_send_json("bt_scan", NULL);
}

int vocavibe_core_request_bt_connect(const char *mac)
{
    return vocavibe_core_send_json("bt_connect", mac);
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
                const char *name = jname && jname->valuestring ? jname->valuestring : "Unknown Headset";
                const char *mac = jmac && jmac->valuestring ? jmac->valuestring : "--:--:--:--";
                int rssi = jrssi ? jrssi->valueint : -60;
                vocavibe_ui_add_bt_device(name, mac, rssi);
            }
        }
    }
    /* 6. 蓝牙耳机连接状态反馈 */
    else if (strcmp(type, "bt_status") == 0) {
        cJSON *jconn = cJSON_GetObjectItem(root, "connected");
        cJSON *jname = cJSON_GetObjectItem(root, "name");
        bool conn = jconn ? cJSON_IsTrue(jconn) : false;
        const char *name = jname && jname->valuestring ? jname->valuestring : "Bluetooth Headset";
        char status_buf[64];
        if (conn) {
            snprintf(status_buf, sizeof(status_buf), "已连接: %s (代理中转)", name);
        } else {
            snprintf(status_buf, sizeof(status_buf), "未连接蓝牙耳机");
        }
        vocavibe_ui_set_bt_status(status_buf, conn);
    }
    /* 7. 卡组全量同步 (来自 AnkiConnect 或 Skill) */
    else if (strcmp(type, "sync_deck") == 0) {
        cJSON *jcards = cJSON_GetObjectItem(root, "cards");
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

                c->id = jid ? (uint32_t)jid->valueint : (s_card_count + 1);
                if (jword && jword->valuestring) strncpy(c->word, jword->valuestring, sizeof(c->word) - 1);
                if (jpho && jpho->valuestring) strncpy(c->phonetic, jpho->valuestring, sizeof(c->phonetic) - 1);
                if (jmean && jmean->valuestring) strncpy(c->meaning, jmean->valuestring, sizeof(c->meaning) - 1);
                if (jex && jex->valuestring) strncpy(c->example, jex->valuestring, sizeof(c->example) - 1);
                c->interval = jint ? (uint16_t)jint->valueint : 0;
                c->factor = jfac ? (uint16_t)jfac->valueint : 2500;
                c->reviewed = false;
                s_card_count++;
            }
            vocavibe_deck_flush();
            s_current_index = 0;
            s_card_showing_back = false;
            vocavibe_ui_show_card(vocavibe_deck_get_current_card(), false, 1, s_card_count);
            vocavibe_ui_update_dashboard(vocavibe_deck_get_total_count(),
                                         vocavibe_deck_get_due_count(),
                                         vocavibe_deck_get_reviewed_count());
            vocavibe_ui_set_sync_status("AnkiConnect: 同步完成");
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

void vocavibe_core_poll(void)
{
    /* 轮询或保持心跳 */
}
