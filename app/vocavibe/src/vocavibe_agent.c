/*
 * VocaVibe AI Agent Engine (Native OpenVela ai_agent Framework Integration)
 *
 * 100% 直接链接与调用 OpenVela 官方 packages/ai_agent 原生组件：
 * - 官方会话管理: core/session_mgr.h (session_mgr_init, session_append, session_get_history_json, session_clear)
 * - 官方技能加载: tools/skill_loader.h (skill_loader_init, skill_loader_build_summary)
 * - 官方上下文构建: core/context_builder.h (context_build_system_prompt, context_build_messages)
 * - 官方标准配置: agent_config.h
 */

#include "vocavibe_agent.h"
#include "vocavibe_core.h"
#include "vocavibe_ui.h"

/* 官方 packages/ai_agent 原生头文件 */
#include "core/session_mgr.h"
#include "tools/skill_loader.h"
#include "core/context_builder.h"
#include "agent_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

#include <sys/stat.h>

#define TAG "[OpenVela ai_agent]"
#define CHAT_ID "chat"

static int s_req_seq = 1;
static char s_pending_user_query[128] = {0};

/* 避免占用线程栈空间，使用静态工作缓冲区 */
static char s_hist_json[2048];
static char s_msgs_str[4096];
static char s_sys_prompt[2048];

static void ensure_official_dirs(void)
{
    mkdir("/data", 0777);
    mkdir("/data/agent", 0777);
    mkdir("/data/agent/sessions", 0777);
    mkdir("/data/agent/skills", 0777);
    mkdir("/data/agent/config", 0777);
    mkdir("/data/ai_agent", 0777);
    mkdir("/data/ai_agent/skills", 0777);
    mkdir("/data/ai_agent/sessions", 0777);
    mkdir("/data/ai_agent/config", 0777);
}

int vocavibe_agent_init(void)
{
    printf("%s 正在初始化 OpenVela 官方原生 AI Agent 核心框架...\n", TAG);

    /* 确保官方目录就绪 */
    ensure_official_dirs();

    /* 1. 初始化官方会话持久化管理器 (自动管理 /data/agent/sessions/) */
    session_mgr_init();

    /* 2. 初始化官方技能系统 (自动扫描并就绪 /data/agent/skills/) */
    skill_loader_init();

    s_pending_user_query[0] = '\0';
    printf("%s 官方原生 ai_agent 框架初始化完成 (会话上限: %d 条消息)\n", 
           TAG, AGENT_SESSION_MAX_MSGS);
    return 0;
}

void vocavibe_agent_clear_history(void)
{
    /* 直接调用官方会话清除 API */
    session_clear(CHAT_ID);
    s_pending_user_query[0] = '\0';
    printf("%s 已调用官方 session_clear(\"%s\") 清空持久化会话记录\n", TAG, CHAT_ID);
}

