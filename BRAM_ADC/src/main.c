#include <stdio.h>
#include "xparameters.h"
#include "xil_io.h"
#include "xscugic.h"
#include "xil_exception.h"

#define BRAM_ADDR XPAR_AXI_BRAM_CTRL_0_BASEADDR        // BRAM基地址
#define LITE_ADDR XPAR_AXI_LITE_PS_TO_PL_PL_0_BASEADDR // AXI Lite基地址

#define ADC_START_ADDR (LITE_ADDR + 0x00) // ADC启动寄存器地址
#define LITE1_ADDR (LITE_ADDR + 0x04)     // 空闲寄存器地址（可选，暂未使用）
#define LITE3_ADDR (LITE_ADDR + 0x0C)     // 空闲寄存器地址（可选，暂未使用）
#define LITE2_ADDR (LITE_ADDR + 0x08)     // 空闲寄存器地址（可选，暂未使用）

// ===== 中断相关 =====
#define INTC_DEVICE_ID XPAR_SCUGIC_SINGLE_DEVICE_ID // GIC设备ID
#define PL_IRQ_ID 61                                // IRQ_F2P[0] → 61 // PL中断ID

XScuGic Intc;

// ===== 原有的BRAM函数（保留） =====
void psWriteBram() // 写入BRAM
{
    printf("This is psWriteBram function\r\n");

    int dataIn[8] = {11, 12, 13, 14, 15, 16, 17, 18};
    for (int i = 0; i < 8; i++)
    {
        Xil_Out32(BRAM_ADDR + 4 * i, dataIn[i]);
    }
}

int dataOut[4096];
void psReadBram() // 读取BRAM 4096个数据
{
    printf("This is psReadBram function\r\n");

    for (int i = 0; i < 4096; i++)
    {
        dataOut[i] = Xil_In32(BRAM_ADDR + i * 4);
        printf("%d\n", dataOut[i]);
    }
    printf("\r\n");
}

// ===== 中断服务函数 =====
void PL_IRQHandler(void *CallbackRef) // PL中断服务函数
{
    (void)CallbackRef;
    Xil_Out32(ADC_START_ADDR, 0);
    psReadBram();
    Xil_Out32(ADC_START_ADDR, 1);
}

// ===== 初始化GIC =====
int SetupInterruptSystem() // 初始化中断系统
{
    XScuGic_Config *IntcConfig;
    int status;

    IntcConfig = XScuGic_LookupConfig(INTC_DEVICE_ID);
    if (NULL == IntcConfig)
        return XST_FAILURE;

    status = XScuGic_CfgInitialize(&Intc, IntcConfig, IntcConfig->CpuBaseAddress);
    if (status != XST_SUCCESS)
        return XST_FAILURE;

    Xil_ExceptionInit();

    Xil_ExceptionRegisterHandler(
        XIL_EXCEPTION_ID_INT,
        (Xil_ExceptionHandler)XScuGic_InterruptHandler,
        &Intc);

    Xil_ExceptionEnable();

    return XST_SUCCESS;
}

// ===== 配置PL中断 =====
int SetupPLInterrupt() // 配置PL中断
{
    int status;

    status = XScuGic_Connect(&Intc,
                             PL_IRQ_ID,
                             (Xil_ExceptionHandler)PL_IRQHandler, // 注册PL中断服务函数
                             NULL);
    if (status != XST_SUCCESS)
        return XST_FAILURE;

    // 配置优先级 + 触发方式（上升沿触发）
    XScuGic_SetPriorityTriggerType(&Intc,
                                   PL_IRQ_ID, // 中断ID
                                   0xA0,      // 中断优先级
                                   0x3);      // 上升沿触发(0x1高电平触发，0x3上升沿触发)

    XScuGic_Enable(&Intc, PL_IRQ_ID);

    return XST_SUCCESS;
}

// ===== main =====
int main(void)
{
    printf("PL IRQ Test Start\r\n");

    // 初始化中断系统
    if (SetupInterruptSystem() != XST_SUCCESS)
    {
        printf("GIC init failed!\r\n");
        return -1;
    }

    // 配置PL中断
    if (SetupPLInterrupt() != XST_SUCCESS)
    {
        printf("PL IRQ setup failed!\r\n");
        return -1;
    }

    Xil_Out32(ADC_START_ADDR, 0);
    Xil_Out32(ADC_START_ADDR, 1);

    while (1)
    {
        // 死循环，等待中断触发
    }

    return 0;
}