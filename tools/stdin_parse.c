/* stdin_parse.c —— 从标准输入读十六进制字节流，喂给解析器并打印结果
 *
 * 用途：把 Python 端 build_frame() 生成的帧直接灌进 C 解析器，
 *       验证两侧协议实现完全一致（跨语言交叉验证）。
 *
 * 编译：make stdin_parse  （或 cc -std=c99 -Wall -Iprotocol -o stdin_parse tools/stdin_parse.c protocol/frame_parser.c）
 * 用法：python -c "import sys; sys.path.insert(0,'tools'); import fake_vision_sender as f; print(f.to_hex(f.make_frame('both',85)))" | ./stdin_parse
 */

#include <stdio.h>
#include <string.h>
#include "frame_parser.h"

#define MAXBUF 4096

int main(void)
{
    static uint8_t    buf[MAXBUF];
    static uint8_t    raw[MAXBUF];
    static uint8_t    stream[MAXBUF];
    uint16_t          n = 0u;
    uint16_t          rn = 0u;
    uint16_t          i;
    int               c;
    int               val;
    int               have = 0;
    uint16_t          got;
    vp_result_t       res;
    vp_stats_t        st;

    while ((c = getchar()) != EOF) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            if (have) {
                if (rn < MAXBUF) { raw[rn++] = (uint8_t)val; }
                have = 0; val = 0;
            }
            continue;
        }
        if (c >= '0' && c <= '9')      { val = (have ? val * 16 : 0) + (c - '0'); }
        else if (c >= 'a' && c <= 'f') { val = (have ? val * 16 : 0) + (c - 'a' + 10); }
        else if (c >= 'A' && c <= 'F') { val = (have ? val * 16 : 0) + (c - 'A' + 10); }
        else                           { continue; }
        have = 1;
    }
    if (have && rn < MAXBUF) { raw[rn++] = (uint8_t)val; }

    printf("收到 %u 字节：", rn);
    for (i = 0u; i < rn; i++) { printf("%02X ", raw[i]); }
    printf("\n");

    VP_Init();
    memset(&res, 0, sizeof(res));
    got = VP_FeedBuffer(raw, rn, &res);

    printf("解出帧数：%u\n", got);
    if (got > 0u) {
        printf("最后一个结果：count=%u", res.count);
        for (i = 0u; i < res.count; i++) {
            printf("  [cls=%u conf=%u]", res.cls[i], res.conf[i]);
        }
        printf("\n");
    }

    VP_GetStats(&st);
    printf("统计：ok=%lu badchk=%lu abort=%lu bytes=%lu\n",
           (unsigned long)st.frames_ok,
           (unsigned long)st.frames_bad_check,
           (unsigned long)st.frames_aborted,
           (unsigned long)st.bytes_fed);

    (void)buf; (void)stream; (void)n;
    return 0;
}
