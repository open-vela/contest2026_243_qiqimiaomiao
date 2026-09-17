# Anki Card Manager

VocaVibe 智能卡组与记忆卡片管理器，支持通过端侧 AI Agent 与自然语言对 Anki 记忆卡片执行增、删、改、查、复习进度查询与多端同步。

## When to use
当用户提到添加单词、背单词、修改卡片释义、删除单词、查询卡片到期与复习进度、或与电脑 AnkiConnect 同步时使用。

## Storage & Resources
- 卡组持久化文件: `/data/vocavibe/deck.json`
- 交互协议: 标准 AnkiConnect JSON (兼容桌面版 Anki 插件与 Web 端)
- 工具支持: `read_file`, `write_file`, `edit_file`, `shell`

## Operations & Schema

卡片标准 JSON 格式：
```json
{
  "id": 1,
  "word": "openvela",
  "phonetic": "/ˈoʊpən ˈvɛlə/",
  "meaning": "面向端侧 AI 与嵌入式微控制器的开源实时操作系统",
  "example": "OpenVela OS powers intelligent edge hardware with microsecond latency.",
  "interval": 0,
  "factor": 2500,
  "reps": 0
}
```

### 1. 添加卡片 (Add Card)
- 触发短语: "把 [单词] 加到生词本" / "添加卡片 [单词]" / "记住这个词"
- 动作流程:
  1. 读取 `/data/vocavibe/deck.json`，检查词库查重
  2. 若单词缺少音标或例句，Agent 自动利用大模型补全高质量音标和语境例句
  3. 分配唯一递增 id，初始化 `factor: 2500`, `interval: 0`, `reps: 0`
  4. 追加并保存至 `/data/vocavibe/deck.json`
  5. 屏幕 UI 即时更新仪表盘与卡组总数
- 响应范例: "已为你将生词 **resonance** [ˈrɛzənəns] 添加至 VocaVibe 卡组！"

### 2. 修改释义与例句 (Update Card)
- 触发短语: "修改 [单词] 的中文意思" / "换一个更地道的例句"
- 动作流程:
  1. 定位目标单词条目
  2. 更新 `meaning` 或 `example`
  3. 保存文件并触发端侧热刷新
- 响应范例: "已更新 **latency** 的语境例句为：'Low latency is crucial for real-time voice conversations.'"

### 3. 删除卡片 (Delete Card)
- 触发短语: "从卡组中删除 [单词]" / "我已经彻底掌握了，删掉吧"
- 动作流程:
  1. 根据 id 或 word 匹配条目并安全移除
  2. 保存 `/data/vocavibe/deck.json`
- 响应范例: "已从当前卡组中移除 **benchmark**。"

### 4. 学习进度与到期查询 (Query Progress)
- 触发短语: "今天还有多少词没背" / "我的学习进度怎么样了"
- 动作流程:
  1. 统计当前卡组中 `interval == 0` 或今日未评分的卡片数量
  2. 回复今日待复习数、已复习数与总词汇量
- 响应范例: "报告！今日共有 20 个待学卡片，已完成 5 个，当前正在复习第 6 个词汇 **neural**。"

### 5. 多端双向同步 (AnkiConnect Sync)
- 触发短语: "同步 Anki" / "把手机/电脑的卡片传过来"
- 动作流程:
  1. 通过中转伴侣向 PC 本地 `http://127.0.0.1:8765` 发起 AnkiConnect API 调研
  2. 双向同步：拉取桌面 Anki 新卡片，推送硬件端 SM-2 复习评分与间隔
  3. 更新 UI 页面 3 中的同步状态提示
- 响应范例: "已完成与 AnkiConnect 的全量双向同步，卡片与复习进度已对齐！"
