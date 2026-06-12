FROM python:3.11-slim

WORKDIR /app

ENV PYTHONDONTWRITEBYTECODE=1
ENV PYTHONUNBUFFERED=1

# 升级 pip 并使用阿里云镜像源（适合阿里云服务器）
COPY requirements.txt .
RUN pip install --upgrade pip && \
    pip install --no-cache-dir -r requirements.txt -i https://mirrors.aliyun.com/pypi/simple/

COPY app.py config.py six_color_epd.py template_renderer.py ./

EXPOSE 5000

CMD ["python", "app.py"]

