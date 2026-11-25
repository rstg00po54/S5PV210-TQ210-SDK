# USB OTG 驱动开发与实机验证

本文按时间顺序记录从只读 iROM 状态探测到独立 USB 枚举、PIO 传输、同连接 DMA 启动的开发过程。各阶段的“未验证”表示当时提交的状态，并非否定后续实机结果。当前推荐入口见 [README.md](README.md)。

## 只读 iROM 接管探测

最初目标：独立枚举为 USB 设备，实现 EP0 控制传输、向已验证 DDR 写入的 bulk OUT，以及原生 C++ 双阶段主机上传。此阶段仅调查寄存器，不实现枚举或下载驱动。

寄存器布局参考：

- Linux S5PV210 设备树：https://code.googlesource.com/linux/torvalds/linux/+/c6e169bc146a76d5ccbf4d3825f705414352bd03/arch/arm/boot/dts/s5pv210.dtsi （OTG 0xec000000；PHY 0xec100000）
- Linux PHY 驱动：https://codebrowser.dev/linux/linux/drivers/phy/samsung/phy-s5pv210-usb2.c.html （设备 PHY 隔离控制 0xe010e80c bit 0）
- Linux 时钟驱动：https://codebrowser.dev/linux/linux/drivers/clk/samsung/clk-s5pv210.c.html （CLK_GATE_IP1 0xe0100464 bit 16）
- DWC2 寄存器定义：https://codebrowser.dev/linux/linux/drivers/usb/dwc2/hw.h.html

在 openboot 目录构建：

    git pull
    sh boot/build.sh

完全断电进入 iROM USB 模式，打开 UART0（115200 8N1）：

    sudo ./build-host/tq210-dnw out/rom_usb_probe.bin --pad-vendor-size

记录完整 USB OTG STATE 段及 DDR 启动结果。探测发生在 DDR 初始化前，正常 DDR scratch/exec PASS 后可发送小写 u 重复快照。在 Ubuntu 记录加载前后的 lsusb（本固件不主动重新枚举）。寄存器读取挂起时，最后的寄存器标签可帮助定位；断电并使用 rom_ddr_full.bin 恢复。如果时钟门控关闭或 PHY 隔离，则停止而不启用；若核心 ID 与预期 OTG 签名不同，或控制器处于主机模式，则避免设备专用读取。不读取/弹出 FIFO，不复位核心，不清除中断，不写 USB 或 NAND/SD。USB 状态可能异步变化，不能断言 iROM USB 仍保持枚举。此版本初次提交时仅审查源码，未交叉编译或实机验证。

## iROM 接管实机观察（2026-11-25）

DDR 初始化前后两份快照一致：GSNPSID=0x4F54281A（DWC OTG 2.81a），PHY_PWR=0x30，PHY_CLK=3，PHY_RST=0，PHY control=1，gate IP1=0xFFFFFFFF，设备模式，GAHBCFG=0x27（DMA 启用），GUSBCFG=0x540F，GRSTCTL=0x80000000，GHWCFG2=0x228FFC50，GHWCFG3=0x1E8084E8，GHWCFG4=0xFFF04030，RX FIFO 1024 字，非周期 TX FIFO 256 字，DCFG=0x40190，DOEPCTL2=0x000B8200（bulk OUT，最大包 512 字节），DOEPDMA2=0xD0022BB4。主机 lsusb 仍显示 04e8:1234。EP0 DMA 地址不是有效的应用缓冲区，不应向继承设备发送进一步 USB 流量。ROM 枚举仍可见并不证明自研驱动工作。

## 实验性 EP0 独立枚举

rom_usb_enum.bin 保留已验证 DDR 参数。启动正常 UART 控制台后发送一次小写 e，断开 ROM 设备、复位 OTG 核心、切换到 PIO/轮询（关闭 DMA），以私有测试 VID:PID 1209:0001 重新连接，制造商 OpenBoot、产品 TQ210 OTG 1。该 PID 并非独占，仅供私有测试，不用于分发的设备：https://pid.codes/1209/0001/。暂不声明 bulk 端点；零端点的厂商类接口是有意设计，仅测试设备描述符与 EP0 标准请求。驱动最初位于 boot/usb_enum.inc，重构后为 drivers/usb/usb_enum.c。DWC2 寄存器与 SET_ADDRESS 行为参考 Linux gadget.c 和 TinyUSB DWC2 slave-mode RX FIFO 处理。初次提交时为新代码，未交叉编译或实机验证。

    git pull
    sh boot/build.sh
    # 冷启动进入 iROM，打开 UART0
    sudo ./build-host/tq210-dnw out/rom_usb_enum.bin --pad-vendor-size

