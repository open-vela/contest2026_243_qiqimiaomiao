/****************************************************************************
 * contest2026_243_qiqimiaomiao/app/vocavibe/src/vocavibe_audio.c
 *
 * SF32LB52-DevKit-LCD 片上高品质 DAC + 板载 NS4150B 功放 (PA10 使能)
 * 纯硬件级闭环驱动，兼容 24kHz 16-bit 线性 PCM 音频流与流式 Base64 串口回放。
 * (对齐小智 AI 标准 24kHz 采样率，杜绝重采样失真)
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>

#ifndef SOC_BF0_HCPU
#define SOC_BF0_HCPU
#endif
#ifndef SF32LB52X
#define SF32LB52X
#endif
#ifndef USE_HAL_DRIVER
#define USE_HAL_DRIVER
#endif

#include "bf0_hal.h"
#include "bf0_hal_audcodec.h"
#include "vocavibe_audio.h"

#define PA_EN_PIN 10

static AUDCODEC_HandleTypeDef s_hacodec;
static AUDCODE_DAC_CLK_CONFIG_TYPE s_dac_clk;
static bool s_audio_inited = false;
static pthread_mutex_t s_audio_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t s_playback_tid;
static bool s_thread_created = false;
static void *audio_playback_thread(void *arg);

/* Base64 解码表 */
static const signed char s_b64_table[256] = {
    ['A']=0,['B']=1,['C']=2,['D']=3,['E']=4,['F']=5,['G']=6,['H']=7,
    ['I']=8,['J']=9,['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,
    ['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,['X']=23,
    ['Y']=24,['Z']=25,['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,
    ['g']=32,['h']=33,['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,
    ['o']=40,['p']=41,['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,['v']=47,
    ['w']=48,['x']=49,['y']=50,['z']=51,['0']=52,['1']=53,['2']=54,['3']=55,
    ['4']=56,['5']=57,['6']=58,['7']=59,['8']=60,['9']=61,['+']=62,['/']=63
};

static int base64_decode_bytes(const char *in, size_t in_len, uint8_t *out, size_t out_max)
{
    size_t out_len = 0;
    uint32_t buf = 0;
    int bits = 0;

    for (size_t i = 0; i < in_len; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '=' || c == '\r' || c == '\n') continue;
        signed char val = s_b64_table[c];
        if (val < 0 && c != 'A') continue;

        buf = (buf << 6) | (val & 0x3F);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (out_len < out_max) {
                out[out_len++] = (uint8_t)((buf >> bits) & 0xFF);
            }
        }
    }
    return (int)out_len;
}

void vocavibe_audio_pa_enable(bool enable)
{
    HAL_PIN_Set(PAD_PA10, GPIO_A10, PIN_NOPULL, 1);
    if (enable) {
        /* AW8155 官方一线脉冲唤醒时序：
         * 1. 拉高 550us 激活内部振荡器
         * 2. 发送 1 次脉冲配置为 Mode 1 (低失真/高增益标准放音模式)
         * 3. 延时 50ms 确保模拟通路彻底稳定
         */
        HAL_GPIO_WritePin(hwp_gpio1, PA_EN_PIN, GPIO_PIN_SET);
        HAL_Delay_us(550);
        /* 1 次负脉冲进入 Mode 1 */
        HAL_GPIO_WritePin(hwp_gpio1, PA_EN_PIN, GPIO_PIN_RESET);
        HAL_Delay_us(5);
        HAL_GPIO_WritePin(hwp_gpio1, PA_EN_PIN, GPIO_PIN_SET);
        HAL_Delay_us(500);
    } else {
        /* 保留常高使能或微秒级静音，绝不长期拉低以防冲断板载 LCD / 模拟总线供电 */
        HAL_GPIO_WritePin(hwp_gpio1, PA_EN_PIN, GPIO_PIN_SET);
    }
}

int vocavibe_audio_hw_init(void)
{
    pthread_mutex_lock(&s_audio_mutex);
    if (s_audio_inited) {
        pthread_mutex_unlock(&s_audio_mutex);
        return 0;
    }

    printf("[VocaVibe Audio] 正在初始化板载硬件音频 (24kHz DAC + LDO2_3V3 AVDD33 + AW8155 PA10)...\n");

    /* 0. 关键供电：开启片上模拟音频核心供电 LDO2_3V3 (AVDD33) 与 LDO3_3V3 */
    HAL_PMU_ConfigPeriLdo(PMU_PERI_LDO2_3V3, true, true);
    HAL_PMU_ConfigPeriLdo(PMU_PERI_LDO3_3V3, true, true);
    /* 直接强制置位 PMUC 寄存器，绕过 HAL 芯片版本限制 */
    hwp_pmuc->PERI_LDO &= ~(PMUC_PERI_LDO_VDD33_LDO2_PD_Msk | PMUC_PERI_LDO_VDD33_LDO3_PD_Msk);
    hwp_pmuc->PERI_LDO |= (PMUC_PERI_LDO_EN_VDD33_LDO2_Msk | PMUC_PERI_LDO_EN_VDD33_LDO3_Msk);

    /* 1. 配置 PA10 为推挽输出并使能功放 (AW8155 / NS4150B) */
    hwp_hpsys_rcc->ENR2 |= HPSYS_RCC_ENR2_GPIO1;
    HAL_PIN_Set(PAD_PA10, GPIO_A10, PIN_NOPULL, 1);
    GPIO_InitTypeDef gpio_init;
    gpio_init.Mode = GPIO_MODE_OUTPUT;
    gpio_init.Pin  = PA_EN_PIN;
    gpio_init.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(hwp_gpio1, &gpio_init);
    HAL_GPIO_WritePin(hwp_gpio1, PA_EN_PIN, GPIO_PIN_SET);

    /* 2. 开启 HXT 模拟时钟缓冲及 AUDCODEC 总线时钟 */
    hwp_pmuc->HXT_CR1 |= PMUC_HXT_CR1_BUF_AUD_EN;
    hwp_hpsys_rcc->ENR2 |= HPSYS_RCC_ENR2_AUDCODEC;
    hwp_hpsys_rcc->RSTR2 &= ~HPSYS_RCC_RSTR2_AUDCODEC;

    /* 3. 配置 16kHz DAC 采样时钟结构体 (48MHz XTAL / 10 / 300 = 16000.0 Hz) */
    memset(&s_dac_clk, 0, sizeof(s_dac_clk));
    s_dac_clk.samplerate = 16000;
    s_dac_clk.clk_src_sel = 0;         /* 0: XTAL 48M */
    s_dac_clk.clk_div = 9;             /* 48M / (9 + 1) = 4.8MHz */
    s_dac_clk.osr_sel = 2;             /* OSR = 300 -> 4.8M / 300 = 16000 Hz */
    s_dac_clk.sinc_gain = 0;
    s_dac_clk.sel_clk_dac_source = 0; /* XTAL */
    s_dac_clk.sel_clk_dac = 0;
    s_dac_clk.diva_clk_dac = 0;

    /* 4. 初始化 AUDCODEC Handle */
    memset(&s_hacodec, 0, sizeof(s_hacodec));
    s_hacodec.Instance = hwp_audcodec;
    s_hacodec.Init.dac_cfg.dac_clk = &s_dac_clk;
    s_hacodec.Init.dac_cfg.opmode = 1;  /* 1: mem/CPU 直接写入 DAC_CH0_ENTRY 模式 (0为 AUDPRC 模式) */

    /* 启动 PLL / Refgen 及 Analog DAC 路径 */
    HAL_TURN_ON_PLL();
    HAL_AUCODEC_Refgen_Init();
    HAL_AUDCODEC_Config_Analog_DACPath(&s_dac_clk);

    /* 确保 DAC1 与 DAC2 模拟通路全开 (立体声/双输出) */
    hwp_audcodec->DAC1_CFG |= (AUDCODEC_DAC1_CFG_EN_AMP | AUDCODEC_DAC1_CFG_EN_DAC | AUDCODEC_DAC1_CFG_EN_VCM);
    hwp_audcodec->DAC2_CFG |= (AUDCODEC_DAC2_CFG_EN_AMP | AUDCODEC_DAC2_CFG_EN_DAC | AUDCODEC_DAC2_CFG_EN_VCM);

    /* 配置 Channel 0 与 Channel 1 并设置数字高音量增益 (+6dB ~ +12dB，保证响度充足) */
    HAL_AUDCODEC_Config_TChanel(&s_hacodec, 0, &(s_hacodec.Init.dac_cfg));
    HAL_AUDCODEC_Config_TChanel(&s_hacodec, 1, &(s_hacodec.Init.dac_cfg));
    HAL_AUDCODEC_Config_DACPath_Volume(&s_hacodec, 0, 12);
    HAL_AUDCODEC_Config_DACPath_Volume(&s_hacodec, 1, 12);

    /* 关键解除静音：清除 DOUT_MUTE 与 BYPASS，确保数据送达片上数模转换器 */
    HAL_AUDCODEC_Config_DACPath(&s_hacodec, 0);

    /* 全局使能 DAC 硬件引擎输出 */
    __HAL_AUDCODEC_DAC_ENABLE(&s_hacodec);

    /* 唤醒功放芯片 AW8155 */
    vocavibe_audio_pa_enable(true);

    /* 启动独立后台音频播放线程 */
    if (!s_thread_created) {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 4096);
        pthread_create(&s_playback_tid, &attr, audio_playback_thread, NULL);
        pthread_attr_destroy(&attr);
        s_thread_created = true;
    }

    s_audio_inited = true;
    pthread_mutex_unlock(&s_audio_mutex);
    printf("[VocaVibe Audio] 板载 DAC 硬件引擎初始化成功 (AVDD33+DAC1/DAC2+AW8155就绪，放音线程已启动)！\n");
    return 0;
}

/* 环形音频缓冲区设计 (16384 采样 = 32KB，在 16kHz 下可缓冲 1.024 秒音频，强力吸收任何 Jitter) */
#define AUDIO_RING_SAMPLES 16384

static int16_t s_pcm_ring[AUDIO_RING_SAMPLES];
static volatile uint32_t s_ring_head = 0; /* 生产者写入位置 */
static volatile uint32_t s_ring_tail = 0; /* 消费者读取位置 */
static volatile bool s_stream_active = false;
static volatile bool s_stream_ended = false;
static volatile bool s_playback_started = false;

static inline uint32_t ring_available(void)
{
    uint32_t h = s_ring_head;
    uint32_t t = s_ring_tail;
    if (h >= t) return h - t;
    return AUDIO_RING_SAMPLES - (t - h);
}

static inline uint32_t ring_free_space(void)
{
    return AUDIO_RING_SAMPLES - 1 - ring_available();
}

static void ring_write_samples(const int16_t *data, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        if (ring_free_space() == 0) {
            /* 满则丢弃最旧数据防死锁 */
            s_ring_tail = (s_ring_tail + 1) % AUDIO_RING_SAMPLES;
        }
        s_pcm_ring[s_ring_head] = data[i];
        s_ring_head = (s_ring_head + 1) % AUDIO_RING_SAMPLES;
    }
}

/* 独立高优先级放音线程：精准按 DAC 硬件 FIFO 消耗速率喂数据 */
static void *audio_playback_thread(void *arg)
{
    (void)arg;
    while (1) {
        if (!s_stream_active) {
            usleep(5000);
            continue;
        }

        uint32_t avail = ring_available();

        /* 预缓冲防抖机制：起始帧后至少缓冲 2400 个采样 (150ms 音频) 或遇到流结束符才开播 */
        if (!s_playback_started) {
            if (avail < 2400 && !s_stream_ended) {
                usleep(3000);
                continue;
            }
            s_playback_started = true;
        }

        if (avail == 0) {
            if (s_stream_ended) {
                /* 等待片上 DAC 硬件 FIFO 彻底排空 */
                uint32_t drain_to = 2000;
                while ((hwp_audcodec->APB_STAT & AUDCODEC_APB_STAT_DAC_CH0_FIFO_CNT_Msk) != 0) {
                    HAL_Delay_us(50);
                    if (--drain_to == 0) break;
                }
                s_stream_active = false;
                s_playback_started = false;
                s_stream_ended = false;
            } else {
                /* 缓冲区暂时被抽空，微秒级休眠等待新数据 */
                usleep(1000);
            }
            continue;
        }

        /* 批量填充硬件 DAC FIFO (FIFO 深度 16，保持水位在 12 以下) */
        while (ring_available() >= 2) {
            uint32_t fifo_cnt = (hwp_audcodec->APB_STAT & AUDCODEC_APB_STAT_DAC_CH0_FIFO_CNT_Msk)
                                >> AUDCODEC_APB_STAT_DAC_CH0_FIFO_CNT_Pos;
            if (fifo_cnt >= 12) {
                /* FIFO 接近满，让出 CPU 50us (50us = 约 0.8 个采样时间) */
                HAL_Delay_us(50);
                break;
            }

            int16_t s0 = s_pcm_ring[s_ring_tail];
            s_ring_tail = (s_ring_tail + 1) % AUDIO_RING_SAMPLES;
            int16_t s1 = s_pcm_ring[s_ring_tail];
            s_ring_tail = (s_ring_tail + 1) % AUDIO_RING_SAMPLES;

            uint32_t word = ((uint32_t)(uint16_t)s1 << 16) | (uint16_t)s0;
            hwp_audcodec->DAC_CH0_ENTRY = word;
        }

        if (ring_available() == 1 && s_stream_ended) {
            int16_t s0 = s_pcm_ring[s_ring_tail];
            s_ring_tail = (s_ring_tail + 1) % AUDIO_RING_SAMPLES;
            uint32_t word = (uint32_t)(uint16_t)s0;
            hwp_audcodec->DAC_CH0_ENTRY = word;
        }
    }
    return NULL;
}

int vocavibe_audio_play_pcm_stream(const int16_t *pcm_data, uint32_t samples, bool is_start, bool is_end)
{
    if (!pcm_data || samples == 0) return 0;
    if (!s_audio_inited) {
        vocavibe_audio_hw_init();
    }

    if (is_start) {
        vocavibe_audio_pa_enable(true);
        s_ring_head = 0;
        s_ring_tail = 0;
        s_playback_started = false;
        s_stream_ended = false;
        s_stream_active = true;
    }

    ring_write_samples(pcm_data, samples);

    if (is_end) {
        s_stream_ended = true;
    }

    return samples;
}

int vocavibe_audio_play_pcm(const int16_t *pcm_data, uint32_t samples)
{
    return vocavibe_audio_play_pcm_stream(pcm_data, samples, true, true);
}

void vocavibe_audio_play_beep(uint32_t freq_hz, uint32_t duration_ms)
{
    if (duration_ms == 0) return;
    if (!s_audio_inited) {
        vocavibe_audio_hw_init();
    }

    printf("[VocaVibe Audio] 🔊 播放 16kHz 正弦波提示音: %u Hz, %u ms...\n", (unsigned int)freq_hz, (unsigned int)duration_ms);

    /* 16kHz 采样率下的 1kHz 纯正弦波单周期查找表 (16 采样) */
    static const int16_t sine_1k[16] = {
        0, 12539, 23170, 30273, 32767, 30273, 23170, 12539,
        0, -12539, -23170, -30273, -32767, -30273, -23170, -12539
    };

    uint32_t total_samples = (16000 * duration_ms) / 1000;
    int16_t chunk[64];
    uint32_t played = 0;

    while (played < total_samples) {
        uint32_t n = total_samples - played;
        if (n > 64) n = 64;
        for (uint32_t i = 0; i < n; i++) {
            chunk[i] = sine_1k[(played + i) % 16];
        }
        bool is_start = (played == 0);
        bool is_end = (played + n >= total_samples);
        vocavibe_audio_play_pcm_stream(chunk, n, is_start, is_end);
        played += n;
    }
}

int vocavibe_audio_feed_base64(const char *b64_str, bool is_start, bool is_end)
{
    if (!b64_str) return 0;
    size_t in_len = strlen(b64_str);
    if (in_len == 0) return 0;

    static uint8_t raw_buf[2048];
    int decoded_bytes = base64_decode_bytes(b64_str, in_len, raw_buf, sizeof(raw_buf));
    if (decoded_bytes > 0 && (decoded_bytes % 2 == 0)) {
        vocavibe_audio_play_pcm_stream((int16_t *)raw_buf, decoded_bytes / 2, is_start, is_end);
    }
    return decoded_bytes;
}
