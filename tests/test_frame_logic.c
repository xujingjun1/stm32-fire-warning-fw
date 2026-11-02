/* test_frame_logic.c —— PC 端单元测试（不需要单片机，zig cc 或 gcc 直接编译运行）
 *
 *   make test        （仓库根目录，等价于下条命令）
 *   cc -std=c99 -Wall -Iprotocol -o test_frame_logic tests/test_frame_logic.c protocol/frame_parser.c
 *
 * 覆盖：正常帧 / 前导垃圾 / 截断 / 校验错 / 非法 N / 连续帧 / 多目标
 */

#include <stdio.h>
#include <string.h>
#include "frame_parser.h"

static int g_fail = 0;

static uint16_t build_frame(uint8_t *buf, const uint8_t *cls,
                            const uint8_t *conf, uint8_t n)
{
    uint8_t  i;
    uint8_t  sum;
    uint16_t k = 0;

    buf[k++] = 0xAAu;
    buf[k++] = 0x55u;
    buf[k++] = n;
    sum = n;
    for (i = 0u; i < n; i++) { buf[k++] = cls[i];  sum += cls[i];  }
    for (i = 0u; i < n; i++) { buf[k++] = conf[i]; sum += conf[i]; }
    buf[k++] = sum;
    buf[k++] = 0x55u;
    buf[k++] = 0xAAu;
    return k;
}

static void check(const char *name, int cond)
{
    if (cond) {
        printf("  [PASS] %s\n", name);
    } else {
        printf("  [FAIL] %s\n", name);
        g_fail++;
    }
}

static uint16_t feed(const uint8_t *buf, uint16_t len, vp_result_t *out)
{
    uint16_t i;
    uint16_t got = 0u;
    for (i = 0u; i < len; i++) {
        got += VP_FeedByte(buf[i], out);
    }
    return got;
}

int main(void)
{
    uint8_t      buf[128];
    uint16_t     len;
    uint16_t     got;
    vp_result_t  res;
    vp_stats_t   st;

    printf("== 视觉结果帧解析状态机测试 ==\n\n");

    /* 1. 正常单目标帧 */
    VP_Init();
    {
        const uint8_t cls[1]  = {1u};
        const uint8_t conf[1] = {87u};
        len = build_frame(buf, cls, conf, 1u);
        memset(&res, 0, sizeof(res));
        got = feed(buf, len, &res);
        check("正常帧解析成功", got == 1u);
        check("目标数 = 1", res.count == 1u);
        check("类别 = 1（火焰）", res.cls[0] == 1u);
        check("置信度 = 87", res.conf[0] == 87u);
    }

    /* 2. 前导垃圾字节后接正常帧（模拟上电/干扰后重新同步） */
    VP_Init();
    {
        const uint8_t cls[1]  = {2u};
        const uint8_t conf[1] = {63u};
        uint8_t stream[64];
        uint16_t k = 0u;
        len = build_frame(buf, cls, conf, 1u);
        stream[k++] = 0x00u; stream[k++] = 0xFFu; stream[k++] = 0x13u;
        memcpy(&stream[k], buf, len); k += len;
        memset(&res, 0, sizeof(res));
        got = feed(stream, k, &res);
        check("前导垃圾后仍能同步", got == 1u);
        check("类别 = 2（烟雾）", res.cls[0] == 2u);
    }

    /* 3. 截断帧（只发到 conf 字段）不应产生结果 */
    VP_Init();
    {
        const uint8_t cls[1]  = {1u};
        const uint8_t conf[1] = {90u};
        len = build_frame(buf, cls, conf, 1u);
        memset(&res, 0, sizeof(res));
        got = feed(buf, len - 3u, &res);   /* 砍掉 chk + 帧尾 */
        check("截断帧不产生结果", got == 0u);
    }

    /* 4. 校验字节错误应拒收 */
    VP_Init();
    {
        const uint8_t cls[1]  = {1u};
        const uint8_t conf[1] = {90u};
        len = build_frame(buf, cls, conf, 1u);
        buf[len - 3u] = (uint8_t)(buf[len - 3u] + 1u);  /* 篡改校验 */
        memset(&res, 0, sizeof(res));
        got = feed(buf, len, &res);
        check("校验错误被拒收", got == 0u);
        VP_GetStats(&st);
        check("校验失败计数 = 1", st.frames_bad_check == 1u);
    }

    /* 5. 非法目标数 N（超过上限）应拒收并重新同步 */
    VP_Init();
    {
        uint8_t bad[8];
        uint16_t k = 0u;
        bad[k++] = 0xAAu; bad[k++] = 0x55u; bad[k++] = 99u;
        bad[k++] = 1u; bad[k++] = 90u; bad[k++] = 0u;
        bad[k++] = 0x55u; bad[k++] = 0xAAu;
        memset(&res, 0, sizeof(res));
        got = feed(bad, k, &res);
        check("非法 N 被拒收", got == 0u);
        VP_GetStats(&st);
        check("格式错误计数 >= 1", st.frames_aborted >= 1u);
    }

    /* 6. 两帧连续拼接（DMA 一次带回多帧） */
    VP_Init();
    {
        const uint8_t cls[1]  = {1u};
        const uint8_t conf[1] = {70u};
        uint8_t stream[64];
        uint16_t k = 0u;
        len = build_frame(buf, cls, conf, 1u);
        memcpy(&stream[k], buf, len); k += len;
        memcpy(&stream[k], buf, len); k += len;
        memset(&res, 0, sizeof(res));
        got = feed(stream, k, &res);
        check("连续两帧都解析出来", got == 2u);
    }

    /* 7. 多目标帧 */
    VP_Init();
    {
        const uint8_t cls[2]  = {1u, 2u};
        const uint8_t conf[2] = {95u, 40u};
        len = build_frame(buf, cls, conf, 2u);
        memset(&res, 0, sizeof(res));
        got = feed(buf, len, &res);
        check("双目标帧解析成功", got == 1u);
        check("目标数 = 2", res.count == 2u);
        check("第 2 个类别 = 2", res.cls[1] == 2u);
        check("第 2 个置信度 = 40", res.conf[1] == 40u);
    }

    printf("\n");
    VP_Init();
    {
        const uint8_t cls[1]  = {1u};
        const uint8_t conf[1] = {87u};
        len = build_frame(buf, cls, conf, 1u);
        feed(buf, len, &res);
        VP_GetStats(&st);
        printf("统计：成功 %lu / 校验错 %lu / 格式错 %lu / 总字节 %lu\n",
               (unsigned long)st.frames_ok,
               (unsigned long)st.frames_bad_check,
               (unsigned long)st.frames_aborted,
               (unsigned long)st.bytes_fed);
    }

    printf("\n%s\n", (g_fail == 0) ? "全部通过" : "存在失败用例");
    return (g_fail == 0) ? 0 : 1;
}
