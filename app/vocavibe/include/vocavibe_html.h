/****************************************************************************
 * contest2026_243_qiqimiaomiao/app/vocavibe/include/vocavibe_html.h
 *
 * VocaVibe 轻量端侧 HTML 卡面解析与富文本处理器
 * 专门针对 AnkiWeb / AnkiConnect 导出的含标签卡片内容
 ****************************************************************************/

#ifndef __VOCAVIBE_HTML_H
#define __VOCAVIBE_HTML_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 将输入的 Anki HTML 文本转换为适合 LVGL Label 渲染的规整文本
 *
 * 功能说明：
 * 1. 自动转换 `<br>`, `<br/>`, `</p>`, `</div>` 为换行符 `\n`。
 * 2. 剥离无用的 HTML 标签（如 `<div>`, `<span>`, `<font>`, `<b>`, `<i>` 等）。
 * 3. 剥离 `<style>...</style>` 与 `<script>...</script>` 块。
 * 4. 转换常见 HTML 实体（如 `&nbsp;`, `&lt;`, `&gt;`, `&amp;`, `&quot;`）。
 * 5. 合并冗余的连续空行，修剪首尾空白。
 *
 * @param html_in 输入的含 HTML 字符串
 * @param text_out 转换后输出的目标缓冲区
 * @param max_len 目标缓冲区最大容纳字节数
 * @return size_t 实际写入的字符数
 */
size_t vocavibe_html_to_plain_text(const char *html_in, char *text_out, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* __VOCAVIBE_HTML_H */