Ubuntu 另一终端可运行 sudo dmesg -w。DDR EXEC PASS 后经 UART 发送 e。预期 USB SOFT DISCONNECT、USB RECONNECT、USB BUS RESET、USB ENUM SPEED FIELD、SETUP 日志、SET ADDRESS、SET CONFIGURATION=1、OPENBOOT USB CONFIGURED。检查 lsusb 和 sudo lsusb -v -d 1209:0001。VMware 中新 VID/PID 可能连接到宿主机而非虚拟机，必要时手动连接至 Ubuntu。若只剩 Samsung 设备或新设备未枚举，记录完整 UART 与内核消息。若 e 挂起或 q 断开连接，断电恢复 ROM。不访问 NAND/SD。不要向仅支持枚举的设备发送另一份 DNW 镜像。

## Windows 枚举结果及沿用 DNW 驱动 ID（2026-11-25）

用户在 Windows USB 检查工具中看到自定义 1209:0001 设备：OpenBoot / TQ210 OTG 1，USB 2.0 High-Speed，设备、配置、接口、限定符及其他速度描述符均正确解析。Windows 报问题代码 28、配置 0，VMware 未提供该设备。未看到 SET_CONFIGURATION。不支持的 Microsoft OS 字符串 0xEE 与 BOS 请求被 STALL，但未妨碍描述符枚举。

SDK 中 `windows-tools/tqboard-usb-download-driver/tqboard-usb-download-driver/EmbedSky_TQ_Board.inf` 指定 DeviceID = "VID_04E8&PID_1234" 与 libusb-win32，默认配置 1。为本板私有开发，rom_usb_dnw_enum.bin 声明该 VID/PID，复用已安装的驱动与 VMware 匹配。制造商/产品仍为 OpenBoot / TQ210 OTG 1，接口仍有意声明零 bulk 端点。这仅是 ID 层兼容，不是逐字节克隆或 DNW 协议兼容。原厂 BL2 blob b271436e5e693a1de43b872a4c8b1d3d2937a83d 包含其他速度配置（全速 IN 0x83 / OUT 0x04，MPS 64；高速 IN 0x81 / OUT 0x02，MPS 512），供实现真实端点时参考。不要声明未实现端点。与 iROM 相同 VID/PID 有歧义，不得以自动检测同 ID 作为 stage2 就绪的证据。

    git pull
    sh boot/build.sh
    # 冷启动进入 iROM USB 模式，打开 UART0
    sudo ./build-host/tq210-dnw out/rom_usb_dnw_enum.bin --pad-vendor-size

DDR EXEC PASS 后发送小写 e。确认 UART 输出 "USB RECONNECT: local DNW driver ID 04e8:1234; EP0 only"。检查 Windows 设备管理器/USB 检查工具与 VMware 可移动设备，必要时连接到 Ubuntu。如果主机选择配置，预期 SET CONFIGURATION=1 和 OPENBOOT USB CONFIGURED。在 Ubuntu 使用 lsusb -d 04e8:1234 与 sudo lsusb -v -d 04e8:1234，确认产品为 TQ210 OTG 1 而非 ROM 产品。原 rom_usb_enum.bin 保持不变。由于尚未实现 bulk，不要再次启动 DNW 工具。不修改原厂固件二进制或驱动。预期但不保证复用驱动（策略、缓存、VMware 均可能影响绑定）。初次提交时仅审查源码，未编译或实机验证。

## DNW-ID 接管诊断

首个 DNW-ID 版本曾在 USB SOFT DISCONNECT 后、重连消息前停止输出。这不能证明 VID/PID 或 Windows 驱动故障，因为新描述符尚未发送。此次更新记录每个接管步骤与复位前 GAHBCFG/DCTL/GRSTCTL，断开连接前屏蔽控制器中断，断开稳定后再复位，并将原 5000 万次 volatile SRAM 循环改为仅寄存器 ARM 延时。保留限时 AHB/核心复位/FIFO 等待及超时寄存器诊断。PHY、DDR、EP0 描述符及测试 VID/PID 不变。延时未实际测量。MMIO 总线挂起无法由软件轮询限定，最后步骤标签用于定位。

