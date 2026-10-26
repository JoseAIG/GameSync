# GameSync server: imagen de despliegue (fork JoseAIG/GameSync).
# Contexto de build: raíz del repo (el servidor importa ../shared).
#   docker compose -f deploy/compose.yaml build
FROM python:3.12-slim AS base

COPY --from=ghcr.io/astral-sh/uv:0.9 /uv /usr/local/bin/uv

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    UV_COMPILE_BYTECODE=1 \
    UV_LINK_MODE=copy \
    UV_PYTHON_DOWNLOADS=never \
    UV_PROJECT_ENVIRONMENT=/opt/venv

WORKDIR /app/server

# Dependencias primero (capa cacheable), fijadas por uv.lock.
COPY server/pyproject.toml server/uv.lock ./
RUN uv sync --frozen --no-dev --no-install-project

# Código. Solo lo que el servidor necesita en tiempo de ejecución.
COPY shared/ /app/shared/
COPY server/app/ ./app/
COPY server/data/ ./data/
COPY server/third_party/ ./third_party/

# Usuario sin privilegios; /data es el único volumen escribible.
RUN useradd --system --uid 10001 --home-dir /nonexistent --shell /usr/sbin/nologin gamesync \
    && mkdir -p /data/saves \
    && chown -R 10001:10001 /data

USER 10001:10001
ENV PATH=/opt/venv/bin:$PATH \
    SYNC_SAVE_DIR=/data/saves \
    SYNC_TMP_DIR=/tmp \
    SYNC_HOST=0.0.0.0 \
    SYNC_PORT=8000

EXPOSE 8000

HEALTHCHECK --interval=60s --timeout=5s --start-period=20s --retries=3 \
    CMD python -c "import urllib.request,sys; sys.exit(0 if urllib.request.urlopen('http://127.0.0.1:8000/api/v1/status', timeout=4).status==200 else 1)"

# Sin --reload y sin access log (evita dejar rastros de rutas/keys en logs).
CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8000", "--no-access-log", "--proxy-headers", "--forwarded-allow-ips", "*"]
