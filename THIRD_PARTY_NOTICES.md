# 第三方代码和工具

根目录 MIT 许可证适用于 Tetro02 原创代码和本次整理文档；不覆盖第三方组件，也不重新授予 FPGA 工具/IP 的许可。保留各文件原有声明。

| 内容 | 来源与许可 | 说明 |
|---|---|---|
| 各应用 `src/CMSIS_DSP/` | Arm CMSIS-DSP / CMSIS Core，文件标记 Apache-2.0 | 头文件来自原本地工程，部分文件版本各异；全文见 `LICENSES/Apache-2.0.txt` |
| 各应用 `src/libCMSISDSP.a` | 本地工程已有的 CMSIS-DSP 静态库 | 按原样保留；无法从二进制确定确切上游提交、编译选项，不能声称与任一 tag 完全对应 |
| `platform.c`、`platform.h`、CMake / linker 模板 | AMD/Xilinx Vitis 生成模板 | 以各文件原有 copyright、SPDX 或许可声明为准 |
| 自定义 AXI-Lite IP | 本地 `MyExample/ip_repo/AXI_Lite_PS_To_PL_PL_1_0`，基于 Vivado AXI 模板 | 保留源文件头和组件信息；模板部分遵循 AMD 条款，用户修改部分遵循项目许可 |
| Vivado 标准 IP | AMD Vivado 安装中的 PS7、AXI BRAM、SmartConnect、FIR Compiler、Block Memory Generator 等 | 仓库只保留配置，生成和使用须具备对应工具/IP 使用权 |

CMSIS 官方来源：[CMSIS-DSP](https://github.com/ARM-software/CMSIS-DSP)、[CMSIS_5](https://github.com/ARM-software/CMSIS_5)。静态库可用官方源码按 Cortex-A9 / ARMv7-A、工程的浮点 ABI 重建；当前仓库未提供已验证的静态库重建流程。

历史提交保留了整理前的厂商生成文件及各自的声明；本次没有改写历史。不要将根 MIT 许可理解为对全部历史第三方内容的重新授权。

## 作者补充的设计报告图片

`docs/images/report-*.png` 提取自作者提供的设计报告参考版，作为该项目的技术参考。图片内容和与现行源码的差异见 `docs/report-reference.md`；其中如有引用的器件电路或第三方材料，根目录 MIT 许可不对其重新授权。未分发原 DOCX 及编辑者元数据。
