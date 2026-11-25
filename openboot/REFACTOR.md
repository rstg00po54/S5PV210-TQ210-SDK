# OpenBoot 模块化源码结构

基线：`79be964c325dd551d511faa57dd0ee2cf175850d`。本次为源码重构，不是新增硬件功能。

| 模块 | 源码 |
| --- | --- |
| 启动与链接脚本 | boot/start.S, boot/iram.ld, boot/ddr_app.* |
| 主流程编排 | boot/main.c |
| 寄存器定义与 MMIO | include/openboot/registers.h, platform.h |
| 时钟与看门狗 | soc/s5pv210/ |
| UART 控制台 | drivers/uart/console.c |
| DDR 初始化 | drivers/ddr/ddr.c |
| 内存与 USB 测试 | diagnostics/ |
| 旧版 PIO / OBL1 | drivers/usb/usb_enum.c |
| ROM USB 接管与 DMA | drivers/usb/usb_handoff.c |
| OBL2 镜像协议与 CRC | protocol/obl2.c |
| 原生 C++ 命令行 | tools/dnw.cpp |
| 主机 ROM 封包、镜像与 CRC | tools/image.cpp |
| 主机 libusb 生命周期与传输 | tools/transport.cpp |
| 主机旧版 PIO 协议 | tools/bulk.cpp |
| 主机同连接 DMA 协议 | tools/handoff.cpp |

原有 .inc 源文件改为独立编译的 C 文件。现有 16 种固件共用 boot/sources.txt 和 boot/variants.txt。设计上保持寄存器写入顺序、时序常量、协议标记、端点编号、镜像地址、大小限制与 CRC 算法不变。不引入 NAND/SD I/O 或新启动协议。

现有构建接口：

    sh boot/build.sh
    cmake --build build-host
    ./build-host/tq210-dnw --selftest

可选 CMake 固件构建（GNU ARM 工具链，输出位于 build-firmware/out）：

    cmake -S boot -B build-firmware -DCMAKE_TOOLCHAIN_FILE=../cmake/arm-none-eabi.cmake
    cmake --build build-firmware

回归测试清单（每项分别冷启动）：

    sudo ./build-host/tq210-dnw --handoff-test out/rom_usb_handoff.bin
    sudo ./build-host/tq210-dnw --dma-test out/rom_usb_dma_boot.bin
    sudo ./build-host/tq210-dnw --dma-test out/rom_usb_dma_fast.bin
    sudo ./build-host/tq210-dnw --dma-boot out/rom_usb_dma_fast.bin out/ddr_app.bin

1 MiB+13 的预期 CRC 为 0x6eefefc7，执行串口日志为 DDR APP EXEC PASS。保留链接断言和 ROM 帧大小限制。新的编译单元可能影响内联、时序与 SRAM 占用，必须重新编译及实机测试才能确认等效。

可单独检出重构前基线：

    git worktree add --detach ../openboot-before-refactor 79be964c325dd551d511faa57dd0ee2cf175850d

重构提交当时检查了源码拆分、词法格式及预处理器嵌套；因环境无法克隆远程仓库，未进行 ARM/本地编译、CMake 配置或目标板回归。

## 接口清理与构建回归

DMA 协议改用驱动接口释放 OUT NAK、校验接收长度及查询端点诊断；OTG 寄存器定义是接管驱动的私有实现。主入口使用显式控制块并拆分诊断控制台。独立编译后利用链接段垃圾回收减少未使用代码，保留启动 KEEP 段及 iRAM 大小断言。二进制布局和时序可能变化。

执行 `sh check-build.sh` 构建全部固件、C++ 主机工具，运行主机封包自测及 ROM 帧大小检查。脚本不访问硬件。该修订当时未编译或执行，需要目标板回归。

## 后续验证

修复 14a783f 中的 MMIO_TIMEOUT 引用后，用户实机验证了 rom_usb_dma_fast.bin 的独立 DDR 镜像执行和 1 MiB+13 数据测试。其余变体仍需分别回归，不应仅凭主要路径成功认定全部等效。
