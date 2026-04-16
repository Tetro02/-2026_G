#include <stdio.h>
#include "xparameters.h"
#include "xil_io.h"
#include "xscugic.h"
#include "xil_exception.h"

#define BRAM_ADDR       XPAR_AXI_BRAM_CTRL_0_BASEADDR

// ===== 中断相关 =====
#define INTC_DEVICE_ID  XPAR_SCUGIC_SINGLE_DEVICE_ID
#define PL_IRQ_ID       61   // IRQ_F2P[0] → 61

XScuGic Intc;

// ===== 中断服务函数 =====
void PL_IRQHandler(void *CallbackRef)
{
    (void)CallbackRef;
    printf("hello\r\n");
}

// ===== 初始化GIC =====
int SetupInterruptSystem()
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
int SetupPLInterrupt()
{
    int status;

    status = XScuGic_Connect(&Intc,
                             PL_IRQ_ID,
                             (Xil_ExceptionHandler)PL_IRQHandler,
                             NULL);
    if (status != XST_SUCCESS)
        return XST_FAILURE;

    // 设置优先级 + 触发方式（上升沿触发）
    XScuGic_SetPriorityTriggerType(&Intc,
                                  PL_IRQ_ID,
                                  0xA0,
                                  0x3);

    XScuGic_Enable(&Intc, PL_IRQ_ID);

    return XST_SUCCESS;
}

// ===== 你的原BRAM函数（保留） =====
void psWriteBram()
{
    printf("This is psWriteBram function\r\n");

    int dataIn[8] = {11,12,13,14,15,16,17,18};
    for(int i = 0;i < 8; i++ )
    {
        Xil_Out32(BRAM_ADDR + 4*i,dataIn[i]);
    }
}

int dataOut[4096];
void psReadBram()
{
    printf("This is psReadBram function\r\n");

    for(int i = 0;i < 4096; i++ )
    {
        dataOut[i] = Xil_In32(BRAM_ADDR + i*4);
        printf("%d\n",dataOut[i]);
    }
    printf("\r\n");
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

    while(1)
    {
        // 空循环，等中断触发
    }

    return 0;
}