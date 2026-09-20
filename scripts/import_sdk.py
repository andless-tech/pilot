#!/usr/bin/env python3
"""Import the existing firmware ABI and matching redistributable build inputs."""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
TC = "arm-rockchip830-linux-uclibcgnueabihf"


def c_xml(path, marker, end):
    text = path.read_text()
    chunk = text.split(marker, 1)[1].split(end, 1)[0]
    return "".join(ast.literal_eval(s) for s in re.findall(r'"(?:[^"\\]|\\.)*"', chunk))


def revision(path):
    return subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()


def import_interfaces(sdk):
    app = sdk / "project/app"
    sources = [
        ("rtc", "tech.andless.RealTimeComm", "/tech/andless/RealTimeComm/Status",
         app / "RealTimeComm_datagram/utils/tools/DBusStatusService.cpp", "kIntrospectXml =", ";"),
        ("selfcheck", "tech.andless.SelfCheck", "/tech/andless/SelfCheck/Report",
         app / "selfcheck/src/dbus_pub.c", "s_introspect_xml =", ";"),
    ]
    services = []
    provenance = []
    for key, name, path, source, marker, end in sources:
        node = ET.fromstring(c_xml(source, marker, end))
        interface = next(i for i in node.findall("interface") if i.get("name").startswith("tech."))
        services.append(dict(key=key, name=name, path=path, interface=interface.get("name"),
                             methods=[], signals=[]))
        for kind in ("method", "signal"):
            for member in interface.findall(kind):
                args = [dict(name=a.get("name", "value"), type=a.get("type"),
                             direction=a.get("direction", "out")) for a in member.findall("arg")]
                services[-1][kind + "s"].append(dict(name=member.get("name"), args=args))
        provenance.append(source)
    # These two services do not provide a complete standalone introspection XML.
    # Check the implementation anchors, and keep the manually described ABI explicit.
    fan = app / "fan_controller/src/main.c"
    modem = app / "quectel-cm/cell_info_dbus.c"
    assert "<method name='GetConfig'><arg type='s' direction='out'/></method>" in fan.read_text()
    assert "<method name='SetConfig'><arg type='s' direction='in'/><arg type='s' direction='out'/></method>" in fan.read_text()
    assert '"CellInfoChanged"' in modem.read_text() and "DBUS_TYPE_STRING, &payload" in modem.read_text()
    services.append(dict(key="fan", name="tech.andless.Fan", path="/tech/andless/Fan",
        interface="tech.andless.Fan.Config1", methods=[
            dict(name="GetConfig", args=[dict(name="json", type="s", direction="out")]),
            dict(name="SetConfig", args=[dict(name="request", type="s", direction="in"),
                                        dict(name="json", type="s", direction="out")])], signals=[]))
    services.append(dict(key="modem", name="tech.andless.Modem", path="/tech/andless/Modem",
        interface="tech.andless.Modem.Status1", methods=[], signals=[
            dict(name="CellInfoChanged", args=[dict(name="json", type="s", direction="out")])]))
    provenance += [fan, modem]
    data = dict(schema_version=1, services=services, source={
        "sdk_commit": revision(sdk), "realtime_commit": revision(app / "RealTimeComm_datagram"),
        "note": "Working-tree snapshot, including local changes. Not a released firmware capability guarantee.",
        "files": {str(p.relative_to(sdk)): hashlib.sha256(p.read_bytes()).hexdigest() for p in provenance}})
    target = ROOT / "protocol/interfaces.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(data, indent=2) + "\n")
    print(f"Imported {sum(len(s['methods']) for s in services)} methods and "
          f"{sum(len(s['signals']) for s in services)} signals")


def import_deps(sdk, toolchain):
    source = sdk / "project/app/RealTimeComm_datagram/thirdparty/dbus"
    dest = ROOT / "vendor/dbus"
    shutil.copytree(source / "include", dest / "include", dirs_exist_ok=True)
    (dest / "lib").mkdir(parents=True, exist_ok=True)
    lib = source / "lib/libdbus-1.so.3.19.17"
    for name in (lib.name, "libdbus-1.so", "libdbus-1.so.3"):
        # Regular files intentionally avoid NTFS/WSL symlink corruption.
        shutil.copyfile(lib.resolve(), dest / "lib" / name)
    manifest = {str(p.relative_to(dest)): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(dest.rglob("*")) if p.is_file()}
    (dest / "SHA256.json").write_text(json.dumps(manifest, indent=2) + "\n")
    if toolchain:
        source = sdk / "tools/linux/toolchain" / TC
        target = ROOT / "toolchain" / (TC + ".tar.gz")
        target.parent.mkdir(parents=True, exist_ok=True)
        with tarfile.open(target, "w:gz") as archive:
            archive.add(source, arcname=TC)
        digest = hashlib.sha256(target.read_bytes()).hexdigest()
        target.with_suffix(target.suffix + ".sha256").write_text(f"{digest}  {target.name}\n")
        print("Bundled matching GCC/uClibc toolchain:", target)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("sdk", type=Path)
    parser.add_argument("--toolchain", action="store_true")
    args = parser.parse_args()
    import_interfaces(args.sdk)
    import_deps(args.sdk, args.toolchain)
