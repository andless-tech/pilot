#!/usr/bin/env python3
"""Generate public business APIs and private transport bindings separately."""
import argparse
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
TYPES = {"b": ("bool", "boolean"), "s": ("const char *", "string"),
         "i": ("int32_t", "i32"), "u": ("uint32_t", "u32"), "q": ("uint16_t", "u16"),
         "x": ("int64_t", "i64"), "t": ("uint64_t", "u64"), "d": ("double", "real")}
METHODS = {
    "BatteryCommand": "battery_request", "GetActiveEndpoint": "connection_get_active",
    "GetCloudStatus": "connection_get_relay", "GetP2PStatus": "connection_get_p2p",
    "GetAllStatus": "connection_get_status", "GetControlSignals": "control_read",
    "SetChannelOutput": "control_set", "GetImuData": "imu_read", "GetDeviceParam": "device_read",
    "GetMagnetometerData": "magnetometer_read", "GetCellLocation": "location_read_cell",
    "GetGpsStatus": "gps_read", "GetChannelOutputStatus": "control_get_outputs",
    "BeginChannelDebug": "control_debug_begin", "SetChannelDebug": "control_debug_set",
    "ChannelDebugKeepAlive": "control_debug_keepalive", "EndChannelDebug": "control_debug_end",
    "GetMediaStatus": "media_read", "GetAudioSpectrum": "audio_read_spectrum",
    "GetAudioVolume": "audio_get_volume", "SetAudioVolume": "audio_set_volume",
    "CaptureCameraPreview": "camera_capture_preview", "GetTransportStats": "connection_get_stats",
    "GetDiagnosticStats": "diagnostics_read", "GetNetworkDiagnostics": "network_read",
    "RequestReconnect": "connection_request_reconnect", "GetReport": "selfcheck_read",
    "RunNow": "selfcheck_run", "GetNetworkRecoveryPolicy": "network_get_recovery_policy",
    "SetNetworkRecoveryMode": "network_set_recovery_mode", "GetConfig": "fan_get_config",
    "SetConfig": "fan_set_config",
}
SIGNALS = {
    "ControlSignalsChanged": "control", "ImuDataChanged": "imu",
    "MagnetometerDataChanged": "magnetometer", "EndpointStateChanged": "connection",
    "NetworkDiagnosticsChanged": "network", "ReportChanged": "selfcheck", "CellInfoChanged": "cellular",
}
ARRAYS = {
    "ai": ("int32_t", []),
    "a(sbsqib)": ("pilot_endpoint", ["type", "connected", "ip", "port", "subscribe_count", "is_active"]),
    "a(uuibb)": ("pilot_channel_output", ["channel", "mode", "value", "enabled", "digital_supported"]),
    "a(sbbitttu)": ("pilot_transport_stats", ["type", "connected", "active", "subscribers", "rtt_us", "bytes_sent", "bytes_lost", "max_datagram_length"]),
}


def snake(name):
    name = name.replace("P2P", "P2p")
    name = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", name)
    return re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name).lower()


def ctype(sig):
    if sig in TYPES:
        return TYPES[sig][0]
    if sig in ARRAYS:
        return "const " + ARRAYS[sig][0] + " *"
    return {"ay": "pilot_bytes", "a{sv}": "const pilot_properties *"}[sig]


