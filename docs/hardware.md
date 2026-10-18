# 硬件与寄存器（主版本）

器件 **XC7Z020CLG484-1**，50 MHz FCLK_CLK0。ADC 输入 12 位，DAC 输出 14 位，GPIO 约束为 LVCMOS33。作者提供的设计报告明确了 AD9226 ADC、AD603 模拟前端及 7 英寸 TJC 屏；DAC 型号、具体模块版号和完整电源接线仍待补齐。

报告描述 BNC 输入、前端标称约 5.5 倍放大、电平移位后进入 AD9226，ADC 按 50 MSPS 工作；50～250 mVpp 信号经标称增益后约为 275～1375 mVpp。报告注明 ADC 量程为 2 Vpp，但其具体模块配置及偏置电路尚无可编辑设计可核对。前端图实际画有两级 AD603 和 OPA690，不能只凭“5.5 倍”文字推导各级增益。

报告采用外部单路 5 V 供电、装置内部变换所需电压。原文列出的各板内部电压轨没有完整原理图支持，因此不能直接作为芯片引脚供电说明。见 [报告参考与原图](report-reference.md)。

PS DDR、MIO、复位、晶振参数在 `design_1.bd`，迁移到另一块板前需按原理图核对。

## 寄存器

自定义 AXI-Lite 基址 `0x43C00000`，AXI BRAM 基址 `0x40000000`（最终以重新生成的 BSP 为准）。PL 完成中断连接 IRQ_F2P，软件 GIC ID 61。

| 偏移 | 内容 |
|---|---|
| 0x00 | DDS/NCO 频率字 [27:0]，对应 50 MHz / 2^28 |
| 0x04 | [30:24] 抽取控制；[20:13] 幅度；[12:11] 波形；[10:0] 相位 |
| 0x08 | [0] 触发；[1] DAC1 源；[2] BRAM 源；[3] DAC2 源 |
| 0x0C | DAC 直写数据 [13:0] |

FIR 固定抽取 40，50 MHz → 1.25 MS/s。FFT 长度 4096，PL BRAM 采样帧 16384 点。BRAM 内通道/符号格式参见 `top.v` 与 `capture_adc()`；原始采集帧和 DDC I/Q 帧应分别解读。

## 引脚表

从主版本 `1.xdc` 提取，作为约束索引，不代表在任意 Zynq-7020 开发板上均可使用。UART TX 为 L17 / 复制 TX AB14，RX 为 AB15；调试前检查是否与板上其他器件连接冲突。

| 信号 | 封装管脚 |
|---|---|
| `UART_0_1_txd` | L17 |
| `UART_0_1_rxd` | AB15 |
| `clk_out_adc_1` | AA13 |
| `clk_out_adc_2` | T22 |
| `adc_data_in_1[11]` | V14 |
| `adc_data_in_1[10]` | Y14 |
| `adc_data_in_1[9]` | AA14 |
| `adc_data_in_1[8]` | W16 |
| `adc_data_in_1[7]` | Y16 |
| `adc_data_in_1[6]` | AA17 |
| `adc_data_in_1[5]` | AB17 |
| `adc_data_in_1[4]` | W18 |
| `adc_data_in_1[3]` | W17 |
| `adc_data_in_1[2]` | V13 |
| `adc_data_in_1[1]` | W13 |
| `adc_data_in_1[0]` | Y13 |
| `adc_data_in_2[11]` | Y19 |
| `adc_data_in_2[10]` | AB19 |
| `adc_data_in_2[9]` | AB20 |
| `adc_data_in_2[8]` | AB21 |
| `adc_data_in_2[7]` | AA21 |
| `adc_data_in_2[6]` | AB22 |
| `adc_data_in_2[5]` | AA22 |
| `adc_data_in_2[4]` | Y21 |
| `adc_data_in_2[3]` | Y20 |
| `adc_data_in_2[2]` | W22 |
| `adc_data_in_2[1]` | V22 |
| `adc_data_in_2[0]` | U22 |
| `clk_out_dac` | B22 |
| `dac_data_out[13]` | C22 |
| `dac_data_out[12]` | D22 |
| `dac_data_out[11]` | E19 |
| `dac_data_out[10]` | E20 |
| `dac_data_out[9]` | F19 |
| `dac_data_out[8]` | G19 |
| `dac_data_out[7]` | C18 |
| `dac_data_out[6]` | C17 |
| `dac_data_out[5]` | G17 |
| `dac_data_out[4]` | F17 |
| `dac_data_out[3]` | F18 |
| `dac_data_out[2]` | E18 |
| `dac_data_out[1]` | H20 |
| `dac_data_out[0]` | H19 |
| `clk_out_dac_2` | G15 |
| `dac_data_out_2[13]` | F16 |
| `dac_data_out_2[12]` | E16 |
| `dac_data_out_2[11]` | C19 |
| `dac_data_out_2[10]` | D18 |
| `dac_data_out_2[9]` | E15 |
| `dac_data_out_2[8]` | D15 |
| `dac_data_out_2[7]` | D16 |
| `dac_data_out_2[6]` | D17 |
| `dac_data_out_2[5]` | C15 |
| `dac_data_out_2[4]` | B15 |
| `dac_data_out_2[3]` | C20 |
| `dac_data_out_2[2]` | D20 |
| `dac_data_out_2[1]` | A16 |
| `dac_data_out_2[0]` | A17 |
| `UART_0_2_txd` | AB14 |
