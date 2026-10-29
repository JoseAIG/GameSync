# Despliegue con Docker

Servidor GameSync en un contenedor blindado. Se puede usar solo en la LAN o
publicarlo a Internet detrás de un túnel o un proxy inverso con HTTPS.

## Primera vez

```bash
git clone -b inciarte/main https://github.com/JoseAIG/GameSync.git gamesync
cd gamesync/deploy
cp .env.example .env
python3 -c "import secrets; print(secrets.token_urlsafe(32))"   # pegar en SYNC_API_KEY
chmod 600 .env
mkdir -p data                           # o la ruta de GAMESYNC_DATA_DIR, con dueño GAMESYNC_UID
docker network create gamesync_proxy    # o la red que ya use tu túnel/proxy (GAMESYNC_TUNNEL_NETWORK)
docker compose up -d --build
docker compose ps                       # debe quedar "healthy"
curl -s http://127.0.0.1:8000/api/v1/status
```

## Publicarlo a Internet (opcional)

Apunta tu túnel o proxy inverso a `http://gamesync:8000` dentro de la red
`GAMESYNC_TUNNEL_NETWORK`, y sirve el exterior **solo por HTTPS**: la API key
viaja en el header `X-API-Key`.

Capas extra recomendadas, si tu proxy o CDN las ofrece:
- Bloquear `/docs`, `/redoc`, `/openapi.json`, `/api/v1/update*` y
  `/api/v1/emulators/*`. El servidor ya las apaga; esto es un refuerzo.
- Limitar la tasa en `/api/v1/` por IP, por ejemplo 50 peticiones cada 10 s.
  Frena a quien intente adivinar la key.

**La 3DS no puede usar endpoints HTTPS modernos.** Su TLS solo habla RSA y
cifrados antiguos, y muchos CDN sirven únicamente ECDSA. Haz que la 3DS
sincronice por LAN, contra `http://<ip-del-servidor>:8000`, y usa
`server_url_alt` si el servidor cambia de IP entre redes.

## Actualizar

```bash
git pull && cd deploy && docker compose up -d --build
```

## Datos y respaldo

Todo el estado vive en `GAMESYNC_DATA_DIR/saves`: las partidas, su historial
y las bases SQLite. Para respaldar, copia esa carpeta con el contenedor en
pausa (`docker compose pause` / `unpause`).

## Blindaje aplicado

- Usuario sin privilegios (`GAMESYNC_UID`, 10001 por defecto).
- Sistema de archivos de solo lectura; solo `/data` y `/tmp` son escribibles.
- Sin capacidades extra (`cap_drop: ALL`, `no-new-privileges`).
- Límites de memoria y de procesos.
- Sin catálogo de ROMs (`SYNC_ROM_DIR` sin definir).
- Sin RetroAchievements.
- Sin documentación pública.
- Sin proxy de actualizaciones. La 3DS muestra "Update check failed" al
  buscar actualizaciones: el servidor responde 503 en vez de fingir que no
  hay versión nueva.
- Sin índice de juegos para emuladores (`GameIndex.yaml` de PS5SX2).
- Key solo por header. Las descargas de la web usan un enlace temporal
  firmado (`?token=`, válido un día y solo para ese archivo) en lugar de la
  key en la URL.
- Sin access log de uvicorn.
