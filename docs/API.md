# D-Bus 接口清单

由 `protocol/interfaces.json` 自动生成。对应工作区快照，不代表所有旧固件均支持。

返回值中的字符串、数组、字典由结果对象持有，使用后调用对应 `_clear()`；重复调用前也要先清理。
信号参数按下面的顺序从 `pilot_reply_at()` 读取，仅在回调期间有效。

## tech.andless.RealTimeComm

- 对象：`/tech/andless/RealTimeComm/Status`
- 接口：`tech.andless.RealTimeComm.Status1`

### BatteryCommand
函数：`pilot_rtc_battery_command()`
- 输入：request: `s`
- 输出：response: `s`

### GetActiveEndpoint
函数：`pilot_rtc_get_active_endpoint()`
- 输入：无
- 输出：index: `i`

### GetCloudStatus
函数：`pilot_rtc_get_cloud_status()`
- 输入：无
- 输出：type: `s`, connected: `b`, ip: `s`, port: `q`, subscribeCount: `i`, isActive: `b`

### GetP2PStatus
函数：`pilot_rtc_get_p2p_status()`
- 输入：无
- 输出：type: `s`, connected: `b`, ip: `s`, port: `q`, subscribeCount: `i`, isActive: `b`

### GetAllStatus
函数：`pilot_rtc_get_all_status()`
- 输入：无
- 输出：endpoints: `a(sbsqib)`, activeIndex: `i`

### GetControlSignals
函数：`pilot_rtc_get_control_signals()`
- 输入：无
- 输出：signals: `ai`, lastRxMs: `x`

### SetChannelOutput
函数：`pilot_rtc_set_channel_output()`
- 输入：channel: `i`, value: `i`
- 输出：applied: `b`

### GetImuData
函数：`pilot_rtc_get_imu_data()`
- 输入：无
- 输出：lastSampleMs: `x`, valid: `b`, gyroX: `d`, gyroY: `d`, gyroZ: `d`, accelX: `d`, accelY: `d`, accelZ: `d`

### GetDeviceParam
函数：`pilot_rtc_get_device_param()`
- 输入：无
- 输出：lastUpdateMs: `x`, hasImu: `b`, gyroX: `d`, gyroY: `d`, gyroZ: `d`, accelX: `d`, accelY: `d`, accelZ: `d`, temperature: `d`, memoryUsage: `d`, cpuUsage: `d`, diskUsage: `d`, batteryPercent: `i`, voltage: `d`, rssiDbm: `i`, hasGps: `b`, latitude: `d`, longitude: `d`, altitude: `d`, gpsSatellites: `i`, hdop: `d`

### GetMagnetometerData
函数：`pilot_rtc_get_magnetometer_data()`
- 输入：无
- 输出：lastUpdateMs: `x`, valid: `b`, xMicrotesla: `d`, yMicrotesla: `d`, zMicrotesla: `d`, magnitudeMicrotesla: `d`, sequence: `t`

### GetCellLocation
函数：`pilot_rtc_get_cell_location()`
- 输入：无
- 输出：state: `s`, valid: `b`, latitude: `d`, longitude: `d`, radius_m: `d`, age_seconds: `u`, detail: `s`

### GetGpsStatus
函数：`pilot_rtc_get_gps_status()`
- 输入：无
- 输出：status: `a{sv}`

### GetChannelOutputStatus
函数：`pilot_rtc_get_channel_output_status()`
- 输入：无
- 输出：channels: `a(uuibb)`, debugActive: `b`, lastRemoteRxMs: `x`

### BeginChannelDebug
函数：`pilot_rtc_begin_channel_debug()`
- 输入：无
- 输出：accepted: `b`, reason: `s`

### SetChannelDebug
函数：`pilot_rtc_set_channel_debug()`
- 输入：channel: `u`, mode: `u`, value: `i`, enabled: `b`
- 输出：accepted: `b`, reason: `s`

### ChannelDebugKeepAlive
函数：`pilot_rtc_channel_debug_keep_alive()`
- 输入：无
- 输出：active: `b`

### EndChannelDebug
函数：`pilot_rtc_end_channel_debug()`
- 输入：无
- 输出：ended: `b`

### GetMediaStatus
函数：`pilot_rtc_get_media_status()`
- 输入：无
- 输出：status: `a{sv}`

