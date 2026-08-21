# cloud_server 部署说明

本目录包含云端服务：

- `backend/`：Flask API
- `frontend/`：Nginx 静态页面与 `/api/` 反向代理
- `service_admin_frontend/`：仅绑定服务器回环地址的最高权限服务管理网页
- `mongodb/`：MongoDB 容器运行数据目录（首次部署自动生成，不提交 Git）
- `docker-compose.yml`：容器编排
- `docker-compose.dev.yml`：仅本地开发使用的前端源码挂载覆盖
- `.env`：部署环境变量（本地私有，不提交）
- `.env.example`：环境变量示例

推荐生产访问方式：

- 浏览器访问：`https://epd.example.com/`
- ESP32 访问：`https://epd.example.com/api/...`
- 宿主机 TLS 反向代理把 `443` 转发到 `127.0.0.1:8080`
- 后端 Flask 仅在 Docker 内网暴露 `5000`
- MongoDB 仅在 Docker 内网暴露 `27017`

直接公开 `http://服务器IP:8080` 只用于受信内网或迁移期兼容。HTTP 下登录令牌和 `X-Device-Key` 都可能被链路窃取，不能视为安全生产配置。

## 1. 准备环境

服务器要求：

- Linux
- 已安装 Docker 和 Docker Compose Plugin
- 已准备域名、有效 TLS 证书和宿主机反向代理
- 服务器安全组 / 防火墙已放行 `443/tcp`

推荐先放行端口：

```bash
sudo ufw allow 443/tcp
sudo ufw enable
```

## 2. 配置环境变量

在 `cloud_server/` 目录执行：

```bash
cp .env.example .env
```

然后编辑 `.env`：

```env
FRONTEND_BIND=127.0.0.1
FRONTEND_PORT=8080
MONGO_INITDB_ROOT_USERNAME=esp32_epd_root
MONGO_INITDB_ROOT_PASSWORD=change_this_mongo_password
MONGODB_DB=esp32_epd
FLASK_HOST=epd.example.com
FLASK_PORT=8080
PUBLIC_BASE_URL=https://epd.example.com
SECRET_KEY=<random-secret>
ALLOW_REGISTRATION=true
AUTH_TOKEN_TTL_SECONDS=604800
SERVICE_ADMIN_TOKEN_TTL_SECONDS=14400
SERVICE_ADMIN_PORT=18081
CORS_ORIGINS=
DEVICE_AUTH_REQUIRED=true
PAIRING_MAX_FAILED_ATTEMPTS=8
PAIRING_LOCK_SECONDS=900
DEVICE_STATUS_MAX_BODY_BYTES=4096
DEVICE_KEY_RESET_WINDOW_SECONDS=300
UNCLAIMED_DEVICE_TTL_SECONDS=172800
```

说明：

- `PUBLIC_BASE_URL` 是后端发给设备的绝对下载 origin，不得包含路径、查询或账号密码
- `PUBLIC_BASE_URL`、固件的云端 host/port/HTTPS 开关、外部 TLS 反向代理必须指向同一个公开 origin
- `FLASK_HOST/FLASK_PORT` 仅作为旧配置回退保留
- `SECRET_KEY` 不要使用示例值，改成随机长字符串
- `ALLOW_REGISTRATION=true` 支持多用户注册；设为 `false` 时仅允许在没有任何账号时创建首个账号，之后会关闭公网注册
- 同源部署保持 `CORS_ORIGINS` 为空；跨域时仅列出精确的 HTTPS origin
- 生产环境保持 `DEVICE_AUTH_REQUIRED=true`
- `UNCLAIMED_DEVICE_TTL_SECONDS` 只清理从未绑定过且长期不再唤醒的 TOFU 身份；每次上报会续期，曾绑定设备的密钥哈希不受影响
- `MONGO_INITDB_ROOT_PASSWORD` 会用于初始化本机 MongoDB 容器；建议只使用字母、数字、下划线，避免 URL 编码问题
- `.env` 已加入 `.gitignore`，不要提交真实凭据

升级已有部署时不要用 `.env.example` 覆盖现有 `.env`，尤其不能改变已初始化 MongoDB 的账号密码。请在原 `.env` 中补入 `FRONTEND_BIND`、`PUBLIC_BASE_URL`、认证/配对限制和设备凭据重置窗口等新字段，再执行重建。

### 已有设备的认证迁移顺序

旧固件不会发送 `X-Device-Key`。直接以默认的 `DEVICE_AUTH_REQUIRED=true` 上线会让尚未升级的设备收到 401，因此已有部署按下面顺序迁移：

