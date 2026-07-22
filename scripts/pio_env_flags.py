"""PlatformIO pre-script: load build flags from .env."""

Import("env")  # type: ignore  # PlatformIO injects this

from pathlib import Path


def _load_dotenv(path: Path) -> dict:
    values = {}
    if not path.is_file():
        return values
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip().strip('"').strip("'")
    return values


def _as_bool01(raw, default="1"):
    text = (raw if raw is not None else default).strip().lower()
    if text in ("0", "false", "no", "off"):
        return 0
    if text in ("1", "true", "yes", "on"):
        return 1
    return 1 if default != "0" else 0


def _as_int(raw, default, min_v, max_v):
    try:
        value = int(str(raw if raw is not None else default).strip())
    except (TypeError, ValueError):
        value = default
    if value < min_v:
        return min_v
    if value > max_v:
        return max_v
    return value


project_dir = Path(env["PROJECT_DIR"])  # type: ignore[name-defined]
dotenv = _load_dotenv(project_dir / ".env")

ir_recv_enabled = _as_bool01(dotenv.get("IR_RECV_ENABLED", "1"))
ir_send_repeat = _as_int(dotenv.get("IR_SEND_REPEAT", "1"), default=1, min_v=1, max_v=20)

env.Append(  # type: ignore[name-defined]
    CPPDEFINES=[
        ("IR_RECV_ENABLED", ir_recv_enabled),
        ("IR_SEND_REPEAT", ir_send_repeat),
    ]
)
print(f"[pio_env_flags] IR_RECV_ENABLED={ir_recv_enabled} IR_SEND_REPEAT={ir_send_repeat}")
