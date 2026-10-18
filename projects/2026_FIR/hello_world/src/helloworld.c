// ========== 2026 电赛底板 - ADC + FFT + DDC + 谐波分析 ==========
// slv_reg0[27:0]=DDS频率 reg1[30:24]=抽取 [20:13]=幅度 [12:11]=波形 [10:0]=相位
// slv_reg2[0]=触发 [1]=DAC1源 [2]=BRAM源 [3]=DAC2源
//
// UI 状态机:
//   顶层状态只有 测量模式(APP_MEASURE) / 校准模式(APP_CALIBRATE) 两种
//   波形/频谱(VIEW_WAVEFORM/VIEW_SPECTRUM)与 1/3周期(cycle_mode)
//   只是测量模式下的显示属性，不影响后台测量流程

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "xil_io.h"
#include "xparameters.h"
#include "xscugic.h"
#include "xil_exception.h"
#include "sleep.h"
#include "tjc_screen.h"

#define ARM_MATH_CA9
#define __FPU_PRESENT 1U
#include "arm_math.h"

// ========== 地址 ==========
#define LITE_ADDR     XPAR_AXI_LITE_PS_TO_PL_PL_0_BASEADDR
#define REG_DDS_FREQ  (LITE_ADDR + 0x00)
#define REG_DDS_CTRL  (LITE_ADDR + 0x04)
#define REG_CTRL      (LITE_ADDR + 0x08)
#define BRAM_ADDR     XPAR_AXI_BRAM_CTRL_0_BASEADDR

// ========== 参数 ==========
#define DDS_CLK_HZ      50000000.0f
#define DDS_PHASE_B     28
#define WAVE_SIN        0
#define CTRL_ADC_SAMPLE (1 << 0)
#define CTRL_DAC1_SRC   (1 << 1)
#define CTRL_BRAM_SRC   (1 << 2)
#define CTRL_DAC2_SRC   (1 << 3)
#define ADC_SAMPLE_N    16384
#define FFT_N           4096
#define FIR_FS_HZ       1250000.0f
#define MEAS_N          14     // 每分量 DDC 测量次数（去最值平均）
#define MAX_PEAKS       5      // 最大检测分量数
#define SPEC_PTS        500    // 频谱显示点数
#define SPEC_K0         33     // 频谱 bin 起点 (~10kHz)
#define SPEC_K1         1638   // 频谱 bin 终点 (~500kHz)

// ========== UI 状态机 ==========
typedef enum {
    APP_MEASURE,      // 测量模式（后台持续循环测量）
    APP_CALIBRATE     // 校准模式（一次性执行，完成后回测量）
} app_mode_t;

typedef enum {
    VIEW_WAVEFORM,    // 波形显示
    VIEW_SPECTRUM     // 频谱显示
} view_mode_t;

static app_mode_t app_mode = APP_MEASURE;
static view_mode_t view_mode = VIEW_WAVEFORM;
static int cycle_mode = 0;       // 0=1周期, 1=3周期
static int cal_pending = 0;      // 校准请求（安全边界执行）

// ========== 显示缓存（测量后台持续更新，显示按 view_mode 取用） ==========
static float last_harm_f[MAX_PEAKS] = {0};
static float last_harm_v[MAX_PEAKS] = {0};
static int   last_n_found = 0;
static float last_vpp = 0, last_vrms = 0;
static uint8_t spectrum_cache[SPEC_PTS];
static int   spectrum_valid = 0;    // 频谱缓存是否有效
static int   spectrum_dirty = 0;    // 频谱数据是否有变化（避免重复发送）
static int   measurement_valid = 0; // 测量结果是否有效
static uint8_t prev_spectrum[SPEC_PTS];

// ========== 采样缓冲 ==========
static int16_t buf1[ADC_SAMPLE_N], buf2[ADC_SAMPLE_N];

// ========== 中断 ==========
#define INTC_DEVICE     XPAR_SCUGIC_SINGLE_DEVICE_ID
#define PL_IRQ          61

static XScuGic Intc;
static volatile int adc_done = 0;

static void IRQHandler(void *Ref) { (void)Ref; adc_done = 1; }

