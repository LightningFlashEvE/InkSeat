#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Create or reuse all MongoDB indexes required by the backend."""

import os

from pymongo import MongoClient

from db_indexes import ensure_all_indexes


MONGODB_URI = os.environ.get(
    'MONGODB_URI',
    'mongodb://esp32_epd_root:change_this_mongo_password@mongodb:27017/esp32_epd?authSource=admin',
)
MONGODB_DB = os.environ.get('MONGODB_DB', 'esp32_epd')


def create_indexes():
    client = None
    try:
        client = MongoClient(MONGODB_URI, serverSelectionTimeoutMS=5000)
        db = client[MONGODB_DB]
        client.admin.command('ping')

        print(f'📊 连接到数据库: {MONGODB_DB}')
        indexes = ensure_all_indexes(db)
        for collection_name, names in indexes.items():
            print(f'✅ {collection_name}: {", ".join(names)}')
        return True
    except Exception as exc:
        print(f'❌ 创建索引失败: {exc}')
        return False
    finally:
        if client is not None:
            client.close()


if __name__ == '__main__':
    print('🚀 开始创建 MongoDB 索引...\n')
    raise SystemExit(0 if create_indexes() else 1)
