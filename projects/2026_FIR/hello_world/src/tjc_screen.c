// ========== TJC 串口屏驱动 — Zynq PS (XUartPs) ==========
// 从 STM32 HAL 移植: HAL_UART_Transmit_IT → XUartPs_SendByte
// 波特率 115200, 命令结束符 FF FF FF

#include "tjc_screen.h"
#include "xuartps_hw.h"
#include "xparameters.h"
#include "sleep.h"
#include <stdio.h>
#include <string.h>

#define TJC_BASE  XPAR_XUARTPS_0_BASEADDR  // UART0, 与 printf 共用

// ========== 环形缓冲区 ==========
typedef struct {
    uint16_t head;
    uint16_t tail;
    uint16_t length;
    uint8_t  data[TJC_RINGBUF_LEN];
} RingBuf_t;

static RingBuf_t rb;

void tjc_ringbuf_init(void)
{
    rb.head = 0;
    rb.tail = 0;
    rb.length = 0;
}

void tjc_ringbuf_write(uint8_t data)
{
    if (rb.length >= TJC_RINGBUF_LEN) return;
    rb.data[rb.tail] = data;
    rb.tail = (rb.tail + 1) % TJC_RINGBUF_LEN;
    rb.length++;
}

uint8_t tjc_ringbuf_read(uint16_t pos)
{
    uint16_t real = (rb.head + pos) % TJC_RINGBUF_LEN;
    return rb.data[real];
}

uint16_t tjc_ringbuf_len(void)
{
    return rb.length;
}

void tjc_ringbuf_delete(uint16_t size)
{
    if (size >= rb.length) {
        tjc_ringbuf_init();
        return;
    }
    rb.head = (rb.head + size) % TJC_RINGBUF_LEN;
    rb.length -= size;
}

void tjc_ringbuf_delete_at(uint16_t pos, uint16_t size)
{
    if (pos >= rb.length) return;
    if (size > rb.length - pos) size = rb.length - pos;
    // 将 [pos+size, length) 前移到 pos，保持其余数据顺序
    for (uint16_t i = pos; i + size < rb.length; i++) {
        rb.data[(rb.head + i) % TJC_RINGBUF_LEN] =
            rb.data[(rb.head + i + size) % TJC_RINGBUF_LEN];
    }
    rb.length -= size;
    rb.tail = (rb.head + rb.length) % TJC_RINGBUF_LEN;
}

// ========== 底层 UART 发送 ==========
static void uart_send_char(char ch)
{
    XUartPs_SendByte(TJC_BASE, (uint8_t)ch);
}

static void uart_send_string(char* str)
{
    while (*str != 0 && str != 0) {
        uart_send_char(*str++);
    }
}

// ========== int → 字符串 ==========
static void intToStr(int num, char* str)
{
    int i = 0, is_neg = 0;

    if (num < 0) {
        is_neg = 1;
        num = -num;
    }

    do {
        str[i++] = (num % 10) + '0';
        num /= 10;
    } while (num);

    if (is_neg) str[i++] = '-';
    str[i] = '\0';

    // 反转
    int start = 0, end = i - 1;
    while (start < end) {
        char t = str[start];
        str[start] = str[end];
        str[end] = t;
        start++;
        end--;
    }
}

// ========== TJC 命令（均以 FF FF FF 结束） ==========

void tjc_send_string(char* str)
{
    uart_send_string(str);
    uart_send_char(0xFF);
    uart_send_char(0xFF);
    uart_send_char(0xFF);
}

void tjc_send_txt(char* objname, char* attribute, char* txt)
{
    uart_send_string(objname);
    uart_send_char('.');
    uart_send_string(attribute);
    uart_send_string("=\"");
    uart_send_string(txt);
    uart_send_char('\"');
    uart_send_char(0xFF);
    uart_send_char(0xFF);
    uart_send_char(0xFF);
}

void tjc_send_val(char* objname, char* attribute, int val)
{
    uart_send_string(objname);
    uart_send_char('.');
    uart_send_string(attribute);
    uart_send_char('=');
    char txt[12] = "";
    intToStr(val, txt);
    uart_send_string(txt);
    uart_send_char(0xFF);
    uart_send_char(0xFF);
    uart_send_char(0xFF);
}