static void setup_irq(void)
{
    XScuGic_Config *cfg = XScuGic_LookupConfig(INTC_DEVICE);
    XScuGic_CfgInitialize(&Intc, cfg, cfg->CpuBaseAddress);
    Xil_ExceptionInit();
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
                                 (Xil_ExceptionHandler)XScuGic_InterruptHandler, &Intc);
    Xil_ExceptionEnable();
    XScuGic_Connect(&Intc, PL_IRQ, (Xil_ExceptionHandler)IRQHandler, NULL);
    XScuGic_SetPriorityTriggerType(&Intc, PL_IRQ, 0xA0, 0x3);
    XScuGic_Enable(&Intc, PL_IRQ);
}

// ========== DDS（控制 NCO 频率，幅度=0 时不输出 DAC） ==========
static uint32_t dds_freq_word(float freq_hz)
{
    uint64_t fw = (uint64_t)(freq_hz * (float)(1u << DDS_PHASE_B) / DDS_CLK_HZ + 0.5f);
    if (fw > 0x0FFFFFFF) fw = 0x0FFFFFFF;
    return (uint32_t)fw;
}

static void dds_set(float freq_hz, uint8_t wave, uint16_t phase, uint8_t amp)
{
    Xil_Out32(REG_DDS_FREQ, dds_freq_word(freq_hz));
    Xil_Out32(REG_DDS_CTRL, ((uint32_t)amp << 13) |
                             ((uint32_t)wave << 11) | (phase & 0x7FF));
}

// ========== ADC 采集 ==========
static void adc_capture(int16_t *ch1_buf, int16_t *ch2_buf, int bram_src)
{
    uint32_t ctrl = CTRL_ADC_SAMPLE;
    if (bram_src) ctrl |= CTRL_BRAM_SRC;

    Xil_Out32(REG_CTRL, 0);
    usleep(10);
    adc_done = 0;
    Xil_Out32(REG_CTRL, ctrl);
    while (!adc_done);
    for (int i = 0; i < ADC_SAMPLE_N; i++) {
        uint32_t v = Xil_In32(BRAM_ADDR + i * 4);
        ch2_buf[i] = (int16_t)(v & 0xFFFF);
        ch1_buf[i] = (int16_t)((v >> 16) & 0xFFFF);
    }
}

// ========== FFT 缓冲 ==========
static float32_t fft_in[FFT_N];
static float32_t fft_out[FFT_N];
static float32_t fft_mag[FFT_N / 2];
static arm_rfft_fast_instance_f32 rfft;

// ========== 屏显辅助：数字转文本发送 ==========
static void tjc_display_num(char* obj, int val)
{
    char buf[16];
    sprintf(buf, "%d", val);
    tjc_send_txt(obj, "txt", buf);
}

// ========== 波形绘制（合成: 各谐波叠加, cycles 个周期）==========
static void tjc_draw_wave(int n, float *harm_f, float *harm_v, float cycles)
{
    if (n <= 0 || harm_f[0] <= 0) return;
    float f0 = harm_f[0], T = cycles / f0;
    #define WAVE_PTS 500
    uint8_t wave[WAVE_PTS];
    for (int i = 0; i < WAVE_PTS; i++) {
        float t = T * i / (WAVE_PTS - 1), x = 0;
        for (int h = 0; h < n; h++)
            x += harm_v[h] * 0.5f * arm_sin_f32(2.0f*PI*harm_f[h]*t + 0);
        int y = (int)(x * 800.0f + 128.0f);
        if (y < 0) y = 0;
        if (y > 250) y = 250;
        wave[i] = (uint8_t)y;
    }
    // 左右翻转
    for (int i = 0; i < WAVE_PTS/2; i++) {
        uint8_t t = wave[i];
        wave[i] = wave[WAVE_PTS-1-i];
        wave[WAVE_PTS-1-i] = t;
    }
    tjc_send_string("cle s0.id,0");
    usleep(50 * 1000);
    tjc_wave_addt(wave, WAVE_PTS);
}

// ========== 频谱绘制（用缓存数据） ==========
static void tjc_draw_spectrum(void)
{
    if (!spectrum_valid) return;
    tjc_send_string("cle s0.id,0");
    usleep(50 * 1000);
    tjc_wave_addt(spectrum_cache, SPEC_PTS);
}

