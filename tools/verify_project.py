#!/usr/bin/env python3
"""Read-only project verifier for the ESP32 EPD firmware/cloud repository."""

from __future__ import annotations

import ast
import csv
import hashlib
import re
import shutil
import subprocess
import sys
import tempfile
from html.parser import HTMLParser
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FIRMWARE_DIR = ROOT / "firmware" / "Loader_esp32wf"
FRONTEND_DIR = ROOT / "cloud_server" / "frontend"
SERVICE_ADMIN_FRONTEND_DIR = ROOT / "cloud_server" / "service_admin_frontend"
BACKEND_DIR = ROOT / "cloud_server" / "backend"


class VerificationError(RuntimeError):
    pass


def run(command: list[str], *, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=ROOT,
        check=check,
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=True,
    )


def verify_python_syntax() -> int:
    files = sorted(BACKEND_DIR.glob("*.py")) + sorted((ROOT / "tools").glob("*.py"))
    for path in files:
        ast.parse(path.read_text(encoding="utf-8-sig"), filename=str(path))
    return len(files)


def verify_javascript_syntax() -> int:
    node = shutil.which("node")
    if not node:
        raise VerificationError("node is required for JavaScript syntax checks")
    files = sorted(FRONTEND_DIR.glob("*.js")) + sorted(SERVICE_ADMIN_FRONTEND_DIR.glob("*.js"))
    for path in files:
        result = run([node, "--check", str(path)], check=False)
        if result.returncode:
            raise VerificationError(f"JavaScript syntax failed: {path.name}\n{result.stderr}")
    return len(files)


def verify_firmware_layout() -> str:
    required_files = {
        "Loader_esp32wf.ino",
        "http_update.h",
        "wifi_config.h",
        "device_identity.h",
        "partitions.csv",
    }
    missing = sorted(name for name in required_files if not (FIRMWARE_DIR / name).is_file())
    if missing:
        raise VerificationError(f"Firmware sketch layout is incomplete: missing={missing}")

    root_firmware_files = sorted(
        path.name
        for path in ROOT.iterdir()
        if path.is_file()
        and (path.suffix in {".ino", ".h", ".c", ".cpp"} or path.name == "partitions.csv")
    )
    if root_firmware_files:
        raise VerificationError(
            "Firmware sources must live under firmware/Loader_esp32wf/: "
            + ", ".join(root_firmware_files)
        )
    return "firmware/Loader_esp32wf"


class LocalAssetParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.references: list[tuple[str, int]] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        attr_map = dict(attrs)
        key = "src" if tag in {"script", "img"} else "href" if tag == "link" else None
        if key and attr_map.get(key):
            self.references.append((attr_map[key] or "", self.getpos()[0]))


class HtmlIntegrityParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.ids: dict[str, int] = {}
        self.duplicate_ids: list[tuple[str, int, int]] = []
        self.inline_scripts: list[tuple[int, str]] = []
        self._script_line: int | None = None
        self._script_parts: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        attr_map = dict(attrs)
        element_id = attr_map.get("id")
        if element_id:
            line = self.getpos()[0]
            if element_id in self.ids:
                self.duplicate_ids.append((element_id, self.ids[element_id], line))
            else:
                self.ids[element_id] = line

        if tag == "script" and not attr_map.get("src"):
            script_type = (attr_map.get("type") or "text/javascript").lower()
            if script_type in {"text/javascript", "application/javascript", "module"}:
                self._script_line = self.getpos()[0]
                self._script_parts = []

    def handle_data(self, data: str) -> None:
        if self._script_line is not None:
            self._script_parts.append(data)

    def handle_endtag(self, tag: str) -> None:
        if tag == "script" and self._script_line is not None:
            self.inline_scripts.append((self._script_line, "".join(self._script_parts)))
            self._script_line = None
            self._script_parts = []


