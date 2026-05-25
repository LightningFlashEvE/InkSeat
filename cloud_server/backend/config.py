import os

class Config:
    """Flask 配置
    
    Deep-sleep + HTTP Pull 架构（无MQTT）
    """
    
    # Flask
    SECRET_KEY = os.environ.get('SECRET_KEY', 'esp32-epd-secret-key')
    DEBUG = os.environ.get('FLASK_DEBUG', 'false').lower() == 'true'
    
    # MongoDB
    MONGODB_URI = os.environ.get('MONGODB_URI', 'mongodb://esp32_epd_root:change_this_mongo_password@mongodb:27017/esp32_epd?authSource=admin')
    MONGODB_DB = os.environ.get('MONGODB_DB', 'esp32_epd')
    
    # Flask (用于构建下载URL)
    FLASK_HOST = os.environ.get('FLASK_HOST', '127.0.0.1')
    FLASK_PORT = int(os.environ.get('FLASK_PORT', 8080))
    
    # 注意：MQTT配置已移除，本架构使用HTTP拉取模式
    # 设备通过HTTP轮询获取更新，不需要MQTT常连接