// ========== 频谱缓存生成（从 FFT 结果，区间取最大值，线性归一化） ==========
static void build_spectrum_cache(void)
{
    float max_mag = 1.0f;
    for (int k = SPEC_K0; k < SPEC_K1; k++)
        if (fft_mag[k] > max_mag) max_mag = fft_mag[k];

    int k_range = SPEC_K1 - SPEC_K0;
    for (int i = 0; i < SPEC_PTS; i++) {
        int k0 = SPEC_K0 + i * k_range / SPEC_PTS;
        int k1 = SPEC_K0 + (i+1) * k_range / SPEC_PTS;
        if (k1 > SPEC_K1) k1 = SPEC_K1;
        float best = 0;
        for (int k = k0; k < k1; k++)
            if (fft_mag[k] > best) best = fft_mag[k];
        int y = (int)(best * 250.0f / max_mag);
        if (y < 0) y = 0;
        if (y > 250) y = 250;
        spectrum_cache[i] = (uint8_t)y;
    }
    // 左右翻转
    for (int i = 0; i < SPEC_PTS/2; i++) {
        uint8_t t = spectrum_cache[i];
        spectrum_cache[i] = spectrum_cache[SPEC_PTS-1-i];
        spectrum_cache[SPEC_PTS-1-i] = t;
    }

    // 与上次比较，有变化才标记刷新（避免重复发送同一份数据）
    if (memcmp(prev_spectrum, spectrum_cache, SPEC_PTS) != 0) {
        memcpy(prev_spectrum, spectrum_cache, SPEC_PTS);
        spectrum_dirty = 1;
    }
    spectrum_valid = 1;
}

// ========== UI 服务：一次处理完所有已收到的按键帧 ==========
// 调用位置：主循环每轮、每次 ADC 采集后、DDC 测量之间、FFT 与 DDC 之间、重试之间
static void service_ui(void)
{
    tjc_poll();
    int32_t msg;
    while ((msg = tjc_get_frame()) != -1) {
        switch (msg) {
        case 1:  // 切换 1/3 周期显示（不影响测量）
            cycle_mode = !cycle_mode;
            if (view_mode == VIEW_WAVEFORM && measurement_valid)
                tjc_draw_wave(last_n_found, last_harm_f, last_harm_v,
                              cycle_mode ? 3.0f : 1.0f);
            break;
        case 2:  // 进入/保持测量模式（不是启停开关）
            app_mode = APP_MEASURE;
            break;
        case 3:  // 波形/频谱视图切换（立即显示缓存，不影响测量）
            view_mode = (view_mode == VIEW_WAVEFORM) ? VIEW_SPECTRUM : VIEW_WAVEFORM;
            if (view_mode == VIEW_WAVEFORM) {
                if (measurement_valid)
                    tjc_draw_wave(last_n_found, last_harm_f, last_harm_v,
                                  cycle_mode ? 3.0f : 1.0f);
            } else {
                if (spectrum_valid)
                    tjc_draw_spectrum();
            }
            break;
        case 4:  // 进入校准（安全边界执行，校准中重复按忽略）
            if (app_mode != APP_CALIBRATE) {
                app_mode = APP_CALIBRATE;
                cal_pending = 1;
            }
            break;
        }
    }
}

// ========== 校准（放大器模式，0~240mV 信号源，步进 20mV）==========
// 直接拟合整条物理链路: mag = k × V_sig + b（V_sig 为信号源端 Vpp）
// 正常测量: V_input = (mag + cal_b) / cal_k，不再手动乘除 5.5
// 放大器真实增益已包含在 cal_k 中
// 旧模型 V_adc=(mag-9.0)/3493.2, V_input=V_adc/5.5 → 合并 V_input=(mag-9.0)/(3493.2*5.5)
// 新模型直接输入信号源端: cal_k = 3493.2×5.5, 截距在原始 mag/pp 坐标轴上不换算
static float cal_k = 19212.6f, cal_b = -9.0f;        // DDC 幅值校准（屏显Vpp系数）
static float cal_k_adc = 4822.4f, cal_b_adc = -6.8f; // ADC 峰峰值校准（屏显Vpp系数）