void tjc_send_nstring(char* str, unsigned char str_length)
{
    for (int i = 0; i < str_length; i++) {
        uart_send_char(*str++);
    }
    uart_send_char(0xFF);
    uart_send_char(0xFF);
    uart_send_char(0xFF);
}

// ========== 应答识别（环形缓冲区内查找，不直接读UART） ==========
// 查找并完整消费 "b0 FF FF FF" 应答序列，找到返回 0，未找到返回 -1。
// 只删除匹配的应答 4 字节（从任意位置），同时到达的按键帧 (AB..BA)
// 无论排在 ACK 前还是后都原样保留在缓冲区。
static int tjc_consume_ack(uint8_t b0)
{
    for (uint16_t i = 0; i + 3 < tjc_ringbuf_len(); i++) {
        if (tjc_ringbuf_read(i) == b0 &&
            tjc_ringbuf_read(i + 1) == 0xFF &&
            tjc_ringbuf_read(i + 2) == 0xFF &&
            tjc_ringbuf_read(i + 3) == 0xFF) {
            tjc_ringbuf_delete_at(i, 4);
            return 0;
        }
    }
    return -1;
}

void tjc_wave_addt(uint8_t *data, uint16_t len)
{
    char cmd[32];
    sprintf(cmd, "addt s0.id,0,%u", len);
    tjc_send_string(cmd);

    // 等屏幕就绪 (FE FF FF FF)：统一经 tjc_poll 进环形缓冲区
    for (int timeout = 0; timeout < 200; timeout++) {
        tjc_poll();
        if (tjc_consume_ack(0xFE) == 0) break;
        usleep(1000);
    }

    for (int i = 0; i < len; i++) {
        uart_send_char((char)data[i]);
    }

    // 等屏幕完成 (FD FF FF FF)
    for (int timeout = 0; timeout < 500; timeout++) {
        tjc_poll();
        if (tjc_consume_ack(0xFD) == 0) break;
        usleep(1000);
    }

    uart_send_char(0x01);
    uart_send_char(0xFF);
    uart_send_char(0xFF);
    uart_send_char(0xFF);
}

// ========== 接收（轮询） ==========

void tjc_poll(void)
{
    while (XUartPs_IsReceiveData(TJC_BASE)) {
        uint8_t b = XUartPs_RecvByte(TJC_BASE);
        tjc_ringbuf_write(b);
    }
}

int32_t tjc_get_frame(void)
{
    // 帧格式: AB + 4 bytes data + BA (共 6 字节)
    // 循环解析：消费 ACK 应答和垃圾字节，直到找到完整按键帧或数据不足，
    // 一次调用尽可能处理完当前缓冲区，不让单个垃圾字节阻塞后续完整帧。
    #define FRAME_LEN 6
    while (tjc_ringbuf_len() >= FRAME_LEN) {
        uint8_t b0 = tjc_ringbuf_read(0);

        // 完整按键帧
        if (b0 == 0xAB && tjc_ringbuf_read(FRAME_LEN - 1) == 0xBA) {
            uint32_t data = (uint32_t)tjc_ringbuf_read(1)
                         | ((uint32_t)tjc_ringbuf_read(2) << 8)
                         | ((uint32_t)tjc_ringbuf_read(3) << 16)
                         | ((uint32_t)tjc_ringbuf_read(4) << 24);
            tjc_ringbuf_delete(FRAME_LEN);
            return (int32_t)data;
        }

        // ACK 应答 (FE/FD + FF FF FF)：消费后继续找按键帧
        if ((b0 == 0xFE || b0 == 0xFD) &&
            tjc_ringbuf_len() >= 4 &&
            tjc_ringbuf_read(1) == 0xFF &&
            tjc_ringbuf_read(2) == 0xFF &&
            tjc_ringbuf_read(3) == 0xFF) {
            tjc_ringbuf_delete(4);
            continue;
        }

        // 垃圾/未对齐字节，丢弃 1 字节继续同步
        tjc_ringbuf_delete(1);
    }
    return -1;
}

// ========== 初始化 ==========
void tjc_init(void)
{
    tjc_ringbuf_init();
}
