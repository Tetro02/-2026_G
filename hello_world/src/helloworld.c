#include <stdio.h>
#include "xparameters.h"
#include <xil_io.h>

#define BRAM_ADDR       XPAR_AXI_BRAM_CTRL_0_BASEADDR

void psWriteBram()
{
    printf("This is psWriteBram function\r\n");
    // 鍚態RAM鐨勫墠8涓�32浣嶅湴鍧€锛屽垎鍒啓鍏�11,12,13,14,15,16,17,18
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
    // 璇诲嚭BRAM鐨勫墠2048涓�32浣嶅湴鍧€,骞舵墦鍗板嚭鏉�
    for(int i = 0;i < 4096; i++ )
    {
        dataOut[i] = Xil_In32(BRAM_ADDR + i*4);//姣忔璇�32浣嶆暟鎹紝鐒跺悗鍦板潃鍚戝悗绉诲姩4涓瓧鑺�
        printf("%d\n",dataOut[i]);
    }
    printf("\r\n");
}

int main(void)
{
	printf("This is AXI BRAM Test(Only PS)\r\n");
	//psReadBram();
	//psWriteBram();
	// psReadBram();
    u32 cnt = 0;
    for (cnt = 0; cnt <= 1000; cnt++) {
    }
	psReadBram();

    return 0;
}