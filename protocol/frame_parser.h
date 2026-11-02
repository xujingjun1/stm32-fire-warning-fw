/* frame_parser.h —— RK3568 视觉结果帧解析器（纯逻辑，不依赖任何 HAL）
 *
 * 帧格式（RK3568 -> STM32，115200 8N1）：
 *   AA 55 | N | cls[N] | conf[N] | chk | 55 AA
 *   N      目标个数，1..8（0 非法）
 *   cls    类别：1=火焰，2=烟雾
 *   conf   置信度 0..100
 *   chk    校验 = (N + sum(cls) + sum(conf)) & 0xFF   —— 累加和
 *          注：工业上通常换 CRC8/CRC16，这里先用累加和降低调试难度，
 *              未来替换只需改 vp_checksum() 一处，两侧同步即可。
 *
 * 设计要点：
 *   1. 逐字节状态机，可从任意字节流中恢复同步（不依赖 DMA 边界）
 *   2. 收到 0xAA 但下一字节不是 0x55 时，不直接丢弃——若该字节本身是 0xAA，
 *      回退成"已收到帧头1"，避免连续帧场景下的丢帧
 *   3. 不含任何硬件调用，可在 PC 上直接编译做单元测试
 */

#ifndef FRAME_PARSER_H
#define FRAME_PARSER_H

#include <stdint.h>

#define VP_MAX_OBJ       8u
#define VP_FRAME_MAXLEN  (2u + 1u + (VP_MAX_OBJ * 2u) + 1u + 2u)

typedef enum {
    VP_ST_HEAD1 = 0,
    VP_ST_HEAD2,
    VP_ST_COUNT,
    VP_ST_CLASS,
    VP_ST_CONF,
    VP_ST_CHECK,
    VP_ST_TAIL1,
    VP_ST_TAIL2
} vp_state_t;

typedef struct {
    uint8_t cls[VP_MAX_OBJ];
    uint8_t conf[VP_MAX_OBJ];
    uint8_t count;
} vp_result_t;

typedef struct {
    uint32_t frames_ok;          /* 成功解析的完整帧 */
    uint32_t frames_bad_check;   /* 校验失败被丢弃 */
    uint32_t frames_aborted;     /* 格式错误被丢弃（非法 N / 帧尾不符等） */
    uint32_t bytes_fed;          /* 已喂入字节总数，用于估算误码率 */
} vp_stats_t;

void     VP_Init(void);

/* 喂入一个字节；解出完整帧时返回 1，并通过 out 输出结果 */
uint16_t VP_FeedByte(uint8_t b, vp_result_t *out);

/* 喂入一段缓冲（DMA+IDLE 收到一批数据后用这个）；返回解出的帧数 */
uint16_t VP_FeedBuffer(const uint8_t *buf, uint16_t len, vp_result_t *out);

void     VP_GetStats(vp_stats_t *s);

#endif /* FRAME_PARSER_H */
