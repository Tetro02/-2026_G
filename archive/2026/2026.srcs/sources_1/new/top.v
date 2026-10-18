module Test_Top (
    // ===================== DDR（Zynq PS 固定端口） =====================
    inout [14:0] DDR_addr,
    inout [ 2:0] DDR_ba,
    inout        DDR_cas_n,
    inout        DDR_ck_n,
    inout        DDR_ck_p,
    inout        DDR_cke,
    inout        DDR_cs_n,
    inout [ 3:0] DDR_dm,
    inout [31:0] DDR_dq,
    inout [ 3:0] DDR_dqs_n,
    inout [ 3:0] DDR_dqs_p,
    inout        DDR_odt,
    inout        DDR_ras_n,
    inout        DDR_reset_n,
    inout        DDR_we_n,

    // ===================== FIXED_IO（Zynq PS 固定端口） =====================
    inout        FIXED_IO_ddr_vrn,
    inout        FIXED_IO_ddr_vrp,
    inout [53:0] FIXED_IO_mio,
    inout        FIXED_IO_ps_clk,
    inout        FIXED_IO_ps_porb,
    inout        FIXED_IO_ps_srstb,

    // ===================== UART（PS 引出） =====================
    input  UART_0_1_rxd,
    output UART_0_1_txd,

    output UART_0_2_txd,


    // ===================== ADC 接口 =====================
    input  [11:0] adc_data_in_1,
    input  [11:0] adc_data_in_2,
    output        clk_out_adc_1,
    output        clk_out_adc_2,

    // ===================== DAC 接口 =====================
    output [13:0] dac_data_out,
    output        clk_out_dac,

    // ===================== DAC2 接口（NCO 输出） =====================
    output [13:0] dac_data_out_2,
    output        clk_out_dac_2
);

    assign UART_0_2_txd = UART_0_1_txd;  // TX 两个口都输出


    // =========================================================
    //  1. PS → PL 控制信号（AXI-Lite 寄存器映射）
    //   slv_reg0: DDS 频率字  [27:0]
    //   slv_reg1: DDS 控制    [12:11]=波形, [10:0]=相位, [20:13]=幅度
    //   slv_reg2: 控制/状态   [0]=ADC采集触发, [1]=DAC1源, [2]=BRAM源, [3]=DAC2源
    //   slv_reg3: DAC直写数据 [13:0]（slv_reg2[1]=1 或 [3]=1 时有效）
    // =========================================================

    wire        FCLK_CLK0;
    wire [31:0] dds_ctrl_reg0;
    wire [31:0] dds_ctrl_reg1;
    wire [31:0] ctrl_reg2;
    wire [31:0] slv_reg3;

    // =========================================================
    //  2. DDS（已验证 dds_core，2's complement 输出）
    // =========================================================

    wire [13:0] dds_dac_out;
    wire        dds_rst_n = 1'b1;  // 始终使能

    dds_core #(
        .PHASE_W(28),
        .ADDR_W (11),
        .DATA_W (14)
    ) u_dds (
        .clk      (FCLK_CLK0),
        .rst_n    (dds_rst_n),
        .freq_word(dds_ctrl_reg0[27:0]),
        .phase_off(dds_ctrl_reg1[10:0]),
        .wave_sel (dds_ctrl_reg1[12:11]),
        .amplitude(dds_ctrl_reg1[20:13]),
        .dac_out  (dds_dac_out)
    );

    // =========================================================
    //  2b. DAC2 DDS — 与 u_dds 共享 slv_reg0/slv_reg1
    //      phase_off + 512 = 90° 移相
    // =========================================================

    wire [13:0] dds2_dac_out;
    wire [10:0] dds2_phase = dds_ctrl_reg1[10:0] + 11'd512;

    dds_core #(
        .PHASE_W(28),
        .ADDR_W (11),
        .DATA_W (14)
    ) u_dds2 (
        .clk      (FCLK_CLK0),
        .rst_n    (1'b1),
        .freq_word(dds_ctrl_reg0[27:0]),
        .phase_off(dds2_phase),
        .wave_sel (dds_ctrl_reg1[12:11]),
        .amplitude(dds_ctrl_reg1[20:13]),
        .dac_out  (dds2_dac_out)
    );

    // =========================================================
    //  3. DAC 输出（来源切换）
    //     slv_reg2[1]=0 → DDS 模式
    //     slv_reg2[1]=1 → PS 直写模式（slv_reg3[13:0]）
    // =========================================================

    wire        dac_src_sel = ctrl_reg2[1];
    wire [13:0] dac_raw = dac_src_sel ? slv_reg3[13:0] : dds_dac_out;

    reg  [13:0] dac_data_out_reg;
    assign dac_data_out = dac_data_out_reg;

    always @(posedge FCLK_CLK0) dac_data_out_reg <= dac_raw;

    // =========================================================
    //  3b. DAC2 输出（来源切换，与 DAC1 结构对称）
    //     slv_reg2[3]=0 → DDS2 模式（90° 移相）
    //     slv_reg2[3]=1 → PS 直写模式（slv_reg3[13:0]）
    // =========================================================

    wire        dac2_src_sel = ctrl_reg2[3];
    wire [13:0] dac2_raw = dac2_src_sel ? slv_reg3[13:0] : dds2_dac_out;

    reg  [13:0] dac2_data_out_reg;
    assign dac_data_out_2 = dac2_data_out_reg;

    always @(posedge FCLK_CLK0) dac2_data_out_reg <= dac2_raw;

    // =========================================================
    //  4. ODDR 时钟输出（ADC / DAC 采样时钟）
    // =========================================================

    ODDR #(
        .DDR_CLK_EDGE("OPPOSITE_EDGE"),
        .INIT        (1'b0),
        .SRTYPE      ("SYNC")
    ) u_oddr_clk_adc1 (
        .Q (clk_out_adc_1),
        .C (FCLK_CLK0),
        .CE(1'b1),
        .D1(1'b1),
        .D2(1'b0),
        .R (1'b0),
        .S (1'b0)
    );

    ODDR #(
        .DDR_CLK_EDGE("OPPOSITE_EDGE"),
        .INIT        (1'b0),
        .SRTYPE      ("SYNC")
    ) u_oddr_clk_adc2 (
        .Q (clk_out_adc_2),
        .C (FCLK_CLK0),
        .CE(1'b1),
        .D1(1'b1),
        .D2(1'b0),
        .R (1'b0),
        .S (1'b0)
    );

    ODDR #(
        .DDR_CLK_EDGE("OPPOSITE_EDGE"),
        .INIT        (1'b0),
        .SRTYPE      ("SYNC")
    ) u_oddr_clk_dac (
        .Q (clk_out_dac),
        .C (FCLK_CLK0),
        .CE(1'b1),
        .D1(1'b1),
        .D2(1'b0),
        .R (1'b0),
        .S (1'b0)
    );

    ODDR #(
        .DDR_CLK_EDGE("OPPOSITE_EDGE"),
        .INIT        (1'b0),
        .SRTYPE      ("SYNC")
    ) u_oddr_clk_dac2 (
        .Q (clk_out_dac_2),
        .C (FCLK_CLK0),
        .CE(1'b1),
        .D1(1'b1),
        .D2(1'b0),
        .R (1'b0),
        .S (1'b0)
    );

    // =========================================================
    //  5. ADC 采集（抽取 + 打包）→ 纯数据流
    // =========================================================

    wire [31:0] adc_stream_data;
    wire        adc_stream_valid;

    adc_capture u_adc_capture (
        .clk       (FCLK_CLK0),
        .rst_n     (ctrl_reg2[0]),          // trigger=0 时复位抽取计数器
        .decimate  (dds_ctrl_reg1[30:24]),  // PS 控制采样率
        .adc_data_1(adc_data_in_1),
        .adc_data_2(adc_data_in_2),
        .data_out  (adc_stream_data),
        .data_valid(adc_stream_valid)
    );

    // =========================================================
    //  5b. DDC 数字下变频 + BRAM 源选择
    //      slv_reg2[2]=0: ADC直采, =1: DDC IQ
    // =========================================================

    wire [31:0] bram_data_in;
    wire        bram_data_valid;
    wire [31:0] ddc_iq_data;
    wire        ddc_iq_valid;

    ddc_core #(
        .FIR_ENABLE(0)
    ) u_ddc (
        .clk      (FCLK_CLK0),
        .rst_n    (ctrl_reg2[0]),         // 与 ADC 捕获同步复位
        .freq_word(dds_ctrl_reg0[27:0]),  // 与 DDS 同频，PS 通过 slv_reg0 控制
        .adc_data (adc_data_in_1),        // ADC1 输入
        .iq_out   (ddc_iq_data),
        .iq_valid (ddc_iq_valid)
    );

    assign bram_data_in = ctrl_reg2[2] ? ddc_iq_data : adc_stream_data;
    assign bram_data_valid = (ctrl_reg2[2] ? ddc_iq_valid : adc_stream_valid) & adc_stream_valid;

    // =========================================================
    //  5c. BRAM 写入器（源无关）
    // =========================================================

    wire        end_adc_flag;
    wire [16:0] addrb;
    wire [31:0] dinb;
    wire [ 3:0] web;

    bram_writer #(
        .SAMPLE_MAX(16384)
    ) u_bram_writer (
        .clk       (FCLK_CLK0),
        .enable    (ctrl_reg2[0]),
        .data_in   (bram_data_in),
        .data_valid(bram_data_valid),
        .addrb     (addrb),
        .dinb      (dinb),
        .web       (web),
        .done      (end_adc_flag)
    );

    // =========================================================
    //  6. PS 端 BD wrapper
    // =========================================================

    design_1_wrapper design_1_wrapper_inst (
        .DDR_addr   (DDR_addr),
        .DDR_ba     (DDR_ba),
        .DDR_cas_n  (DDR_cas_n),
        .DDR_ck_n   (DDR_ck_n),
        .DDR_ck_p   (DDR_ck_p),
        .DDR_cke    (DDR_cke),
        .DDR_cs_n   (DDR_cs_n),
        .DDR_dm     (DDR_dm),
        .DDR_dq     (DDR_dq),
        .DDR_dqs_n  (DDR_dqs_n),
        .DDR_dqs_p  (DDR_dqs_p),
        .DDR_odt    (DDR_odt),
        .DDR_ras_n  (DDR_ras_n),
        .DDR_reset_n(DDR_reset_n),
        .DDR_we_n   (DDR_we_n),

        .FIXED_IO_ddr_vrn (FIXED_IO_ddr_vrn),
        .FIXED_IO_ddr_vrp (FIXED_IO_ddr_vrp),
        .FIXED_IO_mio     (FIXED_IO_mio),
        .FIXED_IO_ps_clk  (FIXED_IO_ps_clk),
        .FIXED_IO_ps_porb (FIXED_IO_ps_porb),
        .FIXED_IO_ps_srstb(FIXED_IO_ps_srstb),

        .UART_0_1_rxd(UART_0_1_rxd),
        .UART_0_1_txd(UART_0_1_txd),

        .IRQ_F2P_0   (end_adc_flag),
        .slv_reg0_o_0(dds_ctrl_reg0),
        .slv_reg1_o_0(dds_ctrl_reg1),
        .slv_reg2_o_0(ctrl_reg2),
        .slv_reg3_o_0(slv_reg3),

        .FCLK_CLK0_0(FCLK_CLK0),

        .addrb_0({15'd0, addrb}),
        .clkb_0 (FCLK_CLK0),
        .dinb_0 (dinb),
        .doutb_0(),                // 只写不读
        .enb_0  (1'b1),
        .web_0  (web),
        .rstb_0 (1'b0)
    );

endmodule
