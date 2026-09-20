#!/usr/bin/env python3
import subprocess
import time
import re
import os
import sys

def main():
    print("=== 开始生成全量汉字字库 (GB2312 全量 6763 字 + 扩展符号 + ASCII) ===")

    # 1. 收集 GB2312 全量汉字 (0xB0..0xF7, 0xA1..0xFE)
    gb_chars = []
    for b1 in range(0xB0, 0xF8):
        for b2 in range(0xA1, 0xFF):
            try:
                ch = bytes([b1, b2]).decode('gb2312')
                gb_chars.append(ch)
            except Exception:
                pass

    print(f"提取 GB2312 标准汉字数量: {len(gb_chars)}")

    # 2. 提取现有字库中的特殊字符与标点
    target_c_file = "contest2026_243_qiqimiaomiao/app/vocavibe/src/vocavibe_font_data.c"
    existing_symbols = []
    if os.path.exists(target_c_file):
        with open(target_c_file, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                if "--symbols" in line:
                    m = re.search(r'--symbols\s+(.*?)\s+--size', line)
                    if m:
                        existing_symbols = list(m.group(1))
                        break

    # 3. 补充常用标点符号与 UI Emoji (含 ❮ ❯ 等导航符号)
    common_punct = list("，。！？；：“”‘’（）【】《》、…—～·•⏰🔄★☆✓✗℃℉±×÷=+/\\|@#$%^&*()_+-=[]{}「」『』❮❯")
    
    # 4. 扫描工程中所有的 C/H 源码，提取所有写在代码中的中文字符与符号
    code_chars = []
    src_dir = "contest2026_243_qiqimiaomiao/app/vocavibe"
    for root, dirs, files in os.walk(src_dir):
        for file in files:
            if file.endswith((".c", ".h")):
                fp = os.path.join(root, file)
                try:
                    with open(fp, "r", encoding="utf-8", errors="ignore") as f:
                        code_chars.extend([ch for ch in f.read() if ord(ch) > 127])
                except Exception:
                    pass

    # 5. 去重合并并排序
    all_syms = sorted(list(set(gb_chars + existing_symbols + common_punct + code_chars)))
    syms_str = "".join(all_syms)
    print(f"最终字库字符总数 (去重后): {len(all_syms)}")

    font_path = "packages/demos/ai_chat/res/fonts/MiSans-Normal.ttf"
    if not os.path.exists(font_path):
        print(f"错误: 找不到矢量字体文件 {font_path}")
        sys.exit(1)

    # 5. 调用 npx lv_font_conv (包含 ASCII + 完整国际音标 IPA + 希腊字母音标 θ 等 0x20-0x03FF)
    cmd = [
        "npx", "lv_font_conv",
        "--font", font_path,
        "-r", "0x20-0x03FF",
        "--symbols", syms_str,
        "--size", "16",
        "--bpp", "4",
        "--no-compress",
        "--format", "lvgl",
        "--lv-font-name", "lv_font_simsun_16_cjk",
        "-o", target_c_file
    ]

    t0 = time.time()
    print("正在运行 lv_font_conv 烘焙字库...")
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.returncode != 0:
        print(f"生成失败: {res.stderr}")
        sys.exit(res.returncode)

    elapsed = time.time() - t0
    size_mb = os.path.getsize(target_c_file) / (1024 * 1024)
    print(f"✅ 字库生成成功！耗时: {elapsed:.2f}s, 输出文件大小: {size_mb:.2f} MB ({target_c_file})")

if __name__ == "__main__":
    main()
