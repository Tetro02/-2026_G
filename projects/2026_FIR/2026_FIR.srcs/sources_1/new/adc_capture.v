// ====================================================================
// adc_capture — ADC 抽取 + 双通道打包（纯数据流，不写 BRAM）
// decimate 由 PS 通过 slv_reg1[30:24] 控制
// 默认 10 → 5MSPS @ 50MHz
// ====================================================================

module adc_capture (
    input         clk,
    input         rst_n,

    input  [6:0]  decimate,
    input  [11:0] adc_data_1,
    input  [11:0] adc_data_2,

    output [31:0] data_out,
    output        data_valid
);

    reg [6:0] dec_cnt;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            dec_cnt <= 7'd0;
        else if (dec_cnt == decimate - 7'd1)
            dec_cnt <= 7'd0;
        else
            dec_cnt <= dec_cnt + 7'd1;
    end

    assign data_valid = (dec_cnt == 0);
    assign data_out = {4'd0, adc_data_1, 4'd0, adc_data_2};

endmodule