def verify_html_assets() -> tuple[int, int]:
    html_files = sorted(FRONTEND_DIR.glob("*.html"))
    checked = 0
    missing: list[str] = []
    for html_path in html_files:
        parser = LocalAssetParser()
        parser.feed(html_path.read_text(encoding="utf-8-sig"))
        for raw_reference, line in parser.references:
            reference = raw_reference.split("?", 1)[0].split("#", 1)[0]
            if not reference or reference.startswith(("data:", "http://", "https://", "//")):
                continue
            target = (FRONTEND_DIR / reference.lstrip("/")).resolve()
            checked += 1
            if FRONTEND_DIR.resolve() not in target.parents or not target.is_file():
                missing.append(f"{html_path.name}:{line} -> {raw_reference}")
    if missing:
        raise VerificationError("Missing frontend assets:\n" + "\n".join(missing))
    return len(html_files), checked


def verify_html_integrity() -> tuple[int, int]:
    node = shutil.which("node")
    if not node:
        raise VerificationError("node is required for inline JavaScript syntax checks")

    inline_count = 0
    id_count = 0
    for html_path in sorted(FRONTEND_DIR.glob("*.html")):
        parser = HtmlIntegrityParser()
        parser.feed(html_path.read_text(encoding="utf-8-sig"))
        id_count += len(parser.ids)
        if parser.duplicate_ids:
            details = ", ".join(
                f"{element_id} (lines {first}/{duplicate})"
                for element_id, first, duplicate in parser.duplicate_ids
            )
            raise VerificationError(f"Duplicate HTML ids in {html_path.name}: {details}")

        for line, script in parser.inline_scripts:
            if not script.strip():
                continue
            inline_count += 1
            temp_path = None
            try:
                with tempfile.NamedTemporaryFile(
                    mode="w", suffix=".js", encoding="utf-8", delete=False,
                ) as handle:
                    handle.write(script)
                    temp_path = Path(handle.name)
                result = run([node, "--check", str(temp_path)], check=False)
                if result.returncode:
                    raise VerificationError(
                        f"Inline JavaScript failed: {html_path.name}:{line}\n{result.stderr}"
                    )
            finally:
                if temp_path is not None:
                    temp_path.unlink(missing_ok=True)
    return inline_count, id_count


def parse_int(value: str) -> int:
    return int(value.strip(), 0)


def verify_partition_table() -> str:
    path = FIRMWARE_DIR / "partitions.csv"
    rows: list[tuple[str, str, str, int, int]] = []
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        for raw_row in csv.reader(line for line in handle if not line.lstrip().startswith("#")):
            if not raw_row or not raw_row[0].strip():
                continue
            if len(raw_row) < 5:
                raise VerificationError(f"Malformed partition row: {raw_row}")
            name = raw_row[0].strip()
            rows.append(
                (
                    name,
                    raw_row[1].strip(),
                    raw_row[2].strip(),
                    parse_int(raw_row[3]),
                    parse_int(raw_row[4]),
                )
            )

    names = [name for name, _type, _subtype, _offset, _size in rows]
    duplicate_names = sorted({name for name in names if names.count(name) > 1})
    if duplicate_names:
        raise VerificationError(f"Duplicate partition names: {duplicate_names}")

    expected_kinds = {
        "nvs": ("data", "nvs"),
        "phy_init": ("data", "phy"),
        "factory": ("app", "factory"),
        "spiffs": ("data", "spiffs"),
    }
    actual_kinds = {name: (part_type, subtype) for name, part_type, subtype, *_rest in rows}
    missing = sorted(expected_kinds.keys() - actual_kinds.keys())
    mismatched = sorted(
        name
        for name, expected in expected_kinds.items()
        if name in actual_kinds and actual_kinds[name] != expected
    )
    if missing or mismatched:
        raise VerificationError(
            f"Partition roles are invalid: missing={missing}, mismatched={mismatched}"
        )

    previous_end = 0x9000  # bootloader + partition table occupy the lower flash region
    unallocated = 0
    for name, _type, _subtype, offset, size in sorted(rows, key=lambda item: item[3]):
        if offset % 0x1000 or size <= 0 or size % 0x1000:
            raise VerificationError(
                f"Partition {name} must have positive 4 KB-aligned offset/size: "
                f"offset=0x{offset:x}, size=0x{size:x}"
            )
        if name == "factory" and offset % 0x10000:
            raise VerificationError(f"App partition is not 64 KB aligned: 0x{offset:x}")
        if offset < previous_end:
            raise VerificationError(f"Partition overlap at {name}: 0x{offset:x} < 0x{previous_end:x}")
        unallocated += offset - previous_end
        previous_end = offset + size
    if previous_end > 0x400000:
        raise VerificationError(f"Partition table exceeds 4 MB: end=0x{previous_end:x}")
    unallocated += 0x400000 - previous_end
    if unallocated:
        raise VerificationError(f"Partition table leaves {unallocated} bytes unallocated")

    spiffs = next((size for name, _type, _subtype, _offset, size in rows if name == "spiffs"), 0)
    if spiffs < 384000:
        raise VerificationError(f"SPIFFS is too small for one EPD payload: {spiffs}")
    return f"{len(rows)} entries, unallocated={unallocated} bytes"


