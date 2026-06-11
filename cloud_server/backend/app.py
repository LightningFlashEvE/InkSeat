#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32 E-Paper Cloud Server - Flask Backend
Deep-sleep + HTTP Pull Architecture (无MQTT版本)

设备通过HTTP拉取更新，服务器持久化保存图片数据
"""

import os
import json
import time
import threading
import hashlib
import secrets
from datetime import datetime, timedelta
from functools import wraps
from pathlib import Path
from urllib.parse import urlsplit, urlunsplit

from flask import Flask, request, jsonify, send_file, Response
from flask_cors import CORS
from pymongo import MongoClient
from pymongo.errors import DuplicateKeyError
import tempfile
import io

from config import Config
from six_color_epd import process_e6_image_from_base64

# ==================== Flask 应用初始化 ====================
app = Flask(__name__)
app.config.from_object(Config)
CORS(app)  # 允许跨域请求

# ==================== EPD 数据格式（7.3" E6，800x480，4bit a~p） ====================
EPD_WIDTH = 800
EPD_HEIGHT = 480
EPD_EXPECTED_CHARS = EPD_WIDTH * EPD_HEIGHT  # 384000
EPD_ALLOWED_CHARS = set('abcdefghijklmnop')
DEFAULT_SLEEP_INTERVAL_SECONDS = 12 * 60 * 60
TEMPLATE_SLEEP_INTERVAL_SECONDS = {
    'clock': 60 * 60,
    'weather': 6 * 60 * 60,
    'calendar': 24 * 60 * 60,
    'quote': 24 * 60 * 60,
    'todo': DEFAULT_SLEEP_INTERVAL_SECONDS,
    'qrcode': DEFAULT_SLEEP_INTERVAL_SECONDS,
}
CONTENT_MODE_LABELS = {
    'image': '普通图片',
    'text': '文字内容',
    'mixed': '图文内容',
    'custom': '自定义内容',
}

def get_template_meta(template_id: str):
    """查找当前仍可用的内置模板。"""
    if not template_id:
        return None
    template_id = str(template_id).strip().lower()
    return next((t for t in TEMPLATES if t.get('templateId') == template_id), None)

def build_content_metadata(content_mode=None, template_id=None):
    """统一计算设备当前内容标签和云端期望的唤醒间隔。"""
    mode = (content_mode or 'image')
    mode = str(mode).strip().lower() if isinstance(mode, str) else 'image'
    template_id = str(template_id).strip().lower() if template_id else None

    if mode == 'template':
        template = get_template_meta(template_id)
        if template:
            sleep_interval = TEMPLATE_SLEEP_INTERVAL_SECONDS.get(template_id, DEFAULT_SLEEP_INTERVAL_SECONDS)
            return {
                'activeContentMode': 'template',
                'activeTemplateId': template_id,
                'activeContentLabel': f"{template.get('name', template_id)}模板",
                'sleepIntervalSeconds': sleep_interval,
            }
        mode = 'custom'

    if mode not in CONTENT_MODE_LABELS:
        mode = 'image'

    return {
        'activeContentMode': mode,
        'activeTemplateId': None,
        'activeContentLabel': CONTENT_MODE_LABELS[mode],
        'sleepIntervalSeconds': DEFAULT_SLEEP_INTERVAL_SECONDS,
    }

def get_device_content_metadata(device):
    if not device:
        return build_content_metadata()
    return build_content_metadata(device.get('activeContentMode'), device.get('activeTemplateId'))

def to_epoch_ms(value):
    if not value:
        return None
    if isinstance(value, (int, float)):
        return int(value)
    if hasattr(value, 'timestamp'):
        return int(value.timestamp() * 1000)
    return None

def validate_epd_text_payload(image_data: str):
    """校验 EPD 原始数据（a~p 编码字符串）是否完整且合法。

    目标：尽量把“数据不完整/格式异常”的问题拦在云端发布阶段，
    避免设备端下载后才发现缺数据导致白屏。
    """
    if not isinstance(image_data, str) or not image_data:
        return False, 'Empty image data'
    if len(image_data) != EPD_EXPECTED_CHARS:
        return False, f'Invalid length: expected {EPD_EXPECTED_CHARS}, got {len(image_data)}'
    # 快速字符集校验（a~p），允许集合最多 16 种字符，set() 成本很小
    invalid = set(image_data) - EPD_ALLOWED_CHARS
    if invalid:
        bad = ''.join(sorted(list(invalid))[:8])
        return False, f'Invalid chars: {bad}'
    return True, None

# ==================== MongoDB 连接 ====================
mongo_client = None
db = None
users_collection = None
devices_collection = None
device_status_collection = None
pages_collection = None
page_lists_collection = None
pairing_codes_collection = None

# ==================== 图片持久化存储目录 ====================
# 图片数据保存在 data/epd/<deviceId>/latest.txt
DATA_DIR = Path(__file__).parent / 'data' / 'epd'
DATA_DIR.mkdir(parents=True, exist_ok=True)

def get_device_data_dir(device_id: str) -> Path:
    """获取设备数据目录"""
    device_dir = DATA_DIR / device_id.upper()
    device_dir.mkdir(parents=True, exist_ok=True)
    return device_dir

def get_device_image_path(device_id: str) -> Path:
    """获取设备最新图片文件路径"""
    return get_device_data_dir(device_id) / 'latest.txt'

def save_device_image(device_id: str, image_data: str) -> bool:
    """保存设备图片数据到磁盘"""
    try:
        image_path = get_device_image_path(device_id)
        # 原子写入：先写临时文件，再 replace，避免出现“文件被半写入”的情况
        tmp_path = image_path.with_suffix(image_path.suffix + '.tmp')
        with open(tmp_path, 'w', encoding='utf-8', newline='') as f:
            f.write(image_data)
            f.flush()
            try:
                os.fsync(f.fileno())
            except Exception:
                # 某些环境/文件系统可能不支持 fsync，忽略但仍然 replace
                pass
        tmp_path.replace(image_path)

        print(f'💾 图片已保存: {image_path} ({len(image_data)} 字符)')
        return True
    except Exception as e:
        print(f'❌ 保存图片失败: {e}')
        return False

def load_device_image(device_id: str) -> str:
    """从磁盘加载设备图片数据"""
    try:
        image_path = get_device_image_path(device_id)
        if image_path.exists():
            with open(image_path, 'r', encoding='utf-8') as f:
                return f.read()
    except Exception as e:
        print(f'❌ 加载图片失败: {e}')
    return None

def redact_uri_secret(uri: str) -> str:
    """Hide password-like credentials before printing a URI to logs."""
    try:
        parsed = urlsplit(uri)
        if parsed.password is None:
            return uri
        username = parsed.username or ''
        host = parsed.hostname or ''
        port = f':{parsed.port}' if parsed.port else ''
        netloc = f'{username}:***@{host}{port}'
        return urlunsplit((parsed.scheme, netloc, parsed.path, parsed.query, parsed.fragment))
    except Exception:
        return '<redacted-uri>'

def connect_mongodb(max_retries: int = 10, retry_delay_seconds: int = 2):
    """连接 MongoDB"""
    global mongo_client, db, users_collection, devices_collection, device_status_collection
    global pages_collection, page_lists_collection, pairing_codes_collection
    for attempt in range(1, max_retries + 1):
        try:
            mongo_client = MongoClient(Config.MONGODB_URI, serverSelectionTimeoutMS=5000)
            # 测试连接
            mongo_client.server_info()
            db = mongo_client[Config.MONGODB_DB]
            users_collection = db['users']
            devices_collection = db['devices']
            device_status_collection = db['device_status']
            pages_collection = db['pages']
            page_lists_collection = db['page_lists']
            pairing_codes_collection = db['pairing_codes']

            # 创建索引
            users_collection.create_index('username', unique=True)
            users_collection.create_index('token', unique=True, sparse=True)

            devices_collection.create_index('deviceId', unique=True)
            devices_collection.create_index('owner')
            devices_collection.create_index('claimed')

            device_status_collection.create_index('deviceId', unique=True)
            device_status_collection.create_index('lastSeen')

            pages_collection.create_index('deviceId')
            pages_collection.create_index([('deviceId', 1), ('name', 1)])
            # 列表页按 updatedAt 排序：需要索引避免全表扫描（数据大时会非常慢）
            pages_collection.create_index([('deviceId', 1), ('updatedAt', -1)])

            page_lists_collection.create_index('deviceId')
            page_lists_collection.create_index([('deviceId', 1), ('isActive', 1)])
            page_lists_collection.create_index([('deviceId', 1), ('updatedAt', -1)])

            pairing_codes_collection.create_index('deviceId', unique=True)
            pairing_codes_collection.create_index('expiresAt', expireAfterSeconds=0)

            print(f'✅ Connected to MongoDB: {redact_uri_secret(Config.MONGODB_URI)}')
            print(f'📊 Database: {Config.MONGODB_DB}')
            return True
        except Exception as e:
            if attempt < max_retries:
                print(f'⚠️  MongoDB connection attempt {attempt}/{max_retries} failed: {e}')
                time.sleep(retry_delay_seconds)
                continue
            print(f'❌ MongoDB connection error after {max_retries} attempts: {e}')
            print('⚠️  Server will continue without MongoDB-backed features')
            return False

# ==================== 用户认证工具函数 ====================

def hash_password(password: str) -> str:
    return hashlib.sha256(password.encode('utf-8')).hexdigest()

def generate_token() -> str:
    return secrets.token_hex(32)

def get_current_user():
    """根据 Authorization: Bearer <token> 获取当前用户"""
    global users_collection
    if users_collection is None:
        return None

    auth_header = request.headers.get('Authorization', '')
    if not auth_header.startswith('Bearer '):
        return None
    token = auth_header[7:].strip()
    if not token:
        return None

    user = users_collection.find_one({'token': token})
    return user

def login_required(f):
    """需要登录的装饰器"""
    @wraps(f)
    def wrapper(*args, **kwargs):
        user = get_current_user()
        if not user:
            return jsonify({'success': False, 'error': 'Unauthorized'}), 401
        request.user = user
        return f(*args, **kwargs)
    return wrapper

def normalize_device_id(device_id: str) -> str:
    """统一规范化 deviceId：去掉分隔符并转大写。

    设备端/前端可能传入带 '-' ':' 或小写的 ID；数据库里统一存 clean uppercase。
    """
    if not device_id:
        return ''
    return (device_id or '').strip().upper().replace('-', '').replace(':', '')

def ensure_device_owner(device_id: str, user) -> bool:
    """检查设备是否属于当前用户"""
    if devices_collection is None or not user:
        return False
    owner = user.get('username')
    if not owner:
        return False
    clean_id = normalize_device_id(device_id)
    device = devices_collection.find_one({'deviceId': clean_id, 'owner': owner})
    return device is not None

# ==================== API: 用户注册 / 登录 ====================

@app.route('/api/auth/register', methods=['POST'])
def register():
    """用户注册"""
    global users_collection
    if users_collection is None:
        return jsonify({'success': False, 'error': 'Database not connected'}), 500

    data = request.get_json() or {}
    username = (data.get('username') or '').strip()
    password = (data.get('password') or '').strip()

    if not username or not password:
        return jsonify({'success': False, 'error': '用户名和密码不能为空'}), 400

    if len(username) < 3 or len(password) < 4:
        return jsonify({'success': False, 'error': '用户名或密码太短'}), 400

    try:
        users_collection.insert_one({
            'username': username,
            'passwordHash': hash_password(password),
            'createdAt': datetime.utcnow()
        })
        return jsonify({'success': True, 'message': '注册成功'})
    except DuplicateKeyError:
        return jsonify({'success': False, 'error': '用户名已存在'}), 400
    except Exception as e:
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/auth/login', methods=['POST'])
def login():
    """用户登录，返回 token"""
    global users_collection
    if users_collection is None:
        return jsonify({'success': False, 'error': 'Database not connected'}), 500

    data = request.get_json() or {}
    username = (data.get('username') or '').strip()
    password = (data.get('password') or '').strip()

    if not username or not password:
        return jsonify({'success': False, 'error': '用户名和密码不能为空'}), 400

    user = users_collection.find_one({'username': username})
    if not user or user.get('passwordHash') != hash_password(password):
        return jsonify({'success': False, 'error': '用户名或密码错误'}), 400

    token = generate_token()
    users_collection.update_one(
        {'_id': user['_id']},
        {'$set': {'token': token, 'lastLoginAt': datetime.utcnow()}}
    )

    return jsonify({
        'success': True,
        'token': token,
        'user': {'username': username}
    })

@app.route('/api/auth/logout', methods=['POST'])
@login_required
def logout():
    """退出登录"""
    global users_collection
    user = getattr(request, 'user', None)
    if not user or users_collection is None:
        return jsonify({'success': False, 'error': 'Unauthorized'}), 401

    users_collection.update_one(
        {'_id': user['_id']},
        {'$unset': {'token': ''}}
    )
    return jsonify({'success': True, 'message': 'Logged out'})

@app.route('/api/auth/me', methods=['GET'])
@login_required
def me():
    """获取当前登录用户信息"""
    user = getattr(request, 'user', None)
    return jsonify({
        'success': True,
        'user': {
            'username': user.get('username')
        }
    })

# ==================== API: 设备管理 ====================

@app.route('/api/devices/preflight', methods=['POST'])
@login_required
def device_preflight():
    """添加设备前预检测：防止用户填错设备码。

    如果设备码从未查询过服务器，则拒绝添加。
    """
    try:
        data = request.get_json() or {}
        raw_id = (data.get('deviceId') or '').strip().upper()

        if not raw_id:
            return jsonify({'success': False, 'error': 'Missing deviceId'}), 400

        clean_id = raw_id.replace('-', '').replace(':', '')

        import re
        if not re.match(r'^[0-9A-F]{6}$|^[0-9A-F]{12}$', clean_id):
            return jsonify({'success': False, 'error': 'Invalid deviceId format'}), 400

        if devices_collection is None:
            return jsonify({'success': False, 'error': 'Database not connected'}), 500

        existing = devices_collection.find_one({'deviceId': clean_id})
        already_added = existing is not None

        has_seen_device = False
        last_seen_time = None
        recently_active = False

        if device_status_collection is not None:
            status = device_status_collection.find_one({'deviceId': clean_id})
            if status:
                has_seen_device = True
                last_seen_time = status.get('lastSeen', 0)
                recently_active = (time.time() * 1000 - last_seen_time) < 13 * 60 * 60 * 1000

        result = {
            'success': True,
            'deviceId': clean_id,
            'alreadyAdded': already_added,
            'deviceExists': has_seen_device,
            'recentlyActive': recently_active,
            'lastSeen': last_seen_time,
        }

        if already_added:
            result['message'] = f'设备 {clean_id} 已存在于您名下'
            result['canProceed'] = True
        elif has_seen_device:
            if recently_active:
                result['message'] = '设备码有效！该设备最近有活动记录'
                result['canProceed'] = True
            else:
                result['message'] = '该设备码历史上有活动记录，但最近未查询'
                result['canProceed'] = True
                result['warning'] = '设备码存在但最近不活跃，请确认是否正确'
        else:
            result['message'] = '未在服务器中找到该设备码的记录'
            result['canProceed'] = False
            result['error'] = f'设备码 {clean_id} 从未查询过本服务器，请检查设备码是否正确（或确认设备已先唤醒并配网）'

        return jsonify(result)
    except Exception as e:
        print(f'❌ Error in preflight check: {e}')
        import traceback
        traceback.print_exc()
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/devices/list', methods=['GET'])
@login_required
def get_devices_list():
    """获取当前用户的设备列表"""
    try:
        user = getattr(request, 'user', None)
        if devices_collection is None or not user:
            return jsonify({'success': True, 'devices': []})

        owner = user.get('username')
        devices = list(
            devices_collection.find({'owner': owner}, {'_id': 0})
            .sort('addedAt', -1)
        )
        return jsonify({'success': True, 'devices': devices})
    except Exception as e:
        print(f'❌ Error fetching devices: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/devices/add', methods=['POST'])
@login_required
def add_device():
    """为当前用户添加设备"""
    try:
        user = getattr(request, 'user', None)
        owner = user.get('username') if user else None

        data = request.get_json()
        device_id = data.get('deviceId', '').strip().upper()
        device_name = data.get('deviceName', '').strip()
        
        if not device_id:
            return jsonify({'success': False, 'error': 'Missing deviceId'}), 400
        
        clean_id = device_id.replace('-', '').replace(':', '')
        
        import re
        if not re.match(r'^[0-9A-F]{6}$|^[0-9A-F]{12}$', clean_id):
            return jsonify({'success': False, 'error': 'Invalid deviceId format'}), 400
        
        if devices_collection is None or not owner:
            return jsonify({'success': False, 'error': 'Database not connected'}), 500
        
        device = {
            'deviceId': clean_id,
            'deviceName': device_name or clean_id,
            'owner': owner,
            'claimed': True,
            'imageVersion': 0,
            'activeContentMode': 'image',
            'activeContentLabel': CONTENT_MODE_LABELS['image'],
            'sleepIntervalSeconds': DEFAULT_SLEEP_INTERVAL_SECONDS,
            'addedAt': datetime.utcnow(),
            'createdAt': datetime.utcnow(),
            'updatedAt': datetime.utcnow()
        }
        
        try:
            devices_collection.insert_one(device)
            if pairing_codes_collection is not None:
                pairing_codes_collection.delete_one({'deviceId': clean_id})
        except DuplicateKeyError:
            devices_collection.update_one(
                {'deviceId': clean_id},
                {
                    '$set': {
                        'owner': owner,
                        'deviceName': device_name or clean_id,
                        'claimed': True,
                        'updatedAt': datetime.utcnow()
                    }
                }
            )
            if pairing_codes_collection is not None:
                pairing_codes_collection.delete_one({'deviceId': clean_id})
        
        print(f'✅ Device added: {clean_id}')
        device.pop('_id', None)
        device['addedAt'] = device['addedAt'].isoformat()
        device['createdAt'] = device['createdAt'].isoformat()
        device['updatedAt'] = device['updatedAt'].isoformat()
        
        return jsonify({'success': True, 'device': device})
    except Exception as e:
        print(f'❌ Error adding device: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/devices/<device_id>', methods=['DELETE'])
@login_required
def delete_device(device_id):
    """删除当前用户的设备"""
    try:
        user = getattr(request, 'user', None)
        owner = user.get('username') if user else None

        if devices_collection is None or not owner:
            return jsonify({'success': False, 'error': 'Database not connected'}), 500
        
        result = devices_collection.delete_one({'deviceId': device_id, 'owner': owner})
        
        if result.deleted_count == 0:
            return jsonify({'success': False, 'error': 'Device not found'}), 404
        
        if device_status_collection is not None:
            device_status_collection.delete_one({'deviceId': device_id})
        
        print(f'✅ Device deleted: {device_id}')
        return jsonify({'success': True, 'message': 'Device deleted'})
    except Exception as e:
        print(f'❌ Error deleting device: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/devices', methods=['GET'])
@login_required
def get_devices_status():
    """获取当前用户的设备列表和状态"""
    try:
        user = getattr(request, 'user', None)
        owner = user.get('username') if user else None

        registered_devices = []
        if devices_collection is not None and owner:
            registered_devices = list(
                devices_collection.find({'owner': owner}, {'_id': 0})
            )
        
        devices = []
        for device in registered_devices:
            device_id = device['deviceId']
            content_meta = get_device_content_metadata(device)

            device_info = {
                'deviceId': device_id,
                'deviceName': device.get('deviceName', device_id),
                'addedAt': device.get('addedAt').isoformat() if hasattr(device.get('addedAt'), 'isoformat') else device.get('addedAt'),
                'online': False,  # Deep-sleep架构下设备通常离线
                'sleeping': False,  # Deep-sleep 架构：离线并不一定异常，后端给出“睡眠态”提示
                'claimed': device.get('claimed', False),
                'imageVersion': device.get('imageVersion', 0),
                'activeContentMode': content_meta['activeContentMode'],
                'activeTemplateId': content_meta['activeTemplateId'],
                'activeContentLabel': content_meta['activeContentLabel'],
                'sleepIntervalSeconds': content_meta['sleepIntervalSeconds'],
                'estimatedNextAutoWakeAt': None,
                'wakePolicyPending': False
            }
            
            # 检查设备最后活动时间
            if device_status_collection is not None:
                status = device_status_collection.find_one({'deviceId': device_id})
                if status:
                    last_seen = status.get('lastSeen', 0)
                    current_time = int(time.time() * 1000)
                    # Deep-sleep架构：最近5分钟内有活动则认为在线
                    device_info['online'] = (current_time - last_seen < 300000)
                    # Deep-sleep架构：在一个唤醒周期内无上报视为“睡眠中”，超过周期则视为“离线/失联”
                    # 默认周期：12小时唤醒一次；如果设备上报了云端动态间隔，则按该间隔再给 1 小时宽限
                    current_sleep_seconds = status.get('currentSleepSeconds')
                    if isinstance(current_sleep_seconds, (int, float)) and current_sleep_seconds > 0:
                        sleep_window_ms = int((current_sleep_seconds + 3600) * 1000)
                    else:
                        sleep_window_ms = 13 * 60 * 60 * 1000
                    device_info['sleeping'] = (not device_info['online']) and (current_time - last_seen < sleep_window_ms)
                    interval_for_estimate = content_meta['sleepIntervalSeconds']
                    content_updated_ms = to_epoch_ms(device.get('activeContentUpdatedAt') or device.get('updatedAt'))
                    if content_updated_ms and last_seen and content_updated_ms > last_seen:
                        if isinstance(current_sleep_seconds, (int, float)) and current_sleep_seconds > 0:
                            interval_for_estimate = int(current_sleep_seconds)
                            device_info['wakePolicyPending'] = True
                    if last_seen:
                        device_info['estimatedNextAutoWakeAt'] = int(last_seen + interval_for_estimate * 1000)
                    device_info['lastSeen'] = last_seen
                    device_info['lastWakeType'] = status.get('lastWakeType')
                    device_info['lastWakeCause'] = status.get('lastWakeCause')
                    device_info['lastManualWake'] = status.get('lastManualWake')
                    device_info['lastAutoWake'] = status.get('lastAutoWake')
                    device_info['ip'] = status.get('ip')
                    device_info['remoteIp'] = status.get('remoteIp')
                    device_info['rssi'] = status.get('rssi')
                    device_info['uptime_ms'] = status.get('uptime_ms')
                    device_info['freeHeap'] = status.get('freeHeap')
                    device_info['currentSleepSeconds'] = status.get('currentSleepSeconds')
            
            devices.append(device_info)
        
        return jsonify({'success': True, 'devices': devices})
    except Exception as e:
        print(f'❌ Error fetching device status: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

# ==================== API: 设备绑定状态查询和绑定 ====================

@app.route('/api/device/status', methods=['POST'])
def device_status():
    """设备查询绑定状态（无需登录，设备调用）
    
    返回：
    - claimed: 是否已绑定
    - imageVersion: 最新图片版本号
    - imageUrl: 图片下载URL（仅已绑定且有图片时返回）
    - pairingCode: 配对码（仅未绑定时返回）
    """
    try:
        data = request.get_json() or {}
        device_id = (data.get('deviceId') or '').strip().upper()
        
        if not device_id:
            return jsonify({'success': False, 'error': 'Missing deviceId'}), 400
        
        clean_id = device_id.replace('-', '').replace(':', '')
        
        import re
        if not re.match(r'^[0-9A-F]{6}$|^[0-9A-F]{12}$', clean_id):
            return jsonify({'success': False, 'error': 'Invalid deviceId format'}), 400
        
        telemetry = {
            'lastSeen': int(time.time() * 1000),
            'updatedAt': datetime.utcnow()
        }
        now_ms = telemetry['lastSeen']

        forwarded_for = request.headers.get('X-Forwarded-For', '')
        if forwarded_for:
            telemetry['remoteIp'] = forwarded_for.split(',')[0].strip()
        elif request.remote_addr:
            telemetry['remoteIp'] = request.remote_addr

        ip = data.get('ip')
        if isinstance(ip, str) and ip.strip():
            telemetry['ip'] = ip.strip()

        for field in ('rssi', 'uptime_ms', 'freeHeap', 'currentSleepSeconds'):
            value = data.get(field)
            if isinstance(value, (int, float)):
                telemetry[field] = int(value)

        wake_type = (data.get('wakeType') or '').strip().lower()
        wake_cause = (data.get('wakeCause') or '').strip()
        if wake_type in ('manual', 'auto', 'reset', 'other'):
            telemetry['lastWakeType'] = wake_type
            if wake_type == 'manual':
                telemetry['lastManualWake'] = now_ms
            elif wake_type == 'auto':
                telemetry['lastAutoWake'] = now_ms
        if wake_cause:
            telemetry['lastWakeCause'] = wake_cause

        print(
            f"📡 设备 {clean_id} 上报: "
            f"ip={telemetry.get('ip', '-')}, "
            f"remoteIp={telemetry.get('remoteIp', '-')}, "
            f"rssi={telemetry.get('rssi', '-')}, "
            f"uptime_ms={telemetry.get('uptime_ms', '-')}, "
            f"freeHeap={telemetry.get('freeHeap', '-')}, "
            f"wakeType={telemetry.get('lastWakeType', '-')}, "
            f"wakeCause={telemetry.get('lastWakeCause', '-')}, "
            f"currentSleepSeconds={telemetry.get('currentSleepSeconds', '-')}"
        )

        # 更新设备最后活动时间和调试遥测
        if device_status_collection is not None:
            device_status_collection.update_one(
                {'deviceId': clean_id},
                {'$set': telemetry},
                upsert=True
            )
        
        if devices_collection is None:
            return jsonify({'success': False, 'error': 'Database not connected'}), 500
        
        device = devices_collection.find_one({'deviceId': clean_id})
        claimed = device is not None and device.get('claimed', False)
        
        response = {
            'success': True,
            'deviceId': clean_id,
            'claimed': claimed
        }
        
        if claimed and device:
            # 已绑定：返回图片版本和下载URL
            image_version = device.get('imageVersion', 0)
            content_meta = get_device_content_metadata(device)
            response['imageVersion'] = image_version
            response['nextSleepSeconds'] = content_meta['sleepIntervalSeconds']

            # 检查是否有持久化的图片
            image_path = get_device_image_path(clean_id)
            if image_path.exists() and image_version > 0:
                # 构建稳定的下载URL
                response['imageUrl'] = f'http://{Config.FLASK_HOST}:{Config.FLASK_PORT}/api/epd/raw/{clean_id}?v={image_version}'
                # 返回云端侧元数据，设备可做轻量校验（不强制）
                if device.get('imageSizeChars') is not None:
                    response['imageSizeChars'] = device.get('imageSizeChars')
                if device.get('imageSha256') is not None:
                    response['imageSha256'] = device.get('imageSha256')
            
            print(f"📊 设备 {clean_id} 查询状态: claimed=True, imageVersion={image_version}, "
                  f"nextSleepSeconds={content_meta['sleepIntervalSeconds']}")
        else:
            # 未绑定：生成或返回配对码
            response['imageVersion'] = 0
            response['nextSleepSeconds'] = DEFAULT_SLEEP_INTERVAL_SECONDS
            
            pairing_code = None
            expires_at = None
            
            if pairing_codes_collection is not None:
                pairing_doc = pairing_codes_collection.find_one({'deviceId': clean_id})
                if pairing_doc:
                    pairing_code = pairing_doc.get('code')
                    expires_at = pairing_doc.get('expiresAt')
            
            if not pairing_code or (expires_at and expires_at < datetime.utcnow()):
                import random
                pairing_code = f"{random.randint(100000, 999999)}"
                expires_at = datetime.utcnow() + timedelta(hours=24)
                
                if pairing_codes_collection is not None:
                    pairing_codes_collection.update_one(
                        {'deviceId': clean_id},
                        {
                            '$set': {
                                'code': pairing_code,
                                'expiresAt': expires_at,
                                'createdAt': datetime.utcnow()
                            }
                        },
                        upsert=True
                    )
            
            if expires_at:
                expires_in = int((expires_at - datetime.utcnow()).total_seconds())
                if expires_in < 0:
                    expires_in = 0
            else:
                expires_in = 86400
            
            response['pairingCode'] = pairing_code
            response['expiresIn'] = expires_in
            
            print(f'📊 设备 {clean_id} 查询状态: claimed=False, pairingCode={pairing_code}')
        
        return jsonify(response)
    except Exception as e:
        print(f'❌ Error querying device status: {e}')
        import traceback
        traceback.print_exc()
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/device/claim', methods=['POST'])
@login_required
def device_claim():
    """用户绑定设备（需要登录）"""
    try:
        user = getattr(request, 'user', None)
        owner = user.get('username') if user else None
        
        if not owner:
            return jsonify({'success': False, 'error': 'Unauthorized'}), 401
        
        data = request.get_json() or {}
        device_id = (data.get('deviceId') or '').strip().upper()
        pairing_code = (data.get('pairingCode') or '').strip()
        
        if not device_id:
            return jsonify({'success': False, 'error': 'Missing deviceId'}), 400
        
        clean_id = device_id.replace('-', '').replace(':', '')
        
        import re
        if not re.match(r'^[0-9A-F]{6}$|^[0-9A-F]{12}$', clean_id):
            return jsonify({'success': False, 'error': 'Invalid deviceId format'}), 400
        
        if devices_collection is None:
            return jsonify({'success': False, 'error': 'Database not connected'}), 500
        
        if pairing_code:
            if pairing_codes_collection is None:
                return jsonify({'success': False, 'error': 'Pairing code verification unavailable'}), 500
            
            pairing_doc = pairing_codes_collection.find_one({'deviceId': clean_id})
            if not pairing_doc:
                return jsonify({'success': False, 'error': 'Pairing code not found'}), 404
            
            if pairing_doc.get('code') != pairing_code:
                return jsonify({'success': False, 'error': 'Invalid pairing code'}), 400
            
            expires_at = pairing_doc.get('expiresAt')
            if expires_at and expires_at < datetime.utcnow():
                return jsonify({'success': False, 'error': 'Pairing code expired'}), 400
        
        existing_device = devices_collection.find_one({'deviceId': clean_id})
        if existing_device:
            existing_owner = existing_device.get('owner')
            existing_claimed = existing_device.get('claimed', False)
            
            if existing_claimed and existing_owner != owner:
                return jsonify({'success': False, 'error': 'Device already claimed by another user'}), 403
            
            devices_collection.update_one(
                {'deviceId': clean_id},
                {
                    '$set': {
                        'owner': owner,
                        'claimed': True,
                        'updatedAt': datetime.utcnow()
                    }
                }
            )
            print(f'✅ Device claimed: {clean_id} by {owner}')
        else:
            device_name = data.get('deviceName', '').strip() or clean_id
            device = {
                'deviceId': clean_id,
                'deviceName': device_name,
                'owner': owner,
                'claimed': True,
                'imageVersion': 0,
                'addedAt': datetime.utcnow(),
                'createdAt': datetime.utcnow(),
                'updatedAt': datetime.utcnow()
            }
            devices_collection.insert_one(device)
            print(f'✅ New device claimed: {clean_id} by {owner}')
        
        if pairing_codes_collection is not None:
            pairing_codes_collection.delete_one({'deviceId': clean_id})
        
        return jsonify({
            'success': True,
            'message': 'Device claimed successfully',
            'deviceId': clean_id
        })
    except DuplicateKeyError:
        return jsonify({'success': False, 'error': 'Device already exists'}), 400
    except Exception as e:
        print(f'❌ Error claiming device: {e}')
        import traceback
        traceback.print_exc()
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/device/unbind', methods=['POST'])
@login_required
def device_unbind():
    """解绑设备（需要登录，仅限设备所有者）"""
    try:
        user = getattr(request, 'user', None)
        owner = user.get('username') if user else None
        
        if not owner:
            return jsonify({'success': False, 'error': 'Unauthorized'}), 401
        
        data = request.get_json() or {}
        device_id = (data.get('deviceId') or '').strip().upper()
        
        if not device_id:
            return jsonify({'success': False, 'error': 'Missing deviceId'}), 400
        
        clean_id = device_id.replace('-', '').replace(':', '')
        
        if devices_collection is None:
            return jsonify({'success': False, 'error': 'Database not connected'}), 500
        
        device = devices_collection.find_one({'deviceId': clean_id, 'owner': owner})
        if not device:
            return jsonify({'success': False, 'error': 'Device not found or no permission'}), 404
        
        devices_collection.update_one(
            {'deviceId': clean_id},
            {
                '$set': {
                    'claimed': False,
                    'updatedAt': datetime.utcnow()
                }
            }
        )
        
        if pairing_codes_collection is not None:
            pairing_codes_collection.delete_one({'deviceId': clean_id})
        
        print(f'✅ Device unbound: {clean_id} by {owner}')
        
        return jsonify({
            'success': True,
            'message': 'Device unbound successfully',
            'deviceId': clean_id
        })
    except Exception as e:
        print(f'❌ Error unbinding device: {e}')
        import traceback
        traceback.print_exc()
        return jsonify({'success': False, 'error': str(e)}), 500

# ==================== API: 页面管理 ====================

@app.route('/api/pages/list/<device_id>', methods=['GET'])
@login_required
def get_pages(device_id):
    """获取设备的所有页面"""
    try:
        user = getattr(request, 'user', None)
        clean_id = normalize_device_id(device_id)
        if not ensure_device_owner(clean_id, user):
            return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403

        if pages_collection is None:
            return jsonify({'success': True, 'pages': []})

        # 兼容历史数据：deviceId 可能未规范化写入（带分隔符/小写等）
        candidates = list({
            (device_id or '').strip(),
            (device_id or '').strip().upper(),
            normalize_device_id(device_id),
        })
        candidates = [c for c in candidates if c]

        # 列表接口仅返回轻量字段，避免把 data.imageData（base64）整包带回导致前端卡顿/不显示
        limit = request.args.get('limit', '200')
        try:
            limit = int(limit)
        except Exception:
            limit = 200
        limit = max(1, min(limit, 500))

        pages = list(pages_collection.find(
            {'deviceId': {'$in': candidates}},
            {
                '_id': 0,
                'pageId': 1,
                'deviceId': 1,
                'name': 1,
                'type': 1,
                'thumbnail': 1,
                'createdAt': 1,
                'updatedAt': 1
            }
        ).sort('updatedAt', -1).limit(limit))
        
        for page in pages:
            if hasattr(page.get('createdAt'), 'isoformat'):
                page['createdAt'] = page['createdAt'].isoformat()
            if hasattr(page.get('updatedAt'), 'isoformat'):
                page['updatedAt'] = page['updatedAt'].isoformat()
        
        return jsonify({'success': True, 'pages': pages})
    except Exception as e:
        print(f'❌ Error fetching pages: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/pages/save', methods=['POST'])
@login_required
def save_page():
    """保存页面"""
    try:
        data = request.get_json()
        device_id = data.get('deviceId')
        page_id = data.get('pageId')
        page_name = data.get('name', '未命名页面')
        page_type = data.get('type', 'custom')
        page_data = data.get('data', {})
        thumbnail = data.get('thumbnail', '')
        
        if not device_id:
            return jsonify({'success': False, 'error': 'Missing deviceId'}), 400

        clean_id = normalize_device_id(device_id)

        user = getattr(request, 'user', None)
        if not ensure_device_owner(clean_id, user):
            return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403
        
        if pages_collection is None:
            return jsonify({'success': False, 'error': 'Database not connected'}), 500
        
        now = datetime.utcnow()
        
        if page_id:
            result = pages_collection.update_one(
                {'pageId': page_id, 'deviceId': clean_id},
                {'$set': {
                    'name': page_name,
                    'type': page_type,
                    'data': page_data,
                    'thumbnail': thumbnail,
                    'updatedAt': now
                }}
            )
            if result.matched_count == 0:
                return jsonify({'success': False, 'error': 'Page not found'}), 404
            
            print(f'✅ Page updated: {page_id}')
        else:
            import uuid
            page_id = str(uuid.uuid4())[:8]
            
            page = {
                'pageId': page_id,
                'deviceId': clean_id,
                'name': page_name,
                'type': page_type,
                'data': page_data,
                'thumbnail': thumbnail,
                'createdAt': now,
                'updatedAt': now
            }
            pages_collection.insert_one(page)
            print(f'✅ Page created: {page_id}')
        
        return jsonify({
            'success': True, 
            'pageId': page_id,
            'message': 'Page saved'
        })
    except Exception as e:
        print(f'❌ Error saving page: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/pages/<page_id>', methods=['GET'])
@login_required
def get_page(page_id):
    """获取单个页面详情"""
    try:
        if pages_collection is None:
            return jsonify({'success': False, 'error': 'Database not connected'}), 500
        
        page = pages_collection.find_one({'pageId': page_id}, {'_id': 0})
        if not page:
            return jsonify({'success': False, 'error': 'Page not found'}), 404

        user = getattr(request, 'user', None)
        device_id = page.get('deviceId')
        if device_id and not ensure_device_owner(device_id, user):
            return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403
        
        if hasattr(page.get('createdAt'), 'isoformat'):
            page['createdAt'] = page['createdAt'].isoformat()
        if hasattr(page.get('updatedAt'), 'isoformat'):
            page['updatedAt'] = page['updatedAt'].isoformat()
        
        return jsonify({'success': True, 'page': page})
    except Exception as e:
        print(f'❌ Error fetching page: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/pages/<page_id>', methods=['DELETE'])
@login_required
def delete_page(page_id):
    """删除页面"""
    try:
        if pages_collection is None:
            return jsonify({'success': False, 'error': 'Database not connected'}), 500

        page = pages_collection.find_one({'pageId': page_id})
        if not page:
            return jsonify({'success': False, 'error': 'Page not found'}), 404

        user = getattr(request, 'user', None)
        device_id = page.get('deviceId')
        if device_id and not ensure_device_owner(device_id, user):
            return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403

        result = pages_collection.delete_one({'pageId': page_id})
        
        if page_lists_collection is not None:
            page_lists_collection.update_many(
                {},
                {'$pull': {'pages': {'pageId': page_id}}}
            )
        
        print(f'✅ Page deleted: {page_id}')
        return jsonify({'success': True, 'message': 'Page deleted'})
    except Exception as e:
        print(f'❌ Error deleting page: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

# ==================== API: 页面列表管理 ====================

@app.route('/api/page-lists/list/<device_id>', methods=['GET'])
@login_required
def get_page_lists(device_id):
    """获取设备的所有页面列表"""
    try:
        user = getattr(request, 'user', None)
        clean_id = normalize_device_id(device_id)
        if not ensure_device_owner(clean_id, user):
            return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403

        if page_lists_collection is None:
            return jsonify({'success': True, 'pageLists': []})

        # 兼容历史数据：deviceId 可能未规范化写入
        candidates = list({
            (device_id or '').strip(),
            (device_id or '').strip().upper(),
            normalize_device_id(device_id),
        })
        candidates = [c for c in candidates if c]

        limit = request.args.get('limit', '200')
        try:
            limit = int(limit)
        except Exception:
            limit = 200
        limit = max(1, min(limit, 500))

        page_lists = list(page_lists_collection.find(
            {'deviceId': {'$in': candidates}},
            {'_id': 0}
        ).sort('updatedAt', -1).limit(limit))
        
        for pl in page_lists:
            if hasattr(pl.get('createdAt'), 'isoformat'):
                pl['createdAt'] = pl['createdAt'].isoformat()
            if hasattr(pl.get('updatedAt'), 'isoformat'):
                pl['updatedAt'] = pl['updatedAt'].isoformat()
        
        return jsonify({'success': True, 'pageLists': page_lists})
    except Exception as e:
        print(f'❌ Error fetching page lists: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/page-lists/save', methods=['POST'])
@login_required
def save_page_list():
    """保存页面列表"""
    try:
        data = request.get_json()
        device_id = data.get('deviceId')
        list_id = data.get('listId')
        list_name = data.get('name', '默认页面列表')
        pages = data.get('pages', [])
        interval = data.get('interval', 60)
        is_active = data.get('isActive', False)
        
        if not device_id:
            return jsonify({'success': False, 'error': 'Missing deviceId'}), 400

        clean_id = normalize_device_id(device_id)

        user = getattr(request, 'user', None)
        if not ensure_device_owner(clean_id, user):
            return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403
        
        if page_lists_collection is None:
            return jsonify({'success': False, 'error': 'Database not connected'}), 500
        
        now = datetime.utcnow()
        
        if is_active:
            page_lists_collection.update_many(
                {'deviceId': clean_id},
                {'$set': {'isActive': False}}
            )
        
        if list_id:
            result = page_lists_collection.update_one(
                {'listId': list_id, 'deviceId': clean_id},
                {'$set': {
                    'name': list_name,
                    'pages': pages,
                    'interval': interval,
                    'isActive': is_active,
                    'updatedAt': now
                }}
            )
            if result.matched_count == 0:
                return jsonify({'success': False, 'error': 'Page list not found'}), 404
            
            print(f'✅ Page list updated: {list_id}')
        else:
            import uuid
            list_id = str(uuid.uuid4())[:8]
            
            page_list = {
                'listId': list_id,
                'deviceId': clean_id,
                'name': list_name,
                'pages': pages,
                'interval': interval,
                'isActive': is_active,
                'createdAt': now,
                'updatedAt': now
            }
            page_lists_collection.insert_one(page_list)
            print(f'✅ Page list created: {list_id}')
        
        return jsonify({
            'success': True, 
            'listId': list_id,
            'message': 'Page list saved'
        })
    except Exception as e:
        print(f'❌ Error saving page list: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/page-lists/<list_id>', methods=['DELETE'])
@login_required
def delete_page_list(list_id):
    """删除页面列表"""
    try:
        if page_lists_collection is None:
            return jsonify({'success': False, 'error': 'Database not connected'}), 500

        page_list = page_lists_collection.find_one({'listId': list_id})
        if not page_list:
            return jsonify({'success': False, 'error': 'Page list not found'}), 404

        user = getattr(request, 'user', None)
        device_id = page_list.get('deviceId')
        if device_id and not ensure_device_owner(device_id, user):
            return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403

        result = page_lists_collection.delete_one({'listId': list_id})
        
        print(f'✅ Page list deleted: {list_id}')
        return jsonify({'success': True, 'message': 'Page list deleted'})
    except Exception as e:
        print(f'❌ Error deleting page list: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

@app.route('/api/page-lists/active/<device_id>', methods=['GET'])
@login_required
def get_active_page_list(device_id):
    """获取设备当前激活的页面列表"""
    try:
        user = getattr(request, 'user', None)
        clean_id = normalize_device_id(device_id)
        if not ensure_device_owner(clean_id, user):
            return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403

        if page_lists_collection is None:
            return jsonify({'success': True, 'pageList': None})
        
        page_list = page_lists_collection.find_one(
            {'deviceId': clean_id, 'isActive': True},
            {'_id': 0}
        )
        
        if page_list:
            if hasattr(page_list.get('createdAt'), 'isoformat'):
                page_list['createdAt'] = page_list['createdAt'].isoformat()
            if hasattr(page_list.get('updatedAt'), 'isoformat'):
                page_list['updatedAt'] = page_list['updatedAt'].isoformat()
        
        return jsonify({'success': True, 'pageList': page_list})
    except Exception as e:
        print(f'❌ Error fetching active page list: {e}')
        return jsonify({'success': False, 'error': str(e)}), 500

# ==================== API: 模板 ====================

TEMPLATES = [
    {
        'templateId': 'clock',
        'name': '时钟',
        'icon': '🕐',
        'description': '显示当前时间和日期',
        'preview': '/templates/clock.png',
        'defaultData': {
            'type': 'template',
            'template': 'clock',
            'showDate': True,
            'showWeekday': True,
            'format24h': True
        }
    },
    {
        'templateId': 'weather',
        'name': '天气',
        'icon': '🌤️',
        'description': '显示天气信息',
        'preview': '/templates/weather.png',
        'defaultData': {
            'type': 'template',
            'template': 'weather',
            'city': '',
            'showForecast': True
        }
    },
    {
        'templateId': 'calendar',
        'name': '日历',
        'icon': '📅',
        'description': '显示日历和日程',
        'preview': '/templates/calendar.png',
        'defaultData': {
            'type': 'template',
            'template': 'calendar',
            'showEvents': True
        }
    },
    {
        'templateId': 'todo',
        'name': '待办事项',
        'icon': '✅',
        'description': '显示待办事项列表',
        'preview': '/templates/todo.png',
        'defaultData': {
            'type': 'template',
            'template': 'todo',
            'items': []
        }
    },
    {
        'templateId': 'quote',
        'name': '每日一言',
        'icon': '💬',
        'description': '显示励志名言或诗词',
        'preview': '/templates/quote.png',
        'defaultData': {
            'type': 'template',
            'template': 'quote',
            'category': 'motivational'
        }
    },
    {
        'templateId': 'qrcode',
        'name': '二维码',
        'icon': '📱',
        'description': '显示自定义二维码',
        'preview': '/templates/qrcode.png',
        'defaultData': {
            'type': 'template',
            'template': 'qrcode',
            'content': '',
            'title': ''
        }
    }
]

@app.route('/api/templates', methods=['GET'])
def get_templates():
    """获取所有可用模板"""
    return jsonify({'success': True, 'templates': TEMPLATES})

@app.route('/api/templates/<template_id>', methods=['GET'])
def get_template(template_id):
    """获取单个模板详情"""
    template = next((t for t in TEMPLATES if t['templateId'] == template_id), None)
    if not template:
        return jsonify({'success': False, 'error': 'Template not found'}), 404
    return jsonify({'success': True, 'template': template})

# ==================== API: EPD 控制（HTTP拉取架构） ====================

@app.route('/api/epd/init', methods=['POST'])
@login_required
def epd_init():
    """初始化 EPD（Deep-sleep架构下此接口仅用于记录，不直接控制设备）"""
    data = request.get_json()
    device_id = data.get('deviceId')
    epd_type = data.get('epdType')
    
    if not device_id or epd_type is None:
        return jsonify({'success': False, 'error': 'Missing deviceId or epdType'}), 400

    user = getattr(request, 'user', None)
    if not ensure_device_owner(device_id, user):
        return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403
    
    clean_id = normalize_device_id(device_id)
    print(f'📱 EPD init recorded for {clean_id}, type={epd_type}')
    return jsonify({'success': True, 'message': 'EPD init recorded (device will apply on next wake)'})

@app.route('/api/epd/load', methods=['POST'])
@login_required
def epd_load():
    """上传图片数据（持久化保存，设备下次唤醒时拉取）"""
    data = request.get_json()
    device_id = data.get('deviceId')
    image_data = data.get('data')
    content_meta = build_content_metadata(data.get('contentMode'), data.get('templateId'))
    
    if not device_id or not image_data:
        return jsonify({'success': False, 'error': 'Missing deviceId or data'}), 400

    user = getattr(request, 'user', None)
    if not ensure_device_owner(device_id, user):
        return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403
    
    clean_id = normalize_device_id(device_id)
    
    # 发布阶段就做完整性校验：长度/字符集不符合直接拒绝
    ok, err = validate_epd_text_payload(image_data)
    if not ok:
        print(f'❌ 发布数据校验失败: {clean_id} -> {err}')
        return jsonify({'success': False, 'error': f'Invalid EPD data: {err}'}), 400

    # 计算元数据（用于 status 提示 / 设备轻量校验）
    image_size_chars = len(image_data)
    image_size_bytes = len(image_data.encode('utf-8'))
    image_sha256 = hashlib.sha256(image_data.encode('utf-8')).hexdigest()

    # 持久化保存图片数据
    if not save_device_image(clean_id, image_data):
        return jsonify({'success': False, 'error': 'Failed to save image'}), 500
    
    # 更新图片版本号（递增）
    if devices_collection is not None:
        device = devices_collection.find_one({'deviceId': clean_id})
        current_version = device.get('imageVersion', 0) if device else 0
        new_version = current_version + 1
        now = datetime.utcnow()
        update_fields = {
            'imageVersion': new_version,
            'imageSizeChars': image_size_chars,
            'imageSizeBytes': image_size_bytes,
            'imageSha256': image_sha256,
            'activeContentMode': content_meta['activeContentMode'],
            'activeContentLabel': content_meta['activeContentLabel'],
            'sleepIntervalSeconds': content_meta['sleepIntervalSeconds'],
            'activeContentUpdatedAt': now,
            'updatedAt': now
        }
        update_doc = {'$set': update_fields}
        if content_meta['activeTemplateId']:
            update_fields['activeTemplateId'] = content_meta['activeTemplateId']
        else:
            update_doc['$unset'] = {'activeTemplateId': ''}

        result = devices_collection.update_one(
            {'deviceId': clean_id},
            update_doc
        )
        
        print(f'✅ 图片已保存: {clean_id}, 版本: {current_version} -> {new_version} '
              f'(matched={result.matched_count}, modified={result.modified_count})')
        print(f'   数据大小: {len(image_data)} 字符 ({len(image_data)/1024:.2f} KB)')
        print(f"   当前内容: {content_meta['activeContentLabel']}, "
              f"唤醒间隔: {content_meta['sleepIntervalSeconds']} 秒")
        print(f'   设备下次唤醒时将自动拉取更新')
        
        return jsonify({
            'success': True, 
            'message': 'Image saved, device will update on next wake',
            'imageVersion': new_version,
            'imageUrl': f'http://{Config.FLASK_HOST}:{Config.FLASK_PORT}/api/epd/raw/{clean_id}?v={new_version}',
            'imageSizeChars': image_size_chars,
            'imageSha256': image_sha256,
            'activeContentLabel': content_meta['activeContentLabel'],
            'sleepIntervalSeconds': content_meta['sleepIntervalSeconds']
        })
    
    return jsonify({'success': True, 'message': 'Image saved'})

@app.route('/api/epd/raw/<device_id>', methods=['GET'])
def epd_raw_download(device_id):
    """下载设备的原始图片数据（ESP32通过HTTP下载）
    
    返回 text/plain 格式的 a~p 编码字符串
    """
    clean_id = normalize_device_id(device_id)
    
    image_path = get_device_image_path(clean_id)
    if not image_path.exists():
        print(f'❌ 图片不存在: {clean_id}')
        return jsonify({'error': 'Image not found'}), 404

    data_size_bytes = image_path.stat().st_size
    print(f'📥 ESP32下载图片: {clean_id}')
    print(f'   文件大小: {data_size_bytes} 字节 ({data_size_bytes/1024:.2f} KB)')

    if data_size_bytes != EPD_EXPECTED_CHARS:
        print(f'⚠️  数据大小不匹配: 期望 {EPD_EXPECTED_CHARS}, 实际 {data_size_bytes}（磁盘文件可能异常）')

    # 用 send_file 直接流式发送文件，减少内存占用，并提供条件请求/ETag（更利于代理/断点续传扩展）
    resp = send_file(
        image_path,
        mimetype='text/plain',
        conditional=True,
        etag=True,
        max_age=0
    )
    resp.headers['Content-Type'] = 'text/plain; charset=utf-8'
    resp.headers['Cache-Control'] = 'no-cache, no-store, must-revalidate'
    resp.headers['Pragma'] = 'no-cache'
    resp.headers['Expires'] = '0'
    resp.headers['Accept-Ranges'] = 'bytes'
    resp.headers['X-EPD-Expected-Chars'] = str(EPD_EXPECTED_CHARS)

    # 如果 DB 里有 hash，就带上；没有也不强制（兼容旧数据）
    try:
        if devices_collection is not None:
            d = devices_collection.find_one({'deviceId': clean_id}, {'_id': 0, 'imageSha256': 1, 'imageSizeChars': 1})
            if d:
                if d.get('imageSha256'):
                    resp.headers['X-EPD-SHA256'] = d['imageSha256']
                if d.get('imageSizeChars') is not None:
                    resp.headers['X-EPD-Chars'] = str(d['imageSizeChars'])
    except Exception:
        pass

    return resp

@app.route('/api/epd/show', methods=['POST'])
@login_required
def epd_show():
    """触发设备显示（Deep-sleep架构下此接口仅用于记录）"""
    data = request.get_json()
    device_id = data.get('deviceId')
    
    if not device_id:
        return jsonify({'success': False, 'error': 'Missing deviceId'}), 400

    user = getattr(request, 'user', None)
    if not ensure_device_owner(device_id, user):
        return jsonify({'success': False, 'error': 'Device not found or no permission'}), 403
    
    clean_id = normalize_device_id(device_id)
    print(f'📺 Show command recorded for {clean_id} (device will display on next wake)')
    return jsonify({'success': True, 'message': 'Show command recorded (device will display on next wake)'})

# ==================== API: 自研6色算法处理 ====================

@app.route('/api/epd/process-sixcolor', methods=['POST'])
@login_required
def process_sixcolor():
    """使用6色算法处理图片（7.3寸E6屏）"""
    try:
        data = request.get_json()
        image_data = data.get('imageData')
        width = data.get('width', 800)
        height = data.get('height', 480)
        algorithm = data.get('algorithm', 'floyd_steinberg')
        grad_thresh = data.get('gradThresh', 40)
        
        if not image_data:
            return jsonify({'success': False, 'error': 'Missing imageData'}), 400
        
        if algorithm not in ['floyd_steinberg', 'gradient_blend', 'grayscale_color_map']:
            return jsonify({'success': False, 'error': f'Invalid algorithm: {algorithm}'}), 400
        
        result = process_e6_image_from_base64(
            image_data,
            width,
            height,
            algorithm=algorithm,
            grad_thresh=grad_thresh
        )
        return jsonify(result)
        
    except Exception as e:
        print(f'❌ 6色处理错误: {e}')
        import traceback
        traceback.print_exc()
        return jsonify({'success': False, 'error': str(e)}), 500

# ==================== 健康检查 ====================

@app.route('/api/health', methods=['GET'])
def health_check():
    """健康检查"""
    mongo_ok = mongo_client is not None
    
    return jsonify({
        'success': True,
        'status': 'healthy' if mongo_ok else 'degraded',
        'mongodb': 'connected' if mongo_ok else 'disconnected',
        'architecture': 'deep-sleep-http-pull',
        'mqtt': 'removed'  # 明确标注MQTT已移除
    })

# ==================== 启动服务器 ====================

def init_app():
    """初始化应用"""
    print('\n🚀 Starting ESP32 E-Paper Cloud Server...')
    print('📡 Architecture: Deep-sleep + HTTP Pull (No MQTT)')
    print(f'💾 MongoDB: {redact_uri_secret(Config.MONGODB_URI)} / db={Config.MONGODB_DB}')
    print(f'📁 Image Storage: {DATA_DIR}\n')
    
    connect_mongodb()

# 初始化
init_app()

if __name__ == '__main__':
    port = int(os.environ.get('PORT', 5000))
    print(f'\n🌐 API Server running on http://0.0.0.0:{port}\n')
    app.run(host='0.0.0.0', port=port, debug=Config.DEBUG)
