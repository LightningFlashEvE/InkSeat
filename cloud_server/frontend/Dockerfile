FROM nginx:alpine

WORKDIR /usr/share/nginx/html

RUN rm -rf ./*

COPY index.html ./
COPY nameplates.html ./
COPY settings.html ./
COPY login.html ./
COPY control.html ./
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
COPY nginx.conf /etc/nginx/conf.d/default.conf

EXPOSE 80