def extract_define(path: Path, name: str) -> int:
    pattern = re.compile(rf"^\s*#define\s+{re.escape(name)}\s+(\d+)", re.MULTILINE)
    match = pattern.search(path.read_text(encoding="utf-8-sig"))
    if not match:
        raise VerificationError(f"Missing #define {name} in {path.name}")
    return int(match.group(1))


def extract_python_constants(path: Path, names: set[str]) -> dict[str, int]:
    tree = ast.parse(path.read_text(encoding="utf-8-sig"), filename=str(path))
    values: dict[str, int] = {}

    def evaluate(node: ast.AST) -> int:
        if isinstance(node, ast.Constant) and isinstance(node.value, int):
            return node.value
        if isinstance(node, ast.Name) and node.id in values:
            return values[node.id]
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Mult):
            return evaluate(node.left) * evaluate(node.right)
        raise ValueError(ast.dump(node))

    for statement in tree.body:
        if not isinstance(statement, ast.Assign) or len(statement.targets) != 1:
            continue
        target = statement.targets[0]
        if isinstance(target, ast.Name):
            try:
                values[target.id] = evaluate(statement.value)
            except ValueError:
                continue
    missing = names - values.keys()
    if missing:
        raise VerificationError(f"Missing backend constants: {sorted(missing)}")
    return {name: values[name] for name in names}


def verify_epd_contract() -> str:
    firmware_chars = extract_define(FIRMWARE_DIR / "http_update.h", "EPD_EXPECTED_CHARS")
    backend = extract_python_constants(
        BACKEND_DIR / "app.py", {"EPD_WIDTH", "EPD_HEIGHT", "EPD_EXPECTED_CHARS"}
    )
    expected = 800 * 480
    if firmware_chars != expected or backend["EPD_EXPECTED_CHARS"] != expected:
        raise VerificationError(
            f"EPD payload mismatch: firmware={firmware_chars}, backend={backend['EPD_EXPECTED_CHARS']}"
        )
    if (backend["EPD_WIDTH"], backend["EPD_HEIGHT"]) != (800, 480):
        raise VerificationError(f"Unexpected backend dimensions: {backend}")
    return f"800x480/{expected} chars"


def extract_backend_routes() -> set[str]:
    tree = ast.parse(
        (BACKEND_DIR / "app.py").read_text(encoding="utf-8-sig"),
        filename=str(BACKEND_DIR / "app.py"),
    )
    routes: set[str] = set()
    for node in tree.body:
        if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            continue
        for decorator in node.decorator_list:
            if (
                isinstance(decorator, ast.Call)
                and isinstance(decorator.func, ast.Attribute)
                and decorator.func.attr == "route"
                and decorator.args
                and isinstance(decorator.args[0], ast.Constant)
                and isinstance(decorator.args[0].value, str)
            ):
                routes.add(decorator.args[0].value)
    return routes


def normalize_api_segments(path: str) -> list[str]:
    path = path.split("?", 1)[0].rstrip("/}")
    path = re.sub(r"\$\{[^}]+}", "<param>", path)
    return [segment for segment in path.split("/") if segment]


def api_reference_matches(reference: str, route: str) -> bool:
    reference_segments = normalize_api_segments(reference)
    route_segments = normalize_api_segments(route)
    if len(reference_segments) != len(route_segments):
        return False
    for actual, expected in zip(reference_segments, route_segments):
        if actual == "<param>" or (expected.startswith("<") and expected.endswith(">")):
            continue
        if actual != expected:
            return False
    return True


