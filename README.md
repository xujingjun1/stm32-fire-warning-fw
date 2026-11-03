# stm32-fire-warning-fw

> 基于 STM32 与多模态感知的视觉火灾预警系统的 STM32 端固件仓库。
> 基于 STM32F103C8T6 + FreeRTOS，负责多路传感器采集、与 K210 视觉模块的串口通信、
> 多模态融合决策、联动执行与云端上报。

**状态：开发中** ｜ MCU：STM32F103C8T6 ｜ 规划 RTOS：FreeRTOS ｜ 协议层：✅ 已完成并通过单元测试

| 模块 | 状态 |
|---|---|
| UART 变长帧协议解析（`protocol/`） | ✅ 已完成，PC 单元测试 16 个断言全部通过 |
| PC 单元测试与协议仿真工具（`tests/` `tools/`） | ✅ 已完成 |
| 裸机传感器驱动（SHT30 / MQ-2 / MQ-7） | 🔨 进行中 |
| DMA + IDLE 串口接收 | 📋 计划中 |
| FreeRTOS 多任务移植与融合决策 | 📋 计划中 |
| ESP8266 MQTT 上云 | 📋 计划中 |

---

## 一、系统架构

```
┌──────────────────┐        UART 115200         ┌──────────────────┐
│    K210 (KPU)    │  AA55|N|cls|conf|chk|55AA  │   STM32F103C8T6  │
│ YOLO→nncase→KPU  │  ────────────────────────▶  │   + FreeRTOS     │
│  视觉火焰 / 烟雾 │        (DMA + IDLE 收帧)     │   融合决策 + 控制 │
└──────────────────┘                             └────────┬─────────┘
                                                          │
        ┌──────────────┬──────────────┬──────────────────┼──────────────┐
        ▼              ▼              ▼                  ▼              ▼
    SHT30(I2C)    MQ-2(ADC)      MQ-7(ADC)        火焰红外(ADC/GPIO)  ESP8266(MQTT)
    温湿度         烟雾           一氧化碳            明火判据          → 云平台
                                                          │
                                          ┌───────────────┼───────────────┐
                                          ▼               ▼               ▼
                                     蜂鸣器 + LED    继电器(断电/排风)   OLED 显示
```

采用**两级异构**架构：K210 跑视觉推理（KPU 加速，非实时要求），STM32 做实时采集与融合决策。
理由：STM32F103（72 MHz、20 KB RAM、无 FPU/NPU）跑不动神经网络；K210 的 KPU 专攻卷积推理（nncase 工具链），而报警联动要求毫秒级确定性响应，由 STM32 承担。

---

## 二、目录结构（当前）

```
stm32-fire-warning-fw/
├── protocol/               帧协议解析（纯 C，不依赖任何 HAL，可单测）
│   ├── frame_parser.h      帧格式定义与状态机接口
│   └── frame_parser.c      逐字节状态机实现
├── tests/
│   └── test_frame_logic.c  PC 端单元测试（7 组场景 / 16 个断言）
├── tools/
│   ├── fake_vision_sender.py  模拟 K210 端发帧（Python，跨语言交叉验证）
│   └── stdin_parse.c          从 stdin 读十六进制喂给 C 解析器
├── docs/
│   └── PROTOCOL.md         协议规范
└── Makefile                一键编译运行单元测试
```

固件工程（Keil MDK / FreeRTOS）随开发进度加入 `firmware/`。

---

## 三、UART 帧协议（K210 → STM32）

| 字段 | 字节 | 说明 |
|---|---|---|
| 帧头 | 2 | `AA 55` |
| 目标数 N | 1 | 本帧目标个数，1..8（0 非法） |
| 类别 | N | 1=火焰，2=烟雾 |
| 置信度 | N | 0–100 |
| 校验 | 1 | `(N + Σcls + Σconf) & 0xFF` 累加和 |
| 帧尾 | 2 | `55 AA` |

波特率 115200，8N1。详见 [docs/PROTOCOL.md](docs/PROTOCOL.md)。

**解析器设计要点**（`protocol/frame_parser.c`）：

1. **逐字节状态机**，可从任意字节流恢复同步，不依赖 DMA 边界
2. 收到 `0xAA` 但下一字节不是 `0x55` 时，若该字节本身是 `0xAA` 则**回退成"已收帧头1"**，避免连续帧丢帧
3. 校验错 / 非法 N / 超长帧一律拒收并重新找帧头
4. 纯逻辑零硬件依赖——PC 上直接编译做单元测试，测试过了再上板

