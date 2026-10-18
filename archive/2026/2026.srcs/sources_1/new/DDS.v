`timescale 1ns / 1ps

// ============================================================================
// DDS 核心 — 与 7020_DDS 仿真验证版完全一致
// 2's complement 输出（AD9744 有符号模式）
// ============================================================================

module dds_core #(
    parameter PHASE_W = 28,
    parameter ADDR_W  = 11,
    parameter DATA_W  = 14
) (
    input                       clk,
    input                       rst_n,
    input  [PHASE_W-1:0]        freq_word,
    input  [ADDR_W-1:0]         phase_off,
    input  [1:0]                wave_sel,
    input  [7:0]                amplitude,
    output [DATA_W-1:0]         dac_out
);
    localparam DC_BIAS = (1 << (DATA_W - 1));  // 8192

    // 相位累加器
    reg [PHASE_W-1:0] phase_acc = 0;
    always @(posedge clk or negedge rst_n)
        if (!rst_n) phase_acc <= 0;
        else        phase_acc <= phase_acc + freq_word;

    // ROM 地址
    wire [ADDR_W-1:0] base_addr  = phase_acc[PHASE_W-1 -: ADDR_W];
    wire [ADDR_W-1:0] final_addr = base_addr + phase_off;
    wire [ADDR_W+1:0] rom_addr   = {wave_sel, final_addr};

    // ROM
    wire [DATA_W-1:0] wave_raw;
    wave_rom u_rom (.clka(clk), .addra(rom_addr), .douta(wave_raw));

    // unsigned → signed: 减去 DC 偏置
    wire signed [DATA_W:0] wave_signed;
    assign wave_signed = $signed({1'b0, wave_raw}) - $signed(DC_BIAS);

    // signed × amplitude（1 拍流水线）
    reg signed [DATA_W+8:0] wave_mult = 0;
    always @(posedge clk or negedge rst_n)
        if (!rst_n) wave_mult <= 0;
        else        wave_mult <= wave_signed * $signed({1'b0, amplitude});

    // +128 舍入
    wire signed [DATA_W+8:0] wave_mult_rnd;
    assign wave_mult_rnd = wave_mult + {{(DATA_W+7){1'b0}}, 1'b1, {7{1'b0}}};

    // 右移 8 位 + 恢复 DC 偏置（offset binary 空间）
    wire signed [DATA_W:0] wave_norm;
    assign wave_norm = $signed(wave_mult_rnd[DATA_W+8 -: DATA_W+1]) + $signed(DC_BIAS);

    // 钳位 [1, 16382]
    wire [DATA_W-1:0] wave_scaled;
    assign wave_scaled = (wave_norm <= 1)
                         ? { {(DATA_W-1){1'b0}}, 1'b1 }
                         : (wave_norm >= (1 << DATA_W) - 2)
                           ? (1 << DATA_W) - 2
                           : wave_norm[DATA_W-1:0];

    // offset binary → 2's complement（AD9744 有符号）
    assign dac_out = wave_scaled ^ 14'h2000;

endmodule