def verify_frontend_api_contract() -> tuple[int, int]:
    routes = extract_backend_routes()
    references: set[str] = set()
    string_pattern = re.compile(r"([`'\"])([^`'\"\r\n]*?/api/[^`'\"\r\n]*)\1")
    for path in sorted(FRONTEND_DIR.glob("*.js")) + sorted(FRONTEND_DIR.glob("*.html")):
        text = path.read_text(encoding="utf-8-sig")
        for match in string_pattern.finditer(text):
            value = match.group(2)
            reference = value[value.index("/api/") :]
            references.add(reference)

    missing = sorted(
        reference
        for reference in references
        if not any(api_reference_matches(reference, route) for route in routes)
    )
    if missing:
        raise VerificationError("Frontend API references without backend routes:\n" + "\n".join(missing))
    return len(references), len(routes)


def verify_git_hygiene() -> int:
    tracked = run(["git", "ls-files"]).stdout.splitlines()
    forbidden = [
        path
        for path in tracked
        if path == "cloud_server/.env"
        or path.startswith(("output/", ".playwright-cli/", "cloud_server/backend/data/", "cloud_server/mongodb/"))
    ]
    if forbidden:
        raise VerificationError("Sensitive/generated paths are tracked:\n" + "\n".join(forbidden))

    diff = run(["git", "diff", "--check"], check=False)
    if diff.returncode:
        raise VerificationError("git diff --check failed:\n" + diff.stdout + diff.stderr)
    return len(tracked)


