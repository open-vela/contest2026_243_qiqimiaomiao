#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vocavibe_core.h"
#include "vocavibe_bt.h"
#include <cJSON.h>

/* BLE 接收到手机端数据时的核心协议解析 */
static void vocavibe_core_on_rx(const uint8_t *data, uint16_t len, void *user_data)
{
    (void)user_data;
    
    /* 防御性拷贝，确保字符串有 \0 结尾以供 cJSON 解析 */
    char *json_str = (char *)malloc(len + 1);
    if (!json_str) return;
    memcpy(json_str, data, len);
    json_str[len] = '\0';
    
    /* (调试信息) 打印原始报文 */
    printf("\n[Core RX] %s\n", json_str);

    /* 解析 JSON */
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        printf("[Core Error] Invalid JSON received or parse failed.\n");
        free(json_str);
        return;
    }

    cJSON *type_item = cJSON_GetObjectItem(root, "type");
    cJSON *data_item = cJSON_GetObjectItem(root, "data");

    if (cJSON_IsString(type_item) && cJSON_IsString(data_item)) {
        const char *type = type_item->valuestring;
        const char *val = data_item->valuestring;

        if (strcmp(type, "chat") == 0) {
            printf("[AI 语音字幕] >>> %s\n", val);
            /* TODO: 下一步开发 UI 时，在这里触发 lv_async_call 调用 vocavibe_ui_show_subtitle(val) */
        } else if (strcmp(type, "action") == 0) {
            printf("[AI 动作指令] >>> 状态切换为: %s\n", val);
            /* TODO: 在这里触发 lv_async_call 切换动态表情，例如 "thinking", "listening", "smile" */
        } else {
            printf("[Core Warning] Unknown JSON type: %s\n", type);
        }
    } else {
        printf("[Core Error] JSON packet missing 'type' or 'data' string fields.\n");
    }

    cJSON_Delete(root);
    free(json_str);
}

/* BLE 连接状态变化 */
static void vocavibe_core_on_conn(bool connected, void *user_data)
{
    (void)user_data;
    if (connected) {
        printf("\n[Core] Phone Gateway Connected! Synchronizing state...\n");
        /* 连上后主动上报就绪状态 */
        vocavibe_core_send_json("status", "ready");
    } else {
        printf("\n[Core] Phone Gateway Disconnected. Waiting for reconnect...\n");
    }
}

int vocavibe_core_init(void)
{
    vocavibe_bt_config_t config = {
        .recv_cb = vocavibe_core_on_rx,
        .conn_cb = vocavibe_core_on_conn,
        .user_data = NULL,
    };
    return vocavibe_bt_init(&config);
}

int vocavibe_core_deinit(void)
{
    return vocavibe_bt_deinit();
}

int vocavibe_core_send_json(const char *type, const char *data)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return -1;
    
    cJSON_AddStringToObject(root, "type", type);
    cJSON_AddStringToObject(root, "data", data);
    
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (!json_str) return -1;
    
    int ret = vocavibe_bt_send_str(json_str);
    
    free(json_str);
    return ret;
}
