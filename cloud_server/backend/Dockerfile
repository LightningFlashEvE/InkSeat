FROM python:3.11-slim

WORKDIR /app

ENV PYTHONDONTWRITEBYTECODE=1
ENV PYTHONUNBUFFERED=1

# 安装中文和常用符号字体，避免模板在 Docker 环境中退回默认小字体或缺字。
# 阿里云服务器访问 Debian 默认源偶发很慢，构建时切到阿里云镜像源。
RUN set -eux; \
    if [ -f /etc/apt/sources.list.d/debian.sources ]; then \
        sed -i 's|http://deb.debian.org/debian|http://mirrors.aliyun.com/debian|g; s|http://security.debian.org/debian-security|http://mirrors.aliyun.com/debian-security|g' /etc/apt/sources.list.d/debian.sources; \
    elif [ -f /etc/apt/sources.list ]; then \
        sed -i 's|http://deb.debian.org/debian|http://mirrors.aliyun.com/debian|g; s|http://security.debian.org/debian-security|http://mirrors.aliyun.com/debian-security|g' /etc/apt/sources.list; \
    fi; \
    apt-get -o Acquire::Retries=3 update && \
    apt-get install -y --no-install-recommends fonts-noto-cjk fonts-noto-core && \
    rm -rf /var/lib/apt/lists/*

# 升级 pip 并使用阿里云镜像源（适合阿里云服务器）
COPY requirements.txt .
RUN pip install --upgrade pip && \
    pip install --no-cache-dir -r requirements.txt -i https://mirrors.aliyun.com/pypi/simple/

COPY app.py config.py db_indexes.py service_admin_cli.py six_color_epd.py template_renderer.py ./
COPY assets ./assets

EXPOSE 5000

HEALTHCHECK --interval=30s --timeout=5s --start-period=90s --retries=3 \
    CMD python -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:5000/api/health', timeout=4).read()" || exit 1

# One threaded worker avoids running database/bootstrap initialization once per
# process while still serving concurrent sleeping devices and browser requests.
CMD ["gunicorn", "--bind=0.0.0.0:5000", "--workers=1", "--threads=4", "--timeout=120", "--graceful-timeout=30", "--keep-alive=5", "--max-requests=1000", "--max-requests-jitter=100", "--access-logfile=-", "--error-logfile=-", "app:app"]
