# Fork JoseAIG/GameSync

Fork de [pepasjc/GameSync](https://github.com/pepasjc/GameSync) para sincronizar
partidas entre una New 3DS XL y Azahar (Android), con el servidor en un
equipo casero (Docker) y publicado a Internet por HTTPS.

Desde octubre de 2026 el fork tiene **su propia rama principal**
(`inciarte/main`). Incorpora las versiones del autor cuando salen y conserva
encima los arreglos y el rediseño propios. Por ahora no se envían PRs al
autor.

## Ramas

| Rama | Qué es | Regla |
|---|---|---|
| `master` | Espejo exacto de `upstream/master` | Nunca se le hace commit; solo se avanza al original |
| `inciarte/main` | Rama principal del fork y la que se despliega | Solo recibe merges de ramas de trabajo ya probadas |
| `wt/<tema>` | Ramas de trabajo (un tema cada una) | Salen de `inciarte/main` y vuelven con `merge --no-ff` |
| `integrate/<versión>` | Integración de una versión nueva del autor | Sale de `inciarte/main`; cuando todo pasa, `inciarte/main` avanza a ella |
| `fix/*` | Históricas: los arreglos de la 0.5.4 | Ya están dentro de `inciarte/main`; no se usan más |

Remotos: `origin` = este fork, `upstream` = el original.

Estado: `inciarte/main` = v0.5.4 + la rama `v055` del autor (aún no publicada
en su `master`) + los arreglos del fork + el rediseño de 3DS y Android.

## Incorporar una versión nueva del autor

```bash
git fetch upstream --tags

# 1. ¿Qué trae que aún no tenemos? (vale para master o para una rama suya, p. ej. v056)
git log --oneline inciarte/main..upstream/master
git diff --stat inciarte/main...upstream/master

# 2. Integrar en una rama aparte, nunca directo en inciarte/main
git switch -c integrate/<versión> inciarte/main
git merge upstream/master          # o upstream/<rama>
```

Al resolver conflictos:

- Mantener **las dos conductas**: lo nuevo del autor y cada arreglo del fork.
  Si el autor reimplementó un arreglo a su manera, quedarse con una sola
  versión (ojo con duplicados que el merge automático deja en pie).
- En la 3DS, los botones son los del autor.
- Revisar que ningún camino nuevo del autor se salte las protecciones del fork:
  - verificación TLS en toda petición de la 3DS;
  - "el servidor no responde" nunca equivale a "no hay partida en el servidor";
  - copias locales antes de sobrescribir;
  - subidas con `force` y hora Unix correcta;
  - escritura atómica en Android;
  - API key fuera del almacenamiento compartido;
  - interruptores de salida a Internet apagados en `deploy/`.

Antes de mover `inciarte/main`:

```bash
cd server && uv lock && uv run pytest tests/ -q        # todos deben pasar
# 3DS: make && make cia sin warnings nuevos + sh 3ds/tests/run_host_tests.sh
# Android: testDebugUnitTest assembleDebug (solo los fallos ya conocidos)
git switch inciarte/main && git merge --ff-only integrate/<versión>
```

Después: redesplegar (ver `deploy/README.md`) y reinstalar 3DS/Android si
cambiaron.

Cuando el autor publique `v055` en su `master`, el paso 1 debería mostrar poco
o nada nuevo: ya la tenemos integrada.

## Pendientes del fork

Hecho en la integración de v055 (octubre 2026, **sin probar aún en hardware**):

- [x] **3DS: dirección alternativa del servidor** (`server_url_alt`).
- [x] **3DS: rediseño de UX** sobre la interfaz citro2d del autor, con los
  lineamientos de la skill `apple-design`:
  - teclado del sistema en todos los campos;
  - búsqueda por nombre en Partidas;
  - selector de carpetas estilo FBI con validación de lo encontrado;
  - táctil completo: arrastrar con inercia, botones grandes, diálogos tocables;
  - trabajos de red en segundo plano con cancelar;
  - configuración inicial en la consola;
  - restaurar desde el historial pide confirmación.
- [x] **Android: rediseño** con la skill `apple-design`: movimiento, foco con
  mando, avisos, accesibilidad, selector de carpetas que dice qué encontró.
- [x] **Android: fechas en el detalle de cada partida**: local, servidor y
  aparato de origen.
- [x] **Android: copias locales antes de sobrescribir**, con "Deshacer".
- [x] **Android: aviso del sync automático** con el emulador abierto.
- [x] **Android: "Sincronizar todo" muestra el plan antes de mover nada** y
  retiene las subidas que parecen un retroceso.
- [x] Servidor: interruptores para las salidas a Internet nuevas de v055
  (`SYNC_EMULATOR_INDEX_ENABLED`, `/ra/*` exige `SYNC_RA_ENABLED`).

Pendiente:

- [ ] **Probar todo en la New 3DS XL y en el teléfono** (respaldar antes).
- [ ] **Protección contra retrocesos en la 3DS.** Android ya la tiene. En la
  3DS, Sync All todavía sube cualquier cambio local sin preguntar.
- [ ] **3DS: restaurar una copia local** desde la app (ítem 5.3 del diseño,
  dejado fuera a propósito).
- [ ] **Clientes: cambios en tiempo real (SSE)**.
  - Hoy hay refresco a demanda: deslizar para refrescar y releer al volver a
    la app en Android; los trabajos de red en la 3DS.
  - Falta el endpoint `GET /api/v1/events` en el servidor.
  - Detrás de un túnel/CDN: `X-Accel-Buffering: no`, `Cache-Control: no-cache`
    y un heartbeat cada ~30 s.
- [ ] **Historial por dispositivo con metadatos.**
  - Tabla `history_versions` (title_id, ts, hash, tamaño, consola, nota, `pinned`).
  - Retención por aparato y versiones fijadas.
  - API y vista en los clientes.
  - Seguir decidiendo "cuál es la última" con el hash a tres bandas, no por fecha.
- [ ] **Android: IDs de juegos de DS.** Buscar la ROM también junto a la
  partida. Si no aparece, avisar en vez de caer en un ID por nombre que no
  empareja con la 3DS.
- [ ] **Versión propia con sufijo `f`** (p. ej. `0.5.5-f1`) en el archivo raíz
  `VERSION`. Revisar que el sufijo no rompa el `RemasterVersion` del CIA.
- [ ] **Personalizar la 3DS:** ícono, banner y audio del menú HOME.
- [ ] **Respaldo periódico del servidor** a otra máquina de la casa.
- [ ] Del code review quedaron para decidir:
  - si la 3DS debe negarse a caer de una dirección `https` a una alternativa
    `http`;
  - actualizaciones de la 3DS sin firma cuando van por `http` en la LAN (en el
    despliegue el proxy de actualizaciones está apagado);
  - en Android, subir Compose/navigation para el gesto "atrás" predictivo.
