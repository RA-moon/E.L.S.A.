import os
import re

from SCons.Script import Import

Import("env")

project_dir = env.get("PROJECT_DIR", ".")
secrets_path = os.path.join(project_dir, "include", "ota_secrets.h")

if not os.path.isfile(secrets_path):
    secrets_path = None

content = ""
if secrets_path:
    with open(secrets_path, "r", encoding="utf-8") as f:
        content = f.read()

password = ""
if content:
    match = re.search(r'^\s*#define\s+OTA_PASSWORD\s+"([^"]*)"\s*$', content, re.MULTILINE)
    if match:
        password = match.group(1)

host_match = None
ip_match = None
if content:
    host_match = re.search(r'^\s*#define\s+OTA_HOSTNAME\s+"([^"]*)"\s*$', content, re.MULTILINE)
    ip_match = re.search(r'^\s*#define\s+OTA_IP\s+"([^"]*)"\s*$', content, re.MULTILINE)

ota_host = host_match.group(1) if host_match else ""
ota_ip = ip_match.group(1) if ip_match else ""

if ota_ip:
    env.Replace(UPLOAD_PORT=ota_ip)
elif ota_host:
    host = ota_host.strip()
    # Allow full hostnames like "device.local" or raw IPs in OTA_HOSTNAME.
    is_ip = re.fullmatch(r"\d{1,3}(?:\.\d{1,3}){3}", host) is not None
    if is_ip:
        env.Replace(UPLOAD_PORT=host)
    else:
        host = host.rstrip(".")
        if host.endswith(".local"):
            env.Replace(UPLOAD_PORT=host)
        else:
            env.Replace(UPLOAD_PORT=f"{host}.local")

if password:
    flags = env.get("UPLOAD_FLAGS", [])
    if isinstance(flags, str):
        flags = [flags]
    auth_flag = f"--auth={password}"
    if auth_flag not in flags:
        env.Append(UPLOAD_FLAGS=[auth_flag])