重新构建并冷启动 rom_usb_dnw_enum.bin，DDR EXEC PASS 后发送一次小写 e。记录 PRE GAHBCFG 至最后的 [USB n] 步骤或 USB RECONNECT。停止时不要反复发送 e，断电恢复。必要时在独立冷启动中比较使用相同接管代码的 rom_usb_enum.bin（1209:0001）。两版均不支持 bulk 下载。此修订当时仅审查源码，未交叉编译或实机验证。

## EP0 接管确认；EP2 PIO bulk 实验

用户板卡上的 7343cb2 构建完成全部 11 个接管步骤，达到 SET_CONFIGURATION=1 与 OPENBOOT USB CONFIGURED。步骤 3、7 短暂停顿是有意的寄存器循环延时，未实测时长。保留 rom_usb_dnw_enum.bin 作为 EP0 基线。

新 rom_usb_bulk.bin 声明一个 OUT 端点 0x02（HS 512 字节，FS 64 字节），产品 TQ210 Bulk Test，bcdDevice 2，本地开发 VID/PID 仍为 04e8:1234。不实现 Bulk IN 或原厂 DNW stage2 协议。设备以 PIO 每次 arm 一个包，接收 16 字节小端包头（OBL1 magic 0x314c424f、有效长度、标准 CRC32、保留 0），随后接收有效数据。固定已验证 DDR 目的地址 0x24000000，最大 16 MiB；无任意地址或执行功能。CRC32 从 DDR 读回计算。厂商 EP0 只读请求 C0/5A，value 4F42，index 4C31，length 20，返回 magic、状态（0 就绪/1 接收/2 通过/3 失败）、已接收数量、预期长度、CRC。复位/重新配置丢弃传输状态。主机 --bulk-test 在 bulk 写之前检查产品字符串、版本、活动配置和协议 magic，发送 1 MiB+13 字节并验证设备状态，不要用于 iROM 或早期固件。

    git pull
    sh boot/build.sh
    cmake --build build-host
    # 冷启动进入 ROM 模式，打开 UART
    sudo ./build-host/tq210-dnw out/rom_usb_bulk.bin --pad-vendor-size

发送 e，将 TQ210 Bulk Test 连接到 Ubuntu。确认 EP2 BULK OUT READY MPS=0x00000200 和 OPENBOOT USB CONFIGURED：

    lsusb -d 04e8:1234
    sudo ./build-host/tq210-dnw --bulk-test

预期主机和 UART 均输出 BULK DDR VERIFY PASS 且 CRC32 相同。错误时停止、断电并记录日志。不可向已用过的接收器第二次上传；不执行代码或写 Flash。初次提交时未交叉编译或实机验证，驱动时序、端点中断及 FS 传输仍为实验性。

## EP2 bulk 数据传输实机验证

用户使用 rom_usb_bulk.bin 与原生 --bulk-test 成功传输 1,048,589 字节有效数据（另有 16 字节包头），共 1,048,605 字节。主机与 DDR 读回 CRC32 均为 0x6eefefc7。最后短包正常接收。未提供耗时，高速 USB 链路速率不等于实测吞吐量。

## 实验性 ARM DDR 镜像加载器

rom_usb_exec.bin 新增可执行标志（包头第 4 个字=1）、产品 TQ210 Exec Test、bcdDevice=3。目的地址仍为 0x24000000，最大 16 MiB；可执行镜像长度须 4 字节对齐。此前 bulk 固件仅接受标志 0。新 --load FILE 检查设备身份及协议，传输镜像，校验状态/CRC，但不自动执行。只有主机验证完成后 UART 发送小写 x 才执行通过校验的 flag-1 镜像；普通 --bulk-test 数据不能执行。检查 MMU/D-cache 关闭及 USB DMA 禁用，再次校验 DDR CRC，断开 USB，屏蔽 IRQ/FIQ，使 I-cache 和分支预测器失效，再调用 ARM 入口 0x24000000。继承 SVC 模式、iRAM 栈与 VBAR；镜像负责自身运行时、栈、异常向量。返回后停止在 OpenBoot。不实现 Linux 启动协议、重定位、ELF 解析或 Flash 写入；用于可信本地裸机镜像，而非认证启动。

    git pull
    sh boot/build.sh
    cmake --build build-host
    # 冷启动进入 iROM USB 模式
    sudo ./build-host/tq210-dnw out/rom_usb_exec.bin --pad-vendor-size

UART 发送 e，连接新设备至 Ubuntu，等待 EP2 就绪和 OPENBOOT USB CONFIGURED：

    sudo ./build-host/tq210-dnw --load out/ddr_app.bin

