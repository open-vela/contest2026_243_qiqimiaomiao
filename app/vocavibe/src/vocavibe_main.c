/****************************************************************************
 * contest2026_243_qiqimiaomiao/app/vocavibe/src/vocavibe_main.c
 *
 * VocaVibe 主入口与 CLI 交互控制
 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vocavibe.h"

static void show_usage(void)
{
    printf("\n========================================\n");
    printf("   VocaVibe (Team 243) - AI Hardware   \n");
    printf("========================================\n");
    printf("Usage:\n");
    printf("  vocavibe start          Start VocaVibe Core (BLE & Logic)\n");
    printf("  vocavibe stop           Stop VocaVibe Core\n");
    printf("  vocavibe status         Show connection & BLE status\n");
    printf("  vocavibe send <msg>     Simulate user saying <msg> to AI\n");
    printf("========================================\n\n");
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("[VocaVibe] Starting VocaVibe AI Hardware Engine...\n");
        int ret = vocavibe_core_init();
        if (ret < 0) {
            printf("[VocaVibe] Failed to start Core: %d\n", ret);
            return ret;
        }

        printf("[VocaVibe] Core is running. Broadcast name: 'VocaVibe-243'\n");
        printf("[VocaVibe] Use 'vocavibe status' or 'vocavibe send <text>' to interact.\n");
        return 0;
    }

    if (strcmp(argv[1], "start") == 0) {
        return vocavibe_core_init();
    } else if (strcmp(argv[1], "stop") == 0) {
        return vocavibe_core_deinit();
    } else if (strcmp(argv[1], "status") == 0) {
        bool connected = vocavibe_bt_is_connected();
        bool notify = vocavibe_bt_is_notify_enabled();
        uint16_t mtu = vocavibe_bt_get_mtu();

        printf("\n[VocaVibe Status]\n");
        printf("  Device Name   : VocaVibe-243\n");
        printf("  Service UUID  : 6E400001-B5A3-F393-E0A9-E50E24DCCA9E (NUS)\n");
        printf("  Connected     : %s\n", connected ? "YES" : "NO");
        printf("  Notify Active : %s\n", notify ? "YES" : "NO");
        printf("  MTU Size      : %u bytes\n\n", mtu);
        return 0;
    } else if (strcmp(argv[1], "send") == 0) {
        if (argc < 3) {
            printf("Error: Message required. e.g. 'vocavibe send hello'\n");
            return -EINVAL;
        }
        
        /* 封装为 JSON 格式发送: {"type":"query", "data":"..."} */
        int ret = vocavibe_core_send_json("query", argv[2]);
        if (ret < 0) {
            printf("[VocaVibe] Send failed (%d): Is phone connected and Notify enabled?\n", ret);
            return ret;
        }
        printf("[VocaVibe] Successfully requested AI with query: '%s'\n", argv[2]);
        return 0;
    } else {
        show_usage();
        return 0;
    }
}
