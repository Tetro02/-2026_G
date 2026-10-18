# 复现步骤

## 环境

- AMD Vivado / Vitis 2025.2，安装 Zynq-7000 器件支持。
- 器件 `xc7z020clg484-1`，顶层 `Test_Top`。PS DDR、MIO 和时钟参数来自原 BD；换板需要重新核对，封装相同不代表板级兼容。
- Windows 建议克隆到短路径（如 `C:/fpga/7020_BRAM_ADC`），避免 IP 生成文件超过路径长度限制。
- 不需要原作者的桌面目录或相邻工程。

## Vivado

从仓库根目录运行（先加载 AMD 工具环境，或使用安装目录内的 vivado.bat）：

```text
vivado -mode batch -source scripts/prepare_vivado.tcl
```

脚本打开 `projects/2026_FIR/2026_FIR.xpr`，加载本地自定义 IP、校验 BD、生成 BD wrapper 和 IP 输出，不综合或烧录。完整生成 bitstream/XSA：

```text
vivado -mode batch -source scripts/prepare_vivado.tcl -tclargs build
```

输出 XSA：`build/Test_Top.xsa`。脚本不会自动连接或烧录开发板。

也可打开 XPR 后 Update IP Catalog，打开并 Validate Block Design，Generate Output Products，Create HDL Wrapper，再运行综合/实现。不要在打开工程时盲目升级不同版本 IP。

## Vitis

1. 用本次生成的 XSA 建立新 platform，CPU `ps7_cortexa9_0`，OS `standalone`，生成 BSP。
2. 新建 Hello World 应用，导入 `projects/2026_FIR/hello_world/src/` 的源码、头文件和 `libCMSISDSP.a`。
3. 使用提供的 `UserConfig.cmake` 作为配置参考：编译源为 `helloworld.c`、`tjc_screen.c`、`platform.c`；include 为 `CMSIS_DSP` 与 `CMSIS_DSP/dsp`；链接库 `CMSISDSP`，库目录为应用 `src`。
4. 确认新 BSP 的 `xparameters.h` 有 AXI BRAM、自定义 AXI-Lite、GIC、UART0 的宏。由新平台生成 linker/BSP 配置，避免复用旧机器的编译命令缓存。
5. 构建生成 ELF。已有有效 Vitis 生成工程时，可用该版本附带 Ninja 在新应用的 build 目录构建。
6. 调试时使用同一次 Vivado 导出的硬件、bitstream 和对应应用 ELF。SD 启动再用同平台生成的 FSBL、bitstream、ELF 创建 BOOT.BIN；本次不附带未经本轮上板验证的启动包。

CMSIS 静态库保留自原工程，确切上游版本/编译参数未知。若出现 hard-float/soft-float ABI 不匹配，须按 Cortex-A9 和 Vitis 实际编译选项从官方源码重建，不能改用 Cortex-M 二进制。

## 静态完整性检查

```text
python scripts/check_sources.py
```

它检查 XPR 的源文件、自定义 IP 组件文件及 XCI 的 COE 引用；BD wrapper 属于生成文件，允许尚不存在。该检查不代替 Vivado 综合、时序收敛或板级实验。