主机 BULK DDR VERIFY PASS 后经 UART 发送小写 x。独立链接的无栈 ARM 测试检查入口地址并打印 "DDR APP EXEC PASS: independent ARM image at 0x24000000"，随后循环。断电恢复。不要在同一次启动中先 --bulk-test 再 --load。初次提交时新代码未编译或实机测试，保留原 rom_usb_bulk.bin。

## 独立 DDR 执行验证

用户运行 rom_usb_exec.bin、--load out/ddr_app.bin，再发送 UART x。在 0x24000000 接收 188 字节（0xbc），首次读回和二次 CRC32 均为 0x9bb258bc，USB 断开，打印独立 DDR APP EXEC PASS。这验证了 ROM 时钟下的手动双阶段启动链。

## 实验性自动双阶段启动

rom_usb_auto.bin 在 DDR 检查后自动接管，revision 4 / TQ210 Auto Boot，增加明确厂商控制 GO（40/5b，value 4f42，index 4c31，无数据）。要求校验通过的 flag-1 镜像及 EP0 IN 状态阶段完成，随后短暂等待、断开并重复 DDR CRC。保留 UART x 后备方式。--boot 验证冷 ROM 产品字符串，将 SRAM 镜像填充至 11168 字节，使用已知 ROM 封包，最长等待 60 秒以发现新产品/版本与就绪协议状态，发送 DDR 镜像并检查 CRC，最后发出 GO。不自动重发 stage1。冷启动只连接一块匹配板卡。精确 ROM 字符串检查较保守，不同 ROM 描述符可能需调整。VMware 可能要求手动重连；除手动诊断外不要发送 e/x。GO ACK 不代表 CPU 已执行，需 UART 证明。无 NAND/SD 写入、Linux ABI、密码学认证或任意地址。此修订当时未编译或实机验证，保留已知手动固件。

    git pull
    sh boot/build.sh
    cmake --build build-host
    # 冷启动进入 iROM USB 模式，保持 UART 监视
    sudo ./build-host/tq210-dnw --boot out/rom_usb_auto.bin out/ddr_app.bin

预期最终 UART：DDR APP EXEC PASS: independent ARM image at 0x24000000。若固件超过 11168 字节，主机拒绝发送未验证的 ROM 帧。超时或错误时记录主机及 UART 日志，断电后再试。

## 自动启动主机路径验证；耗时实验

用户的 0a2416b 测试完成两个阶段和 USB GO ACK。未提供总耗时或该次自动启动的 UART 成功日志，因此此特定自动运行的实际执行未独立确认（此前手动 x 执行已确认）。

新主机报告 ROM 上传、stage2 就绪（含重新枚举）、DDR 上传（含主机 CRC）、验证状态、GO ACK 及总耗时，单位 ms。自动发现先检查设备版本，再读取字符串；轮询间隔 200ms 改为 50ms，就绪控制请求超时 1s（上传后的验证保持 30s）。60s 发现截止时间不是固定等待；缓慢的同步 libusb 调用可能稍微延长截止时间。

可选 rom_usb_fast.bin 保留两处 4000 万次断开/设备模式稳定延时、ROM 封包、DDR 检查、二次 CRC 和 USB 身份；独立的接管前与 GO 后延时缩短至 400 万次，并取消每个 EP0 SETUP 的三行详细日志。实际收益取决于 CPU 和主机时序。原 rom_usb_auto.bin 保留原延时与日志。两版均不改变 400 MHz CPU 时钟或 PIO 引擎。当时是实验修改而非实测速升，应在同一 VM 上分别冷启动比较。

    git pull
    sh boot/build.sh
    cmake --build build-host
    # 每条命令单独冷启动
    sudo ./build-host/tq210-dnw --boot out/rom_usb_auto.bin out/ddr_app.bin
    sudo ./build-host/tq210-dnw --boot out/rom_usb_fast.bin out/ddr_app.bin

记录所有 [TIME] 行及最终 UART DDR APP EXEC PASS。总耗时截止于主机 GO ACK，而非最终镜像执行。快速版退化时使用基线。此提交当时未交叉构建或实机验证，后续优化应依据测量，VMware/Windows 枚举可能占主要时间。

## 快速启动耗时实测

