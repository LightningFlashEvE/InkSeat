import os
from urllib.parse import urlsplit, urlunsplit


def _validated_public_base_url(value: str) -> str:
    value = str(value or '').strip().rstrip('/')
    parsed = urlsplit(value)
    try:
        port = parsed.port
    except ValueError as exc:
        raise ValueError('PUBLIC_BASE_URL contains an invalid port') from exc
    if (
        parsed.scheme not in ('http', 'https')
        or not parsed.hostname
        or parsed.username is not None
        or parsed.password is not None
        or parsed.path not in ('', '/')
        or parsed.query
        or parsed.fragment
    ):
        raise ValueError('PUBLIC_BASE_URL must be an http(s) origin without credentials, path, query or fragment')
    hostname = parsed.hostname.lower()
    if ':' in hostname:
        hostname = f'[{hostname}]'
    netloc = f'{hostname}:{port}' if port is not None else hostname
    return urlunsplit((parsed.scheme.lower(), netloc, '', '', ''))

class Config:
    """Flask 配置
    
    Deep-sleep + HTTP Pull 架构（无MQTT）
    """
    
    # Flask
    SECRET_KEY = os.environ.get('SECRET_KEY', 'esp32-epd-secret-key')
    DEBUG = os.environ.get('FLASK_DEBUG', 'false').lower() == 'true'
    MAX_CONTENT_LENGTH = int(os.environ.get('MAX_CONTENT_LENGTH', 25 * 1024 * 1024))
    DEVICE_AUTH_REQUIRED = os.environ.get('DEVICE_AUTH_REQUIRED', 'true').strip().lower() not in (
        '0', 'false', 'no', 'off'
    )
    CORS_ORIGINS = [
        origin.strip()
        for origin in os.environ.get('CORS_ORIGINS', '').split(',')
        if origin.strip()
    ]
    SERVICE_ADMIN_TOKEN_TTL_SECONDS = min(
        24 * 60 * 60,
        max(15 * 60, int(os.environ.get('SERVICE_ADMIN_TOKEN_TTL_SECONDS', 4 * 60 * 60))),
    )
    PAIRING_MAX_FAILED_ATTEMPTS = min(
        100, max(1, int(os.environ.get('PAIRING_MAX_FAILED_ATTEMPTS', 8)))
    )
    PAIRING_LOCK_SECONDS = min(
        24 * 60 * 60,
        max(60, int(os.environ.get('PAIRING_LOCK_SECONDS', 15 * 60))),
    )
    DEVICE_STATUS_MAX_BODY_BYTES = min(
        64 * 1024,
        max(1024, int(os.environ.get('DEVICE_STATUS_MAX_BODY_BYTES', 4096))),
    )
    DEVICE_KEY_RESET_WINDOW_SECONDS = min(
        60 * 60,
        max(60, int(os.environ.get('DEVICE_KEY_RESET_WINDOW_SECONDS', 5 * 60))),
    )
    UNCLAIMED_DEVICE_TTL_SECONDS = min(
        30 * 24 * 60 * 60,
        max(60 * 60, int(os.environ.get('UNCLAIMED_DEVICE_TTL_SECONDS', 48 * 60 * 60))),
    )
    
    # MongoDB
    MONGODB_URI = os.environ.get('MONGODB_URI', 'mongodb://esp32_epd_root:change_this_mongo_password@mongodb:27017/esp32_epd?authSource=admin')
    MONGODB_DB = os.environ.get('MONGODB_DB', 'esp32_epd')
    
    # Flask (用于构建下载URL)
    FLASK_HOST = os.environ.get('FLASK_HOST', '127.0.0.1')
    FLASK_PORT = int(os.environ.get('FLASK_PORT', 8080))
    PUBLIC_BASE_URL = _validated_public_base_url(
        os.environ.get('PUBLIC_BASE_URL') or f'http://{FLASK_HOST}:{FLASK_PORT}'
    )
    
    # 注意：MQTT配置已移除，本架构使用HTTP拉取模式
    # 设备通过HTTP轮询获取更新，不需要MQTT常连接
