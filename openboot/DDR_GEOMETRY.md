# v0.4 DMC0 内存几何参数 A/B 测试

## v0.3 实机地址镜像现象

DMC0 的 0x20010000、0x28010000、0x30010000 均返回最后写入的 A11A0005；16、32、64 MiB 偏移各自独立。DMC1 的 0、16、32、64、128、256 MiB 偏移返回六个不同的值。此前两个 16 KiB 测试及 DMC0 执行测试均通过。这表明当前控制器几何参数下存在地址镜像，并不能确定物理芯片容量。

TQ210 CoreB 公开产品介绍标称 1 GB DDR2（https://m.elecfans.com/article/1092302.html）。另一份板卡资料描述八颗 K4T1G084QQ，每个 DMC 四颗 x8 芯片，14 位行地址、10 位列地址、8 个 bank（https://www.eepw.com.cn/article/201611/322804.htm）。这种布局每个 DMC 为 512 MiB；当时尚未核实手中板卡的实际芯片数量。原厂 BL2 的 DMC0 配置 0x20E01343 与 DMC1 的 0x40E01323 行地址字段不同。A/B 版本仅将有效 DMC0 MEMCONFIG0 改为 0x20E01323（另加识别日志）；无效 CS1 的 MEMCONFIG1、时钟、时序与初始化顺序保持不变。假设镜像由几何参数不匹配而非缺少芯片引起。保留旧版供对照。

在 openboot 目录：

    git pull
    sh boot/build.sh
    # 完全断电并进入 USB ROM 模式
    sudo ./build-host/tq210-dnw out/rom_ddr_row14.bin --pad-vendor-size

DDR SCRATCH PASS 和 EXEC PASS 后依次发送小写 a、b、m。m 对两个 512 MiB 窗口中的 172 个稀疏字（覆盖每个 16 MiB 区域和低地址位）正反顺序写入两种与地址有关的模式。写入前保存所有原始值，访问完成后恢复。两个窗口分别计数错误。零错误不代表完整压力测试或最终容量确认。无效地址可能挂起；记录最后输出，断电并按需退回已知基线 rom_ddr.bin。不得在运行中的 OS 使用。不写 NAND/SD。该实验初次提交时仅审查源码，未编译或实机测试。

## 实机验证 — 2026-11-25

用户在消除除法依赖的修复 d7db969 后使用 GCC 构建并运行成功。DMC0 MEMCONFIG0=0x20E01323，DMC1=0x40E01323：

- 两个 16 KiB 测试与 DMC0 执行代码均通过。
- 两组六地址采样均依次返回 A11A0000 至 A11A0005。
- 两个 512 MiB 窗口共 172 个稀疏地址正反顺序测试均通过，DMC0 MAP ERRORS=0，DMC1 MAP ERRORS=0。
- DMC0 原先的 128/256 MiB 镜像在采样中消失。几何参数修改已在本板验证，但稀疏采样不能证明具体物理芯片组织或全面可靠性。

## 可选 v0.5 全窗口测试

rom_ddr_row14.bin 保留为已验证基线。rom_ddr_full.bin 使用相同 DMC 和时钟参数，新增大写 T 命令执行破坏性的仅 DDR 测试。构建：

    git pull
    sh boot/build.sh

断电进入 iROM USB 模式，打开 UART0：

    sudo ./build-host/tq210-dnw out/rom_ddr_full.bin --pad-vendor-size

小范围测试与执行 PASS 后，可先运行 m，再发送一次大写 T。所有 DDR 内容将被覆盖，包括先前测试代码；iRAM 代码和栈保持不变。要求 MMU、D-cache 关闭且两组 MEMCONFIG0 为修正值。先写完两个窗口的每个对齐 32 位字，再读取校验；随后用地址模式的反码重复。每 32 MiB 打印进度。完整成功运行每轮检查 268435456 个字，两轮共 4 GiB 读写流量。错误按窗口累计。不得用于含有效数据或 OS 的 DDR。不访问 NAND/SD，挂起时复位。新增命令初次提交时仅审查源码，未交叉编译或实机验证。此测试覆盖全部地址和指定模式，不证明长期保持、全部数据模式或其他频率下的稳定性。

## 完整 1 GiB 测试 — 通过（2026-11-25）

用户独立启动 rom_ddr_full.bin 并发送大写 T。两轮均写入 [0x20000000,0x60000000) 的每个对齐 32 位字，然后读取两个窗口。每轮 DMC0 和 DMC1 累计错误均为 0。最终输出：

    DMC0 CUMULATIVE ERRORS=0x00000000
    DMC1 CUMULATIVE ERRORS=0x00000000
    FULL DDR TEST PASS: 1024 MiB, two patterns, every 32-bit word
    DDR contents overwritten; iRAM console still running

每轮 32 个写入块和 32 个校验块，两轮共 4 GiB 内存流量。不使用原厂 BL2，在继承 ROM 时钟下确认修正后的几何参数。尚未验证长期保持、温度变化、全部模式或更高频率。保留此固件作为基线。
