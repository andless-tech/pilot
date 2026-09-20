# Pilot 业务 API

只需包含 `<pilot/api.h>` 并链接 Pilot 库，不需要任何底层服务名或消息编号。

所有结果先用 `{0}` 初始化，使用后调用对应 `_clear()`；重复使用结果前先清理。
数组有对应 `_count` 字段；图片/频谱字节使用 `.data` 和 `.size`；属性使用 `pilot_properties_*` 读取。

## pilot_battery_request
- 输入：`request`: `const char *`
- 返回结构：`pilot_battery_request_result`
- 输出：`response`: `const char *`

## pilot_connection_get_active
- 输入：无
- 返回结构：`pilot_connection_get_active_result`
- 输出：`index`: `int32_t`

## pilot_connection_get_relay
- 输入：无
- 返回结构：`pilot_connection_get_relay_result`
- 输出：`type`: `const char *`, `connected`: `bool`, `ip`: `const char *`, `port`: `uint16_t`, `subscribe_count`: `int32_t`, `is_active`: `bool`

## pilot_connection_get_p2p
- 输入：无
- 返回结构：`pilot_connection_get_p2p_result`
- 输出：`type`: `const char *`, `connected`: `bool`, `ip`: `const char *`, `port`: `uint16_t`, `subscribe_count`: `int32_t`, `is_active`: `bool`

## pilot_connection_get_status
- 输入：无
- 返回结构：`pilot_connection_get_status_result`
- 输出：`endpoints`: `const pilot_endpoint *`, `active_index`: `int32_t`

## pilot_control_read
- 输入：无
- 返回结构：`pilot_control_read_result`
- 输出：`signals`: `const int32_t *`, `last_rx_ms`: `int64_t`

## pilot_control_set
- 输入：`channel`: `int32_t`, `value`: `int32_t`
- 返回结构：`pilot_control_set_result`
- 输出：`applied`: `bool`

## pilot_imu_read
- 输入：无
- 返回结构：`pilot_imu_read_result`
- 输出：`last_sample_ms`: `int64_t`, `valid`: `bool`, `gyro_x`: `double`, `gyro_y`: `double`, `gyro_z`: `double`, `accel_x`: `double`, `accel_y`: `double`, `accel_z`: `double`

## pilot_device_read
- 输入：无
- 返回结构：`pilot_device_read_result`
- 输出：`last_update_ms`: `int64_t`, `has_imu`: `bool`, `gyro_x`: `double`, `gyro_y`: `double`, `gyro_z`: `double`, `accel_x`: `double`, `accel_y`: `double`, `accel_z`: `double`, `temperature`: `double`, `memory_usage`: `double`, `cpu_usage`: `double`, `disk_usage`: `double`, `battery_percent`: `int32_t`, `voltage`: `double`, `rssi_dbm`: `int32_t`, `has_gps`: `bool`, `latitude`: `double`, `longitude`: `double`, `altitude`: `double`, `gps_satellites`: `int32_t`, `hdop`: `double`

## pilot_magnetometer_read
- 输入：无
- 返回结构：`pilot_magnetometer_read_result`
- 输出：`last_update_ms`: `int64_t`, `valid`: `bool`, `x_microtesla`: `double`, `y_microtesla`: `double`, `z_microtesla`: `double`, `magnitude_microtesla`: `double`, `sequence`: `uint64_t`

## pilot_location_read_cell
- 输入：无
- 返回结构：`pilot_location_read_cell_result`
- 输出：`state`: `const char *`, `valid`: `bool`, `latitude`: `double`, `longitude`: `double`, `radius_m`: `double`, `age_seconds`: `uint32_t`, `detail`: `const char *`

## pilot_gps_read
- 输入：无
- 返回结构：`pilot_gps_read_result`
- 输出：`status`: `const pilot_properties *`

## pilot_control_get_outputs
- 输入：无
- 返回结构：`pilot_control_get_outputs_result`
- 输出：`channels`: `const pilot_channel_output *`, `debug_active`: `bool`, `last_remote_rx_ms`: `int64_t`

## pilot_control_debug_begin
- 输入：无
- 返回结构：`pilot_control_debug_begin_result`
- 输出：`accepted`: `bool`, `reason`: `const char *`

## pilot_control_debug_set
- 输入：`channel`: `uint32_t`, `mode`: `uint32_t`, `value`: `int32_t`, `enabled`: `bool`
- 返回结构：`pilot_control_debug_set_result`
- 输出：`accepted`: `bool`, `reason`: `const char *`

