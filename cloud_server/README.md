# cloud_server 部署说明

本目录包含云端服务：

- `backend/`：Flask API
- `frontend/`：Nginx 静态页面与 `/api/` 反向代理
- `docker-compose.yml`：容器编排
- `.env`：部署环境变量（本地私有，不提交）
- `.env.example`：环境变量示例

当前访问方式：

- 浏览器访问：`http://你的服务器IP:8080/`
- ESP32 访问：`http://你的服务器IP:8080/api/...`
- 后端 Flask 仅在 Docker 内网暴露 `5000`

## 1. 准备环境

服务器要求：

- Linux
- 已安装 Docker 和 Docker Compose Plugin
- 服务器安全组 / 防火墙已放行 `8080/tcp`

推荐先放行端口：

```bash
sudo ufw allow 8080/tcp
sudo ufw enable
```

## 2. 配置环境变量

在 `cloud_server/` 目录执行：

```bash
cp .env.example .env
```

然后编辑 `.env`：

```env
MONGODB_URI=mongodb://<user>:<password>@<host>:27017/esp32_epd?authSource=admin
MONGODB_DB=esp32_epd
FLASK_HOST=<public-ip-or-domain>
FLASK_PORT=8080
SECRET_KEY=change-this-to-a-random-secret
```

说明：

- `FLASK_HOST` 填公网 IP 或域名，例如 `8.135.238.216`
- `FLASK_PORT` 固定为 `8080`
- `SECRET_KEY` 不要使用示例值，改成随机长字符串
- `.env` 已加入 `.gitignore`，不要提交真实凭据

## 3. 部署命令

首次部署或更新代码后，在仓库根目录执行：

```bash
cd cloud_server
cp .env.example .env
# 编辑 .env

docker compose build --no-cache
docker compose up -d --force-recreate
docker compose ps
```

查看日志：

```bash
docker compose logs -f backend
docker compose logs -f frontend
```

停止服务：

```bash
docker compose down
```

## 4. 访问与验证

部署完成后：

```text
http://你的服务器IP:8080/
```

建议验证：

1. 浏览器能打开登录页
2. `docker compose ps` 中两个容器都为运行状态
3. 后端健康检查可访问：

```bash
curl http://你的服务器IP:8080/api/health
```

期望返回类似：

```json
{
  "success": true,
  "status": "healthy",
  "mongodb": "connected",
  "architecture": "deep-sleep-http-pull",
  "mqtt": "removed"
}
```

## 5. 当前端口设计

- `frontend`：宿主机 `8080` -> 容器 `80`
- `backend`：仅 Docker 内网 `5000`
- Nginx 负责把 `/api/` 代理到 `backend:5000`

这意味着：

- 外部用户不需要直接访问 `5000`
- 浏览器和 ESP32 都统一走 `8080`
- 对外只开放一个业务端口，部署更简单

## 6. 常用维护命令

重建并重启：

```bash
docker compose build --no-cache && docker compose up -d --force-recreate
```

仅重启：

```bash
docker compose restart
```

查看后端最近日志：

```bash
docker compose logs --tail=200 backend
```

查看前端最近日志：

```bash
docker compose logs --tail=200 frontend
```

## 7. 常见问题

### 无法打开网页

- 检查阿里云安全组是否放行 `8080`
- 检查服务器防火墙是否放行 `8080`
- 检查 `docker compose ps` 是否显示 `frontend` 正常运行

### 页面能打开，但接口报错

- 看后端日志：`docker compose logs -f backend`
- 检查 `.env` 中 `MONGODB_URI` 是否正确
- 检查 MongoDB 是否允许当前服务器访问

### ESP32 无法拉取图片

- 确认固件里的 `CLOUD_API_HOST` 指向公网 IP/域名
- 确认固件里的 `CLOUD_API_PORT` 为 `8080`
- 确认 `.env` 中 `FLASK_HOST` 与实际公网地址一致
- 在服务器上先测试：`curl http://你的服务器IP:8080/api/health`
