/****************************************************************************
 * contest2026_243_qiqimiaomiao/app/vocavibe/src/vocavibe_boot.c
 *
 * VocaVibe 开机自启动守护与引导入口
 *
 * 核心设计:
 *   作为 CONFIG_INIT_ENTRYPOINT 入口，硬件复位/上电后由 NuttX 启动内核线程直接调用。
 *   1. 执行 boardctl(BOARDIOC_INIT, 0) 确保板级驱动（/data, LCD, Touch, Buttons 等）就绪。
 *   2. 创建后台独立 task 运行 nsh_consolemain，保留完整串口交互与调试 NSH 终端能力。
 *   3. 前台主线程直接调用 vocavibe_main(argc, argv)，无需任何终端输入直接点亮屏幕呈现 UI。
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <errno.h>
#include <sys/boardctl.h>

#include <nshlib/nshlib.h>

/* 外部函数声明 */
extern int vocavibe_main(int argc, char *argv[]);
extern int nsh_consolemain(int argc, char *argv[]);

static int nsh_background_task(int argc, char *argv[])
{
  return nsh_consolemain(argc, argv);
}

/****************************************************************************
 * Name: vocavibe_boot_entry
 *
 * Description:
 *   VocaVibe 自启动引导主入口。
 ****************************************************************************/

int vocavibe_boot_entry(int argc, char *argv[])
{
  printf("\n[VocaVibe Boot] 系统复位/上电启动，开始执行自启动引导...\n");
  fflush(stdout);

#ifdef CONFIG_BOARDCTL
  /* 1. 板级初始化（如果尚未执行） */
  boardctl(BOARDIOC_INIT, 0);
#endif

#ifdef CONFIG_SYSTEM_NSH
  /* 2. 初始化 NSH 基础环境（包括网络及必要系统服务） */
  nsh_initialize();
#endif

  /* 2. 启动后台 NSH 终端任务，保障串口调试与网关交互不中断 */
#ifdef CONFIG_NSH_CONSOLE
  int nsh_pid = task_create("nsh_console",
                            CONFIG_SYSTEM_NSH_PRIORITY,
                            CONFIG_INIT_STACKSIZE,
                            nsh_background_task,
                            argv);
  if (nsh_pid < 0)
    {
      printf("[VocaVibe Boot] 警告: 创建后台 NSH 终端任务失败 (errno=%d)\n", errno);
    }
  else
    {
      printf("[VocaVibe Boot] 后台 NSH 终端已启动 (pid=%d)\n", nsh_pid);
    }
  fflush(stdout);
#endif

  /* 3. 前台立即启动 VocaVibe 参赛主程序，点亮 AMOLED 并开启手账 UI */
  printf("[VocaVibe Boot] 启动 VocaVibe 主界面...\n");
  fflush(stdout);
  return vocavibe_main(argc, argv);
}
