FROM nginx:alpine

WORKDIR /usr/share/nginx/html

RUN rm -rf ./*

COPY index.html ./
COPY nameplates.html ./
COPY settings.html ./
COPY login.html ./
COPY change-password.html ./
COPY control.html ./
COPY favicon.ico ./
COPY favicon.svg ./
COPY app.js ./
COPY devices.js ./
COPY nameplates.js ./
COPY editor.js ./
COPY auth.js ./
COPY theme.js ./
COPY ui.css ./
COPY assets ./assets
COPY vendor ./vendor
# 使用 nginx 默认主配置，只在 http 块中追加我们的 server 配置
RUN mkdir -p /etc/nginx/snippets
COPY security_headers.conf /etc/nginx/snippets/security_headers.conf
COPY nginx.conf /etc/nginx/conf.d/default.conf

EXPOSE 80

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD wget -q -O /dev/null http://127.0.0.1/ || exit 1

