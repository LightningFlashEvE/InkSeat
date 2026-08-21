#!/usr/bin/env python3
"""Interactive SSH-only lifecycle tool for service administrator accounts."""

import argparse
import getpass
import re
import sys
from datetime import datetime, timezone

from pymongo import MongoClient
from pymongo.errors import DuplicateKeyError
from werkzeug.security import generate_password_hash

from config import Config
from db_indexes import ensure_all_indexes


USERNAME_PATTERN = re.compile(r'^[A-Za-z0-9_.-]{3,64}$')


def utcnow():
    return datetime.now(timezone.utc).replace(tzinfo=None)


def prompt_username():
    username = input('服务管理员用户名: ').strip()
    if not USERNAME_PATTERN.fullmatch(username):
        raise ValueError('用户名需为 3–64 位，仅可包含字母、数字、点、下划线和连字符')
    return username


def prompt_password():
    password = getpass.getpass('密码（至少 12 位）: ')
    confirmation = getpass.getpass('再次输入密码: ')
    if len(password) < 12 or len(password) > 256:
        raise ValueError('密码长度需为 12–256 位')
    if password != confirmation:
        raise ValueError('两次输入的密码不一致')
    return password


def write_audit(collection, action, username):
    collection.insert_one({
        'action': action,
        'adminUsername': 'ssh-cli',
        'targetType': 'service_admin',
        'targetId': username,
        'remoteIp': '',
        'createdAt': utcnow(),
    })


def main():
    parser = argparse.ArgumentParser(description='服务管理员账号维护工具')
    parser.add_argument('command', choices=('create', 'reset-password', 'disable', 'enable', 'list'))
    args = parser.parse_args()

    client = MongoClient(Config.MONGODB_URI, serverSelectionTimeoutMS=5000)
    db = client[Config.MONGODB_DB]
    client.admin.command('ping')
    ensure_all_indexes(db)
    admins = db['service_admins']
    audit = db['service_admin_audit']

    if args.command == 'list':
        rows = list(admins.find({}, {'_id': 0, 'username': 1, 'disabled': 1, 'lastLoginAt': 1}).sort('username', 1))
        if not rows:
            print('尚未创建服务管理员账号。')
        for row in rows:
            state = '已停用' if row.get('disabled') else '启用'
            last_login = row.get('lastLoginAt') or '从未登录'
            print(f"{row.get('username')}\t{state}\t最近登录: {last_login}")
        return 0

    username = prompt_username()
    existing = admins.find_one({'username': username})

    if args.command == 'create':
        if existing:
            raise ValueError('该服务管理员已存在')
        password = prompt_password()
        try:
            admins.insert_one({
                'username': username,
                'passwordHash': generate_password_hash(password, method='scrypt'),
                'disabled': False,
                'createdAt': utcnow(),
            })
        except DuplicateKeyError as exc:
            raise ValueError('该服务管理员已存在') from exc
        write_audit(audit, 'service_admin_created', username)
        print(f'已创建服务管理员: {username}')
        return 0

    if not existing:
        raise ValueError('未找到该服务管理员')

    if args.command == 'reset-password':
        password = prompt_password()
        admins.update_one(
            {'_id': existing['_id']},
            {
                '$set': {'passwordHash': generate_password_hash(password, method='scrypt'), 'passwordChangedAt': utcnow()},
                '$unset': {'token': '', 'tokenHash': '', 'tokenExpiresAt': ''},
            },
        )
        write_audit(audit, 'service_admin_password_changed', username)
        print(f'已修改密码并撤销现有会话: {username}')
        return 0

    disabled = args.command == 'disable'
    admins.update_one(
        {'_id': existing['_id']},
        {
            '$set': {'disabled': disabled, 'statusChangedAt': utcnow()},
            '$unset': {'token': '', 'tokenHash': '', 'tokenExpiresAt': ''},
        },
    )
    write_audit(audit, f'service_admin_{args.command}d', username)
    print(f"已{'停用' if disabled else '启用'}服务管理员: {username}")
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, KeyboardInterrupt) as exc:
        message = str(exc) or '操作已取消'
        print(f'错误: {message}', file=sys.stderr)
        raise SystemExit(2)
