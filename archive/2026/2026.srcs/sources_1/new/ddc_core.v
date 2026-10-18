// ====================================================================
// ddc_core — 数字下变频（内部两路 dds_core 做 NCO + 混频器 + 可选 FIR）
// NCO 频率由 PS 通过 slv_reg0 控制，与 DDS/DAC 同频
// ====================================================================

module ddc_core #(
    parameter FIR_ENABLE = 0     // 0=bypass, 1=使能 FIR
) (
    input         clk,
    input         rst_n,

    input  [27:0] freq_word,                // PS 频率字，与 DDS 共享 slv_reg0[27:0]
    input  [11:0] adc_data,                 // ADC 单通道, unsigned

    output [31:0] iq_out,                   // {I[15:0], Q[15:0]}
    output        iq_valid
);

    // ==================== NCO = 两路 dds_core，复用已验证模块 ====================
    // cos: phase_off=0,  sin: phase_off+512=90°
    wire [13:0] nco_cos_raw, nco_sin_raw;   // dds_core 输出已是 2's complement

    dds_core #(.PHASE_W(28), .ADDR_W(11), .DATA_W(14)) u_nco_cos (
        .clk       (clk),
        .rst_n     (rst_n),
        .freq_word (freq_word),
        .phase_off (11'd0),
        .wave_sel  (2'd0),                  // 正弦
        .amplitude (8'd255),                // 满幅
        .dac_out   (nco_cos_raw)
    );

    dds_core #(.PHASE_W(28), .ADDR_W(11), .DATA_W(14)) u_nco_sin (
        .clk       (clk),
        .rst_n     (rst_n),
        .freq_word (freq_word),
        .phase_off (11'd512),               // 90° 移相
        .wave_sel  (2'd0),
        .amplitude (8'd255),
        .dac_out   (nco_sin_raw)
    );

    // ==================== ADC unsigned → signed ====================
    wire signed [12:0] adc_signed;
    assign adc_signed = $signed({1'b0, adc_data}) - $signed(13'd2048);

    // ==================== 混频器（1 拍流水线） ====================
    // signed 13-bit(ADC) × signed 14-bit(NCO) → signed 27-bit
    reg signed [26:0] mix_i, mix_q;
    reg               mix_valid;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            mix_i     <= 0;
            mix_q     <= 0;
            mix_valid <= 1'b0;
        end
        else begin
            mix_i     <= adc_signed * $signed(nco_cos_raw);
            mix_q     <= adc_signed * $signed(nco_sin_raw);
            mix_valid <= 1'b1;
        end
    end

    // ==================== FIR / Bypass ====================
    wire signed [15:0] i_out, q_out;
    wire               out_valid;

    if (FIR_ENABLE) begin : gen_fir
        // TODO: FIR Compiler IP AXI-Stream 接口
        assign i_out     = mix_i[24:9];
        assign q_out     = mix_q[24:9];
        assign out_valid = mix_valid;
    end
    else begin : gen_bypass
        assign i_out     = mix_i[24:9];     // 27-bit → 16-bit 截断
        assign q_out     = mix_q[24:9];
        assign out_valid = mix_valid;
    end

    // ==================== IQ 打包输出 ====================
    assign iq_out   = {i_out[15:0], q_out[15:0]};
    assign iq_valid = out_valid;

endmodule
