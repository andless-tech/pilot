# 接口使用约定

## 生命周期、线程与超时

所有函数返回 `PILOT_OK` 或负的 `pilot_status`，详细错误位于调用者提供的 `pilot_error`。不使用全局 last-error。`pilot_open()` 的默认超时为 2000 ms，可在打开时通过 `pilot_options.timeout_ms` 设置 1～60000 ms。

一个 `pilot_client` 只由一个线程使用；多线程分别建立自己的 client。库使用两条私有本地 D-Bus 连接分别处理 RPC 与信号，阻塞方法调用不会消费信号连接上的消息。没有常驻后台线程。`pilot_poll()` 必须定期调用，不应长期停止消费广播；回调应短小，不做耗时运算。

方法结果用 `{0}` 初始化，字符串/数组/字典归结果内部的 `_reply` 所有，调用相应 `_clear()` 释放。**重复使用结果前先 clear，禁止按值复制后分别释放，禁止直接修改或释放字段指针。** 失败时输出被置空。信号数据仅在回调期间有效，要跨回调使用必须自行复制所需字段。

回调中不允许 `pilot_close()`、`pilot_reconnect()`、递归 `pilot_poll()`，可以安排下一轮执行。`pilot_reconnect()` 重建两条连接和所有有效订阅，不重试之前的写操作。

## 订阅广播

```c
static void on_imu(unsigned signal_id, const pilot_reply *reply, void *context) {
    (void)signal_id;
    (void)context;
    bool valid = pilot_reply_at(reply, 1)->as.boolean;
    double gyro_x = pilot_reply_at(reply, 2)->as.real;
    /* 使用 valid 和 gyro_x；SDK 已检查完整信号签名。 */
}

pilot_subscribe(client, PILOT_SIGNAL_RTC_IMU_DATA_CHANGED, on_imu, NULL, &error);
/* 应用事件循环中调用，最长等待 100 ms。返回负数时处理错误。 */
int callbacks = pilot_poll(client, 100, &error);
pilot_unsubscribe(client, PILOT_SIGNAL_RTC_IMU_DATA_CHANGED, &error);
```

当前 IMU 信号约 20 Hz，与芯片实际采样率不是同一概念。库不会启动轮询线程，也不会提升芯片采样率。`poll` 每次最多处理 64 条总线消息。超长消息、过深容器和异常签名被拒绝；信号需要匹配服务所有者、对象路径、接口和成员。服务重启后的名称所有者变化会更新，不能将其他进程伪造的同名广播当作传感器数据。

## 数组、字典、图片

`pilot_value.type` 使用 D-Bus 类型字符：`b` 布尔、`i/u` 32 位整数、`x/t` 64 位整数、`d` double、`s` 字符串、`a` 数组、`r` 结构体、`e` 字典条目、`v` variant。64 位值不会经过 JSON/double，避免精度损失。

- `pilot_value_at(value, index)` 访问数组/结构体成员。
- `pilot_dict_get(dict, "key")` 查找 `a{sv}` 字段并自动解开 variant；不存在返回 NULL。还要检查字段的 `type`。
- `ay` 字节数组使用 `value->bytes` 和 `value->count`，不使用 `items`。图片像素不再复制一份。
- `GetAllStatus` 的结构体顺序是 type、connected、ip、port、subscribeCount、isActive。
- `GetChannelOutputStatus` 的结构体顺序是 channel、mode、value、enabled、digitalSupported；最后一项表示是否支持数字输出，不是远端接管状态。
- `GetTransportStats` 的结构体顺序是 type、connected、active、subscribers、rtt_us、bytes_sent、bytes_lost、max_datagram_length。
- 字典保留原始字段名称和类型，新增字典键不影响旧客户端。

```c
pilot_rtc_capture_camera_preview_result frame = {0};
int rc = pilot_rtc_capture_camera_preview(client, &frame, &error);
if (rc == PILOT_OK && frame.success) {
    const uint8_t *pixels = frame.rgb565->bytes;
    size_t size = frame.rgb565->count;
    /* 先检查 stride_bytes、height 和 size 一致，再按 RGB565 处理。 */
    (void)pixels;
    (void)size;
}
pilot_rtc_capture_camera_preview_clear(&frame);
```

预览只是一次获取缩略图，不是完整视频流 SDK，也不适合不停高频调用替代现有媒体链路。

## 写接口与业务状态

### 音量

`pilot_rtc_set_audio_volume(client, "input", 60, &result, &error)` 设置麦克风，`"output"` 设置扬声器，范围 0～100。检查 `result.accepted`、`actual_volume` 和 `reason`。是否持久化由现有板端实现负责，SDK 不再写配置文件。

### 电池与 ADC

`pilot_rtc_battery_command()` 完整透传已存在的 JSON 协议，最大请求由服务限制为 1024 字节。举例：

```json
{"version":1,"op":"get_profile"}
{"version":1,"op":"get_adc"}
{"version":1,"op":"set_profile","revision":"上次读取的revision","cells":2,"chemistry":0}
{"version":1,"op":"set_adc","revision":"上次读取的revision","channel":1,"raw":438,"voltage":8.09}
{"version":1,"op":"reset_adc","revision":"上次读取的revision"}
```

`revision` 是十进制字符串，不能写成 JSON 数字。`cells` 为 1～6、`chemistry` 为固件枚举 0/1，不允许凭名称自行猜测枚举。校准必须使用真实万用表读数和对应采样，以上数值仅说明格式。结果 JSON 中的 `status/error` 需要由应用解析，不代表 D-Bus 返回成功就已保存。

### 风扇

`pilot_fan_get_config()` 返回当前 JSON 配置及温度、实际输出、保护状态；修改后用 `pilot_fan_set_config()` 提交。保留读取的 `revision`，冲突时重新读取，不以旧配置覆盖其他端修改。温度保护由风扇服务强制执行，SDK 不绕过保护。风扇当前没有 D-Bus 信号，需要应用自行低频查询，建议仅在界面打开时查询。

### 通道调试

`SetChannelOutput` 和 `BeginChannelDebug` / `SetChannelDebug` / `ChannelDebugKeepAlive` / `EndChannelDebug` 全部有包装函数，但只读示例不会调用它们。开始和每次写入都需要检查 `applied` 或 `accepted/reason`，远程控制端存在时服务可能拒绝本地接管。不要把没有数据、超时或断线解释为维持危险输出的许可；退出时显式结束调试，同时依赖板端现有超时保护。

当前 `SetChannelDebug` 接受 channel=1～8、mode=0（PWM）/1（数字）、value=0～200、enabled 布尔；CH1 数字模式被菜单键保留。不要把普通遥控的 -1000～1000 数值直接传进这个调试接口。

### 自检与网络

`pilot_selfcheck_set_network_recovery_mode(..., "ignore", ...)` 或 `"retry"` 修改本次启动的网络恢复策略；这会影响自动恢复行为，不能由后台示例擅自调用。板端要求 root。`RequestReconnect` 则是主通信服务的连接恢复请求，二者不是同一个能力。

## 不属于本版 SDK 的能力

文件上传/安装、进程生命周期、资源限制、任意寄存器读写、远程总线代理均不在此版。设备已有能力但没有导出 D-Bus 的，也不会在 SDK 中绕过服务直接控制硬件。向用户应用开放这些能力应另设计权限和资源管理，不应扩展为无限制 root RPC。
