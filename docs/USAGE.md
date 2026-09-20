# Pilot 业务接口使用说明

## 基本约定

- 只包含 `<pilot/api.h>`。所有连接和通信细节由库管理。
- `pilot_open()` 创建句柄，`pilot_close()` 释放。一个句柄由一个线程使用；多线程各自创建句柄。
- 函数返回 `PILOT_OK` 或负错误码，`pilot_error.message` 给出可读说明。默认超时 2000 ms，可通过 `pilot_options.timeout_ms` 设置 1～60000 ms。
- 结果结构用 `{0}` 初始化，使用后调用同名 `_clear()`。再次使用前先清理，不按值复制所有权，不修改 `_private`。
- 字符串、属性和字节缓冲区在结果清理后失效；事件中的所有数据仅在回调期间有效。需要长期保留时自行复制。
- 通信成功不等于业务成功，必须检查 `valid`、`available`、`accepted`、`success` 或业务 JSON 状态。
- 写请求超时可能已经执行，不要自动重放；先重新查询状态。

## 类型化事件

```c
static void on_imu(const pilot_imu_event *event, void *context) {
    (void)context;
    if (!event->has_imu) return;
    double gyro_x = event->gyro_x;
    (void)gyro_x;
}

pilot_watch_imu(client, on_imu, NULL, &error);
/* 由应用事件循环调用；回调在当前线程执行。 */
int delivered = pilot_poll(client, 100, &error);
/* 取消订阅。 */
pilot_watch_imu(client, NULL, NULL, &error);
```

还有 `pilot_watch_magnetometer`、`pilot_watch_control`、`pilot_watch_connection`、`pilot_watch_network`、`pilot_watch_selfcheck`、`pilot_watch_cellular`。每类回调接收自己的业务结构，不接收原始消息或消息编号。

回调应短小，禁止在回调内 close/reconnect 或递归 poll；可修改/取消订阅。断线后显式调用 `pilot_reconnect()`，已有回调会重新注册，不会自动重放写操作。IMU 事件仍使用现有低频遥测，不是精密时间同步或新增高频采样能力。

## 数组、属性和图像

连接列表：`pilot_connection_get_status_result.endpoints[i]` 和 `endpoints_count`。

通道读数：`pilot_control_read_result.signals[i]` 和 `signals_count`；通道输出状态：`pilot_control_get_outputs_result.channels[i]`，字段包含 `channel/mode/value/enabled/digital_supported`。

链路统计：`pilot_connection_get_stats_result.endpoints[i]`，包含连接状态、RTT 微秒、发送/丢失字节数、最大报文长度。

GPS 和媒体状态提供可扩展的只读属性，不暴露底层编码类型：

```c
pilot_gps_read_result gps = {0};
if (pilot_gps_read(client, &gps, &error) == PILOT_OK) {
    bool has_fix;
    double latitude;
    bool known = pilot_properties_bool(gps.status, "has_fix", &has_fix);
    if (known && has_fix && pilot_properties_double(gps.status, "latitude", &latitude)) {
        /* 使用已验证的纬度。 */
    }
}
pilot_gps_read_clear(&gps);
```

属性函数还有 `pilot_properties_int/uint/string`、`pilot_properties_count/key`。不存在或类型不匹配时返回 false，输出不改；不自动将整数转 double，以免丢失精度。

相机预览使用 `pilot_camera_capture_preview()`，成功后通过 `result.rgb565.data/size`、`width/height/stride_bytes` 读取。频谱使用 `pilot_audio_read_spectrum()`，频段数据在 `result.levels.data/size`。图片采用只读引用，不额外复制像素；预览不是完整视频流接口，不建议不停高频调用。

## 音量、风扇与电池

`pilot_audio_set_volume(client, "input", 60, &result, &error)` 设置麦克风；`"output"` 设置扬声器，范围 0～100。检查 `accepted/actual_volume/reason`，由设备现有逻辑即时生效和持久化。

`pilot_fan_get_config()` 获取配置和状态，`pilot_fan_set_config()` 提交业务 JSON。保留读取的 `revision`，冲突时重新读取；温度保护仍由设备强制执行，SDK 不绕过。风扇没有事件通知，应用可在相关界面打开时低频查询。

`pilot_battery_request()` 接收现有业务 JSON，例子：

```json
{"version":1,"op":"get_profile"}
{"version":1,"op":"get_adc"}
{"version":1,"op":"set_profile","revision":"上次读取的revision","cells":2,"chemistry":0}
{"version":1,"op":"set_adc","revision":"上次读取的revision","channel":1,"raw":438,"voltage":8.09}
{"version":1,"op":"reset_adc","revision":"上次读取的revision"}
```

这些是业务配置字段，不是通信方法/主题。`revision` 是十进制字符串，不能用 JSON 数字。`cells` 为 1～6，`chemistry` 是固件定义的 0/1 枚举。校准必须使用真实万用表读数与对应采样，示例数值不能直接用于其他设备。检查 JSON 中的 `status/error`。

## 控制与网络安全

通道调试调用 `pilot_control_debug_begin/set/keepalive/end`，每次都检查业务回执。当前 set 的 channel=1～8、mode=0（PWM）/1（数字）、value=0～200、enabled 为布尔；CH1 数字模式由菜单键保留，不要传入普通遥控的 -1000～1000 数值。远程控制端活动时，本地接管可能被拒绝。退出需显式结束调试，并保留固件超时保护。

`pilot_network_set_recovery_mode()` 的 `"ignore"` / `"retry"` 影响本次启动的网络恢复策略，当前板端需要 root；不要由后台示例随意调用。`pilot_connection_request_reconnect()` 是连接恢复请求，不等同于修改网络恢复策略。

只读示例不会调用这些写接口。本版没有资源隔离、权限代理或安装服务。不要为了调用受限接口就让任意第三方程序长期以 root 运行。
