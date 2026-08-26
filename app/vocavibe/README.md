# VocaVibe（参赛 app）

VocaVibe 队伍（243）的应用，映射到 openvela `packages/demos/contest2026_243_vocavibe`。

## 代码结构

```
app/vocavibe/
├── Kconfig                 # 配置菜单定义（LVX_USE_DEMO_CONTEST2026_243_VOCAVIBE）
├── CMakeLists.txt          # cmake 构建（SRCS 指 src/，INCLUDE_DIRECTORIES 指 include/）
├── Makefile                # 传统 make 构建（与 cmake 保持一致）
├── Make.defs
├── README.md
├── include/
│   └── vocavibe.h          # 应用公共接口声明
└── src/
    └── vocavibe_main.c     # 应用入口 main()
```

## 构建

构建配置用公共仓板级配置（`vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh/defconfig`），
**开发期间直接改这个公共配置**（开 `CONFIG_LVX_USE_DEMO_CONTEST2026_243_VOCAVIBE=y`、
`CONFIG_SYSTEM_SYSTEM=y`、`CONFIG_SYSTEM_POPEN=y`），从 openvela 根目录执行：

```bash
cmake -B cmake_out/sf32lb52_devkit_lcd -S nuttx -GNinja \
  -DBOARD_CONFIG=../vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh
cmake --build cmake_out/sf32lb52_devkit_lcd
```

> **交作品时**：把这个改好的 defconfig 复制到参赛仓 `app/vocavibe/configs/nsh/defconfig`，
> 然后 `git checkout` 还原公共仓版 → 公共仓零改动。
> `system`/`popen`/`pclose` 由 openvela 自带的 `apps/system/system` 和 `apps/system/popen`
> 提供（`CONFIG_SYSTEM_SYSTEM`/`CONFIG_SYSTEM_POPEN`），无需参赛仓补桩。
