#ifndef VOCAVIBE_AUDIO_H
#define VOCAVIBE_AUDIO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化板载 DAC 及功放使能引脚 PA10 */
int vocavibe_audio_hw_init(void);

/* 开启/关闭板载功放 (PA10) */
void vocavibe_audio_pa_enable(bool enable);

/* 播放一段 PCM 样本 (24kHz 16-bit signed mono, 对齐小智 AI 标准) */
int vocavibe_audio_play_pcm(const int16_t *pcm_data, uint32_t samples);

/* 发送内置提示音 (如 1kHz 蜂鸣 / 欢迎音) */
void vocavibe_audio_play_beep(uint32_t freq_hz, uint32_t duration_ms);

/* Base64 编码分片流式推入播放 */
int vocavibe_audio_feed_base64(const char *b64_str, bool is_start, bool is_end);

#ifdef __cplusplus
}
#endif

#endif /* VOCAVIBE_AUDIO_H */
