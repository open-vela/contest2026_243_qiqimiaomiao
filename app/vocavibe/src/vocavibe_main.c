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

/* BLE 接收到手机端数据时的回调处理 */
static void on_ble_rx_data(const uint8_t *data, uint16_t len, void *user_data)
{
    (void)user_data;
    char buf[256];
    uint16_t print_len = (len < sizeof(buf) - 1) ? len : (sizeof(buf) - 1);
    memcpy(buf, data, print_len);
    buf[print_len] = '\0';

    printf("\n[VocaVibe RX] (%u bytes): %s\n", len, buf);

    /* 自动回复 ACK 给手机端 */
    char ack[300];
    snprintf(ack, sizeof(ack), "ACK: Received %u bytes", len);
    vocavibe_bt_send_str(ack);
}

/* BLE 连接状态变化回调 */
static void on_ble_conn_changed(bool connected, void *user_data)
{
    (void)user_data;
    if (connected) {
        printf("\n[VocaVibe] >>> Phone Connected! Ready for AI Voice & Data Exchange <<<\n");
    } else {
        printf("\n[VocaVibe] >>> Phone Disconnected. Broadcasting restarted <<<\n");
    }
}

static void show_usage(void)
{
    printf("\n========================================\n");
    printf("   VocaVibe (Team 243) - AI Hardware   \n");
    printf("========================================\n");
    printf("Usage:\n");
    printf("  vocavibe start          Start BLE Service & Advertising\n");
    printf("  vocavibe stop           Stop BLE Service\n");
    printf("  vocavibe status         Show connection & BLE status\n");
    printf("  vocavibe send <msg>     Send text message to connected phone\n");
    printf("========================================\n\n");
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        /* 默认无参数时直接启动 BLE 服务 */
        vocavibe_bt_config_t config = {
            .recv_cb = on_ble_rx_data,
            .conn_cb = on_ble_conn_changed,
            .user_data = NULL,
        };

        printf("[VocaVibe] Starting VocaVibe AI Hardware Engine...\n");
        int ret = vocavibe_bt_init(&config);
        if (ret < 0) {
            printf("[VocaVibe] Failed to start BLE: %d\n", ret);
            return ret;
        }

        printf("[VocaVibe] Service is running. Broadcast name: 'VocaVibe-243'\n");
        printf("[VocaVibe] Use 'vocavibe status' or 'vocavibe send <text>' to interact.\n");
        return 0;
    }

    if (strcmp(argv[1], "start") == 0) {
        vocavibe_bt_config_t config = {
            .recv_cb = on_ble_rx_data,
            .conn_cb = on_ble_conn_changed,
            .user_data = NULL,
        };
        return vocavibe_bt_init(&config);
    } else if (strcmp(argv[1], "stop") == 0) {
        return vocavibe_bt_deinit();
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
        int ret = vocavibe_bt_send_str(argv[2]);
        if (ret < 0) {
            printf("[VocaVibe] Send failed (%d): Is phone connected and Notify enabled?\n", ret);
            return ret;
        }
        printf("[VocaVibe] Sent: '%s' (%d bytes)\n", argv[2], ret);
        return 0;
    } else {
        show_usage();
        return 0;
    }
}
