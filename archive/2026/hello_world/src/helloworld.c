// ========== 2026 电赛底板 - ADC + FFT + DDC + 谐波分析 ==========
// slv_reg0[27:0]=DDS频率 reg1[30:24]=抽取 [20:13]=幅度 [12:11]=波形 [10:0]=相位
// slv_reg2[0]=触发 [1]=DAC1源 [2]=BRAM源 [3]=DAC2源

#include <stdio.h>
#include <stdint.h>
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

// ========== 校准（放大器模式，20mV~240mV 信号源，步进 20mV）==========
// 校准系数: mag → ADC端Vpp, 显示时再/5.5还原为信号源Vpp
static float cal_k = 3493.2f, cal_b = -9.0f;       // DDC 幅值校准
static float cal_k_adc = 876.8f, cal_b_adc = -6.8f; // ADC 峰峰值校准

#define CAL_TRIALS  8
#define CAL_STEPS   13   // 0mV + 20mV~240mV, step 20mV

static void calibrate(void)
{
    float mags[CAL_STEPS][CAL_TRIALS] = {{0}}, pps[CAL_STEPS][CAL_TRIALS] = {{0}};
    int valid;
    static int16_t b1[ADC_SAMPLE_N], b2[ADC_SAMPLE_N];

    dds_set(50000.0f, WAVE_SIN, 0, 255);

    for (int step = 0; step < CAL_STEPS; step++) {
        float v_sig = step * 0.02f;           // 信号源: 0, 20, ..., 240mV
        float v_adc = v_sig * 5.5f;            // ADC端实际电压
        tjc_display_num("t32", (int)(v_sig * 1000 + 0.5f));
        int ok = 0;
        for (int t = 0; t < CAL_TRIALS; t++) {
            adc_capture(b1, b2, 1);
            int64_t si = 0, sq = 0;
            for (int i = 0; i < ADC_SAMPLE_N; i++) { si += (int64_t)(int16_t)b1[i]; sq += (int64_t)(int16_t)b2[i]; }
            float I = (float)si / ADC_SAMPLE_N, Q = (float)sq / ADC_SAMPLE_N, mag;
            arm_sqrt_f32(I*I + Q*Q, &mag);
            float v_meas = (mag + cal_b) / cal_k;
            if (v_meas != v_meas) v_meas = 0;
            tjc_display_num("t31", (int)(v_meas / 5.5f * 1000 + 0.5f));
            // 0mV 点无容差检查（噪底 mag 换算后不会接近 0），直接采
            if (v_adc > 0.0f && fabsf(v_meas - v_adc) > 0.028f) { t--; sleep(1); continue; }
            mags[step][ok] = mag; ok++;
            adc_capture(b1, b2, 0);
            float adc_max = -99999, adc_min = 99999;
            for (int i = 0; i < ADC_SAMPLE_N; i++) { if (b1[i] > adc_max) adc_max = b1[i]; if (b1[i] < adc_min) adc_min = b1[i]; }
            pps[step][ok-1] = adc_max - adc_min;
        }
        if (ok > 0) valid = step + 1;
    }

    // 线性回归: mag = k × V_adc + b,  pp = k_adc × V_adc + b_adc
    if (valid >= 3) {
        float sx = 0, sy = 0, sxx = 0, sxy = 0; int n = 0;
        for (int s = 0; s < valid; s++) {
            float vt = (0.02f + s * 0.02f) * 5.5f;
            for (int t = 0; t < CAL_TRIALS; t++) { if (mags[s][t] < 10) continue; sx += vt; sy += mags[s][t]; sxx += vt * vt; sxy += vt * mags[s][t]; n++; }
        }
        cal_k = (n * sxy - sx * sy) / (n * sxx - sx * sx);
        float b = (sy - cal_k * sx) / n; cal_b = -b;
        sx = 0; sy = 0; sxx = 0; sxy = 0; n = 0;
        for (int s = 0; s < valid; s++) {
            float vt = (0.02f + s * 0.02f) * 5.5f;
            for (int t = 0; t < CAL_TRIALS; t++) { if (pps[s][t] < 1) continue; sx += vt; sy += pps[s][t]; sxx += vt * vt; sxy += vt * pps[s][t]; n++; }
        }
        cal_k_adc = (n * sxy - sx * sy) / (n * sxx - sx * sx);
        b = (sy - cal_k_adc * sx) / n; cal_b_adc = -b;
        if (cal_k < 100.0f || cal_k != cal_k) cal_k = 3493.2f;
        if (cal_k_adc < 10.0f || cal_k_adc != cal_k_adc) cal_k_adc = 876.8f;
        printf("\r\n=== Cal Done: cal_k=%.1f cal_b=%.1f cal_k_adc=%.1f cal_b_adc=%.1f ===\r\n",
               cal_k, cal_b, cal_k_adc, cal_b_adc);
    }
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

// ========== 主函数 ==========
// 频率(kHz) t10-t14, 幅值(mV) t21-t25
// 校准: 实测 t31, 目标 t32
// 汇总: 全谐波Vpp t3, 全谐波Vrms t4, 基波频率 t5
#define MAX_PEAKS 5

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
    for (int i = 0; i < 5; i++) {
        char obj[8];
        sprintf(obj, "t1%d", i);
        tjc_send_txt(obj, "txt", "0");
        sprintf(obj, "t2%d", i+1);
        tjc_send_txt(obj, "txt", "0");
    }
    setup_irq();

    // DDS 关（幅度=0），5MSPS
    dds_set(0, WAVE_SIN, 0, 0);
    float fs_fft = FIR_FS_HZ;

    static int16_t buf1[ADC_SAMPLE_N], buf2[ADC_SAMPLE_N];
    int cycle_mode = 0;  // 0=1周期, 1=3周期

    // 上次成功测量的结果，供周期切换重绘
    static float last_harm_f[MAX_PEAKS] = {0};
    static float last_harm_v[MAX_PEAKS] = {0};
    static int last_n_found = 0;

    while (1) {
        tjc_poll();
        int32_t msg = tjc_get_frame();

        // ---- 消息4: 校准 ----
        if (msg == 4) {
            calibrate();
            dds_set(0, WAVE_SIN, 0, 0);
        }

        // ---- 消息1: 切换周期数, 用上次测量结果直接重绘波形 ----
        if (msg == 1) {
            cycle_mode = !cycle_mode;
            tjc_draw_wave(last_n_found, last_harm_f, last_harm_v,
                          cycle_mode ? 3.0f : 1.0f);
        }

        // ---- 消息2: 20次采样取平均（剔除最大最小） ----
        #define MEAS_N  14

        if (msg == 2) {
            float harm_f[MAX_PEAKS] = {0}, harm_v[MAX_PEAKS] = {0};
            float vpp_avg = 0, vrms_avg = 0;
            int n_found = 0;

            // 重试最多 3 次
            for (int retry = 0; retry < 3; retry++) {
                adc_capture(buf1, buf2, 0);
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
                if (f0 == 0) continue;  // 没找到基波，重试

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

                int real_found = 0;
                for (int p = 0; p < found_raw; p++) {
                    for (int a = 0; a < MEAS_N-1; a++)
                        for (int b = a+1; b < MEAS_N; b++)
                            if (harm_v_raw[p][a] > harm_v_raw[p][b]) { float t=harm_v_raw[p][a]; harm_v_raw[p][a]=harm_v_raw[p][b]; harm_v_raw[p][b]=t; }
                    float sum = 0;
                    for (int m = 1; m < MEAS_N-1; m++) sum += harm_v_raw[p][m];
                    float v_screen = (sum / (MEAS_N - 2)) / 5.5f;  // 转为屏显Vpp
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

            // Step 4: 屏显
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

                // 保存结果供周期切换重绘
                last_n_found = n_found;
                for (int i = 0; i < n_found; i++) {
                    last_harm_f[i] = harm_f[i];
                    last_harm_v[i] = harm_v[i];
                }

                // 波形
                tjc_draw_wave(n_found, harm_f, harm_v, cycle_mode ? 3.0f : 1.0f);
            }

            dds_set(0, WAVE_SIN, 0, 0);
        }

        // ---- 消息3: 频谱图 ----
        if (msg == 3) {
            adc_capture(buf1, buf2, 0);

            for (int i = 0; i < FFT_N; i++)
                fft_in[i] = (float32_t)buf1[i];
            for (int i = 0; i < FFT_N; i++) {
                float32_t w = 0.5f - 0.5f * arm_cos_f32(2.0f * PI * (float)i / (FFT_N - 1));
                fft_in[i] *= w;
            }
            arm_rfft_fast_init_f32(&rfft, FFT_N);
            arm_rfft_fast_f32(&rfft, fft_in, fft_out, 0);

            // 更新 fft_mag
            for (int k = 1; k < FFT_N/2; k++) {
                float32_t re = fft_out[2*k], im = fft_out[2*k+1];
                arm_sqrt_f32(re*re + im*im, &fft_mag[k]);
            }

            // 频谱 500 点，区间取最大值，线性归一化
            #define SPEC_PTS 500
            #define SPEC_K0  33    // ~10kHz
            #define SPEC_K1  1638  // ~500kHz
            uint8_t spec[SPEC_PTS];
            int spec_N = FFT_N / 2;

            // 找区间内最大值做参考
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
                spec[i] = (uint8_t)y;
            }
            // 左右翻转
            for (int i = 0; i < SPEC_PTS/2; i++) {
                uint8_t t = spec[i]; spec[i] = spec[SPEC_PTS-1-i]; spec[SPEC_PTS-1-i] = t;
            }
            tjc_send_string("cle s0.id,0");
            usleep(50 * 1000);
            tjc_wave_addt(spec, SPEC_PTS);
        }

        usleep(50 * 1000);
    }

    return 0;
}