def verify_deployment_contract() -> str:
    frontend_dockerfile = (FRONTEND_DIR / "Dockerfile").read_text(encoding="utf-8-sig")
    backend_dockerfile = (BACKEND_DIR / "Dockerfile").read_text(encoding="utf-8-sig")
    nginx_config = (FRONTEND_DIR / "nginx.conf").read_text(encoding="utf-8-sig")
    security_headers = (FRONTEND_DIR / "security_headers.conf").read_text(encoding="utf-8-sig")
    service_admin_dockerfile = (SERVICE_ADMIN_FRONTEND_DIR / "Dockerfile").read_text(encoding="utf-8-sig")
    service_admin_nginx = (SERVICE_ADMIN_FRONTEND_DIR / "nginx.conf").read_text(encoding="utf-8-sig")
    production_compose = (ROOT / "cloud_server" / "docker-compose.yml").read_text(
        encoding="utf-8-sig"
    )
    development_compose = (ROOT / "cloud_server" / "docker-compose.dev.yml").read_text(
        encoding="utf-8-sig"
    )

    required_frontend_copies = (
        "COPY assets ./assets",
        "COPY vendor ./vendor",
        "COPY security_headers.conf /etc/nginx/snippets/security_headers.conf",
    )
    missing_copies = [item for item in required_frontend_copies if item not in frontend_dockerfile]
    if missing_copies:
        raise VerificationError(f"Frontend image omits deploy assets: {missing_copies}")
    if "gunicorn" not in backend_dockerfile.lower():
        raise VerificationError("Backend container must run through Gunicorn")
    if "include /etc/nginx/snippets/security_headers.conf" not in nginx_config:
        raise VerificationError("Nginx does not include the shared security headers")
    if "location ^~ /api/service-admin/" not in nginx_config:
        raise VerificationError("Public frontend must explicitly deny service-admin APIs")
    if "location = /service-admin.html" not in nginx_config:
        raise VerificationError("Public frontend must explicitly deny the service-admin page")
    if "COPY service_admin_frontend/service-admin.html ./" not in service_admin_dockerfile:
        raise VerificationError("Service-admin image omits its private HTML entry point")
    if "location ^~ /api/service-admin/" not in service_admin_nginx:
        raise VerificationError("Service-admin frontend does not proxy its isolated API")
    if "location ^~ /api/" not in service_admin_nginx:
        raise VerificationError("Service-admin frontend must deny ordinary APIs")

    required_nginx_controls = (
        "real_ip_header X-Real-IP;",
        "real_ip_recursive on;",
        "limit_conn_zone $binary_remote_addr zone=upload_per_ip:10m;",
        "limit_conn_status 429;",
        "client_body_timeout 30s;",
        "proxy_set_header X-Forwarded-For $remote_addr;",
    )
    missing_nginx_controls = [
        item for item in required_nginx_controls if item not in nginx_config
    ]
    if missing_nginx_controls:
        raise VerificationError(
            f"Nginx is missing real-IP/upload controls: {missing_nginx_controls}"
        )
    required_trusted_proxies = (
        "set_real_ip_from 127.0.0.1;",
        "set_real_ip_from ::1;",
        "set_real_ip_from 172.16.0.0/12;",
    )
    if any(item not in nginx_config for item in required_trusted_proxies):
        raise VerificationError("Nginx must trust only the default local ingress paths")
    forbidden_trusted_proxies = (
        "set_real_ip_from 10.0.0.0/8;",
        "set_real_ip_from 192.168.0.0/16;",
        "set_real_ip_from fc00::/7;",
    )
    if any(item in nginx_config for item in forbidden_trusted_proxies):
        raise VerificationError("Nginx real-IP trust must not include broad LAN ranges")
    if "$proxy_add_x_forwarded_for" in nginx_config:
        raise VerificationError("Nginx must replace, not append, the trusted client address")
    if nginx_config.count("proxy_pass http://backend:5000;") != nginx_config.count(
        "proxy_set_header X-Forwarded-For $remote_addr;"
    ):
        raise VerificationError("Every backend proxy location must forward one restored client IP")

    expected_upload_routes = {
        "/api/nameplates/parse": ("20M", 2),
        "/api/epd/process-sixcolor": ("12M", 2),
        "/api/device/template": ("12M", 2),
        "/api/pages/save": ("6M", 2),
        "/api/epd/load": ("512k", 4),
        "/api/nameplates/dispatch": ("1M", 1),
    }
    for route, (body_limit, concurrency) in expected_upload_routes.items():
        match = re.search(
            rf"location = {re.escape(route)} \{{(?P<body>.*?)^    \}}",
            nginx_config,
            re.MULTILINE | re.DOTALL,
        )
        if not match:
            raise VerificationError(f"Nginx is missing an exact upload route for {route}")
        location_body = match.group("body")
        required_route_controls = (
            f"client_max_body_size {body_limit};",
            f"limit_conn upload_per_ip {concurrency};",
        )
        missing_route_controls = [
            item for item in required_route_controls if item not in location_body
        ]
        if missing_route_controls:
            raise VerificationError(
                f"Nginx route {route} is missing controls: {missing_route_controls}"
            )

    generic_api = re.search(
        r"location /api/ \{(?P<body>.*?)^    \}",
        nginx_config,
        re.MULTILINE | re.DOTALL,
    )
    if not generic_api or "client_max_body_size 1M;" not in generic_api.group("body"):
        raise VerificationError("Generic Nginx API route must keep a 1M body ceiling")
    if "proxy_read_timeout 110s;" not in nginx_config:
        raise VerificationError("Long-running upload routes must stay below Gunicorn's timeout")

    if "Content-Security-Policy" not in security_headers:
        raise VerificationError("Content-Security-Policy is missing")
    if "./frontend:/usr/share/nginx/html" in production_compose:
        raise VerificationError("Production compose must not bind-mount frontend source")
    if "./frontend:/usr/share/nginx/html" not in development_compose:
        raise VerificationError("Development compose is missing the frontend bind mount")
    if "${FRONTEND_BIND:-127.0.0.1}" not in production_compose:
        raise VerificationError("Production frontend bind must fail safe to loopback")
    if '"127.0.0.1:${SERVICE_ADMIN_PORT:-18081}:80"' not in production_compose:
        raise VerificationError("Service-admin frontend must bind explicitly to loopback")
    if production_compose.count("logging: *default-logging") != 4:
        raise VerificationError("All production containers must use bounded log rotation")

    env_example = (ROOT / "cloud_server" / ".env.example").read_text(encoding="utf-8-sig")
    configured_keys = {
        match.group(1)
        for match in re.finditer(r"^([A-Z][A-Z0-9_]*)=", env_example, re.MULTILINE)
    }
    required_keys = {
        "MONGO_INITDB_ROOT_USERNAME",
        "MONGO_INITDB_ROOT_PASSWORD",
        "SECRET_KEY",
        "PUBLIC_BASE_URL",
        "DEVICE_AUTH_REQUIRED",
        "UNCLAIMED_DEVICE_TTL_SECONDS",
        "AUTH_TOKEN_TTL_SECONDS",
        "SERVICE_ADMIN_TOKEN_TTL_SECONDS",
        "SERVICE_ADMIN_PORT",
        "CORS_ORIGINS",
    }
    missing_keys = sorted(required_keys - configured_keys)
    if missing_keys:
        raise VerificationError(f".env.example is missing deployment keys: {missing_keys}")

    return f"env={len(configured_keys)}, headers=ok, images=ok"


