/****************************************************************************
 * contest2026_243_qiqimiaomiao/app/vocavibe/include/vocavibe_touch.h
 *
 * VocaVibe (随声记) - 硬件级电容触控直驱与诊断引擎
 * 针对 SF32LB52-DevKit-LCD FT6146 触控芯片
 ****************************************************************************/

#ifndef __VOCAVIBE_TOUCH_H
#define __VOCAVIBE_TOUCH_H

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 VocaVibe 双模触控引擎
 * 自动探测 FT6146 I2C 总线，绑定 LVGL pointer 输入设备
 *
 * @param disp 关联的 LVGL 显示器指针
 * @return lv_indev_t* 成功返回创建的 indev，失败返回 NULL
 */
lv_indev_t *vocavibe_touch_init(lv_display_t *disp);

/**
 * @brief 释放触控引擎资源
 */
void vocavibe_touch_deinit(void);

/**
 * @brief 触控健康诊断测试（在终端输出寄存器及硬件探针信息）
 */
void vocavibe_touch_diagnose(void);

#ifdef __cplusplus
}
#endif

#endif /* __VOCAVIBE_TOUCH_H */
