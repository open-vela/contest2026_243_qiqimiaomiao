/*
 * VocaVibe AI Agent Engine (MCU-Side Agent Core)
 *
 * 运行于 SF32LB52 MCU 端侧的智能体决策核心。
 * 负责：
 * 1. 意图决策与技能匹配 (Skills)
 * 2. 多轮对话上下文 (5-Round History Memory)
 * 3. 构造完整的大模型 API 请求帧 (MiMo 2.5 REST Payload)
 * 4. 通过 MCU 网络代理客户端调用云端推理
 * 5. 解析推理结果并调度端侧 UI 与外设 TTS 播报
 */

#ifndef VOCAVIBE_AGENT_H
#define VOCAVIBE_AGENT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VOCAVIBE_MAX_HISTORY_ROUNDS 10
#define VOCAVIBE_AGENT_MODEL_DEFAULT "mimo-v2.5"
#define VOCAVIBE_AGENT_API_URL "https://token-plan-cn.xiaomimimo.com/v1/chat/completions"
#define VOCAVIBE_AGENT_API_KEY "tp-c2rn3aytnmxcfash8yv6xenmzntatkev0btwhp06540wnhz3"

typedef struct {
    char user[128];
    char assistant[256];
} vocavibe_chat_turn_t;

typedef struct {
    const char *id;
    const char *name;
    const char *desc;
    const char *prompt_guide;
} vocavibe_skill_t;

/* 初始化端侧 Agent 引擎与技能树 */
int vocavibe_agent_init(void);

/* 用户发起对话或 ASR 结果进入 Agent 处理流水线 */
int vocavibe_agent_ask(const char *query);

/* 处理来自网络代理的 HTTP 响应体 (由串口/网络中转层回调) */
int vocavibe_agent_handle_http_resp(int req_id, int status, const char *resp_body);

/* 重置多轮对话历史 */
void vocavibe_agent_clear_history(void);

/* 获取当前加载的技能数量 */
int vocavibe_agent_get_skill_count(void);

/* 获取指定索引技能 */
const vocavibe_skill_t *vocavibe_agent_get_skill(int index);

#ifdef __cplusplus
}
#endif

#endif /* VOCAVIBE_AGENT_H */
