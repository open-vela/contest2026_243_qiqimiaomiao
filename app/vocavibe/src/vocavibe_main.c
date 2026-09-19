/****************************************************************************
 * contest2026_243_qiqimiaomiao/app/vocavibe/src/vocavibe_main.c
 *
 * VocaVibe 参赛主程序：统一端云交互入口、UI 初始化与主线程事件分发
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <nuttx/input/buttons.h>

#include "vocavibe_core.h"
#include "vocavibe_ui.h"
#include "vocavibe_touch.h"

static volatile bool s_app_running = true;
static pthread_t s_serial_tid;

static void show_banner(void)
{
    printf("\n======================================================\n");
    printf("  🚀 VocaVibe (随声记) - 2026 OpenVela AI 硬件开发者大赛\n");
    printf("  参赛团队: Team 243 (qiqimiaomiao)\n");
    printf("  核心功能: Anki SM-2 记忆算法 + 小智声波动效 + MiMo 2.5\n");
    printf("======================================================\n\n");
    fflush(stdout);
}

static void *serial_reader_thread(void *arg)
{
    (void)arg;
    char line[512];
    while (s_app_running) {
        if (!fgets(line, sizeof(line), stdin)) {
            clearerr(stdin);
            usleep(500000);
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
        char *json_start = strchr(p, '{');
        if (json_start != NULL) {
            char *json_end = strrchr(json_start, '}');
            if (json_end != NULL) {
                *(json_end + 1) = '\0';
                printf("[VocaVibe] 接收到串口 JSON 报文: %s\n", json_start);
                vocavibe_core_handle_line(json_start);
                continue;
            }
        }

        /* 命令行调试指令 */
        if (strcmp(p, "help") == 0) {
            printf("\n[VocaVibe 终端指令集]\n");
            printf("  status         - 查看卡组统计与当前学习状态\n");
            printf("  touch          - 诊断与测试触摸屏硬件状态\n");
            printf("  flip           - 翻转当前卡片正面/背面\n");
            printf("  answer <1..4>  - Anki 评分 (1:重来, 2:困难, 3:良好, 4:简单)\n");
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
        } else if (strcmp(p, "touch") == 0) {
            vocavibe_touch_diagnose();
        } else if (strcmp(p, "flip") == 0) {
            vocavibe_ui_show_card(vocavibe_deck_get_current_card(), true,
                                 vocavibe_deck_get_current_index() + 1,
                                 vocavibe_deck_get_total_count());
        } else if (strncmp(p, "answer ", 7) == 0) {
            int rating = atoi(p + 7);
            if (rating >= 1 && rating <= 4) {
                vocavibe_deck_answer_card((anki_rating_t)rating);
            } else {
                printf("评分范围: 1~4 (1:重来 2:困难 3:良好 4:简单)\n");
            }
        } else if (strcmp(p, "next") == 0) {
            vocavibe_deck_answer_card(ANKI_RATING_GOOD);
        } else if (strncmp(p, "ai ", 3) == 0) {
            vocavibe_core_request_ai(p + 3);
        } else if (strcmp(p, "exit") == 0 || strcmp(p, "quit") == 0) {
            printf("[VocaVibe] 退出应用程序\n");
            s_app_running = false;
            break;
        } else {
            printf("未知指令 '%s'，输入 'help' 查看帮助\n", p);
        }
        fflush(stdout);
    }
    return NULL;
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
        .on_net_connect = (vocavibe_ui_net_connect_cb_t)vocavibe_core_request_net_connect,
    };

    /* 3. 在主线程中初始化 UI 与触摸屏驱动 (保证 TLS 隔离环境一致) */
    ret = vocavibe_ui_init(&cbs);
    if (ret == 0) {
        vocavibe_ui_update_dashboard(vocavibe_deck_get_total_count(),
                                     vocavibe_deck_get_due_count(),
                                     vocavibe_deck_get_reviewed_count());
        vocavibe_ui_show_card(vocavibe_deck_get_current_card(), false, 1, vocavibe_deck_get_total_count());
    }

    /* 4. 启动后台串口指令监听线程 */
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 4096);
    pthread_create(&s_serial_tid, &attr, serial_reader_thread, NULL);
    pthread_attr_destroy(&attr);

    printf("[VocaVibe] 进入主事件轮询循环，触控与渲染已就绪\n");
    fflush(stdout);

    /* 5. 主线程 UI 循环：负责图形渲染、触摸事件实时派发与物理按键响应 */
    int btn_fd = open("/dev/buttons", O_RDONLY | O_NONBLOCK);
    btn_buttonset_t idle_buttons = 0;
    btn_buttonset_t last_buttons = 0;
    if (btn_fd >= 0) {
        printf("[VocaVibe] 成功启用 /dev/buttons 物理按键监听 (KEY 功能键支持菜单轮换)\n");
        /* 采样初始空闲电平，自动校准按键极性并消除开机误触发 */
        usleep(10000);
        read(btn_fd, &idle_buttons, sizeof(btn_buttonset_t));
        last_buttons = idle_buttons;
    } else {
        printf("[VocaVibe] 提示: 未检测到 /dev/buttons 或打开失败 (errno=%d)\n", errno);
    }

    uint32_t last_btn_tick = 0;
    while (s_app_running) {
        if (btn_fd >= 0) {
            btn_buttonset_t current_buttons = 0;
            ssize_t nbytes = read(btn_fd, &current_buttons, sizeof(btn_buttonset_t));
            if (nbytes == sizeof(btn_buttonset_t)) {
                /* 当电平偏离空闲电平时判定为按下，结合 200ms 防抖 */
                if (current_buttons != idle_buttons && last_buttons == idle_buttons) {
                    struct timespec ts;
                    clock_gettime(CLOCK_MONOTONIC, &ts);
                    uint32_t now_ms = (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
                    if (now_ms - last_btn_tick > 200) {
                        last_btn_tick = now_ms;
                        int cur = vocavibe_ui_get_current_page();
                        int next_p = (cur + 1) % 4;
                        printf("[VocaVibe Key] KEY 物理按键触发 -> 切换到页面 %d\n", next_p);
                        vocavibe_ui_switch_page(next_p);
                    }
                }
                last_buttons = current_buttons;
            }
        }

        vocavibe_ui_poll();
    }

    if (btn_fd >= 0) {
        close(btn_fd);
    }

    pthread_join(s_serial_tid, NULL);
    vocavibe_core_deinit();
    return 0;
}
