"""PlatformIO pre-script: injects .env values as C/C++ build-time macros.

This script is normally executed by PlatformIO as a pre-build hook, where
`Import("env")` is available. Running it directly with Python only prints a
helpful status and exits cleanly.
"""
import os

STRING_KEYS = {
    "WIFI_SSID",
    "WIFI_PASS",
    "MQTT_BROKER",
    "MQTT_TOPIC_INF",
    "MQTT_TOPIC_STATUS",
    "MQTT_TOPIC_CMD",
}


def _parse_env_file(path):
    result = {}
    with open(path, encoding="utf-8") as fh:
        for raw in fh:
            line = raw.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, _, val = line.partition("=")
            key = key.strip()
            val = val.strip().strip("'\"")
            result[key] = val
    return result


def _run_in_platformio():
    Import("env")  # noqa: F821 — SCons global
    env_path = os.path.join(env.subst("$PROJECT_DIR"), ".env")  # noqa: F821

    if not os.path.exists(env_path):
        print("[load_env] WARNING: .env not found — copy .env.example to .env and fill in your values.")
        return 0

    for key, val in _parse_env_file(env_path).items():
        if key in STRING_KEYS or not val.lstrip("-+").isdigit():
            env.Append(CPPDEFINES=[(key, f'\\"{val}\\"')])  # noqa: F821
        else:
            env.Append(CPPDEFINES=[(key, val)])  # noqa: F821

    print("[load_env] Loaded .env into build flags.")
    return 0


def _run_standalone():
    env_path = os.path.join(os.path.dirname(__file__), ".env")
    print("[load_env] This script is intended to run from PlatformIO as a pre-build hook.")
    if not os.path.exists(env_path):
        print("[load_env] WARNING: .env not found — copy .env.example to .env and fill in your values.")
        return 0

    entries = _parse_env_file(env_path)
    print("[load_env] Parsed .env contents:")
    for key, val in entries.items():
        print(f"  {key}={val}")
    return 0


if __name__ == "__main__":
    try:
        Import("env")  # noqa: F821
    except NameError:
        raise SystemExit(_run_standalone())
    raise SystemExit(_run_in_platformio())
else:
    # When imported by PlatformIO, this file is executed in the SCons environment.
    try:
        Import("env")  # noqa: F821
    except NameError:
        pass
    else:
        _run_in_platformio()