def emit_result(header, source, name, result, outputs, export_clear):
    header.append('typedef struct {')
    for a in outputs:
        field, sig = snake(a['name']), a['type']
        header.append(f'    {ctype(sig)} {field};')
        if sig in ARRAYS:
            header.append(f'    size_t {field}_count;')
    header += ['    void *_private; /* Library-owned; never modify or copy ownership. */', f'}} {result};']
    clear = name + '_clear'
    if export_clear:
        header.append(f'PILOT_API void {clear}({result} *out);')
    source += [f'{"" if export_clear else "static "}void {clear}({result} *out)', '{', '    if (!out) return;']
    for a in outputs:
        if a['type'] in ARRAYS:
            source.append(f'    free((void *)out->{snake(a["name"])});')
    source += ['    pilot_reply_free(out->_private);', '    memset(out, 0, sizeof(*out));', '}',
               f'static int {name}_decode(const pilot_reply *reply, bool own, {result} *out, pilot_error *error)', '{',
               '    (void)error;', '    memset(out, 0, sizeof(*out));', '    out->_private = own ? (void *)reply : NULL;']
    has_array = False
    for i, a in enumerate(outputs):
        field, sig = snake(a['name']), a['type']
        value = f'pilot_reply_at(reply, {i})'
        if sig in TYPES:
            source.append(f'    out->{field} = {value}->as.{TYPES[sig][1]};')
        elif sig == 'a{sv}':
            source.append(f'    out->{field} = (const pilot_properties *){value};')
        elif sig == 'ay':
            source.append(f'    out->{field} = (pilot_bytes){{{value}->bytes, {value}->count}};')
        else:
            has_array = True
            typ, fields = ARRAYS[sig]
            source += ['    {', f'        const pilot_value *array = {value};',
                       f'        {typ} *items = array->count ? calloc(array->count, sizeof(*items)) : NULL;',
                       '        if (array->count && !items) goto no_memory;', f'        out->{field} = items;',
                       f'        out->{field}_count = array->count;', '        for (size_t j = 0; j < array->count; ++j) {',
                       '            const pilot_value *item = pilot_value_at(array, j);']
            if not fields:
                source.append('            items[j] = item->as.i32;')
            else:
                for k, (key, t) in enumerate(zip(fields, sig[2:-1])):
                    source.append(f'            items[j].{key} = pilot_value_at(item, {k})->as.{TYPES[t][1]};')
            source += ['        }', '    }']
    source.append('    return PILOT_OK;')
    if has_array:
        source += ['no_memory:', f'    {clear}(out);', '    return api_error(error, PILOT_NO_MEMORY);']
    source.append('}')


