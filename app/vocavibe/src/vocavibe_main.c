/****************************************************************************
 * contest2026_243_qiqimiaomiao/app/vocavibe/src/vocavibe_main.c
 *
 * VocaVibe 参赛主程序：统一端云交互入口、UI 初始化与后台协议交互
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "vocavibe_core.h"
#include "vocavibe_ui.h"

static void show_banner(void)
{
    printf("\n======================================================\n");
    printf("  🚀 VocaVibe (随声记) - 2026 OpenVela AI 硬件开发者大赛\n");
    printf("  参赛团队: Team 243 (qiqimiaomiao)\n");
    printf("  核心功能: Anki SM-2 记忆算法 + 小智声波动效 + MiMo 2.5\n");
    printf("======================================================\n\n");
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    show_banner();

    /* 1. 初始化端侧核心逻辑与 Anki 卡组 */
    int ret = vocavibe_core_init();
    if (ret < 0) {
        printf("[VocaVibe] 核心层初始化失败: %d\n", ret);
        return ret;
    }

    /* 2. 绑定 UI 回调函数 */
    vocavibe_ui_callbacks_t cbs = {
        .on_card_answer = (vocavibe_ui_card_answer_cb_t)vocavibe_deck_answer_card,
        .on_ai_query    = (vocavibe_ui_ai_query_cb_t)vocavibe_core_request_ai,
        .on_sync        = (vocavibe_ui_sync_cb_t)vocavibe_core_request_sync,
        .on_bt_scan     = (vocavibe_ui_bt_scan_cb_t)vocavibe_core_request_bt_scan,
        .on_bt_connect  = (vocavibe_ui_bt_connect_cb_t)vocavibe_core_request_bt_connect,
    };

    /* 3. 启动 4 页面全触控 UI */
    ret = vocavibe_ui_init(&cbs);
    if (ret < 0) {
        printf("[VocaVibe] UI 初始化异常或未检测到显示屏 (继续命令行模式)\n");
    } else {
        /* 填充初始页面数据 */
        vocavibe_ui_update_dashboard(vocavibe_deck_get_total_count(),
                                     vocavibe_deck_get_due_count(),
                                     vocavibe_deck_get_reviewed_count());
        vocavibe_ui_show_card(vocavibe_deck_get_current_card(), false, 1, vocavibe_deck_get_total_count());
    }

    /* 4. 监听串口协议报文与调试交互控制台 */
    printf("[VocaVibe] 正在监听串口中转指令与 JSON 数据流...\n");
    fflush(stdout);

    char line[512];
    while (1) {
        if (!fgets(line, sizeof(line), stdin)) {
            usleep(50000);
            continue;
        }

        /* 过滤换行 */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        size_t len = strlen(p);
        while (len > 0 && (p[len - 1] == '\r' || p[len - 1] == '\n')) {
            p[len - 1] = '\0';
            len--;
        }

        if (len == 0) continue;

        /* 如果收到 JSON 格式报文，交由核心协议层解析 */
        if (p[0] == '{') {
            vocavibe_core_handle_line(p);
            continue;
        }

        /* 命令行调试指令 */
        if (strcmp(p, "help") == 0) {
            printf("\n[VocaVibe 指令集]\n");
            printf("  status         - 查看卡组与系统状态\n");
            printf("  flip           - 翻转当前卡片正面/背面\n");
            printf("  answer <1..4>  - Anki 评分 (1:Again, 2:Hard, 3:Good, 4:Easy)\n");
            printf("  next           - 下一张卡片\n");
            printf("  ai <query>     - 向大模型提问\n");
            printf("  exit / quit    - 退出程序\n\n");
        } else if (strcmp(p, "status") == 0) {
            printf("\n[VocaVibe 状态]\n");
            printf("  卡组总数: %d\n", vocavibe_deck_get_total_count());
            printf("  今日待学: %d\n", vocavibe_deck_get_due_count());
            printf("  今日已学: %d\n", vocavibe_deck_get_reviewed_count());
            const anki_card_t *c = vocavibe_deck_get_current_card();
            if (c) {
                printf("  当前卡片: [%u] %s (间隔:%dd, 难度:%.2f)\n\n",
                       (unsigned int)c->id, c->word, c->interval, c->factor / 1000.0f);
            }
        } else if (strcmp(p, "flip") == 0) {
            vocavibe_ui_show_card(vocavibe_deck_get_current_card(), true,
                                 vocavibe_deck_get_current_index() + 1,
                                 vocavibe_deck_get_total_count());
            printf("[VocaVibe] 已翻转至背面\n");
        } else if (strncmp(p, "answer ", 7) == 0) {
            int rating = atoi(p + 7);
            if (rating >= 1 && rating <= 4) {
                vocavibe_deck_answer_card((anki_rating_t)rating);
            } else {
                printf("评分范围: 1~4\n");
            }
        } else if (strcmp(p, "next") == 0) {
            vocavibe_deck_answer_card(ANKI_RATING_GOOD);
        } else if (strncmp(p, "ai ", 3) == 0) {
            vocavibe_core_request_ai(p + 3);
        } else if (strcmp(p, "exit") == 0 || strcmp(p, "quit") == 0) {
            printf("[VocaVibe] 退出应用程序\n");
            break;
        } else {
            printf("未知指令 '%s'，输入 'help' 查看帮助\n", p);
        }
        fflush(stdout);
    }

    vocavibe_core_deinit();
    return 0;
}