> 校验目前用累加和降低调试难度；工业上通常换 CRC8/CRC16。替换只需改 `vp_checksum()` 一处，两侧同步即可。

---

## 四、编译与测试

单元测试不需要单片机和 Keil，任意 C 编译器即可：

```bash
# 方式一：Makefile
make test

# 方式二：直接编译（zig cc / gcc / clang 均可）
cc -std=c99 -Wall -Iprotocol -o test_frame_logic tests/test_frame_logic.c protocol/frame_parser.c
./test_frame_logic
```

测试覆盖 7 组场景：正常帧（单/多目标）/ 前导垃圾重同步 / 截断帧 / 校验错拒收 / 非法 N 拒收 / 连续帧 / 双目标帧。
当前 **16 个断言全部 PASS**。

**跨语言交叉验证**（Python 编码 ↔ C 解码，验证两侧协议实现一致）：

```bash
cc -std=c99 -Wall -Iprotocol -o stdin_parse tools/stdin_parse.c protocol/frame_parser.c
python -c "import sys; sys.path.insert(0,'tools'); import fake_vision_sender as f; print(f.to_hex(f.make_frame('both', 85)))" | ./stdin_parse
```

---

## 五、多模态融合决策（方案已定稿）

| 判据 | 权重 | 触发条件 |
|---|---|---|
| 视觉火焰 | +40 | 置信度 > 0.6 |
| 视觉烟雾 | +25 | 置信度 > 0.6 |
| 明火红外 | +25 | 超阈值 |
| CO（MQ-7） | +20 | 超阈值 |
| 温升速率 | +20 | 超阈值 |
| 烟雾（MQ-2） | +15 | 超阈值 |

- 总分 **≥ 60** → 报警（声光 + 继电器 + 上云）
- 总分 **30–60** → 预警（仅显示 + 上云）

**核心性质：任一单路判据的满分都低于 60 分报警阈值。** 单独点打火机、或单独起烟，都只能进"预警"；必须两路异构证据同时成立才报警。这是本设计抑制误报的机理，也是误报率对比实验要验证的对象（实验数据随联调补充）。

---

## 六、FreeRTOS 任务设计（规划，随移植更新）

| 任务 | 优先级 | 周期 | 职责 |
|---|---|---|---|
| task_monitor | 最高 | 100 ms | 检查各任务心跳，统一喂狗 |
| task_comm（RX） | 高 | 事件驱动 | DMA+IDLE 收帧、解析视觉结果 |
| task_sensor | 中 | 500 ms | 多路传感器采集与滤波 |
| task_fusion | 中 | 1 s | 融合打分与分级 |
| task_alarm | 中高 | 事件驱动 | 声光 + 继电器联动 |
| task_display | 低 | 1 s | OLED 刷新 |
| task_comm（TX） | 低 | 10 s | MQTT 上报 |

栈大小与高水位（`uxTaskGetStackHighWaterMark`）实测数据移植后补充——先给经验值，实测后收窄，保留 ≥30% 余量。

**可靠性设计**：IWDG + 任务心跳统一喂狗（不让各任务自己喂：某任务卡死时其他任务仍在喂狗，看门狗会失效）；HardFault 寄存器现场输出；`vApplicationStackOverflowHook` 栈溢出定位。

---

## 七、硬件清单

| 器件 | 接口 | 作用 | 备注 |
|---|---|---|---|
| STM32F103C8T6 | — | 主控 | 72 MHz，20 KB RAM，无 FPU |
| SHT30 | I2C | 温湿度、温升速率判据 | 地址 0x44 |
| MQ-2 | ADC | 烟雾 / 可燃气体 | 需上电预热后标定阈值 |
| MQ-7 | ADC | 一氧化碳 | 需周期切换加热电压保持 CO 选择性 |
| 红外火焰传感器 | ADC + GPIO | 明火判据 | 对日光灯等红外源敏感 → 融合机制存在的理由 |
| ESP-01S (ESP8266) | UART | MQTT 上云 | 另占一路串口 |
| OLED 0.96" | I2C/SPI | 本地显示 | — |
| 继电器 + 蜂鸣器 + LED | GPIO | 联动执行 | — |
| K210 | UART | 视觉推理结果 | 必须与 STM32 **共地** |

---

## 八、已知局限

- 本装置为**补充性监测终端**，无 CCCF 认证，**不能替代法定火灾自动报警系统**
- 视觉判据在无明显可见火焰的阴燃阶段失效，需与烟感协同
- 单点摄像头覆盖范围有限

---

## License

仅供学习与展示使用。