/* 核心推理请求：使用官方 context_builder 与 session_mgr 驱动 */
int vocavibe_agent_ask(const char *query)
{
    if (!query || strlen(query) == 0) {
        return -1;
    }

    printf("%s 收到端侧推理请求: '%s'，正在由官方 context_builder 装配...\n", TAG, query);

    strncpy(s_pending_user_query, query, sizeof(s_pending_user_query) - 1);
    s_pending_user_query[sizeof(s_pending_user_query) - 1] = '\0';

    /* 1. 从官方 session_mgr 读取持久化历史 JSON (最多 AGENT_SESSION_MAX_MSGS 条) */
    memset(s_hist_json, 0, sizeof(s_hist_json));
    session_get_history_json(CHAT_ID, s_hist_json, sizeof(s_hist_json), AGENT_SESSION_MAX_MSGS);

    /* 2. 由官方 context_builder 组装消息列表 (messages) */
    memset(s_msgs_str, 0, sizeof(s_msgs_str));
    if (context_build_messages(s_hist_json, query, s_msgs_str, sizeof(s_msgs_str)) != 0 || !s_msgs_str[0]) {
        printf("%s 官方 context_build_messages 失败\n", TAG);
        return -1;
    }

    /* 3. 由官方 context_builder 装配官方 System Prompt (读取 SOUL.md + 技能摘要) */
    memset(s_sys_prompt, 0, sizeof(s_sys_prompt));
    if (context_build_system_prompt(s_sys_prompt, sizeof(s_sys_prompt)) != 0 || !s_sys_prompt[0]) {
        /* 若无文件则使用官方默认人设保底 */
        snprintf(s_sys_prompt, sizeof(s_sys_prompt),
                 "你是 OpenVela 智能硬件背单词终端的专属双语助教。\n"
                 "核心准则：回答必须简明扼要、生动地道，严格控制在 80 字以内。\n"
                 "支持技能：[Anki记忆复习]、[口语助教互动]、[词汇拓展辨析]。");
    }

    /* 4. 组合标准 OpenAI / MiMo 2.5 API 请求 JSON 体 */
    cJSON *req_body = cJSON_CreateObject();
    cJSON_AddStringToObject(req_body, "model", VOCAVIBE_AGENT_MODEL_DEFAULT);
    cJSON_AddNumberToObject(req_body, "max_tokens", 512);

    /* 解析官方 context_builder 产出的 messages 数组并插入 System Prompt */
    cJSON *parsed_msgs = cJSON_Parse(s_msgs_str);
    if (!parsed_msgs) {
        parsed_msgs = cJSON_CreateArray();
    }

    /* 将官方系统提示词作为首条消息插入 */
    cJSON *sys_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_obj, "role", "system");
    cJSON_AddStringToObject(sys_obj, "content", s_sys_prompt);
    cJSON_InsertItemInArray(parsed_msgs, 0, sys_obj);

    cJSON_AddItemToObject(req_body, "messages", parsed_msgs);

    char *body_str = cJSON_PrintUnformatted(req_body);
    cJSON_Delete(req_body);

    if (!body_str) {
        printf("%s 内存不足，生成请求体失败\n", TAG);
        return -1;
    }

    /* 5. 封装并通过串口透明网络代理发给 PC 伴侣网关转发 */
    int cur_id = s_req_seq++;
    cJSON *proxy_frame = cJSON_CreateObject();
    cJSON_AddStringToObject(proxy_frame, "type", "http_req");
    cJSON_AddNumberToObject(proxy_frame, "id", cur_id);
    cJSON_AddStringToObject(proxy_frame, "method", "POST");
    cJSON_AddStringToObject(proxy_frame, "url", VOCAVIBE_AGENT_API_URL);

    cJSON *headers = cJSON_CreateObject();
    char auth_val[128];
    snprintf(auth_val, sizeof(auth_val), "Bearer %s", VOCAVIBE_AGENT_API_KEY);
    cJSON_AddStringToObject(headers, "Authorization", auth_val);
    cJSON_AddStringToObject(headers, "Content-Type", "application/json");
    cJSON_AddItemToObject(proxy_frame, "headers", headers);

    cJSON_AddStringToObject(proxy_frame, "body", body_str);
    free(body_str);

    char *frame_str = cJSON_PrintUnformatted(proxy_frame);
    if (frame_str) {
        printf("[JSON] %s\n", frame_str);
        fflush(stdout);
        free(frame_str);
    }
    cJSON_Delete(proxy_frame);

    /* 切换 UI 状态为 AI 思考中 */
    vocavibe_ui_switch_page(2);
    vocavibe_ui_set_ai_state(AI_STATE_THINKING);
    vocavibe_ui_set_ai_chat(query, "正在思考中...");

    return 0;
}

/* 核心响应解析：使用官方 session_mgr 原生写入持久化记忆 */
int vocavibe_agent_handle_http_resp(int req_id, int status, const char *resp_body)
{
    printf("%s 收到网络代理回传响应: id=%d, status=%d\n", TAG, req_id, status);

    if (status != 200 || !resp_body || strlen(resp_body) == 0) {
        char err_msg[64];
        snprintf(err_msg, sizeof(err_msg), "网络响应异常 (HTTP %d)", status);
        vocavibe_ui_set_ai_chat(s_pending_user_query, err_msg);
        vocavibe_ui_set_ai_state(AI_STATE_IDLE);
        return -1;
    }

    cJSON *root = cJSON_Parse(resp_body);
    if (!root) {
        printf("%s 无法解析云端返回的 JSON 数据\n", TAG);
        vocavibe_ui_set_ai_chat(s_pending_user_query, "解析回答失败");
        vocavibe_ui_set_ai_state(AI_STATE_IDLE);
        return -1;
    }

    const char *ai_reply = NULL;
    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
        if (first_choice) {
            cJSON *msg = cJSON_GetObjectItem(first_choice, "message");
            if (msg) {
                cJSON *content = cJSON_GetObjectItem(msg, "content");
                if (content && content->valuestring) {
                    ai_reply = content->valuestring;
                }
            }
        }
    }

    if (ai_reply && strlen(ai_reply) > 0) {
        printf("%s 成功解析大模型答复: '%s'\n", TAG, ai_reply);

        /* 1. 直接调用官方 session_append() 持久化至 /data/agent/sessions/ */
        session_append(CHAT_ID, "user", s_pending_user_query);
        session_append(CHAT_ID, "assistant", ai_reply);

        /* 2. 更新端侧 UI 显示并转入播报态 */
        vocavibe_ui_set_ai_chat(s_pending_user_query, ai_reply);
        vocavibe_ui_set_ai_state(AI_STATE_SPEAKING);

        /* 3. 指令外设播放小智台湾腔 TTS 语音 */
        vocavibe_core_request_tts(ai_reply);
    } else {
        vocavibe_ui_set_ai_chat(s_pending_user_query, "未获取到有效回答");
        vocavibe_ui_set_ai_state(AI_STATE_IDLE);
    }

    cJSON_Delete(root);
    return 0;
}