#define CAL_TRIALS  8
#define CAL_STEPS   13   // 0mV + 20mV~240mV, step 20mV

// 到位判定窗口：旧系数估算值进入目标 ±5mV（小于 20mV 步进的一半）
#define CAL_GATE_MV 0.005f

static void calibrate(void)
{
    float mags[CAL_STEPS][CAL_TRIALS] = {{0}}, pps[CAL_STEPS][CAL_TRIALS] = {{0}};
    int valid = 0;
    static int16_t b1[ADC_SAMPLE_N], b2[ADC_SAMPLE_N];

    // 校准开始前保存旧系数快照（到位判断全程使用，中途不变）
    float gate_k = cal_k, gate_b = cal_b;

    dds_set(50000.0f, WAVE_SIN, 0, 255);

    for (int step = 0; step < CAL_STEPS; step++) {
        float v_sig = step * 0.02f;           // 信号源目标: 0, 20, ..., 240mV
        tjc_display_num("t32", (int)(v_sig * 1000 + 0.5f));

        // 每步等待用户把信号源调到目标值：旧系数估算进入 ±5mV 窗口连续 3 次
        // 旧系数仅作粗判据（防止上一档被误采）；最终拟合用原始 mag
        int near_cnt = 0;
        for (int timeout = 0; timeout < 60; timeout++) {
            service_ui();   // 长等待期间保持按键响应
            adc_capture(b1, b2, 1);
            int64_t si = 0, sq = 0;
            for (int i = 0; i < ADC_SAMPLE_N; i++) { si += (int64_t)b1[i]; sq += (int64_t)b2[i]; }
            float I = (float)si / ADC_SAMPLE_N, Q = (float)sq / ADC_SAMPLE_N, m;
            arm_sqrt_f32(I*I + Q*Q, &m);
            // 旧系数估算信号源端电压（仅作到位粗判据 + t31 参考显示）
            // gate_k 已是"mag per 信号源伏特"，无需再 /5.5
            float v_est = (m + gate_b) / gate_k;
            tjc_display_num("t31", (int)(v_est * 1000 + 0.5f));

            if (fabsf(v_est - v_sig) <= CAL_GATE_MV) near_cnt++; else near_cnt = 0;
            if (near_cnt >= 3) break;   // 目标附近连续 3 次 → 开始采集
            sleep(1);
        }

        // 超时未进入目标窗口（用户长时间未调好）：本档跳过，绝不把当前信号标记成目标档
        if (near_cnt < 3) continue;

        // 采集 8 次样本（等待已确认信号源到位，不再做旧系数容差检查）
        int ok = 0;
        for (int t = 0; t < CAL_TRIALS; t++) {
            service_ui();   // 长等待期间保持按键响应
            adc_capture(b1, b2, 1);
            int64_t si = 0, sq = 0;
            for (int i = 0; i < ADC_SAMPLE_N; i++) { si += (int64_t)(int16_t)b1[i]; sq += (int64_t)(int16_t)b2[i]; }
            float I = (float)si / ADC_SAMPLE_N, Q = (float)sq / ADC_SAMPLE_N, mag;
            arm_sqrt_f32(I*I + Q*Q, &mag);
            mags[step][ok] = mag; ok++;
            adc_capture(b1, b2, 0);
            float adc_max = -99999, adc_min = 99999;
            for (int i = 0; i < ADC_SAMPLE_N; i++) { if (b1[i] > adc_max) adc_max = b1[i]; if (b1[i] < adc_min) adc_min = b1[i]; }
            pps[step][ok-1] = adc_max - adc_min;
        }
        if (ok > 0) valid = step + 1;
    }

    // 线性回归到临时变量，验证合格后原子提交（失败完整保留旧系数）
    if (valid >= 3) {
        float new_k = 0, new_b = 0, new_k_adc = 0, new_b_adc = 0;
        int n = 0; float sx = 0, sy = 0, sxx = 0, sxy = 0;

        // DDC 幅值拟合: mag = k × V_adc + b
        for (int s = 0; s < valid; s++) {
            float vt = s * 0.02f;   // 信号源端 Vpp（含放大器，不再手动 ×5.5）
            for (int t = 0; t < CAL_TRIALS; t++) {
                if (mags[s][t] < 10) continue;
                sx += vt; sy += mags[s][t]; sxx += vt * vt; sxy += vt * mags[s][t]; n++;
            }
        }
        float denom = n * sxx - sx * sx;
        if (denom != 0.0f) {
            new_k = (n * sxy - sx * sy) / denom;
            new_b = (sy - new_k * sx) / n;
        }

        // ADC 峰峰值拟合: pp = k_adc × V_sig + b_adc（信号源端）
        int n2 = 0; float s2x = 0, s2y = 0, s2xx = 0, s2xy = 0;
        for (int s = 0; s < valid; s++) {
            float vt = s * 0.02f;
            for (int t = 0; t < CAL_TRIALS; t++) {
                if (pps[s][t] < 1) continue;
                s2x += vt; s2y += pps[s][t]; s2xx += vt * vt; s2xy += vt * pps[s][t]; n2++;
            }
        }
        float denom2 = n2 * s2xx - s2x * s2x;
        if (denom2 != 0.0f) {
            new_k_adc = (n2 * s2xy - s2x * s2y) / denom2;
            new_b_adc = (s2y - new_k_adc * s2x) / n2;
        }

        // 验证：有限性 + 斜率范围 + 平均相对残差
        int fit_ok = 1;
        if (n < 3 || denom == 0.0f || denom2 == 0.0f) fit_ok = 0;
        if (new_k != new_k || new_b != new_b || new_k_adc != new_k_adc || new_b_adc != new_b_adc) fit_ok = 0;
        if (new_k < 100.0f || new_k > 100000.0f) fit_ok = 0;
        if (new_k_adc < 10.0f || new_k_adc > 100000.0f) fit_ok = 0;
        float err_sum = 0; float err_max = 0; int err_n = 0;
        int good_steps = 0; int top_step = -1;
        if (fit_ok) {
            for (int s = 0; s < valid; s++) {
                float vt = s * 0.02f;
                int step_n = 0;
                for (int t = 0; t < CAL_TRIALS; t++) {
                    if (mags[s][t] < 10) continue;
                    // 归一化尺度带下限，避免零点（vt=0）除零
                    float scale = fabsf(new_k * vt + new_b);
                    if (scale < 10.0f) scale = 10.0f;
                    float e = fabsf(mags[s][t] - (new_k * vt + new_b)) / scale;
                    err_sum += e;
                    if (e > err_max) err_max = e;
                    err_n++;
                    step_n++;
                }
                if (step_n > 0) { good_steps++; top_step = s; }
            }
            float mean_err = (err_n > 0) ? err_sum / err_n : 1.0f;
            if (mean_err > 0.05f) fit_ok = 0;   // 平均相对残差 >5% → 拒绝
            if (err_max > 0.15f) fit_ok = 0;    // 最大单点残差 >15% → 拒绝
            if (good_steps < 5) fit_ok = 0;     // 有效档位 <5 → 拒绝
            if (top_step < 10) fit_ok = 0;      // 最高有效档 <200mV → 范围覆盖不足
        }

        if (fit_ok) {
            // 原子提交：全部通过后同时更新
            cal_k = new_k;      cal_b = -new_b;
            cal_k_adc = new_k_adc; cal_b_adc = -new_b_adc;
        }
        // 失败（fit_ok=0）：完整保留旧系数
    }
}

