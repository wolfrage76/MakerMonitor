# MakerMonitor poller. config.yaml and data/ live in the /config volume.
FROM python:3.12-slim

ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1
# libnss-mdns resolves .local panel names through the host's Avahi daemon
# (its socket is mounted by docker-compose.yml).
RUN apt-get update && apt-get install -y --no-install-recommends tzdata libnss-mdns && rm -rf /var/lib/apt/lists/* \
 && sed -i 's/^hosts:.*/hosts: files mdns4_minimal [NOTFOUND=return] dns/' /etc/nsswitch.conf
WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY makermonitor ./makermonitor
COPY config.example.yaml docker-entrypoint.sh ./
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
ENTRYPOINT ["/app/docker-entrypoint.sh"]
CMD ["python", "-m", "makermonitor", "-c", "/config/config.yaml"]
