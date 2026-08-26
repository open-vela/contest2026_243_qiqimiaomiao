# hello_app（参赛 app）

VocaVibe 队伍（243）的应用形态样例，映射到 openvela `packages/demos/contest2026_243_hello_app`。

## 构建

本 app 的构建配置在 `configs/nsh/defconfig`，从 openvela 根目录执行：

```bash
cmake -B cmake_out/sf32lb52_devkit_lcd -S nuttx \
  -DBOARD_CONFIG=../contest2026_243_qiqimiaomiao/app/hello_app/configs/nsh
ninja -C cmake_out/sf32lb52_devkit_lcd
```

固件产物：`cmake_out/sf32lb52_devkit_lcd/nuttx.bin`。

## 目录说明

- `hello_app_main.c`：应用入口 `main()`，并附带 libc 兼容桩（weak `system`/`popen`/`pclose`），
  供 ai_agent 在无 shell 的板子上链接通过。
- `configs/nsh/defconfig`：本队伍的构建配置（开启网络协议栈与 ai_agent，
  关闭 `CONFIG_AI_AGENT_BLE_NET` / `CONFIG_RNDIS`，USB 联网暂不启用）。