## pilot_control_debug_keepalive
- 输入：无
- 返回结构：`pilot_control_debug_keepalive_result`
- 输出：`active`: `bool`

## pilot_control_debug_end
- 输入：无
- 返回结构：`pilot_control_debug_end_result`
- 输出：`ended`: `bool`

## pilot_media_read
- 输入：无
- 返回结构：`pilot_media_read_result`
- 输出：`status`: `const pilot_properties *`

## pilot_audio_read_spectrum
- 输入：无
- 返回结构：`pilot_audio_read_spectrum_result`
- 输出：`available`: `bool`, `sample_rate`: `uint32_t`, `captured_ms`: `int64_t`, `sequence`: `uint64_t`, `rms_dbfs`: `double`, `peak_dbfs`: `double`, `levels`: `pilot_bytes`

## pilot_audio_get_volume
- 输入：无
- 返回结构：`pilot_audio_get_volume_result`
- 输出：`available`: `bool`, `input_volume`: `uint32_t`, `output_volume`: `uint32_t`

## pilot_audio_set_volume
- 输入：`target`: `const char *`, `volume`: `uint32_t`
- 返回结构：`pilot_audio_set_volume_result`
- 输出：`accepted`: `bool`, `actual_volume`: `uint32_t`, `reason`: `const char *`

## pilot_camera_capture_preview
- 输入：无
- 返回结构：`pilot_camera_capture_preview_result`
- 输出：`success`: `bool`, `width`: `uint32_t`, `height`: `uint32_t`, `stride_bytes`: `uint32_t`, `captured_ms`: `int64_t`, `rgb565`: `pilot_bytes`, `reason`: `const char *`

## pilot_connection_get_stats
- 输入：无
- 返回结构：`pilot_connection_get_stats_result`
- 输出：`endpoints`: `const pilot_transport_stats *`

## pilot_diagnostics_read
- 输入：无
- 返回结构：`pilot_diagnostics_read_result`
- 输出：`json`: `const char *`

## pilot_network_read
- 输入：无
- 返回结构：`pilot_network_read_result`
- 输出：`json`: `const char *`

## pilot_connection_request_reconnect
- 输入：`endpoint`: `const char *`
- 返回结构：`pilot_connection_request_reconnect_result`
- 输出：`accepted`: `bool`, `reason`: `const char *`

## pilot_selfcheck_read
- 输入：无
- 返回结构：`pilot_selfcheck_read_result`
- 输出：`json`: `const char *`

## pilot_selfcheck_run
- 输入：无
- 返回结构：`pilot_selfcheck_run_result`
- 输出：`accepted`: `bool`, `reason`: `const char *`

## pilot_network_get_recovery_policy
- 输入：无
- 返回结构：`pilot_network_get_recovery_policy_result`
- 输出：`ignored`: `bool`

## pilot_network_set_recovery_mode
- 输入：`mode`: `const char *`
- 返回结构：`pilot_network_set_recovery_mode_result`
- 输出：`ignored`: `bool`

## pilot_fan_get_config
- 输入：无
- 返回结构：`pilot_fan_get_config_result`
- 输出：`json`: `const char *`

## pilot_fan_set_config
- 输入：`request`: `const char *`
- 返回结构：`pilot_fan_set_config_result`
- 输出：`json`: `const char *`

## pilot_watch_control
回调接收 `const pilot_control_event *`，字段直接访问，无需解析消息。
传入 NULL 回调取消订阅；事件数据仅在回调期间有效。

## pilot_watch_imu
回调接收 `const pilot_imu_event *`，字段直接访问，无需解析消息。
传入 NULL 回调取消订阅；事件数据仅在回调期间有效。

## pilot_watch_magnetometer
回调接收 `const pilot_magnetometer_event *`，字段直接访问，无需解析消息。
传入 NULL 回调取消订阅；事件数据仅在回调期间有效。

## pilot_watch_connection
回调接收 `const pilot_connection_event *`，字段直接访问，无需解析消息。
传入 NULL 回调取消订阅；事件数据仅在回调期间有效。

## pilot_watch_network
回调接收 `const pilot_network_event *`，字段直接访问，无需解析消息。
传入 NULL 回调取消订阅；事件数据仅在回调期间有效。

## pilot_watch_selfcheck
回调接收 `const pilot_selfcheck_event *`，字段直接访问，无需解析消息。
传入 NULL 回调取消订阅；事件数据仅在回调期间有效。

## pilot_watch_cellular
回调接收 `const pilot_cellular_event *`，字段直接访问，无需解析消息。
传入 NULL 回调取消订阅；事件数据仅在回调期间有效。
