/****************************************************************************
 * contest2026_243_qiqimiaomiao/app/vocavibe/include/vocavibe_bt.h
 *
 * VocaVibe BLE 通信与 Nordic UART Service (NUS) 接口
 ****************************************************************************/

#ifndef __VOCAVIBE_BT_H
#define __VOCAVIBE_BT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 接收数据回调函数类型 */
typedef void (*vocavibe_bt_recv_cb_t)(const uint8_t *data, uint16_t len, void *user_data);

/* 连接状态改变回调类型 (connected: true 为已连接, false 为已断开) */
typedef void (*vocavibe_bt_conn_cb_t)(bool connected, void *user_data);

/* BLE 配置参数 */
typedef struct {
    vocavibe_bt_recv_cb_t recv_cb;
    vocavibe_bt_conn_cb_t conn_cb;
    void *user_data;
} vocavibe_bt_config_t;

/* 初始化并启动 VocaVibe BLE 服务与广播 */
int vocavibe_bt_init(const vocavibe_bt_config_t *config);

/* 停止 BLE 服务并释放资源 */
int vocavibe_bt_deinit(void);

/* 查询当前是否已有客户端连接 */
bool vocavibe_bt_is_connected(void);

/* 查询当前是否已开启 Notify 订阅 */
bool vocavibe_bt_is_notify_enabled(void);

/* 向连接的客户端（手机/微信小程序等）发送 Notify 数据 */
int vocavibe_bt_send(const uint8_t *data, uint16_t len);

/* 发送以 null 结尾的字符串 */
int vocavibe_bt_send_str(const char *str);

/* 获取当前协商的 MTU */
uint16_t vocavibe_bt_get_mtu(void);

#ifdef __cplusplus
}
#endif

#endif /* __VOCAVIBE_BT_H */
