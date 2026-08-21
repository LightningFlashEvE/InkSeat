FROM nginx:alpine

WORKDIR /usr/share/nginx/html
RUN rm -rf ./* && mkdir -p /etc/nginx/snippets

COPY service_admin_frontend/service-admin.html ./
COPY service_admin_frontend/service-admin.css ./
COPY service_admin_frontend/service-admin.js ./
COPY frontend/theme.js ./theme.js
COPY frontend/security_headers.conf /etc/nginx/snippets/security_headers.conf
COPY service_admin_frontend/nginx.conf /etc/nginx/conf.d/default.conf

EXPOSE 80

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD wget -q -O /dev/null http://127.0.0.1/service-admin.html || exit 1
