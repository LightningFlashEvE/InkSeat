#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
MongoDB索引创建脚本
用于创建设备绑定相关的索引
"""

from pymongo import MongoClient
from datetime import datetime, timedelta
import os

# 从环境变量或config读取配置
MONGODB_URI = os.environ.get('MONGODB_URI', 'mongodb://esp32_epd_root:change_this_mongo_password@mongodb:27017/esp32_epd?authSource=admin')
MONGODB_DB = os.environ.get('MONGODB_DB', 'esp32_epd')

def create_indexes():
    """创建所有必要的索引"""
    try:
        client = MongoClient(MONGODB_URI, serverSelectionTimeoutMS=5000)
        db = client[MONGODB_DB]
        
        print(f'📊 连接到数据库: {MONGODB_DB}')
        
        # 1. devices集合索引
        devices_collection = db['devices']
        
        # 设备ID唯一索引
        devices_collection.create_index('deviceId', unique=True, name='deviceId_unique')
        print('✅ 创建索引: devices.deviceId (unique)')
        
        # 所有者索引
        devices_collection.create_index('owner', name='owner_idx')
        print('✅ 创建索引: devices.owner')
        
        # 绑定状态索引
        devices_collection.create_index('claimed', name='claimed_idx')
        print('✅ 创建索引: devices.claimed')
        
        # 2. pairing_codes集合索引
        pairing_codes_collection = db['pairing_codes']
        
        # 设备ID唯一索引
        pairing_codes_collection.create_index('deviceId', unique=True, name='pairing_deviceId_unique')
        print('✅ 创建索引: pairing_codes.deviceId (unique)')
        
        # TTL索引：expiresAt字段，0秒后自动删除文档
        pairing_codes_collection.create_index(
            'expiresAt',
            expireAfterSeconds=0,
            name='expiresAt_ttl'
        )
        print('✅ 创建TTL索引: pairing_codes.expiresAt (自动过期)')
        
        # 3. 其他集合索引（如果不存在）
        users_collection = db['users']
        if 'username_unique' not in [idx['name'] for idx in users_collection.list_indexes()]:
            users_collection.create_index('username', unique=True, name='username_unique')
            print('✅ 创建索引: users.username (unique)')
        
        device_status_collection = db['device_status']
        if 'deviceId_unique' not in [idx['name'] for idx in device_status_collection.list_indexes()]:
            device_status_collection.create_index('deviceId', unique=True, name='deviceId_unique')
            print('✅ 创建索引: device_status.deviceId (unique)')
        
        print('\n✅ 所有索引创建完成！')
        
        # 显示所有索引
        print('\n📋 当前索引列表:')
        print('\n--- devices ---')
        for idx in devices_collection.list_indexes():
            print(f'  {idx["name"]}: {idx.get("key", {})}')
        
        print('\n--- pairing_codes ---')
        for idx in pairing_codes_collection.list_indexes():
            print(f'  {idx["name"]}: {idx.get("key", {})}')
            if 'expireAfterSeconds' in idx:
                print(f'    TTL: {idx["expireAfterSeconds"]}秒')
        
        client.close()
        return True
        
    except Exception as e:
        print(f'❌ 创建索引失败: {e}')
        import traceback
        traceback.print_exc()
        return False

if __name__ == '__main__':
    print('🚀 开始创建MongoDB索引...\n')
    success = create_indexes()
    if success:
        print('\n✅ 索引创建成功！')
    else:
        print('\n❌ 索引创建失败！')
        exit(1)