// ========== 单轮测量（后台持续执行，与显示视图解耦） ==========
// 顺序：ADC采集 → FFT → 频谱缓存 → 峰值 → 谐波投票 → DDC精确测量
//      → 门限 → 频率排序 → 基波保护 → 谐波复核 → RMS/Vpp → 缓存+显示
static void measure_round(void)
{
    float harm_f[MAX_PEAKS] = {0}, harm_v[MAX_PEAKS] = {0};
    float vpp_avg = 0, vrms_avg = 0;
    int n_found = 0;
    float fs_fft = FIR_FS_HZ;

    // 重试最多 3 次
    for (int retry = 0; retry < 3; retry++) {
        adc_capture(buf1, buf2, 0);
        service_ui();   // 采集完成后立即响应按键
        for (int i = 0; i < FFT_N; i++)
            fft_in[i] = (float32_t)buf1[i];
        for (int i = 0; i < FFT_N; i++) {
            float32_t w = 0.5f - 0.5f * arm_cos_f32(2.0f * PI * (float)i / (FFT_N - 1));
            fft_in[i] *= w;
        }
        arm_rfft_fast_init_f32(&rfft, FFT_N);
        arm_rfft_fast_f32(&rfft, fft_in, fft_out, 0);
        for (int k = 1; k < FFT_N / 2; k++) {
            float32_t re = fft_out[2*k], im = fft_out[2*k+1];
            arm_sqrt_f32(re*re + im*im, &fft_mag[k]);
        }

        // 用这次 FFT 直接缓存频谱（无论当前显示什么视图）
        build_spectrum_cache();
        if (view_mode == VIEW_SPECTRUM && spectrum_dirty) {
            tjc_draw_spectrum();   // 频谱视图下刷新最新频谱
            spectrum_dirty = 0;
        }
        service_ui();

        int min_bin = (int)(10000.0f / fs_fft * FFT_N);
        int peaks[MAX_PEAKS] = {0};
        float peak_mags[MAX_PEAKS]={0};
        for (int k = min_bin; k < FFT_N / 2; k++) {
            if (fft_mag[k] < 10.0f) continue;
            if (k > 1 && fft_mag[k] <= fft_mag[k-1]) continue;
            if (k < FFT_N/2-1 && fft_mag[k] <= fft_mag[k+1]) continue;
            for (int j = 0; j < MAX_PEAKS; j++) {
                if (fft_mag[k] > peak_mags[j]) {
                    for (int p = MAX_PEAKS-1; p > j; p--) { peaks[p]=peaks[p-1]; peak_mags[p]=peak_mags[p-1]; }
                    peaks[j]=k; peak_mags[j]=fft_mag[k]; break;
                }
            }
        }

        // 谐波投票：选被最多其他峰支持的候选频率作为基波
        float f_candidates[MAX_PEAKS];
        int n_candidates = 0;
        for (int p = 0; p < MAX_PEAKS && peaks[p] > 0; p++) {
            float f = (float)((int)(peaks[p] * fs_fft / FFT_N / 500.0f + 0.5f)) * 500.0f;
            if (f >= 10000.0f) f_candidates[n_candidates++] = f;
        }

        float f0 = 0;
        int best_votes = -1;
        for (int c = 0; c < n_candidates; c++) {
            int votes = 0;
            for (int p = 0; p < n_candidates; p++) {
                if (p == c) continue;
                float f_check = f_candidates[p];
                if (fabsf(f_check - f_candidates[c]) <= 500.0f) {
                    votes++;
                } else {
                    float ratio = f_check / f_candidates[c];
                    int n = (int)(ratio + 0.5f);
                    if (n >= 2 && fabsf(f_check - n * f_candidates[c]) <= 500.0f) {
                        votes++;
                    }
                }
            }
            if (votes > best_votes || (votes == best_votes && f_candidates[c] < f0)) {
                best_votes = votes;
                f0 = f_candidates[c];
            }
        }
        if (f0 == 0) { service_ui(); continue; }  // 没找到基波，重试

        float harm_f_raw[MAX_PEAKS] = {0};
        float harm_v_raw[MAX_PEAKS][MEAS_N];
        int found_raw = 0;
        for (int p = 0; p < MAX_PEAKS && peaks[p] > 0; p++) {
            float freq_raw = (float)peaks[p] * fs_fft / FFT_N;
            float freq = (float)((int)(freq_raw / 500.0f + 0.5f)) * 500.0f;
            if (freq > 501000.0f) continue;
            // 谐波约束: 基波豁免, 其他峰 n*f0 ±500Hz (n>=2)
            if (fabsf(freq - f0) > 500.0f) {
                float ratio = freq / f0;
                int n = (int)(ratio + 0.5f);
                if (n < 2) continue;
                if (fabsf(freq - n * f0) > 500.0f) continue;
            }
            dds_set(freq, WAVE_SIN, 0, 255);
            // 紧凑写入: found_raw 既是计数也是目标下标
            harm_f_raw[found_raw] = freq;
            for (int m = 0; m < MEAS_N; m++) {
                adc_capture(buf1, buf2, 1);
                service_ui();   // DDC 多次采集之间响应按键
                int64_t si = 0, sq = 0;
                for (int i = 0; i < ADC_SAMPLE_N; i++) { si+=(int64_t)(int16_t)buf1[i]; sq+=(int64_t)(int16_t)buf2[i]; }
                float I=(float)si/ADC_SAMPLE_N, Q=(float)sq/ADC_SAMPLE_N, mag;
                arm_sqrt_f32(I*I+Q*Q, &mag);
                float v = (mag + cal_b) / cal_k;
                if (v != v) v = 0;
                harm_v_raw[found_raw][m] = (v > 0.010f) ? v : 0.0f;
            }
            found_raw++;
        }
        service_ui();   // DDC 阶段结束后

        int real_found = 0;
        for (int p = 0; p < found_raw; p++) {
            for (int a = 0; a < MEAS_N-1; a++)
                for (int b = a+1; b < MEAS_N; b++)
                    if (harm_v_raw[p][a] > harm_v_raw[p][b]) { float t=harm_v_raw[p][a]; harm_v_raw[p][a]=harm_v_raw[p][b]; harm_v_raw[p][b]=t; }
            float sum = 0;
            for (int m = 1; m < MEAS_N-1; m++) sum += harm_v_raw[p][m];
            float v_screen = sum / (MEAS_N - 2);  // 屏显Vpp（cal_k 已含放大器增益）
            if (v_screen < 0.010f) continue;              // 屏显 >10mV
            harm_v[real_found] = v_screen;
            harm_f[real_found] = harm_f_raw[p];
            real_found++;
        }
        n_found = real_found;

        // 按频率排序（harm_f[0] = 最低频 = 基波）
        for (int a = 0; a < n_found-1; a++)
            for (int b = a+1; b < n_found; b++)
                if (harm_f[a] > harm_f[b]) {
                    float tf = harm_f[a]; harm_f[a] = harm_f[b]; harm_f[b] = tf;
                    float tv = harm_v[a]; harm_v[a] = harm_v[b]; harm_v[b] = tv;
                }

        // 基波保护：真正基波被 10mV 门限删除则本轮失败重试
        if (n_found == 0 || fabsf(harm_f[0] - f0) > 500.0f) continue;

        // 最终谐波复核：其余分量必须是显示基波的整数倍
        if (n_found > 1) {
            float base_f = harm_f[0];
            int valid = 1;  // harm_f[0] 保留
            for (int i = 1; i < n_found; i++) {
                float ratio = harm_f[i] / base_f;
                int n = (int)(ratio + 0.5f);
                if (n < 2 || fabsf(harm_f[i] - n * base_f) > 500.0f) continue;
                harm_f[valid] = harm_f[i];
                harm_v[valid] = harm_v[i];
                valid++;
            }
            n_found = valid;
        }

        if (n_found > 0) break;  // 成功
    }

    // 真有效值: 各分量 Vpp 平方和 → RMS（分量均为正弦，基于复核后最终列表）
    float rms_sq = 0;
    for (int i = 0; i < n_found; i++)
        rms_sq += harm_v[i] * harm_v[i];
    arm_sqrt_f32(rms_sq, &vrms_avg);
    vrms_avg *= 0.3536f;

    // 总 Vpp: 分量合成波形扫峰峰值（零相位叠加，与波形显示一致）
    // harm_f[0] 已是按频率排序后的基波，T0 = 基波周期
    if (n_found > 0) {
        float T0 = 1.0f / harm_f[0];
        float syn_max = -1e9f, syn_min = 1e9f;
        for (int i = 0; i <= 10000; i++) {
            float t = T0 * i / 10000.0f, x = 0;
            for (int h = 0; h < n_found; h++)
                x += harm_v[h] * 0.5f * arm_sin_f32(2.0f * PI * harm_f[h] * t);
            if (x > syn_max) syn_max = x;
            if (x < syn_min) syn_min = x;
        }
        vpp_avg = syn_max - syn_min;
    }

    // 屏显数字（波形/频谱视图均更新数字，s0 绘制按视图控制）
    char fobj[8], vobj[8], buf[16];
    for (int i = 0; i < MAX_PEAKS; i++) {
        sprintf(fobj, "t1%d", i);
        sprintf(vobj, "t2%d", i+1);
        if (i < n_found) {
            sprintf(buf, "%.1f", harm_f[i] / 1000.0f);
            tjc_send_txt(fobj, "txt", buf);
            tjc_display_num(vobj, (int)(harm_v[i] * 1000 + 0.5f));
        } else {
            tjc_send_txt(fobj, "txt", "0");
            tjc_send_txt(vobj, "txt", "0");
        }
    }

    if (n_found > 0) {
        sprintf(buf, "%.1f", harm_f[0] / 1000.0f);
        tjc_send_txt("t5", "txt", buf);
        tjc_display_num("t3", (int)(vpp_avg * 1000 + 0.5f));
        tjc_display_num("t4", (int)(vrms_avg * 1000 + 0.5f));

        // 更新测量缓存
        last_n_found = n_found;
        last_vpp = vpp_avg;
        last_vrms = vrms_avg;
        for (int i = 0; i < n_found; i++) {
            last_harm_f[i] = harm_f[i];
            last_harm_v[i] = harm_v[i];
        }
        measurement_valid = 1;

        // 测量完成后：波形视图刷新最新波形（不覆盖频谱视图）
        if (view_mode == VIEW_WAVEFORM)
            tjc_draw_wave(last_n_found, last_harm_f, last_harm_v,
                          cycle_mode ? 3.0f : 1.0f);
    } else {
        // 本轮未找到有效基波：清零，不保留互相矛盾的旧数值
        tjc_send_txt("t5", "txt", "0");
        tjc_send_txt("t3", "txt", "0");
        tjc_send_txt("t4", "txt", "0");
        for (int i = 0; i < MAX_PEAKS; i++) {
            sprintf(fobj, "t1%d", i);
            tjc_send_txt(fobj, "txt", "0");
            sprintf(vobj, "t2%d", i+1);
            tjc_send_txt(vobj, "txt", "0");
        }
        last_n_found = 0;
        measurement_valid = 0;
    }

    dds_set(0, WAVE_SIN, 0, 0);
}