def verify_security_contract() -> str:
    backend_source = (BACKEND_DIR / "app.py").read_text(encoding="utf-8-sig")
    firmware_source = (FIRMWARE_DIR / "http_update.h").read_text(encoding="utf-8-sig")
    epd_dispatch_source = (FIRMWARE_DIR / "epd.h").read_text(encoding="utf-8-sig")
    qr_source = (FIRMWARE_DIR / "qrcode.c").read_text(encoding="utf-8-sig")
    identity_source = (FIRMWARE_DIR / "device_identity.h").read_text(encoding="utf-8-sig")
    required_backend_markers = (
        "X-Device-Key",
        "PUBLIC_BASE_URL",
        "generate_password_hash",
        "tokenHash",
    )
    required_firmware_markers = (
        "DEVICE_KEY_HEADER",
        "CLOUD_API_USE_HTTPS",
        "imageSha256",
        "mbedtls_sha256",
        "CLOUD_STATUS_TOTAL_TIMEOUT_MS",
        "CLOUD_DOWNLOAD_TOTAL_TIMEOUT_MS",
        "PROVISIONING_QR_MAX_BYTE_PAYLOAD",
        "PROVISIONING_QR_QUIET_ZONE_MODULES",
        "const bool loadCompleted = EPD_dispLoad();",
        "deriveDeviceIdentity",
    )
    missing_backend = [item for item in required_backend_markers if item not in backend_source]
    missing_firmware = [item for item in required_firmware_markers if item not in firmware_source]
    if missing_backend or missing_firmware:
        raise VerificationError(
            f"Authentication/integrity contract incomplete: backend={missing_backend}, "
            f"firmware={missing_firmware}"
        )
    if "f'http://{Config.FLASK_HOST}" in backend_source:
        raise VerificationError("Backend still hard-codes an HTTP image URL")
    if "bool (*EPD_dispLoad)();" not in epd_dispatch_source:
        raise VerificationError("EPD load failures are not propagated through the dispatcher")
    required_qr_markers = (
        "uint8_t result[400];",
        "uint8_t codewordBytes[400];",
        "uint8_t isFunctionGridBytes[400];",
        "version < 1 || version > 9",
    )
    missing_qr = [item for item in required_qr_markers if item not in qr_source]
    if missing_qr:
        raise VerificationError(f"QR bounds contract incomplete: {missing_qr}")
    if "readDeviceIdentityMac" not in identity_source or "deriveDeviceIdentity" not in identity_source:
        raise VerificationError("Shared device identity helper is incomplete")
    return "device-key + pairing + TLS/SHA + bounded I/O + EPD/QR propagation"


