# 编译与运行

## 两种链接方式

| 产物 | 用途 | 运行时是否需要 libpilot |
| --- | --- | --- |
| `libpilot.a` | 编译时将 Pilot 的实现静态链接进可执行文件 | 不需要 |
| `libpilot.so` → `libpilot.so.1` | 动态链接 | 需要 `libpilot.so.1` |

**`.so` 不能当成 `.a` 静态链接。** 两种库对外 API 完全一致。静态模式只是把 Pilot 静态嵌入，并非把整个程序、libc 和底层系统库全部静态化；开发板仍需已有的系统服务及 `libdbus-1.so.3`，无需用户自己编写通信代码。

## 用户使用包

`pilot-0.3.0-arm-sdk.tar.gz` 是二进制使用包，不包含库实现源码、私有头文件、协议清单或第三方通信头文件：

```text
pilot/
  include/pilot/       公共业务头文件
  lib/arm/             libpilot.a、libpilot.so、libpilot.so.1
  bin/arm/             只读监测示例和 LCD 示例程序
  examples/monitor/   示例源码与 Makefile
  examples/lcd/       LCD 页面与弹窗示例源码、Makefile
  pilot.mk            用户工程编译/链接入口
  toolchain/          匹配的工具链、解包脚本
  vendor/             构建所需的私有链接依赖
  docs/               公共 API、使用说明、测试记录
```

二进制使用包暂仅用于本地测试，第三方再分发条件见 [THIRD_PARTY.md](../THIRD_PARTY.md)。公开源码仓库不包含工具链或预编译库，维护者构建方式在本文后半部分。

### 编译示例

在 Linux x86_64 / WSL2 下解压使用包，需要 `make`、`tar`、`sha256sum`：

```sh
cd /path/to/pilot
sh toolchain/setup.sh
# 默认静态链接 Pilot，不需要在板上安装 libpilot.so。
make -C examples/monitor
# 动态链接版本。
make -C examples/monitor PILOT_LINK=shared OUTPUT=pilot-monitor-shared
# LCD 菜单页、进度条与弹窗示例，默认静态链接 Pilot。
make -C examples/lcd
```

用户工程只需要包含这个 Makefile 片段：

```makefile
PILOT_ROOT := /path/to/pilot
PILOT_LINK := static
include $(PILOT_ROOT)/pilot.mk

my-app: main.c
	$(CC) $(PILOT_CPPFLAGS) main.c $(PILOT_LDLIBS) -o $@
```

不需要安装第三方通信头文件，也不需要手工配置其编译/链接参数。`PILOT_LINK=shared` 改用动态库。源码工程和二进制使用包均可使用同一个 `pilot.mk`。不要用普通 glibc ARM 工具链代替匹配的 uClibc 工具链。

### 代码示例

```c
#include <pilot/api.h>
#include <stdio.h>

int main(void) {
    pilot_client *client = NULL;
    pilot_error error;
    if (pilot_open(NULL, &client, &error) != PILOT_OK) return 1;

    pilot_imu_read_result imu = {0};
    int rc = pilot_imu_read(client, &imu, &error);
    if (rc == PILOT_OK && imu.valid)
        printf("gyro: %.3f %.3f %.3f\n", imu.gyro_x, imu.gyro_y, imu.gyro_z);
    pilot_imu_read_clear(&imu);
    pilot_close(client);
    return rc == PILOT_OK ? 0 : 1;
}
```

### 板端运行

只上传自己编译的应用，不要上传交叉编译器或整个工具链。默认只读示例支持 `--monitor`，不会修改控制输出、音量、风扇或配置。

```sh
chmod +x /tmp/pilot-monitor
/tmp/pilot-monitor --monitor
```

动态链接应用需携带同版本 `libpilot.so.1`，放在应用私有目录，并仅为该程序配置 `LD_LIBRARY_PATH`。不要覆盖系统 libc 或通信库。SDK 不绕过现有权限和控制保护；例如风扇控制和网络恢复策略仍受固件权限限制。

## 源码维护者

源码工程中的 `src/private/` 和 `protocol/` 是库内部实现，仅构建和测试使用，不安装、不放进用户使用包。协议名称位于库内部，动态库只导出公共头文件声明的业务 API。

首次从 GitHub 克隆后，使用已有固件 SDK 导入匹配依赖：

```sh
python3 scripts/import_sdk.py /path/to/quicore --toolchain
python3 scripts/generate.py
sh toolchain/setup.sh
make -j2
make verify
```

生成 `build/arm/libpilot.a`、`libpilot.so.1`、`pilot-monitor`、`pilot-monitor-shared` 和 `pilot-lcd`。更新源协议后，必须同时更新业务映射；生成器会拒绝遗漏的新能力。

```sh
make TARGET=host test
make TARGET=host SANITIZE=1 test
make package
python3 scripts/verify_bundle.py dist/pilot-0.3.0-arm-sdk.tar.gz
```

主机测试需 `gcc`、`g++`、`pkg-config`、`libdbus-1-dev`、`dbus-daemon` 和 Python 3。测试不会操作真实开发板。[测试记录](TEST_REPORT.md) 明确区分模拟通信验证和实板测试。

## 兼容和边界

0.3.0 以新增接口的形式提供 LCD 页面扩展，保留 0.2.0 的现有接口、符号版本与 `libpilot.so.1` ABI；配套显示程序需要同时更新。新增示例为 `examples/lcd`，编译输出为 `pilot-lcd`。

0.2.0 收紧了公共接口，与 0.1.0 的头文件/ABI 不兼容，动态库 SONAME 提升为 `libpilot.so.1`。迁移时使用业务 API 重新编译程序，不能仅替换旧 `.so.0` 文件。

这是 API 封装，不是协议保密或安全隔离：源码维护者可以看到内部实现，二进制中的字符串也可能被分析。先前已经公开的 Git 历史不会被本次修改删除。SDK 提供 USB 上传工具；安装服务和运行资源约束由配套固件提供，具体限制见 [程序上传与运行](DEPLOY.md)。SDK 不自动修改固件、烧录、提交或推送。
