/****************************************************************************
 * contest2026_243_qiqimiaomiao/app/vocavibe/src/vocavibe_html.c
 *
 * VocaVibe 轻量端侧 HTML 卡面解析与富文本处理器
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "vocavibe_html.h"

static bool starts_with_case_insensitive(const char *str, const char *prefix)
{
  while (*prefix)
    {
      if (tolower((unsigned char)*str) != tolower((unsigned char)*prefix))
        {
          return false;
        }
      str++;
      prefix++;
    }
  return true;
}

size_t vocavibe_html_to_plain_text(const char *html_in, char *text_out, size_t max_len)
{
  if (!text_out || max_len == 0)
    {
      return 0;
    }

  if (!html_in || *html_in == '\0')
    {
      text_out[0] = '\0';
      return 0;
    }

  const char *src = html_in;
  char *dst = text_out;
  size_t written = 0;
  bool in_tag = false;
  bool in_script_or_style = false;
  int consecutive_newlines = 0;

  while (*src != '\0' && written < max_len - 1)
    {
      /* 1. 处理 <style> 和 <script> 忽略块 */
      if (in_script_or_style)
        {
          if (*src == '<' && (starts_with_case_insensitive(src, "</style>") ||
                              starts_with_case_insensitive(src, "</script>")))
            {
              in_script_or_style = false;
              while (*src != '\0' && *src != '>')
                {
                  src++;
                }
              if (*src == '>')
                {
                  src++;
                }
            }
          else
            {
              src++;
            }
          continue;
        }

      /* 2. 标签开始 */
      if (*src == '<')
        {
          if (starts_with_case_insensitive(src, "<style") ||
              starts_with_case_insensitive(src, "<script"))
            {
              in_script_or_style = true;
              src++;
              continue;
            }

          /* 块级换行标签检测：<br>, <p>, </p>, </div>, </li>, </tr> */
          if (starts_with_case_insensitive(src, "<br") ||
              starts_with_case_insensitive(src, "</p>") ||
              starts_with_case_insensitive(src, "</div>") ||
              starts_with_case_insensitive(src, "</li>") ||
              starts_with_case_insensitive(src, "</tr>"))
            {
              if (written > 0 && consecutive_newlines < 2)
                {
                  *dst++ = '\n';
                  written++;
                  consecutive_newlines++;
                }
            }

          in_tag = true;
          src++;
          continue;
        }

      /* 3. 标签结束 */
      if (*src == '>')
        {
          in_tag = false;
          src++;
          continue;
        }

      /* 如果正在标签内部，跳过属性等内容 */
      if (in_tag)
        {
          src++;
          continue;
        }

      /* 4. HTML 实体转义 */
      if (*src == '&')
        {
          if (starts_with_case_insensitive(src, "&nbsp;"))
            {
              *dst++ = ' ';
              written++;
              src += 6;
              consecutive_newlines = 0;
              continue;
            }
          else if (starts_with_case_insensitive(src, "&lt;"))
            {
              *dst++ = '<';
              written++;
              src += 4;
              consecutive_newlines = 0;
              continue;
            }
          else if (starts_with_case_insensitive(src, "&gt;"))
            {
              *dst++ = '>';
              written++;
              src += 4;
              consecutive_newlines = 0;
              continue;
            }
          else if (starts_with_case_insensitive(src, "&amp;"))
            {
              *dst++ = '&';
              written++;
              src += 5;
              consecutive_newlines = 0;
              continue;
            }
          else if (starts_with_case_insensitive(src, "&quot;"))
            {
              *dst++ = '\"';
              written++;
              src += 6;
              consecutive_newlines = 0;
              continue;
            }
        }

      /* 5. 普通字符输出 */
      if (*src == '\n' || *src == '\r')
        {
          if (*src == '\r' && *(src + 1) == '\n')
            {
              src++;
            }
          if (written > 0 && consecutive_newlines < 2)
            {
              *dst++ = '\n';
              written++;
              consecutive_newlines++;
            }
        }
      else
        {
          *dst++ = *src;
          written++;
          consecutive_newlines = 0;
        }
      src++;
    }

  /* 移除尾部多余换行 */
  while (written > 0 && (dst[-1] == '\n' || dst[-1] == ' ' || dst[-1] == '\t'))
    {
      dst--;
      written--;
    }

  *dst = '\0';
  return written;
}