def verify_frontend_release_assets() -> str:
    version_pattern = re.compile(r"\?v=([A-Za-z0-9._-]+)")
    versions: set[str] = set()
    references = 0
    for path in sorted(FRONTEND_DIR.glob("*.html")) + sorted(FRONTEND_DIR.glob("*.js")):
        matches = version_pattern.findall(path.read_text(encoding="utf-8-sig"))
        versions.update(matches)
        references += len(matches)
    if len(versions) != 1:
        raise VerificationError(f"Frontend cache-buster versions are inconsistent: {sorted(versions)}")

    asset_names = ("pheno-logo-black.png", "pheno-logo-white.png", "pheno-mark-square.png")
    for asset_name in asset_names:
        frontend_asset = FRONTEND_DIR / "assets" / "nameplate" / asset_name
        backend_asset = BACKEND_DIR / "assets" / "nameplate" / asset_name
        frontend_hash = hashlib.sha256(frontend_asset.read_bytes()).digest()
        backend_hash = hashlib.sha256(backend_asset.read_bytes()).digest()
        if frontend_hash != backend_hash:
            raise VerificationError(f"Frontend/backend asset copies differ: {asset_name}")
    return f"version={next(iter(versions))}, refs={references}, paired-assets={len(asset_names)}"


def verify_tooling_contract() -> str:
    build_helper = (ROOT / "tools" / "build_firmware.ps1").read_text(encoding="utf-8-sig")
    required_build_guards = (
        "$MirrorMarkerName",
        "$AdoptLegacyMirror",
        "non-empty unmarked directory",
        "Find-ReparsePoint",
        "Find-ReparseAncestor",
        "$firmwareDir",
        "/MIR /XJ",
        "/XD .build __pycache__",
    )
    missing_build_guards = [item for item in required_build_guards if item not in build_helper]
    if missing_build_guards or "legacyMirrorSignature" in build_helper:
        raise VerificationError(
            f"Firmware build mirror safety guards are incomplete: {missing_build_guards}"
        )

    workflow = (ROOT / ".github" / "workflows" / "verify.yml").read_text(
        encoding="utf-8-sig"
    )
    required_workflow_steps = (
        "fetch-depth: 0",
        "python tools/verify_project.py",
        "python -m pip install -r cloud_server/backend/requirements.txt",
        "python -m pip check",
        "python -m pip_audit -r cloud_server/backend/requirements.txt",
        "npx --yes retire@5.4.3 --path cloud_server/frontend",
        "python cloud_server/backend/test_app.py",
    )
    missing_workflow_steps = [item for item in required_workflow_steps if item not in workflow]
    if missing_workflow_steps:
        raise VerificationError(f"CI verification is incomplete: {missing_workflow_steps}")

    requirements = (BACKEND_DIR / "requirements.txt").read_text(encoding="utf-8-sig")
    banned_vulnerable_pins = (
        "flask==3.0.0",
        "flask-cors==4.0.0",
        "python-dotenv==1.0.0",
        "gunicorn==21.2.0",
        "Pillow==10.3.0",
        "Pillow==12.2.0",
        "requests==2.32.3",
    )
    stale_pins = [pin for pin in banned_vulnerable_pins if pin in requirements]
    if stale_pins:
        raise VerificationError(f"Known vulnerable dependency pins returned: {stale_pins}")

    ignore_lines = {
        line.strip()
        for line in (ROOT / ".gitignore").read_text(encoding="utf-8-sig").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    }
    broad_generated_patterns = sorted(
        {"output/", ".playwright-cli/", ".cursor/", ".codex/", "nul"} & ignore_lines
    )
    if broad_generated_patterns:
        raise VerificationError(
            "Generated/tool ignores must be repository-root scoped: "
            + ", ".join(broad_generated_patterns)
        )
    return "safe mirror + CI regression + root-scoped ignores"


def main() -> int:
    checks = [
        ("Python syntax", verify_python_syntax),
        ("JavaScript syntax", verify_javascript_syntax),
        ("firmware layout", verify_firmware_layout),
        ("HTML local assets", verify_html_assets),
        ("HTML integrity", verify_html_integrity),
        ("partition table", verify_partition_table),
        ("EPD contract", verify_epd_contract),
        ("frontend/backend API contract", verify_frontend_api_contract),
        ("deployment contract", verify_deployment_contract),
        ("security contract", verify_security_contract),
        ("frontend release assets", verify_frontend_release_assets),
        ("tooling contract", verify_tooling_contract),
        ("Git hygiene", verify_git_hygiene),
    ]
    failed = False
    for label, check in checks:
        try:
            result = check()
            print(f"PASS {label}: {result}")
        except Exception as exc:
            failed = True
            print(f"FAIL {label}: {exc}", file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
