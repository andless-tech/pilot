# 程序上传与运行

需要包含 Pilot 程序管理功能的新固件。旧固件不支持这些操作；工具不会退回普通文件传输或以 root 执行。

## 单程序部署

- 开发板只安装一个用户程序，固定路径为 `/opt/pilot/app`，不提供远端路径、名称、运行参数或执行用户选择。
- 最大 **1 MiB = 1,048,576 字节**。大小在主机和开发板两端检查。
- 只接收 Pilot 工具链兼容的 ARM32 小端 EABI5 hard-float ELF 可执行文件，不接收脚本、压缩包、Windows 程序或 ARM64 文件。
- 推荐静态链接 `libpilot.a`，运行时仍使用固件内已有的基础动态库。不要上传依赖尚未安装库的动态链接示例。
- 每块分片校验 CRC32，完整文件校验 SHA-256 和 ELF，再原子覆盖旧程序。传输失败保留旧程序；不在开发板保留历史备份。主机自行保留源码和已验证二进制。
- 默认仅安装，当前运行实例继续运行；下次开机使用新程序。`--restart` 只重启用户程序，不重启开发板。
- 文件跨正常关机、重启保留。重新烧录 rootfs 或擦除 Flash 不保证保留用户程序。

## 使用

连接处于 USB 设备模式的开发板，避免 Dashboard 或其他工具同时占用 HID 接口。在电脑安装 Python 3 和 `hidapi`：

```sh
python -m pip install hidapi
python scripts/pilot-hid.py upload build/arm/pilot-lcd
python scripts/pilot-hid.py status
python scripts/pilot-hid.py logs
```

立即启用新程序：

```sh
python scripts/pilot-hid.py upload build/arm/pilot-lcd --restart
python scripts/pilot-hid.py stop
python scripts/pilot-hid.py restart
```

二进制 SDK 包中的示例路径为 `bin/arm/pilot-lcd`。多台设备时，在子命令前使用 `--serial USB序列号`，工具不会随意挑选一台。

已有未完成传输时返回忙，不抢占别人的传输；无有效上传活动 120 秒后清理。HID 服务异常退出后，至多留下一个临时文件，下次上传覆盖该临时文件，不形成多份累积。

## 运行约束

| 项目 | 设置 |
| --- | --- |
| 身份 | 独立的 pilot 用户，UID/GID 450，禁止登录，无附加用户组 |
| 开机 | 后台启动，不等待网络，不阻塞系统初始化 |
| CPU 调度 | nice=19，低优先级；不是固定 CPU 百分比上限 |
| 内存 | 32 MiB 虚拟地址空间上限，不是 RSS 或系统总内存配额 |
| 线程/文件 | 最多 16 个任务、64 个描述符，单文件写入上限 1 MiB，禁用 core dump |
| 进程 | 支持线程，禁止 fork、创建子进程和脱离进程组；不能通过 system/popen 启动命令 |
| 提权 | 清空 capabilities，启用 no_new_privs，不允许通过 setuid 文件提权 |
| 退出 | TERM 后 1 秒未退出则 KILL；监管进程退出时不遗留用户程序 |
| 异常重试 | 5 秒、10 秒退避；连续三次短时退出停止重试，手动 restart 或下次开机再尝试 |

应用应作为前台常驻程序运行，不要自行 daemonize。运行超过 60 秒后再次退出会开始新一轮失败计数。不安装程序也能正常开机，状态显示 `not_installed`。

这些机制减少开发程序失控的影响，**不是执行恶意程序的完整安全沙箱**：当前固件没有启用 CPU/内存 cgroup 配额和命名空间隔离，已有系统能力的权限策略仍需单独收紧。只运行可信代码；调用通道等控制功能仍须注意实物安全。

## 日志

`logs` 返回 stdout/stderr 合并后的最后 **1024 字节**，仅存内存，重启开发板或监管服务后清空，不写 Flash。跨用户程序重启保留最近日志；状态还包含本次监管服务累计收到的字节数。

大量打印会收到管道背压，不会造成日志文件无限增长。二进制日志、截断的 UTF-8 字符在终端按替换字符显示。程序使用缓冲 stdio 时，请调用 `fflush` 或设置行缓冲，否则日志可能尚未输出。

## 当前验证范围

已完成主机异常路径测试、运行监管测试和 ARM 交叉编译，未自动烧录或部署。真正的 USB 传输、断电持久化、板端专用用户权限与长时间负载仍需要实板验收。
