# Pilot 开发板 SDK

Pilot 把 Andless 开发板现有的 D-Bus 能力封装为 C 库，供用户的独立程序调用，也可以从 C++ 使用。程序运行在开发板 Linux 上，通过系统 D-Bus 与现有服务通信，不需要链接 `quic_client`、修改固件业务代码，也不需要自己拼 D-Bus 报文。

当前版本：`0.1.0`。目标平台：RV1106，ARM 32 位小端、hard-float、uClibc。**不要使用普通 `arm-linux-gnueabihf` 的 glibc 工具链替代附带工具链。**

## 已封装的能力

| 服务 | 方法 / 信号 | 能力 |
| --- | --- | --- |
| RealTimeComm | 26 / 5 | IMU、磁力计、GPS、基站定位、设备遥测、音量、音频频谱、相机预览、通道输出/调试、连接/传输诊断、电池及 ADC 配置 |
| SelfCheck | 4 / 1 | 自检报告、重新自检、查询/设置本次启动的网络恢复策略 |
| Fan | 2 / 0 | 查询风扇状态和配置、设置并持久化配置 |
| Modem | 0 / 1 | 基站与信号状态广播 |

合计 **32 个方法、7 个广播信号**，来自当前开发板工作区的实际实现；`dbus_test` 演示服务不属于产品接口。每个方法都有对应的 C 函数和结果结构，不只提供原始消息转发。完整方法、参数顺序、D-Bus 类型见 [接口清单](docs/API.md)，复杂返回值和写入规则见 [使用说明](docs/USAGE.md)。

这不是新建一个板端服务。不同固件可能缺少部分方法，调用返回 `PILOT_NOT_SUPPORTED`，不能据此假定设备损坏。未来计划中的接口不会冒充已经存在的能力，例如当前快照没有 EIS 校准 D-Bus 接口。

## 目录

```text
pilot/
  include/pilot/        公共头文件 pilot.h、api.h，不暴露 libdbus 类型
  src/                 通信实现和生成的类型化包装函数
  protocol/            完整 ABI 快照、源码提交及工作区文件 SHA-256
  examples/monitor/    只读查询与广播订阅示例，含独立 Makefile
  toolchain/           ARM 工具链压缩包、校验文件、解包脚本、Make 配置
  vendor/dbus/         与当前固件匹配的 libdbus 头文件及动态库
  scripts/             依赖导入、接口生成、打包工具
  tests/               独立 D-Bus 测试服务与回归测试
  build/arm/           libpilot.a、libpilot.so.0、pilot-monitor
  dist/                自包含 SDK 测试包
```

工具链和导入的第三方二进制不进入 Git，随本地开发包交付。公开仓库仅包含源码、文档、示例和构建脚本；从 GitHub 克隆后，需要先按下文从已有固件 SDK 导入依赖。工具链压缩包保留原有符号链接和许可证，解包到 Linux 原生文件系统，避免 Windows 复制 `.so` 符号链接引起交叉链接失败。

## 快速交叉编译

在 Linux x86_64 或 Windows WSL2 Ubuntu 中执行，需要 `make`、`python3`、`tar`、`sha256sum`。以下命令适用于已包含交叉编译器、sysroot 和 libdbus 的本地开发包；GitHub 源码版请先完成“从源码仓库导入依赖”。

```sh
cd /path/to/pilot
sh toolchain/setup.sh
make -j2
```

产物在 `build/arm/`：

- `libpilot.a`：推荐用户程序静态链接这个小型 SDK 库，但 libdbus 和 libc 仍动态链接。
- `libpilot.so.0`：需要动态链接 SDK 时使用，SONAME 为 `libpilot.so.0`。
- `pilot-monitor`：可以在开发板运行的 ARM 示例程序。

默认从 `$HOME/.cache/pilot/toolchains/<压缩包摘要>/` 使用解包后的工具链，不依赖原 `quicore` 目录。也支持手动指定：

```sh
make TOOLCHAIN_ROOT=/path/to/arm-rockchip830-linux-uclibcgnueabihf
make -C examples/monitor
```

`examples/monitor/Makefile` 可作为用户工程模板：包含 `toolchain/arm.mk`，链接 `libpilot.a`、`libdbus-1` 和 pthread。构建目录放在不含空格的路径中。

