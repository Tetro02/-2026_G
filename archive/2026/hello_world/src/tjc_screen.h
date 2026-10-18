#ifndef __TJC_SCREEN_H__
#define __TJC_SCREEN_H__

#include <stdint.h>

// ========== TJC 串口屏 API ==========
// 协议: 命令字符串 + 0xFF 0xFF 0xFF
// 波特率: 115200 (BSP 默认)

void tjc_init(void);

// 发送字符串 + 结束符 FFFFFF
void tjc_send_string(char* str);

// 设置文本: obj.attr="txt" + FFFFFF
void tjc_send_txt(char* objname, char* attribute, char* txt);

// 设置数值: obj.attr=val + FFFFFF
void tjc_send_val(char* objname, char* attribute, int val);

// 发送指定长度字符串 + FFFFFF
void tjc_send_nstring(char* str, unsigned char str_length);

// 波形数据: addt s0.id,0,len + data + 0x01 FFFFFF
void tjc_wave_addt(uint8_t *data, uint16_t len);

// ========== 接收（轮询，主循环调） ==========
void tjc_poll(void);

// 检查是否有完整帧，返回帧中 4 字节数据，无帧返回 -1
int32_t tjc_get_frame(void);

// ========== 环形缓冲区 ==========
#define TJC_RINGBUF_LEN 500

void tjc_ringbuf_init(void);
void tjc_ringbuf_write(uint8_t data);
uint8_t tjc_ringbuf_read(uint16_t pos);
uint16_t tjc_ringbuf_len(void);
void tjc_ringbuf_delete(uint16_t size);

#endif
