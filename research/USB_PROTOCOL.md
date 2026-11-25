# USB 协议研究更新

## 直接二进制证据

原厂 x86-64 ELF 的 Git blob：f3455348b6ce5f52d967632508f2c47817ea11f1。

- 0x427ab0 将第五个参数（镜像类型，r9d）保存到对象+0x20。TQMain::download210Boot（0x417910）中的类型 2 选择普通封包。
- 0x417710 在启动固件模式使用 0xd0020010，在另一分支的镜像类型 2 使用 0x23e00000。
- 0x427f39 计算文件长度+10；0x427f4f–0x427f5e 设置地址及长度包头。0x4280b8 首块请求 2040 字节，0x428091 后续请求 2048 字节。
- 0x42804c 调用 libusb_bulk_transfer，超时 2000 ms；0x428143 独立传输 2 字节校验和，超时 1000 ms。二者均从全局地址 0x641114 读取 OUT 端点。
- csum_upload（0x4278a0）仅调用一次 fread，size=1、count=0x1000，然后累加返回字节；无外部读取循环。默认实验复制前 4096 字节校验，全文件累加为可选模式。原厂 feof 循环在恰好块边界时可能多发一个零长度 bulk 请求；实验工具省略该请求。
- 283 字节特殊前缀分支仅在镜像类型 0 时进入，并检查文件是否以 "nb0" 结尾（字符串位于 0x42bdb4）；它不属于类型 2 普通启动路径。

## BL2 二进制

Git blob b271436e5e693a1de43b872a4c8b1d3d2937a83d，11168 字节。

- 首字 EA000006（跳到文件偏移 0x20）。
- 若链接基址为 0xd0020010，文件偏移 0x10f8 的常量 0xd0022ab2 对应文件偏移 0x2aa2 的描述符数据，支持原厂包头地址。0x1d68 处为 0x23e00000。
- 包含全速 OUT 0x04、高速 OUT 0x02 描述符及 SEC S5PC110 Test B/D 字符串。接管后 USB ID 可能相同；仅重新枚举不能证明执行。
- 累加校验和：前 4096 字节 0xeef6，全镜像 0x18b2。
- TQ210 资料佐证：https://www.eepw.com.cn/article/201611/322804.htm 描述向 0xd0020010 发送板卡固件，再向 0x23e00000 发送 DDR 用户镜像。不要混淆 USB 包地址和 ROM 入口。

## Ubuntu 实验

Python 工具通过 ctypes 使用现有 libusb 运行库，无 Qt 或 pip 依赖，仅进行 USB 写入，不执行 Flash 命令。提供的 stage1 是原厂固件而非自研。stage2 使用新编写的无栈 ARM 汇编 UART 程序，链接至 DDR。假设 PCLK 与原厂时钟配置一致（约 66.7 MHz）；BL2 后不修改 PLL。UART0/1 TX 以 115200 8N1 测试。初次提交时尚未实机验证。

    cd research
    python3 dnw_ram.py --selftest
    python3 dnw_ram.py stage1 --dry-run
    bash build_uart_ddr.sh
    sudo python3 dnw_ram.py stage1

等待重新枚举，检查 lsusb 和 VMware 设备连接，然后：

    sudo python3 dnw_ram.py stage2 uart_ddr.bin

若 stage1 未启动，复位并用 --checksum full 重试一次。确认 stage1 接管前不要发送 stage2。不自动重试。观察 Windows COM5，stage2 应持续打印。未验证电压兼容性时断开 CH340 TX 与 CPU RX。USB 传输成功或 VID/PID 相同均不能证明 CPU 执行。后续实机确认的 stage2 选项见 [HW_PROBE.md](HW_PROBE.md)。
