// ====================================================================
// ddc_core — 数字下变频（内部两路 dds_core 做 NCO + 混频器）
// FIR 版: 输入为 signed13(FIR输出), 仅 adc_valid=1 时更新混频
// ====================================================================

module ddc_core #(
    parameter FIR_ENABLE = 0
) (
    input                 clk,
    input                 rst_n,

    input  [27:0]         freq_word,
    input  wire signed [12:0] adc_data,     // FIR 输出 signed13
    input  wire               adc_valid,    // FIR 输出有效 (1.25MSPS)

    output [31:0]         iq_out,
    output                iq_valid
);

    // ==================== NCO（50MHz 连续运行） ====================
    wire [13:0] nco_cos_raw, nco_sin_raw;

    dds_core #(.PHASE_W(28), .ADDR_W(11), .DATA_W(14)) u_nco_cos (
        .clk       (clk),
        .rst_n     (rst_n),
        .freq_word (freq_word),
        .phase_off (11'd0),
        .wave_sel  (2'd0),
        .amplitude (8'd255),
        .dac_out   (nco_cos_raw)
    );

    dds_core #(.PHASE_W(28), .ADDR_W(11), .DATA_W(14)) u_nco_sin (
        .clk       (clk),
        .rst_n     (rst_n),
        .freq_word (freq_word),
        .phase_off (11'd512),
        .wave_sel  (2'd0),
        .amplitude (8'd255),
        .dac_out   (nco_sin_raw)
    );

    // ==================== 混频器（仅在 FIR 有效时更新） ====================
    reg signed [26:0] mix_i, mix_q;
    reg               mix_valid;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            mix_i     <= 27'sd0;
            mix_q     <= 27'sd0;
            mix_valid <= 1'b0;
        end
        else begin
            mix_valid <= adc_valid;
            if (adc_valid) begin
                mix_i <= adc_data * $signed(nco_cos_raw);
                mix_q <= adc_data * $signed(nco_sin_raw);
            end
        end
    end

    // ==================== 输出 ====================
    wire signed [15:0] i_out = mix_i[24:9];
    wire signed [15:0] q_out = mix_q[24:9];
    assign iq_out   = {i_out[15:0], q_out[15:0]};
    assign iq_valid = mix_valid;

endmodule