// ========== 主函数 ==========
// 频率(kHz) t10-t14, 幅值(mV) t21-t25
// 校准: 实测 t31, 目标 t32
// 汇总: 全谐波Vpp t3, 全谐波Vrms t4, 基波频率 t5

int main(void)
{
    tjc_init();
    tjc_send_txt("t10", "txt", "123");  // 必须：首条消息防吞

    // 开机清零所有字符控件
    tjc_send_txt("t3", "txt", "0");
    tjc_send_txt("t4", "txt", "0");
    tjc_send_txt("t5", "txt", "0");
    tjc_send_txt("t31", "txt", "0");
    tjc_send_txt("t32", "txt", "0");
    for (int i = 0; i < MAX_PEAKS; i++) {
        char obj[8];
        sprintf(obj, "t1%d", i);
        tjc_send_txt(obj, "txt", "0");
        sprintf(obj, "t2%d", i+1);
        tjc_send_txt(obj, "txt", "0");
    }
    setup_irq();

    // DDS 关（幅度=0）
    dds_set(0, WAVE_SIN, 0, 0);

    // 上电默认测量模式，持续循环测量
    while (1) {
        service_ui();   // 主循环每轮开始：立即处理按键

        // 校准（安全边界：不在 ADC 数据处理中途切换）
        if (cal_pending) {
            cal_pending = 0;
            calibrate();
            dds_set(0, WAVE_SIN, 0, 0);
            app_mode = APP_MEASURE;   // 校准完成自动恢复测量
        }

        if (app_mode == APP_MEASURE)
            measure_round();

        usleep(50 * 1000);
    }

    return 0;
}