1. 在原 `.env` 补齐新字段，但临时设置 `DEVICE_AUTH_REQUIRED=false`。若旧固件仍固定访问公网 `http://服务器IP:8080`，迁移期只能暂时保留 `FRONTEND_BIND=0.0.0.0` 和该端口；应通过安全组、VPN 或来源 IP 白名单尽量缩小暴露面。
2. 同时部署新版 backend 与 frontend；这个兼容开关只允许“尚无密钥哈希”的旧设备继续访问，已有哈希的设备仍必须提供正确密钥。
3. 逐台刷入已配置域名、443、HTTPS 和根 CA 的新版固件，并唤醒至少一次。设备会生成 NVS `devKey`，服务端以 TOFU 方式只保存其哈希。通过设备页最后唤醒时间和 backend 日志确认不再出现 401。
4. 所有在用设备完成登记后，把 `.env` 改回 `DEVICE_AUTH_REQUIRED=true`、`FRONTEND_BIND=127.0.0.1`，重建容器并关闭公网 8080，只保留 443：`docker compose up -d --force-recreate backend frontend`。

兼容期应尽量短，并在受信网络或已完成 TLS 的入口上进行。若某台设备服务端已有 `deviceKeyHash`、但设备 NVS 已被擦除，`DEVICE_AUTH_REQUIRED=false` 也不会绕过旧哈希；必须使用第 7 节的所有者授权重置流程。

## 3. 运行数据目录

以下目录不是源码，首次 `docker compose up -d` 时会自动创建：

```text
mongodb/data/      # MongoDB 数据库文件
mongodb/restore/   # 预留的数据库恢复目录
backend/data/      # 设备最新墨水屏图片 latest.txt
```

更新代码或同步服务器文件时必须保留这些目录。不要把 `mongodb/` 或 `backend/data/` 提交到 Git，也不要用整目录覆盖的方式删除它们。

## 4. 部署命令

首次部署或更新代码后，在仓库根目录执行：

```bash
cd cloud_server
cp .env.example .env
# 编辑 .env

docker compose build --no-cache
docker compose up -d --force-recreate
docker compose ps
```

生产 compose 不再把 `./frontend` 挂进容器，避免宿主机残缺文件覆盖镜像。前端代码更新后也必须重新构建镜像。仅本地开发需要热加载时使用：

```bash
docker compose -f docker-compose.yml -f docker-compose.dev.yml up -d --build
```

### 仅更新用户前端

只改 `frontend/`（页面、样式、脚本或 `favicon.ico`）时，不必重启后端或 MongoDB：

```bash
cd cloud_server
docker compose build frontend
docker compose up -d --force-recreate --no-deps frontend
docker compose ps frontend
curl --fail http://127.0.0.1:${FRONTEND_PORT:-8080}/api/health
```

部署前请备份待替换的前端文件；不要同步或删除 `mongodb/`、`backend/data/`、`.env`。后端接口、数据库索引或依赖有变更时，仍需按完整部署流程重建相应服务。

查看日志：

```bash
docker compose logs -f backend
docker compose logs -f frontend
```

## 服务管理控制台（仅 SSH 隧道）

服务管理网页运行在独立的 `service-admin-frontend` 容器中，只绑定服务器
`127.0.0.1:${SERVICE_ADMIN_PORT:-18081}`。公网用户前端明确拒绝
`/api/service-admin/*`，普通用户令牌与服务管理员令牌不能互换。

首次部署后，在服务器 SSH 终端交互式创建管理员（密码不会进入命令历史或日志）：

```bash
cd /opt/cloud_server
docker compose exec backend python service_admin_cli.py create
```

账号维护命令：

```bash
docker compose exec backend python service_admin_cli.py list
docker compose exec backend python service_admin_cli.py reset-password
docker compose exec backend python service_admin_cli.py disable
docker compose exec backend python service_admin_cli.py enable
```

在管理电脑建立隧道后访问控制台：

```bash
ssh -L 18080:127.0.0.1:18081 root@8.135.238.216
```

浏览器打开 `http://127.0.0.1:18080/service-admin.html`。管理员会话保存在
`sessionStorage`，固定有效 4 小时，退出或关闭浏览器后本地会话清除。

停止服务：

```bash
docker compose down
```

## 5. 访问与验证

TLS 反向代理配置完成后：

```text
https://epd.example.com/
```

### 会议牌用户界面

普通用户登录后只会看到自己名下的数据，左侧导航依次为：

- **设备**：添加、查看和编辑设备。按住任意设备列表条可拖动排序；松开后前端会提交该账号完整的设备 ID 顺序。搜索状态下不可排序，清除搜索条件后再操作。
- **模板设计**：编辑会议名牌模板。初次进入只读取模板摘要；点击已保存模板时才读取完整配置（包括图片背景、Logo 和画布元素），并显示加载进度，减少模板列表首次打开的传输量。
- **名单下发**：导入或整理名单，选择模板和目标设备后下发会议名牌。
- **设置**：切换主题、查看当前登录用户名，以及退出登录。

单台设备屏幕编辑从“设备”页进入，只作用于该设备；模板设计页用于保存可复用模板；批量下发在“名单下发”页完成。浏览器标签图标由 `frontend/favicon.ico` 提供。

