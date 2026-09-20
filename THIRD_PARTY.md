# 第三方组件与来源

- `vendor/dbus/` 来自当前固件 `project/app/RealTimeComm_datagram/thirdparty/dbus/`。头文件保留上游版权/许可说明，库保持原样；摘要在 `vendor/dbus/SHA256.json`。它只用于交叉链接，不自动安装或覆盖开发板系统库。
- `toolchain/arm-rockchip830-linux-uclibcgnueabihf.tar.gz` 原样打包固件 SDK 的对应工具链，包含 GCC、binutils、uClibc、sysroot 等。各组件许可文本位于归档的 `share/licenses/`。这是 Linux x86_64 主机工具链，不是 Windows 原生编译器。
- D-Bus 接口来自本项目固件工作区，源提交与文件摘要在 `protocol/interfaces.json`。生成的是客户端接口，不复制或运行固件业务服务。

公开源码仓库不包含上述第三方二进制和工具链归档，也未另行指定 SDK 自有代码的开源许可证；仓库公开不等于已经授予任意使用和再分发许可。本地开发包的第三方源码提供义务尚未核验。对外分发二进制包前，应由项目维护者确认所需许可，并补齐对应源码、通知和许可文本。不要只拷贝二进制后删除上游许可证。
