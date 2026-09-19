/****************************************************************************
 * contest2026_243_qiqimiaomiao/app/vocavibe/src/vocavibe_touch.c
 *
 * VocaVibe (随声记) - 硬件级高性能电容触控直驱引擎
 * 针对 SF32LB52-DevKit-LCD FT6146 触控芯片
 * 具备 35ms 自动抬手监测看门狗，彻底解决硬件无 TOUCH_UP 导致的触控粘连与卡死
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <nuttx/input/touchscreen.h>

#include "vocavibe_touch.h"
#include "lvgl.h"

static int s_input_fd = -1;
static lv_indev_t *s_indev = NULL;
static lv_indev_state_t s_last_state = LV_INDEV_STATE_RELEASED;
static int16_t s_last_x = 0;
static int16_t s_last_y = 0;
static uint32_t s_last_touch_time = 0;

void vocavibe_touch_diagnose(void)
{
  printf("\n--- [VocaVibe Touch 状态诊断] ---\n");
  printf("  /dev/input0 fd: %d\n", s_input_fd);
  printf("  LVGL indev:     %p\n", s_indev);
  printf("  当前触控状态:   %s\n", s_last_state == LV_INDEV_STATE_PRESSED ? "PRESSED (按下)" : "RELEASED (释放)");
  printf("  当前坐标:       (%d, %d)\n", s_last_x, s_last_y);
  printf("  上次接触时间戳: %u ms (当前 tick: %u ms)\n", (unsigned int)s_last_touch_time, (unsigned int)lv_tick_get());
  printf("----------------------------------\n\n");
}

static void vocavibe_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
  (void)indev;
  data->continue_reading = false;

  uint32_t now = lv_tick_get();

  if (s_input_fd >= 0)
    {
      struct touch_sample_s sample;
      int n = read(s_input_fd, &sample, sizeof(sample));
      if (n == (int)sizeof(sample) && sample.npoints > 0)
        {
          if (sample.point[0].flags & (TOUCH_DOWN | TOUCH_MOVE))
            {
              s_last_state = LV_INDEV_STATE_PRESSED;
              s_last_x = sample.point[0].x;
              s_last_y = sample.point[0].y;
              s_last_touch_time = now;
            }
          else if (sample.point[0].flags & TOUCH_UP)
            {
              s_last_state = LV_INDEV_STATE_RELEASED;
            }

          /* 若缓冲区还有待读样本，通知 LVGL 紧接着调用本回调处理 */
          data->continue_reading = true;
        }
      else
        {
          /* 核心机制：FT6146 触控芯片在中断轮询模式下，手指抬起时 INT 保持高电平，
           * 内核驱动不产生任何中断事件，因此队列中永不会有 TOUCH_UP 样本。
           * 这里采用 35ms 自动抬手监测看门狗（FT6146 采样率约 60Hz 即 16.6ms/次，
           * 超过 35ms 无新样本即表明手指已物理脱离屏幕），立即触发 RELEASED 状态，
           * 从而让 LVGL 精准触发 LV_EVENT_CLICKED / LV_EVENT_SHORT_CLICKED。
           */
          if (s_last_state == LV_INDEV_STATE_PRESSED)
            {
              if (now - s_last_touch_time > 35)
                {
                  s_last_state = LV_INDEV_STATE_RELEASED;
                }
            }
        }
    }

  /* 边界限幅 (390 x 450 AMOLED 屏幕) */
  if (s_last_x < 0) s_last_x = 0;
  if (s_last_x > 389) s_last_x = 389;
  if (s_last_y < 0) s_last_y = 0;
  if (s_last_y > 449) s_last_y = 449;

  data->point.x = s_last_x;
  data->point.y = s_last_y;
  data->state = s_last_state;

  static lv_indev_state_t s_prev_reported_state = LV_INDEV_STATE_RELEASED;
  if (data->state != s_prev_reported_state)
    {
      printf("[Touch] 触控状态: %s 坐标=(%d, %d)\n",
             data->state == LV_INDEV_STATE_PRESSED ? "按下 (PRESSED)" : "抬起 (RELEASED)",
             (int)data->point.x, (int)data->point.y);
      s_prev_reported_state = data->state;
    }
}

lv_indev_t *vocavibe_touch_init(lv_display_t *disp)
{
  printf("[VocaVibe Touch] 正在初始化 FT6146 高性能触控引擎...\n");

  /* 打开内核字符设备 input0 (非阻塞，支持等候异步板级驱动就绪) */
  for (int retry = 0; retry < 12; retry++)
    {
      s_input_fd = open("/dev/input0", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
      if (s_input_fd >= 0)
        {
          break;
        }
      usleep(25000);
    }

  if (s_input_fd >= 0)
    {
      printf("[VocaVibe Touch] /dev/input0 内核中断触控已连接 (fd=%d)\n", s_input_fd);
    }
  else
    {
      printf("[VocaVibe Touch] 错误: /dev/input0 打开失败 (errno=%d)\n", errno);
      return NULL;
    }

  /* 注册 LVGL 指针输入设备 */
  s_indev = lv_indev_create();
  if (s_indev == NULL)
    {
      printf("[VocaVibe Touch] 错误: lv_indev_create 失败!\n");
      return NULL;
    }

  lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(s_indev, vocavibe_touch_read_cb);
  if (disp != NULL)
    {
      lv_indev_set_display(s_indev, disp);
    }

  printf("[VocaVibe Touch] 触控引擎初始化成功，LVGL indev=%p\n", s_indev);
  return s_indev;
}

void vocavibe_touch_deinit(void)
{
  if (s_indev != NULL)
    {
      lv_indev_delete(s_indev);
      s_indev = NULL;
    }
  if (s_input_fd >= 0)
    {
      close(s_input_fd);
      s_input_fd = -1;
    }
  printf("[VocaVibe Touch] 触控引擎已安全卸载\n");
}