def render():
    services = json.loads((ROOT / 'protocol/interfaces.json').read_text())['services']
    methods = [(s, m) for s in services for m in s['methods']]
    signals = [(s, m) for s in services for m in s['signals']]
    assert set(METHODS) == {m['name'] for _, m in methods}, 'Add a business API for each new method'
    assert set(SIGNALS) == {s['name'] for _, s in signals}, 'Add a typed callback for each new event'
    banner = '/* Generated by scripts/generate.py. Do not edit. */'
    header = [banner, '#ifndef PILOT_API_H', '#define PILOT_API_H', '#include "pilot.h"',
              '#ifdef __cplusplus', 'extern "C" {', '#endif']
    private = [banner, '#ifndef PILOT_PROTOCOL_H', '#define PILOT_PROTOCOL_H', '#include "transport.h"']
    source = [banner, '#include "pilot/api.h"', '#include "private/protocol.h"', '#include <stdlib.h>', '#include <string.h>',
              'static int api_error(pilot_error *error, int code)', '{',
              '    if (error) { memset(error, 0, sizeof(*error)); error->code = code;',
              '        strncpy(error->message, pilot_status_string(code), sizeof(error->message) - 1); }', '    return code;', '}']
    private += ['typedef enum {'] + [f'    PILOT_SERVICE_{s["key"].upper()},' for s in services] + ['} pilot_service_id;']
    private += ['typedef enum {'] + [f'    PILOT_METHOD_{s["key"].upper()}_{snake(m["name"]).upper()},' for s, m in methods] + ['} pilot_method_id;']
    private += ['typedef enum {'] + [f'    PILOT_SIGNAL_{s["key"].upper()}_{snake(m["name"]).upper()},' for s, m in signals] + ['} pilot_signal_id;']
    source += ['const pilot_service pilot_services[] = {'] + [
        f'    {{"{s["name"]}", "{s["path"]}", "{s["interface"]}"}},' for s in services] + ['};',
        'const size_t pilot_service_count = sizeof(pilot_services) / sizeof(pilot_services[0]);',
        'const pilot_method pilot_methods[] = {']
    for s, m in methods:
        sig = lambda d: ''.join(a['type'] for a in m['args'] if a['direction'] == d)
        source.append(f'    {{PILOT_SERVICE_{s["key"].upper()}, "{m["name"]}", "{sig("in")}", "{sig("out")}"}},')
    source += ['};', 'const size_t pilot_method_count = sizeof(pilot_methods) / sizeof(pilot_methods[0]);', 'const pilot_signal pilot_signals[] = {']
    for s, m in signals:
        sig = ''.join(a['type'] for a in m['args'])
        source.append(f'    {{PILOT_SERVICE_{s["key"].upper()}, "{m["name"]}", "{sig}"}},')
    source += ['};', 'const size_t pilot_signal_count = sizeof(pilot_signals) / sizeof(pilot_signals[0]);']
    docs = ['# Pilot 业务 API', '', '只需包含 `<pilot/api.h>` 并链接 Pilot 库，不需要任何底层服务名或消息编号。',
            '', '所有结果先用 `{0}` 初始化，使用后调用对应 `_clear()`；重复使用结果前先清理。',
            '数组有对应 `_count` 字段；图片/频谱字节使用 `.data` 和 `.size`；属性使用 `pilot_properties_*` 读取。', '']
    exports = re.findall(r'PILOT_API\s+[^;]+?\b(pilot_\w+)\(', (ROOT / 'include/pilot/pilot.h').read_text())
    tests = [banner, 'static void exercise_public_calls(pilot_client *client)', '{', '    pilot_error error;']
    for s, m in methods:
        name = 'pilot_' + METHODS[m['name']]
        result = name + '_result'
        inputs = [a for a in m['args'] if a['direction'] == 'in']
        outputs = [a for a in m['args'] if a['direction'] == 'out']
        emit_result(header, source, name, result, outputs, True)
        params = ['pilot_client *client'] + [f'{ctype(a["type"])} {snake(a["name"])}' for a in inputs] + [f'{result} *out', 'pilot_error *error']
        proto = f'int {name}({", ".join(params)})'
        header += ['PILOT_API ' + proto + ';', '']
        source += [proto, '{', '    if (!out) return api_error(error, PILOT_INVALID_ARGUMENT);',
                   '    memset(out, 0, sizeof(*out));', '    pilot_reply *reply = NULL;']
        if inputs:
            source.append(f'    pilot_value args[{len(inputs)}] = {{0}};')
            for i, a in enumerate(inputs):
                source += [f"    args[{i}].type = '{a['type']}';", f'    args[{i}].as.{TYPES[a["type"]][1]} = {snake(a["name"])};']
        source += [f'    int rc = pilot_call(client, PILOT_METHOD_{s["key"].upper()}_{snake(m["name"]).upper()},',
                   f'                        {"args" if inputs else "NULL"}, {len(inputs)}, &reply, error);',
                   '    if (rc != PILOT_OK) return rc;', f'    return {name}_decode(reply, true, out, error);', '}']
        exports += [name, name + '_clear']
        args = [({'s': '"sample"', 'b': 'true', 'u': '42', 'i': '-23'})[a['type']] for a in inputs]
        tests += ['    {', f'        {result} out = {{0}};',
                  f'        assert({name}({", ".join(["client"] + args + ["&out", "&error"])}) == PILOT_OK);']
        for a in outputs:
            field, sig = snake(a['name']), a['type']
            if sig == 's': tests.append(f'        assert(!strcmp(out.{field}, "sample"));')
            elif sig in TYPES:
                expected = {'b': 'true', 'i': '-23', 'u': '42', 'q': '1234', 'x': '-9007199254740999LL', 't': 'large_number', 'd': '1.25'}[sig]
                tests.append(f'        assert(out.{field} == {expected});')
            elif sig == 'ay': tests.append(f'        assert(out.{field}.size == 2 && out.{field}.data[0] == 231);')
            elif sig in ARRAYS:
                tests.append(f'        assert(out.{field}_count == 2 && out.{field});')
                typ, fields = ARRAYS[sig]
                if not fields: tests.append(f'        assert(out.{field}[0] == -23);')
                else:
                    for key, t in zip(fields, sig[2:-1]):
                        expression = f'out.{field}[0].{key}'
                        if t == 's': tests.append(f'        assert(!strcmp({expression}, "sample"));')
                        else:
                            expected = {'b': 'true', 'i': '-23', 'u': '42', 'q': '1234', 't': 'large_number'}[t]
                            tests.append(f'        assert({expression} == {expected});')
        tests += [f'        {name}_clear(&out);', f'        {name}_clear(&out);', '    }']
        fmt = lambda args: ', '.join(f'`{snake(a["name"])}`: `{ctype(a["type"])}`' for a in args) or '无'
        docs += [f'## {name}', f'- 输入：{fmt(inputs)}', f'- 返回结构：`{result}`', f'- 输出：{fmt(outputs)}', '']
    tests += ['}', 'static unsigned public_event_counts[7];']
    for event_index, (s, m) in enumerate(signals):
        key = SIGNALS[m['name']]
        name = 'pilot_watch_' + key
        event = 'pilot_' + key + '_event'
        cb = 'pilot_' + key + '_callback'
        emit_result(header, source, name, event, m['args'], False)
        header += [f'typedef void (*{cb})(const {event} *event, void *userdata);',
                   f'PILOT_API int {name}(pilot_client *client, {cb} callback, void *userdata, pilot_error *error);', '']
        source += [f'typedef struct {{ {cb} callback; void *userdata; }} {name}_context;',
                   f'static int {name}_dispatch(unsigned id, const pilot_reply *reply, void *context, pilot_error *error)', '{',
                   '    (void)id;', f'    {name}_context *ctx = context;', f'    {event} event;',
                   f'    int rc = {name}_decode(reply, false, &event, error);', '    if (rc != PILOT_OK) return rc;',
                   '    ctx->callback(&event, ctx->userdata);', f'    {name}_clear(&event);', '    return PILOT_OK;', '}',
                   f'int {name}(pilot_client *client, {cb} callback, void *userdata, pilot_error *error)', '{',
                   f'    unsigned id = PILOT_SIGNAL_{s["key"].upper()}_{snake(m["name"]).upper()};',
                   '    if (!callback) return pilot_watch_install(client, id, NULL, NULL, error);',
                   f'    {name}_context *ctx = malloc(sizeof(*ctx));', '    if (!ctx) return api_error(error, PILOT_NO_MEMORY);',
                   '    ctx->callback = callback; ctx->userdata = userdata;',
                   f'    return pilot_watch_install(client, id, {name}_dispatch, ctx, error);', '}']
        exports.append(name)
        tests += [f'static void public_on_{key}(const {event} *event, void *userdata)', '{',
                  '    assert(userdata == public_event_counts);', f'    public_event_counts[{event_index}]++;']
        for a in m['args']:
            field, sig = snake(a['name']), a['type']
            if sig == 's': tests.append(f'    assert(!strcmp(event->{field}, "sample"));')
            elif sig == 'ai': tests.append(f'    assert(event->{field}_count == 2 && event->{field}[0] == -23);')
            else:
                expected = {'b': 'true', 'i': '-23', 'x': '-9007199254740999LL', 't': 'large_number', 'd': '1.25'}[sig]
                tests.append(f'    assert(event->{field} == {expected});')
        tests += ['}']
        docs += [f'## {name}', f'回调接收 `const {event} *`，字段直接访问，无需解析消息。',
                 '传入 NULL 回调取消订阅；事件数据仅在回调期间有效。', '']
    header += ['#ifdef __cplusplus', '}', '#endif', '#endif']
    private.append('#endif')
    tests += ['static void exercise_public_watches(pilot_client *client, bool enable)', '{', '    pilot_error error;']
    for key in SIGNALS.values():
        tests.append(f'    assert(pilot_watch_{key}(client, enable ? public_on_{key} : NULL, public_event_counts, &error) == PILOT_OK);')
    tests += ['}']
    export_map = ['PILOT_0.2 {', '  global:'] + [f'    {name};' for name in sorted(exports)] + ['  local: *;', '};']
    return {ROOT / path: '\n'.join(lines).rstrip() + '\n' for path, lines in (
        ('include/pilot/api.h', header), ('src/api.c', source), ('src/private/protocol.h', private),
        ('src/pilot.map', export_map), ('docs/API.md', docs), ('tests/public_calls.h', tests))}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    for path, content in render().items():
        if args.check:
            if not path.exists() or path.read_text() != content:
                raise SystemExit(f'Generated file out of date: {path}')
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding='utf-8')
