/****************************************************************************
 * contest2026_243_qiqimiaomiao/app/vocavibe/src/vocavibe_touch.c
 *
 * VocaVibe (随声记) - 硬件级双模电容触控直驱引擎
 * 针对 SF32LB52-DevKit-LCD FT6146 触控芯片
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <nuttx/i2c/i2c_master.h>
#include <nuttx/input/touchscreen.h>

#include "vocavibe_touch.h"
#include "lvgl.h"

#define FT6146_I2C_ADDR       0x38
#define FT6146_I2C_FREQ       400000
#define FT6146_REG_TD_STATUS  0x02
#define FT6146_REG_ID_H       0xa3
#define FT6146_REG_ID_L       0x9f

static int s_i2c_fd = -1;
static int s_input_fd = -1;
static lv_indev_t *s_indev = NULL;
static lv_indev_state_t s_last_state = LV_INDEV_STATE_RELEASED;
static int16_t s_last_x = 0;
static int16_t s_last_y = 0;

static int ft6146_i2c_read(uint8_t reg, uint8_t *buf, int len)
{
  if (s_i2c_fd < 0)
    {
      return -1;
    }

  struct i2c_msg_s msgs[2];
  struct i2c_transfer_s xfer;

  msgs[0].addr = FT6146_I2C_ADDR;
  msgs[0].flags = I2C_M_NOSTOP;
  msgs[0].buffer = &reg;
  msgs[0].length = 1;
  msgs[0].frequency = FT6146_I2C_FREQ;

  msgs[1].addr = FT6146_I2C_ADDR;
  msgs[1].flags = I2C_M_READ | I2C_M_NOSTART;
  msgs[1].buffer = buf;
  msgs[1].length = len;
  msgs[1].frequency = FT6146_I2C_FREQ;

  xfer.msgv = msgs;
  xfer.msgc = 2;

  return ioctl(s_i2c_fd, I2CIOC_TRANSFER, (unsigned long)(uintptr_t)&xfer);
}

void vocavibe_touch_diagnose(void)
{
  printf("\n--- [VocaVibe Touch 诊断] ---\n");
  printf("  /dev/input0 fd: %d\n", s_input_fd);
  printf("  /dev/i2c0   fd: %d\n", s_i2c_fd);

  if (s_i2c_fd >= 0)
    {
      uint8_t id_h = 0, id_l = 0, status = 0;
      int ret_h = ft6146_i2c_read(FT6146_REG_ID_H, &id_h, 1);
      int ret_l = ft6146_i2c_read(FT6146_REG_ID_L, &id_l, 1);
      int ret_s = ft6146_i2c_read(FT6146_REG_TD_STATUS, &status, 1);

      printf("  FT6146 I2C 读取测试 (addr 0x%02x):\n", FT6146_I2C_ADDR);
      printf("    ID_H (reg 0x%02x): 0x%02x (ret=%d)\n", FT6146_REG_ID_H, id_h, ret_h);
      printf("    ID_L (reg 0x%02x): 0x%02x (ret=%d)\n", FT6146_REG_ID_L, id_l, ret_l);
      printf("    STATUS (reg 0x%02x): 0x%02x (ret=%d, 点数=%d)\n",
             FT6146_REG_TD_STATUS, status, ret_s, status & 0x0f);
    }
  else
    {
      printf("  [警告] /dev/i2c0 未就绪\n");
    }
  printf("-----------------------------\n\n");
}

static void vocavibe_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
  (void)indev;
  data->continue_reading = false;

  bool got_event = false;

  /* 1. 优先非阻塞轮询内核中断队列 /dev/input0 (单次循环将排队样本处理到最新) */
  if (s_input_fd >= 0)
    {
      struct touch_sample_s sample;
      while (read(s_input_fd, &sample, sizeof(sample)) == (int)sizeof(sample))
        {
          if (sample.npoints > 0)
            {
              got_event = true;
              if (sample.point[0].flags & (TOUCH_DOWN | TOUCH_MOVE))
                {
                  s_last_state = LV_INDEV_STATE_PRESSED;
                  s_last_x = sample.point[0].x;
                  s_last_y = sample.point[0].y;
                }
              else if (sample.point[0].flags & TOUCH_UP)
                {
                  s_last_state = LV_INDEV_STATE_RELEASED;
                }
            }
        }
    }

  /* 2. 硬件级双模校验：
   * 若状态处于 PRESSED 但本次未读到新 input0 样本，或 input0 未就绪，
   * 立即通过 I2C 直读 FT6146 状态寄存器确认物理接触状态。
   * 彻底杜绝因中断窄脉冲偶发丢包导致 LVGL 误以为屏幕持续被按下的假死问题！
   */
  if (s_i2c_fd >= 0 && (!got_event || s_input_fd < 0))
    {
      if (s_last_state == LV_INDEV_STATE_PRESSED || s_input_fd < 0)
        {
          uint8_t buf[5] = {0};
          int ret = ft6146_i2c_read(FT6146_REG_TD_STATUS, buf, sizeof(buf));
          if (ret >= 0)
            {
              uint8_t touch_num = buf[0] & 0x0f;
              if (touch_num > 0)
                {
                  s_last_state = LV_INDEV_STATE_PRESSED;
                  s_last_x = ((uint16_t)(buf[1] & 0x0f) << 8) | buf[2];
                  s_last_y = ((uint16_t)(buf[3] & 0x0f) << 8) | buf[4];
                }
              else
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
}

lv_indev_t *vocavibe_touch_init(lv_display_t *disp)
{
  printf("[VocaVibe Touch] 正在初始化 FT6146 双模触控引擎...\n");

  /* 1. 打开内核字符设备 input0 (非阻塞，支持等候异步板级驱动就绪) */
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
      printf("[VocaVibe Touch] /dev/input0 暂未就绪 (errno=%d)，启用 I2C 直连与热重连\n", errno);
    }

  /* 2. 打开底层 I2C0 总线用于硬件直通兜底 */
  s_i2c_fd = open("/dev/i2c0", O_RDWR | O_CLOEXEC);
  if (s_i2c_fd >= 0)
    {
      printf("[VocaVibe Touch] /dev/i2c0 已连接 (fd=%d)\n", s_i2c_fd);
      vocavibe_touch_diagnose();
    }
  else
    {
      printf("[VocaVibe Touch] /dev/i2c0 打开失败 (errno=%d)\n", errno);
    }

  /* 3. 注册 LVGL 指针输入设备 */
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

  printf("[VocaVibe Touch] 触控引擎初始化成功，LVGL indev=0x%p\n", s_indev);
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
  if (s_i2c_fd >= 0)
    {
      close(s_i2c_fd);
      s_i2c_fd = -1;
    }
  printf("[VocaVibe Touch] 触控引擎已安全卸载\n");
}