### GetAudioSpectrum
函数：`pilot_rtc_get_audio_spectrum()`
- 输入：无
- 输出：available: `b`, sampleRate: `u`, capturedMs: `x`, sequence: `t`, rmsDbfs: `d`, peakDbfs: `d`, levels: `ay`

### GetAudioVolume
函数：`pilot_rtc_get_audio_volume()`
- 输入：无
- 输出：available: `b`, inputVolume: `u`, outputVolume: `u`

### SetAudioVolume
函数：`pilot_rtc_set_audio_volume()`
- 输入：target: `s`, volume: `u`
- 输出：accepted: `b`, actualVolume: `u`, reason: `s`

### CaptureCameraPreview
函数：`pilot_rtc_capture_camera_preview()`
- 输入：无
- 输出：success: `b`, width: `u`, height: `u`, strideBytes: `u`, capturedMs: `x`, rgb565: `ay`, reason: `s`

### GetTransportStats
函数：`pilot_rtc_get_transport_stats()`
- 输入：无
- 输出：endpoints: `a(sbbitttu)`

### GetDiagnosticStats
函数：`pilot_rtc_get_diagnostic_stats()`
- 输入：无
- 输出：json: `s`

### GetNetworkDiagnostics
函数：`pilot_rtc_get_network_diagnostics()`
- 输入：无
- 输出：json: `s`

### RequestReconnect
函数：`pilot_rtc_request_reconnect()`
- 输入：endpoint: `s`
- 输出：accepted: `b`, reason: `s`

### 信号 ControlSignalsChanged
枚举：`PILOT_SIGNAL_RTC_CONTROL_SIGNALS_CHANGED`
signals: `ai`, lastRxMs: `x`

### 信号 ImuDataChanged
枚举：`PILOT_SIGNAL_RTC_IMU_DATA_CHANGED`
lastUpdateMs: `x`, hasImu: `b`, gyroX: `d`, gyroY: `d`, gyroZ: `d`, accelX: `d`, accelY: `d`, accelZ: `d`

### 信号 MagnetometerDataChanged
枚举：`PILOT_SIGNAL_RTC_MAGNETOMETER_DATA_CHANGED`
lastUpdateMs: `x`, valid: `b`, xMicrotesla: `d`, yMicrotesla: `d`, zMicrotesla: `d`, magnitudeMicrotesla: `d`, sequence: `t`

### 信号 EndpointStateChanged
枚举：`PILOT_SIGNAL_RTC_ENDPOINT_STATE_CHANGED`
type: `s`, connected: `b`, subscribeCount: `i`, isActive: `b`

### 信号 NetworkDiagnosticsChanged
枚举：`PILOT_SIGNAL_RTC_NETWORK_DIAGNOSTICS_CHANGED`
json: `s`

## tech.andless.SelfCheck

- 对象：`/tech/andless/SelfCheck/Report`
- 接口：`tech.andless.SelfCheck.Report1`

### GetReport
函数：`pilot_selfcheck_get_report()`
- 输入：无
- 输出：json: `s`

### RunNow
函数：`pilot_selfcheck_run_now()`
- 输入：无
- 输出：accepted: `b`, reason: `s`

### GetNetworkRecoveryPolicy
函数：`pilot_selfcheck_get_network_recovery_policy()`
- 输入：无
- 输出：ignored: `b`

### SetNetworkRecoveryMode
函数：`pilot_selfcheck_set_network_recovery_mode()`
- 输入：mode: `s`
- 输出：ignored: `b`

### 信号 ReportChanged
枚举：`PILOT_SIGNAL_SELFCHECK_REPORT_CHANGED`
json: `s`

## tech.andless.Fan

- 对象：`/tech/andless/Fan`
- 接口：`tech.andless.Fan.Config1`

### GetConfig
函数：`pilot_fan_get_config()`
- 输入：无
- 输出：json: `s`

### SetConfig
函数：`pilot_fan_set_config()`
- 输入：request: `s`
- 输出：json: `s`

## tech.andless.Modem

- 对象：`/tech/andless/Modem`
- 接口：`tech.andless.Modem.Status1`

### 信号 CellInfoChanged
枚举：`PILOT_SIGNAL_MODEM_CELL_INFO_CHANGED`
json: `s`
