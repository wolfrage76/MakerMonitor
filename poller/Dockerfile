# MakerMonitor poller. config.yaml and data/ live in the /config volume.
FROM python:3.12-slim

ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1
RUN apt-get update && apt-get install -y --no-install-recommends tzdata && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY makermonitor ./makermonitor
COPY config.example.yaml ./
RUN chmod -R a+rX /app

RUN useradd --uid 1000 --create-home makermonitor && mkdir /config && chown makermonitor /config
USER makermonitor
# Relative paths in config.yaml (data_dir: "data") resolve inside /config.
WORKDIR /config
VOLUME /config
EXPOSE 8787
HEALTHCHECK --interval=60s --timeout=5s --start-period=20s \
  CMD python -c "import urllib.request,sys; urllib.request.urlopen('http://127.0.0.1:8787/health', timeout=4)" || exit 1
ENV PYTHONPATH=/app
CMD ["python", "-m", "makermonitor", "-c", "/config/config.yaml"]