### 设备排序与模板接口

所有接口都要求普通用户登录，并以令牌对应的 `username` 作为数据隔离边界：

- `POST /api/devices/reorder`：请求体为 `{"deviceIds": ["…"]}`。必须一次提交当前账号全部且不重复的设备 ID；若设备列表在操作期间发生变化，接口返回 `409`，前端应重新加载后再试。
- `GET /api/nameplate/templates`：只返回模板列表所需的摘要信息，不返回背景、Logo 等大字段。
- `GET /api/nameplate/templates/<templateId>`：读取当前账号的单个完整模板；`POST /api/nameplate/templates` 新建或更新模板，`DELETE /api/nameplate/templates/<templateId>` 删除模板。

建议验证：

1. 浏览器能打开登录页
2. `docker compose ps` 中 `frontend`、`backend`、`mongodb` 都为运行状态
3. 后端健康检查可访问：

```bash
curl --fail https://epd.example.com/api/health
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

## 6. TLS 与端口设计

- `frontend`：默认由 `.env` 绑定 `127.0.0.1:8080` -> 容器 `80`
- 宿主机 TLS 反向代理：公开 `443` -> `http://127.0.0.1:8080`
- `backend`：仅 Docker 内网 `5000`
- `mongodb`：仅 Docker 内网 `27017`
- Nginx 负责把 `/api/` 代理到 `backend:5000`

这意味着：

- 外部用户不需要直接访问 `5000`
- 外部用户不需要直接访问 `27017`
- 浏览器和 ESP32 都统一走公开 HTTPS origin
- 对外只开放 `443`；`8080` 保持回环监听

宿主机 Nginx 可使用下面的最小转发结构，证书路径按实际环境填写：

```nginx
server {
    listen 443 ssl;
    server_name epd.example.com;

    ssl_certificate /etc/letsencrypt/live/epd.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/epd.example.com/privkey.pem;
    client_max_body_size 25M;

    location / {
        proxy_pass http://127.0.0.1:8080;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        # 此入口直接面向公网时覆盖客户端伪造的 X-Forwarded-For；若前面
        # 还有受信 CDN，请先配置 real_ip 模块，再按实际代理链传递。
        proxy_set_header X-Forwarded-For $remote_addr;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_connect_timeout 15s;
        proxy_send_timeout 120s;
        proxy_read_timeout 120s;
    }
}
```

固件需同步设置 `CLOUD_API_USE_HTTPS=1`、公开域名、端口 `443`，并把签发服务器证书的根 CA PEM 配到 `CLOUD_API_ROOT_CA_PEM`。启用 HTTPS 但 CA 为空时固件会拒绝连接，不会自动降级。

## 7. 首个账号与设备凭据恢复

首次部署时，通过已验证证书的 HTTPS 登录页创建账号，切勿在 HTTP 页面提交。面向多用户平台保持 `ALLOW_REGISTRATION=true`；只有需要单账号内测时才设为 `false`。

设备会把随机 `deviceKey` 保存在 NVS，服务端只保存其哈希。若设备擦除了 NVS，旧哈希会让新密钥持续收到 401。安全恢复步骤是：

1. 不要先删除或解绑设备；这两种操作都不会清除 `deviceKeyHash`。
2. 以设备所有者登录，在设备页选中设备，点击“重置设备凭据”。
3. 按提示输入完整设备编号，开启短时重置窗口（默认 300 秒）。
4. 在窗口到期前让已擦除 NVS 的设备联网一次，服务端才会接受并保存新密钥哈希。

该操作只应在实体设备已由你控制时使用。窗口到期后，未知新密钥仍会被拒绝。

## 8. 常用维护命令

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

查看 MongoDB 最近日志：

```bash
docker compose logs --tail=200 mongodb
```

## 9. 常见问题

### 无法打开网页

- 检查安全组和防火墙是否放行 `443`
- 检查 TLS 证书、域名解析和宿主机反向代理
- 检查 `docker compose ps` 是否显示 `frontend` 正常运行

### 页面能打开，但接口报错

- 看后端日志：`docker compose logs -f backend`
- 检查 `.env` 中 MongoDB 用户名/密码是否和 `mongodb/data` 初次初始化时一致
- 如果改过 MongoDB 初始化密码，旧的 `mongodb/data` 不会自动改密码；需要恢复旧密码或迁移数据库

### ESP32 无法拉取图片

- 确认固件 `CLOUD_API_HOST`、`CLOUD_API_PORT`、`CLOUD_API_USE_HTTPS` 与 `PUBLIC_BASE_URL` 一致
- HTTPS 模式确认 `CLOUD_API_ROOT_CA_PEM` 是正确根 CA，而不是留空或服务器叶证书
- 确认 `.env` 中 `PUBLIC_BASE_URL` 与证书域名一致
- 在服务器上先测试：`curl --fail https://epd.example.com/api/health`
