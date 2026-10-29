# GameSync server: imagen de despliegue (fork JoseAIG/GameSync).
# Contexto de build: raíz del repo (el servidor importa ../shared).
#   docker compose -f deploy/compose.yaml build

# mcr2vmp (PS1 card -> VMP) se compila aquí: la imagen final no trae
# compilador y su raíz es de solo lectura, así que el servidor no puede
# compilarlo al vuelo. Sin él, toda subida de tarjeta PS1 da 500.
FROM python:3.12-slim AS mcr2vmp
RUN apt-get update \
    && apt-get install -y --no-install-recommends gcc libc6-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY server/third_party/mcr2vmp/ ./
RUN rm -f mcr2vmp mcr2vmp.exe \
    && gcc -O2 -I include -o mcr2vmp mcr2vmp.c aes.c sha1.c

FROM python:3.12-slim AS base

COPY --from=ghcr.io/astral-sh/uv:0.9 /uv /usr/local/bin/uv

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    UV_COMPILE_BYTECODE=1 \
    UV_LINK_MODE=copy \
    UV_PYTHON_DOWNLOADS=never \
    UV_PROJECT_ENVIRONMENT=/opt/venv

WORKDIR /app/server

# Dependencias primero (capa cacheable), fijadas por uv.lock. --locked hace
# fallar el build si uv.lock no corresponde con pyproject.toml.
COPY server/pyproject.toml server/uv.lock ./
RUN uv sync --locked --no-dev --no-install-project

# Código. Lo que el servidor necesita en tiempo de ejecución, más los
# scripts de server/ (migraciones, ra_login.py) para poder correrlos contra
# /data con `docker compose exec gamesync python <script>.py`.
COPY shared/ /app/shared/
COPY server/*.py ./
COPY server/app/ ./app/
COPY server/data/ ./data/
COPY server/third_party/ ./third_party/
# Binario Linux recién compilado; el touch lo deja más nuevo que las fuentes
# para que mcr2vmp_tool no intente recompilarlo.
COPY --from=mcr2vmp /src/mcr2vmp ./third_party/mcr2vmp/mcr2vmp
RUN touch ./third_party/mcr2vmp/mcr2vmp

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
