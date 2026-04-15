module adc_bram_sample(
    input         clk_bram,   // 50MHz
    input         rst_n,

    input  [11:0] adc_data,   // AD9226输出

    output reg [16:0] addrb,
    output reg [31:0] dinb,
    output reg [3:0]  web
);

// ===================== 参数 =====================
localparam SAMPLE_MAX = 12'd4095;  // 4096点
localparam DECIMATE   = 167;       // 抽取比（≈300kHz）

// ===================== 抽取计数器 =====================
reg [7:0] dec_cnt;
wire sample_en;

always @(posedge clk_bram or negedge rst_n) begin
    if(!rst_n)
        dec_cnt <= 0;
    else if(dec_cnt == DECIMATE-1)
        dec_cnt <= 0;
    else
        dec_cnt <= dec_cnt + 1;
end

assign sample_en = (dec_cnt == 0);

// ===================== 采样计数 =====================
reg [11:0] sample_cnt;

always @(posedge clk_bram or negedge rst_n) begin
    if(!rst_n)
        sample_cnt <= 0;
    else if(sample_en) begin
        if(sample_cnt < SAMPLE_MAX)
            sample_cnt <= sample_cnt + 1;
    end
end

// ===================== 写BRAM =====================
always @(posedge clk_bram or negedge rst_n) begin
    if(!rst_n) begin
        addrb <= 0;
        dinb  <= 0;
        web   <= 0;
    end
    else if(sample_en && sample_cnt <= SAMPLE_MAX) begin
        addrb <= sample_cnt * 4;

        // 12bit → 32bit（右对齐）
        dinb  <= {20'd0, adc_data};

        web   <= 4'b1111;
    end
    else begin
        web <= 4'b0000;
    end
end

endmodule