用户测得 ROM 上传 19 ms、stage2 就绪 4925 ms（包括 DDR 初始化及 USB 重新枚举）、DDR 上传 1 ms、验证 18 ms、GO ACK 0 ms，总计 4968 ms。未单独测量设备初始化或 VMware 枚举；99.1% 是综合就绪时间。

## 不重新枚举的 DMA 接管实验

新 rom_usb_handoff.bin 与 --handoff-test 有意保留原 USB 地址及主机 libusb 句柄；stage1 后不复位、不 SET_CONFIGURATION、不查询产品或状态。DWC OTG 2.81a buffer DMA 保持启用，EP2 必须未启用传输、类型 bulk/HS 512 字节且非 STALL。设置全局 OUT NAK，等待 AHB 空闲；将继承 EP0 DMA 指针转向自身对齐的 512 字节 SRAM 缓冲区，读回验证；遇意外 SETUP、复位、DMA 错误停止。这是受控 EP2 实验，不是通用 USB 栈：EP0 SETUP 可能绕过 NAK；禁止其他主机流量。写入活动 EP0 DMA 寄存器在此版本可能被忽略或行为未定义，读回仅为预防，不能证明安全。硬件故障或失败需冷复位。保留 SRAM/DDR 检查；使用两个独立固定 DMA 包（512、37 字节），不发送 DATA0/DATA1 重置命令，验证长度、内容及 0x24000000 的 DDR 副本。无任意代码执行或 NAND/SD 写入。DMA/端点寄存器参考 https://codebrowser.dev/linux/linux/drivers/usb/dwc2/hw.h.html ；全局 OUT NAK/端点关闭参考 https://cos.googlesource.com/third_party/kernel/+/7666206b23e339344a2974d8af2b7fce882288f5/drivers/usb/dwc2/gadget.c 。这些资料不能证明活动 ROM 接管受支持。

    git pull
    sh boot/build.sh
    cmake --build build-host
    # 冷 ROM，单板，打开 UART，不使用 lsusb -v 或其他 USB 工具
    sudo ./build-host/tq210-dnw --handoff-test out/rom_usb_handoff.bin

旧版实验需要等待 UART 确切的 HANDOFF EP2 READY，再在主机输入 READY。未就绪时输入其他内容或 Ctrl-C 并冷复位；不再发送数据。预期 UART HANDOFF SAME CONNECTION PASS 及主机两个同句柄传输。无自动回退或重试。初次采用手动就绪门槛以避免接管失败时向 ROM 发送数据，无法测量自动总耗时。保留原 rom_usb_fast.bin。此提交当时未交叉编译或实机验证。

## 自动同句柄就绪实验

用户在初始手动版 UART 看到 HANDOFF EP2 READY，尚未确认包传输。改用同一 USB 句柄上的只读 Bulk IN 0x81 就绪消息取代 stdin READY。stage1 前主机检查 ROM 活动配置在同接口声明 HS Bulk IN 0x81 和 OUT 0x02。设备额外检查继承 EP1 活动 bulk 配置、空闲及 MPS=512，保留 TX FIFO 选择与数据 PID，提供对齐的自有 SRAM DMA 缓冲和一次 16 字节 IN 传输。EP2 arm 后发 OB-HANDOFF-RDY1!；仅收到精确标记才允许主机发送 512 和 37 字节测试包。DDR 内容校验后返回 OB-HANDOFF-OK01!；只有收到精确标记主机才打印 HANDOFF DDR VERIFY PASS。IN 请求超时 5 秒；无自动重试、stage1 后无 EP0 请求、不重新枚举、不执行。EP0 DMA 保护限制仍适用，当时 IN 硬件行为尚未测试。UART 仅诊断。主机与固件一起重建，仍使用 --handoff-test out/rom_usb_handoff.bin，无需输入。错误时记录日志并冷启动。此修订当时未交叉编译或实机验证。

## 同句柄双向 DMA 实机验证

0ada083 固件打印 EP1 就绪/校验通知完成、EP2 包长 512 和 37、EP2 控制在 0x000A8200 和 0x000B8200 间交替，并输出 HANDOFF SAME CONNECTION PASS: 549 bytes, DMA + DDR readback。证明受控同连接双向传输工作，无 USB 复位或重新枚举。未报告此次总耗时。

## 实验性连续直接 DDR DMA 启动

