// ====================================================================
// bram_writer — 源无关 BRAM 写入器
// enable=0 → 复位(done=0, addr=0); enable=1 & !done → 逐笔写 BRAM
// 满 SAMPLE_MAX 笔后 done=1 → IRQ_F2P
// ====================================================================

module bram_writer #(
    parameter SAMPLE_MAX = 16384     // 采集点数
) (
    input         clk,
    input         enable,            // slv_reg2[0]: 0=复位, 1=采集
    input  [31:0] data_in,
    input         data_valid,

    output reg [16:0] addrb,         // 字节地址 (×4)
    output reg [31:0] dinb,
    output reg [ 3:0] web,
    output reg        done            // → IRQ_F2P
);

    localparam CNT_W = $clog2(SAMPLE_MAX);    // 14 for 16384

    // ===================== 采样计数器 + 字节地址 =====================
    reg [CNT_W-1:0] sample_cnt;
    wire [16:0]     byte_addr;
    assign byte_addr = { {(17-CNT_W){1'b0}}, sample_cnt } * 17'd4;

    always @(posedge clk) begin
        if (!enable) begin
            sample_cnt <= 0;
            done        <= 1'b0;
        end
        else if (!done) begin
            if (data_valid && sample_cnt < SAMPLE_MAX - 1) begin
                sample_cnt <= sample_cnt + 1'b1;
                done        <= 1'b0;
            end
            else if (data_valid && sample_cnt == SAMPLE_MAX - 1) begin
                sample_cnt <= sample_cnt;       // 停住
                done        <= 1'b1;
            end
        end
    end

    // ===================== 写 BRAM =====================
    always @(posedge clk) begin
        if (!enable) begin
            addrb <= 17'd0;
            dinb  <= 32'd0;
            web   <= 4'b0000;
        end
        else if (data_valid && !done) begin
            addrb <= byte_addr;
            dinb  <= data_in;
            web   <= 4'b1111;
        end
        else begin
            web   <= 4'b0000;
        end
    end

endmodule
