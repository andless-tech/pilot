# Pilot SDK 测试记录

日期：2026-09-20。这是主机与构建验证记录，**不是实板验收报告**。

## 接口基线

接口从当前 `quicore` 工作区采集，包含未提交的 BatteryCommand 等修改。确切源提交和四个服务文件的 SHA-256 在 `protocol/interfaces.json` 中。没有修改原板端服务、设备树、启动项或固件。

## 已验证

| 项目 | 结果 |
| --- | --- |
| 32 个方法的输入/返回签名与封装传输 | 独立 session bus 测试通过 |
| 7 个信号的订阅、参数解析、重复订阅、取消 | 通过 |
| IMU、GPS 字典、音量等类型化函数 | 通过 |
| 字符串、布尔、带符号整数、64 位整数、double | 通过，包含超过 2^53 的值 |
| 数组、结构体、variant 字典、字节数组、空数组 | 通过 |
| 错误签名、错误参数、无效 UTF-8、空指针 | 通过 |
| 超时、权限拒绝、旧固件不支持方法 | 通过，保持各自错误码 |
| 服务名称所有者变化、订阅自动跟随服务重启 | 通过 |
| 非服务所有者发出的同名广播隔离 | 通过 |
| 断开私有测试总线、重连失败、重启总线后恢复 | 通过 |
| 显式重连恢复订阅、不自动重放写入 | 通过 |
| AddressSanitizer / UndefinedBehaviorSanitizer | 通过 |
| C++11 公共头文件编译检查 | 通过 |
| 当前工具链交叉编译静态库、动态库、示例 | 通过 |
| 独立示例 Makefile 交叉链接 | 通过 |
| SDK 包解压到新目录，用包内工具链和全新缓存重新编译 | 通过，不依赖原 quicore 路径 |
| ELF ABI | ARM / ELF32 / little endian / EABI5 / hard-float |
| 运行时加载器 | `/lib/ld-uClibc.so.0` |
| 依赖 | 动态依赖板端 libdbus 与 uClibc，不包含 quic_client/media 栈 |

## 复现

```sh
make TARGET=host test
make TARGET=host SANITIZE=1 test
make verify
make -C examples/monitor
make package
python3 scripts/verify_bundle.py dist/pilot-0.1.0-arm-sdk.tar.gz
```

协议测试使用模拟服务，验证的是客户端 ABI 与通信行为，不替代服务业务逻辑测试。模拟服务不会接触真实 GPIO、音频设备或配置文件。总线重启测试只终止它自己创建的临时 `dbus-daemon`。

## 尚未验证

- 没有上传到开发板，也没有执行示例；各版本固件的服务可用性和实际权限需实板验证。
- 没有测量板端 CPU、内存、持续运行负载，也没有验证真实传感器或相机数据。
- 没有实际改变风扇、音量、PWM、电池校准或网络策略。
- 暂不提供沙箱、安装服务或资源限额；本版是客户端通信 SDK。