rom_usb_dma_boot.bin 保留已验证接管保护及 EP1 通知路径。OBL2 协议：16 字节包头（magic 0x324c424f、长度、CRC32、可执行标志），DDR 基址固定 0x24000000，最大 16 MiB。包头单独短 OUT，随后 EP2 直接 DMA 到 DDR，每窗口至多 65536 字节/128 包。最终 DMA 容量在保留区内向上对齐 512 字节，并按剩余量检查实际长度。上一 DMA 完成后才 arm 下一次。读回 CRC32 用 16 项半字节表。EP1 发送精确就绪、包头接受、校验、GO 应答标记。可执行镜像需单独精确 GO 包，并在 GO ACK 前二次 CRC。断开连接，AHB 空闲后关闭 DMA，使 I-cache/分支预测失效，进入 0x24000000 ARM 镜像。继承早期 SVC 栈/VBAR ABI，不实现 Linux ABI。不写 NAND/SD；纯数据测试不执行。主机及固件须一起更新，保留 rom_usb_handoff.bin 和 rom_usb_fast.bin。继承 EP0 保护限制仍存在，仅受控主机，不允许其他 USB 查询。

    git pull
    sh boot/build.sh
    cmake --build build-host
    # 每条命令单独冷启动
    sudo ./build-host/tq210-dnw --dma-boot out/rom_usb_dma_boot.bin out/ddr_app.bin
    sudo ./build-host/tq210-dnw --dma-test out/rom_usb_dma_boot.bin

预期启动 UART DDR APP EXEC PASS；纯数据测试 1048589 字节，CRC32 0x6eefefc7，不执行。主机计时截止于 GO ACK，不代表实际应用执行。此修订当时无直接 DDR 吞吐实测，新多包 DMA、CRC 与启动路径未编译或实机验证。异常状态停止，重试前冷启动。

## 直接 DDR DMA 启动与 1 MiB 测量

用户 30a40e9 测试：188 字节镜像 CRC32 0x9bb258bc，同句柄就绪 153 ms，上传 1 ms，验证 10 ms，GO 与二次 CRC 4 ms，总计至 GO ACK 197 ms（此前重新枚举路径 4968 ms）。该次未提供 UART 执行行。纯数据 1048589 字节 CRC32 0x6eefefc7：上传 51 ms（约 19.61 MiB/s），验证/状态 813 ms（约 1.23 MiB/s），总计 1046 ms。两次设备验证标记均被接受。命令之间重启进入 ROM；首次未重连的尝试无法访问 04e8:1234。

## 可选更快的 DDR CRC

rom_usb_dma_fast.bin 保留 OBL2 协议、DMA 窗口及双重校验。在 iRAM 生成 256 项 CRC32 表（增加 1024 字节 BSS），目标板自检 123456789 -> 0xcbf43926。以对齐小端 DDR 字读取代替四次字节读取，每字四次查表；处理 0..3 尾字节，不越界。每 4KiB 及尾部后检查 USB 状态。不修改 MMU/cache 或时钟。主机验证/GO 超时增至 30s，就绪仍 5s。主机输出近似有效数据及验证/状态 MiB/s。保留原半字节表 rom_usb_dma_boot.bin 用于 A/B。JS 等价参考算法已与逐位 CRC 在 21 种长度（含 1 MiB+13）及已知向量对比通过，不是 C/ARM 编译或实机测试。此版本初次提交时未交叉编译，需检查 iRAM 链接断言及原厂帧大小，尚无实测速升。

    git pull
    sh boot/build.sh
    cmake --build build-host
    # 每次冷启动进入 ROM
    sudo ./build-host/tq210-dnw --dma-test out/rom_usb_dma_fast.bin
    sudo ./build-host/tq210-dnw --dma-boot out/rom_usb_dma_fast.bin out/ddr_app.bin

预期相同 CRC32、校验标记，以及可执行测试 UART DDR APP EXEC PASS。报告 TIME 和 RATE 行。不访问 Flash。

## 模块化重构后的实机回归（2026-11-25）

用户在 14a783f 修复后重新构建，rom_usb_dma_fast.bin 下载 188 字节独立镜像，CRC32 0x9bb258bc，打印 HANDOFF DDR EXEC ENTRY=0x24000000，随后独立 DDR APP EXEC PASS。纯数据测试 1048589 字节、CRC32 0x6eefefc7，stage1 20 ms、就绪 153 ms、上传 54 ms（18.35 MiB/s）、验证 628 ms（1.59 MiB/s），总计 867 ms。相比此前 1046 ms，整体耗时降低约 17%，但不是所有变体或环境的性能保证。主要 DMA 路径已验证，其余变体仍需回归。详见 [REFACTOR.md](REFACTOR.md)。
