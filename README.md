# VocaVibe (随声记) - 2026 首届 openvela AI 硬件开发者大赛

> **参赛团队**：Team 243 (`qiqimiaomiao`)  
> **赛道方向**：AI 硬件产品创新赛道  
> **目标硬件**：思澈 SF32LB52-DevKit-LCD（1.85" 390×450 AMOLED 高清屏 + FT6146 全触控）  
> **端侧系统**：OpenVela OS (NuttX RTOS + LVGL 9 + cJSON)  
> **大模型后端**：Xiaomi MiMo 2.5 (端云协同流式推理)

---

## 一、作品概述 (Product Overview)

**VocaVibe（随声记）** 是一款面向下一代端侧智能硬件的**多模态 AI 英语记忆伴侣终端**。

针对传统背单词软件枯燥、缺乏语境互动、硬件终端交互单调的痛点，VocaVibe 将**标准 Anki 记忆算法（SM-2）**、**小智声波律动视觉动效**、**Xiaomi MiMo 2.5 大语言模型**与**分布式蓝牙耳机代理**深度融合，构建了一款触手可及的桌面级全触控 AI 硬件学习伴侣。

```
                       ┌──────────────────────────────────────────────┐
                       │        Xiaomi MiMo 2.5 云端大模型             │
                       │           (流式语义理解与例句解析)              │
                       └──────────────────────▲───────────────────────┘
                                              │ HTTPS / Streaming
                                              ▼
 ┌────────────────────────┐      ┌────────────────────────┐      ┌────────────────────────┐
 │   用户蓝牙耳机 / 麦克风   │◄────►│  PC 伴侣网关 (Companion) │◄────►│ AnkiConnect (桌面 Anki) │
 │ (AirPods / FreeBuds等) │ BLE  │  (tools/companion_pc)  │ JSON │   (127.0.0.1:8765)     │
 └────────────────────────┘      └────────────▲───────────┘      └────────────────────────┘
                                              │ 1,000,000 Baud Serial / JSON
                                              ▼
 ┌────────────────────────────────────────────────────────────────────────────────────────┐
 │                      VocaVibe 硬件终端 (SF32LB52-DevKit-LCD)                            │
 │  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐   │
 │  │ Page 0: 仪表盘   │  │ Page 1: Anki卡片 │  │ Page 2: AI 助教  │  │ Page 3: 蓝牙代理 │   │
 │  │ 今日待学/复习统计 │  │ 4档评分/正反翻转 │  │ 小智声波/打字气泡│  │ 耳机扫描/Anki同步 │   │
 │  └─────────────────┘  └─────────────────┘  └─────────────────┘  └─────────────────┘   │
 │                        OpenVela OS + LVGL 9 + cJSON 本地引擎                            │
 └────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 二、六大核心功能特性

### 1. 4 页面无缝滑动架构 (`lv_tileview`)
基于 OpenVela 官方推荐架构，采用 `lv_tileview` 打造 390×450 视网膜级全触控多页流转：
- **Page 0（仪表盘）**：今日待复习、已掌握、总词库实时统计卡片与快速直达入口。
- **Page 1（Anki 记忆复习）**：大字词汇展示、美式音标、点击翻转、4 档评分矩阵。
- **Page 2（VocaVibe AI 助教）**：大赛指定唤醒词响应、小智声波呼吸波纹、打字机流式对话气泡、快捷问答胶囊。
- **Page 3（同步与代理设置）**：AnkiConnect 状态监测、蓝牙耳机扫描与遥控代理连接列表。

### 2. 标准 Anki SM-2 记忆算法与 4 档评分
严格遵循标准 Anki 算法体系，杜绝简化为二元选项：
- **Again (重来 1m)**：重置复习间隔为 0，今日重新滚入复习队列。
- **Hard (困难 1d)**：微调间隔为 1.2 倍，缩短巩固周期。
- **Good (良好 3d)**：标准推进记忆因子与复习天数（默认 2.5x 难度因子递增）。
- **Easy (简单 7d)**：显著拉长复习周期（1.3x 额外加成），降低认知负荷。
- **闪存持久化**：本地维护 `/data/vocavibe/deck.json`，断电不丢失学习数据。

### 3. 小智声波律动动效 (Sonic Waveform) 与 MiMo 2.5 流式对话
- 支持大赛指定唤醒词交互：**“你好，openvela / Hello，openvela”**。
- 采用 7 柱正弦相位律动动态波形，视觉反馈拾音（Listening）、思考（Thinking）、播报（Speaking）三种心智状态。
- Xiaomi MiMo 2.5 云端高并发低延迟流式推送，端侧打字机动态渲染。

### 4. 分布式蓝牙耳机代理联动
- 突破 MCU 无 BR/EDR 经典蓝牙耳机驱动的物理限制，通过 PC 伴侣实现分布式硬件外设代理。
- 开发板屏幕提供完整的周围蓝牙耳机扫描列表（如 AirPods Pro、HUAWEI FreeBuds、Sony WH-1000XM5），用户在板端点击即可遥控电脑中转配对。
- 耳机麦克风拾音识别后直达板端，板端触发 TTS 语音通过耳机立体声回放。

### 5. 标准 AnkiConnect 本地双向同步
- 兼容 AnkiConnect 插件（端口 8765），打通桌面 Anki 与硬件端双向闭环：从 Anki 导入词库卡片，并将硬件端复习进度无损同步回电脑。

### 6. AI Agent 卡组端侧 CRUD 与标准 Skill 规范
- 开放端侧卡组的动态增删改查权限，沉淀首届大赛标准 AI Skill：[`skills/anki-card-manager.md`](skills/anki-card-manager.md)。
- 用户语音发出“把 paradigm 加到生词本”，AI Agent 自动补充音标释义并热刷新至卡组。

---

## 三、快速开始与构建指南

### 1. 编译全量工程
在 openvela 根目录下执行构建：
```bash
cmake --build cmake_out/sf32lb52_devkit_lcd
```

### 2. 一键烧录固件到开发板
利用 `sftool` 通过板载高速串口（1,000,000 baud）烧录：
```bash
sftool -c SF32LB52 -p /dev/ttyACM0 -b 1000000 \
       --before default_reset --after soft_reset \
       write_flash cmake_out/sf32lb52_devkit_lcd/nuttx.bin@0x12010000
