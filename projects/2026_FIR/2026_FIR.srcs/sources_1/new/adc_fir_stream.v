// ====================================================================
// adc_fir_stream — ADC offset-binary → FIR 抽取 (50M→1.25M)
// 仅 ADC1，AD9226 12-bit offset binary → signed13 FIR 输出
// ====================================================================

module adc_fir_stream (
    input  wire        clk,
    input  wire [11:0] adc_offset,     // AD9226 offset binary

    output wire signed [12:0] sample_out,
    output wire               sample_valid,
    output wire               sample_ready
);

    // offset binary → signed: 翻转 MSB
    wire signed [11:0] adc_signed;
    assign adc_signed = $signed(adc_offset ^ 12'h800);

    // 符号扩展至 16-bit AXI 端口
    wire [15:0] fir_s_tdata;
    assign fir_s_tdata = {{4{adc_signed[11]}}, adc_signed};

    wire        fir_m_valid;
    wire [15:0] fir_m_tdata;

    fir_compiler_0 u_fir_adc1 (
        .aclk               (clk),
        .s_axis_data_tvalid (1'b1),
        .s_axis_data_tready (sample_ready),
        .s_axis_data_tdata  (fir_s_tdata),
        .m_axis_data_tvalid (fir_m_valid),
        .m_axis_data_tdata  (fir_m_tdata)
    );

    // 输出取低13位（bit12 是符号位）
    assign sample_out   = $signed(fir_m_tdata[12:0]);
    assign sample_valid = fir_m_valid;

endmodule
