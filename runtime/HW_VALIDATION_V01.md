# Runtime v0.1 实机验证（2026-11-25）

用户提供 runtime-safe.bin 的串口记录。独立 DDR 启动及 Shell 工作正常，逐项开启 MMU 和 D-Cache 后小范围内存测试均通过。

```text
MIDR=0x412FC082
CPSR=0x200001D3
SCTLR=0x00C51078
VBAR=0x240000C0
CLIDR=0x0A000023
CTR=0x82048004
APLL=0xA0C80601
MPLL=0xA29B0C01
CLK_SRC0=0x00000111
CLK_DIV0=0x14141251
ARMCLK: nominal 400 MHz (inherited ROM clock)
Physical DDR: 1024 MiB
Reserved: 18 MiB
Free pages: 257536 (1006 MiB, rounded down)
ALLOC=0x20100000
Pages: 16
Free pages: 257520 (1005 MiB, rounded down)
FREE PASS
Free pages: 257536 (1006 MiB, rounded down)
MEMTEST PASS
MMU ON
MMU: ON
I-Cache: ON
D-Cache: OFF
TTBR0=0x24004000
TTBCR=0x00000000
DACR=0x00000001
MEMTEST PASS
D-Cache ON
MMU: ON
I-Cache: ON
D-Cache: ON
TTBR0=0x24004000
TTBCR=0x00000000
DACR=0x00000001
MEMTEST PASS
```

以上是用户日志的关键行，省略交互提示。证明当前恒等映射、页分配和小范围缓存读写可用；VBAR 读回正确不代表异常入口已实机验证。自动开启版本未在此日志中单独验证。尚无用户态、进程隔离、IRQ 或完整缓存一致性测试。
