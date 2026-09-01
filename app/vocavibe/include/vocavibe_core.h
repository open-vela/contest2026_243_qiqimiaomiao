/****************************************************************************
 * contest2026_243_qiqimiaomiao/app/vocavibe/include/vocavibe_core.h
 *
 * VocaVibe 核心业务逻辑（JSON协议解析与状态管理）
 ****************************************************************************/

#ifndef __INCLUDE_VOCAVIBE_CORE_H
#define __INCLUDE_VOCAVIBE_CORE_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 初始化 VocaVibe 核心逻辑 (并启动底层的 BLE 模块)
 * @return 0 成功，其他值为错误码
 */
int vocavibe_core_init(void);

/**
 * @brief 停止 VocaVibe 核心逻辑
 * @return 0 成功
 */
int vocavibe_core_deinit(void);

/**
 * @brief 向手机网关发送组装好的 JSON 请求
 * @param type 消息类型 (如 "query", "status")
 * @param data 消息内容 (如 "北京天气", "ready")
 * @return 发送的字节数，<0为错误
 */
int vocavibe_core_send_json(const char *type, const char *data);

#endif /* __INCLUDE_VOCAVIBE_CORE_H */