```

### 3. 运行 PC 伴侣网关 (Companion)
启动配套网关，打通 MiMo 2.5 大模型、蓝牙耳机代理与 AnkiConnect 同步：
```bash
python3 tools/companion_pc.py /dev/ttyACM0
```

---

## 四、仓库目录结构

```text
contest2026_243_qiqimiaomiao/
├── app/
│   └── vocavibe/                     # VocaVibe 端侧核心应用程序
│       ├── CMakeLists.txt            # 构建脚本 (链接 lvgl 与 cJSON)
│       ├── include/
│       │   ├── vocavibe_core.h       # Anki SM-2 算法与核心数据结构定义
│       │   └── vocavibe_ui.h         # 4 页面 Tileview 视图与动效声明
│       └── src/
│           ├── vocavibe_main.c       # 主入口与控制台交互
│           ├── vocavibe_core.c       # SM-2 算法、cJSON 持久化与意图分类
│           └── vocavibe_ui.c         # LVGL 9 页面布局、小智声波与触控逻辑
├── skills/
│   └── anki-card-manager.md          # 官方标准端侧 AI Skill 规范
├── tools/
│   ├── companion_pc.py               # PC 伴侣网关 (MiMo 2.5 + 蓝牙代理 + Anki)
│   ├── auto_reset.py                 # RTS 自动软复位脚本
│   └── test_mimo_live.py             # MiMo 云端大模型连通性验证
├── logs/                             # AI Coding 轨迹日志归集
└── README.md                         # 项目主说明文档
```

---

## 五、版权与开源协议

本项目专为 2026 首届 openvela AI 硬件开发者大赛（Team 243 `qiqimiaomiao`）设计研发，遵循 Apache 2.0 开源许可证。