### 从源码仓库导入依赖

若没有附带二进制文件，只在首次准备或有意更新接口时执行：

```sh
python3 scripts/import_sdk.py /path/to/quicore --toolchain
python3 scripts/generate.py
sh toolchain/setup.sh
make -j2
```

导入脚本读取 SDK，不修改开发板源码。它会更新 Pilot 中的 ABI 快照，包含源工作区尚未提交的接口变更；请审阅生成差异。日常编译不需要再次导入。接口生成只用 Python 标准库，不需要 Python 运行在板上。

## 最小调用示例

```c
#include <stdio.h>
#include <pilot/api.h>

int main(void) {
    pilot_client *client = NULL;
    pilot_error error;
    int rc = pilot_open(NULL, &client, &error);
    if (rc != PILOT_OK) {
        fprintf(stderr, "%s: %s\n", error.name, error.message);
        return 1;
    }
    pilot_rtc_get_imu_data_result imu = {0};
    rc = pilot_rtc_get_imu_data(client, &imu, &error);
    if (rc == PILOT_OK) {
        printf("valid=%d gyro=(%.3f, %.3f, %.3f)\n",
               imu.valid, imu.gyro_x, imu.gyro_y, imu.gyro_z);
    } else {
        fprintf(stderr, "%s: %s\n", error.name, error.message);
    }
    pilot_rtc_get_imu_data_clear(&imu);
    pilot_close(client);
    return rc == PILOT_OK ? 0 : 1;
}
```

必须检查数据的 `valid` / `available`，不能只看方法调用成功。获取到的 IMU 是现有遥测数据，不是新增高频 FIFO 或精密视频同步接口。

## 在开发板运行

把 `build/arm/pilot-monitor` 用现有上传渠道传到板端临时目录，再通过终端运行：

```sh
chmod +x /tmp/pilot-monitor
/tmp/pilot-monitor
/tmp/pilot-monitor --monitor
```

示例默认只读，不修改 PWM、音量、电池、风扇、不重启设备。`--monitor` 订阅 IMU、磁力计、自检、基站信息，Ctrl+C 退出；总线断开后每秒尝试重连，恢复原订阅，但不自动重放写入命令。输出不要长期无界写入板端 Flash。

板端需要已有 `dbus-daemon` 和 `libdbus-1.so.3`。**不要把主机库、交叉编译器或整个 SDK 拷到开发板，不要覆盖板端的 libc/libdbus。** 若单独使用动态版 libpilot，将 `libpilot.so.0` 放到应用私有 `lib/`，仅给该程序配置 `LD_LIBRARY_PATH`。

本轮只完成主机测试与 ARM 编译，未上传或执行板端程序，实板权限、传感器新鲜度、服务负载需要后续验证。

## 资源和权限边界

- SDK 本身不提供 CPU/内存沙箱，不是安全边界，也不要求用户程序进入主通信进程。后续资源限制应交给应用管理器和内核控制组。
- 默认使用系统总线。当前风扇服务在板端要求 root，网络恢复模式写入也检查 root 身份；SDK 不绕过这些检查，不自动修改 D-Bus policy。
- 不建议为了某个接口让任意第三方程序长期以 root 运行。正式用户应用部署前应增加受控代理或最小权限策略；这版不擅自修改现有固件权限。
- 超时意味着执行结果可能未知，尤其是写入操作。先重新读取状态，不要无脑重试。业务 `accepted=false`、JSON 的错误状态不等于通信失败，要额外检查。

## 验证和打包

Linux 主机测试需要 `gcc`、`g++`、`pkg-config`、`libdbus-1-dev`、`dbus-daemon`。

```sh
make TARGET=host test
make TARGET=host SANITIZE=1 test
make verify
make package
python3 scripts/verify_bundle.py dist/pilot-0.1.0-arm-sdk.tar.gz
```

测试使用 `dbus-run-session` 隔离测试总线，覆盖全部方法和信号，不会操作开发板、风扇或 PWM。`make verify` 检查生成物一致性并展示 ELF 信息；`make package` 生成 `dist/pilot-0.1.0-arm-sdk.tar.gz` 及 SHA-256。

测试结果和未验证项见 [测试报告](docs/TEST_REPORT.md)。本项目不会自动 push、发布、烧录或开机启动。
