# Fork JoseAIG/GameSync

Fork de [pepasjc/GameSync](https://github.com/pepasjc/GameSync) para sincronizar
partidas entre una New 3DS XL y Azahar (Android), con el servidor en un
equipo casero (Docker) y publicado a Internet por HTTPS.

El objetivo **no** es mantener una versión paralela para siempre: es aplicar
arreglos que hacen falta hoy, proponérselos al autor y **volver al original**
en cuanto los tenga.

## Ramas

| Rama | Qué es | Regla |
|---|---|---|
| `master` | Espejo exacto de `upstream/master` | Nunca se le hace commit; solo se avanza al original |
| `fix/server-hardening` | Arreglos de seguridad del servidor | Sale de `master`, un commit por tema → PR al autor |
| `fix/3ds-client` | Arreglos del cliente 3DS | Ídem |
| `fix/android-client` | Arreglos de la app Android | Ídem |
| `inciarte/main` | Lo que se despliega: `master` + las ramas `fix/*` + `deploy/` + este archivo | Se reconstruye, no se edita a mano salvo `deploy/` y `FORK.md` |

Remotos: `origin` = tu fork, `upstream` = el original.

## Cuando el autor saca una versión nueva

```bash
git fetch upstream --tags

# 1. ¿Qué trae de nuevo?
git log --oneline master..upstream/master
git diff --stat master upstream/master

# 2. ¿Cuáles de nuestros parches ya absorbió?  ("-" = ya está en upstream)
git cherry -v upstream/master fix/server-hardening
git cherry -v upstream/master fix/3ds-client
git cherry -v upstream/master fix/android-client
```

`git cherry` solo reconoce un parche si el autor lo aplicó **idéntico**. Si lo
reimplementó a su manera, compara a mano el archivo afectado
(`git diff master upstream/master -- <ruta>`).

### Avanzar el fork

```bash
git switch master && git merge --ff-only upstream/master && git push origin master

for b in fix/server-hardening fix/3ds-client fix/android-client; do
  git switch "$b" && git rebase master   # los parches ya absorbidos desaparecen solos
done

# Reconstruir la rama de despliegue
git switch inciarte/main
git reset --hard master          # deploy/ y FORK.md se recuperan con el cherry-pick de abajo
git merge --no-ff fix/server-hardening fix/3ds-client fix/android-client
git cherry-pick <commit-de-deploy>   # o: git checkout <inciarte/main anterior> -- deploy FORK.md .gitignore
```

Después: tests del servidor (`cd server && uv run pytest -q`), recompilar
3DS/Android si cambiaron, y redesplegar (ver `deploy/README.md`).

## Cuándo abandonar el fork

Volver a la versión oficial cuando **todo** esto esté resuelto en upstream:

- [ ] Servidor: rutas de archivos del bundle validadas (path traversal).
- [ ] Servidor: límite de descompresión y de tamaño de subida.
- [ ] Servidor: `/update/download` restringido o apagable (SSRF).
- [ ] Servidor: docs públicas apagables y key solo por header.
- [ ] 3DS: timestamp correcto (`osGetTime` cuenta desde 1900) o subidas con `force` tras el plan.
- [ ] 3DS: hash con archivos ordenados (igual que Android y el servidor).
- [ ] 3DS: no trunca partidas de más de 64 archivos.
- [ ] 3DS: verificación TLS posible (o se acepta usarla solo por LAN).
- [ ] Android: escritura atómica y key fuera de `/sdcard`.
- [ ] Hay un Dockerfile oficial (o el nuestro sigue sirviendo con su código).

Para migrar: desplegar desde `upstream/master` con el mismo `deploy/`. Las
partidas en `/data/saves` sirven tal cual porque el formato de almacenamiento
es el mismo. Instalar en la 3DS y en Android los binarios oficiales y archivar
el fork.

## Ideas pendientes del fork

- [ ] **Android: fechas en el detalle de cada partida** (pedido 25-oct-2026).
  - Mostrar la **fecha de última modificación de la partida local**: el archivo
    más reciente de `data/00000001/` en Azahar. Sirve para comprobar que
    corresponde a la última sesión de juego.
  - Mostrar la **fecha de la partida en el servidor**: el `client_timestamp` /
    `last_sync` de su metadata, y desde qué consola se subió.
  - Así se ve de un vistazo si hay desfase antes de elegir Smart Sync o Force
    Upload. Podría proponerse al autor como PR, porque aplica a todos los
    emuladores.
- [x] **3DS: dirección alternativa del servidor** (`server_url_alt`), hecho el 25-oct-2026. Si la
  principal no responde, prueba la otra. Sirve cuando el servidor tiene una
  IP distinta en cada una de dos redes de casa.
- [ ] **Clientes: cambios en tiempo real (SSE) + refrescar a demanda** (pedido 25-oct-2026).
  - Servidor: endpoint `GET /api/v1/events` (Server-Sent Events) que avise
    `save_updated {title_id, save_hash, console_id, timestamp}` cuando cualquier
    cliente sube una partida.
  - Android: escuchar el stream mientras la app está abierta y actualizar el
    estado de cada partida sin reiniciar.
  - 3DS: más difícil. httpc no está pensado para streams largos y la app no
    corre en segundo plano. Alternativa realista: sondeo ligero periódico de
    `/sync` mientras la app está abierta.
  - Ambos: botón **Refrescar** para volver a pedir el estado del servidor.
    Hoy en la 3DS hay que cerrar y reabrir la app (visto el 25-oct con Pushmo:
    seguía mostrando "no server save" tras la subida desde Android).
  - Detrás de un túnel/CDN: SSE funciona, pero hay que desactivar
    el buffering con `X-Accel-Buffering: no` / `Cache-Control: no-cache` y
    mandar un heartbeat cada ~30 s para que no se corte la conexión.
- [ ] **Versión propia con sufijo `f`** (de fork), p. ej. `0.5.4-f1`.
  - Hay que tocar el archivo raíz `VERSION`, que es la única fuente de la
    versión. Ojo: los Makefiles lo parsean (el CIA usa `major.minor.micro`
    para `RemasterVersion`); revisar que el sufijo no rompa ese cálculo.
  - Mostrar la versión `f` en la 3DS y en Android, para distinguirla de la
    oficial a simple vista.
- [ ] **Personalizar la 3DS:** ícono, banner/splash y el audio del banner del
  menú HOME (assets del CIA en `3ds/`).
- [ ] **Rediseño de la app Android** con los lineamientos de la skill
  `apple-design`: movimiento físico/interruptible, tipografía, materiales,
  feedback, accesibilidad (reduced motion).
- [ ] **Historial por dispositivo con metadatos** (diseño propuesto por el
  usuario el 25-oct-2026). Hoy el servidor guarda las últimas 10 versiones por
  juego *en total*, solo archivos + fecha. Propuesta:
  - una tabla SQLite `history_versions`: title_id, ts, save_hash, size,
    file_count, console_id, nombre del dispositivo, nota opcional, `pinned`;
  - retención por dispositivo (últimas N de cada uno) y versiones fijadas que
    nunca se podan;
  - API para listar, anotar, fijar y restaurar, y vista en los clientes:
    "volver a la versión de la 3DS del sábado".
  - No cambiar la decisión "cuál es la última": seguir con el hash a tres
    bandas, no por fecha.
- [ ] **Protección contra "retrocesos"** (análisis del 25-oct-2026). El hash a tres
  bandas trata *cualquier* cambio local como avance. Si en un aparato ya
  sincronizado se empieza una partida nueva, se reinstala el juego o se
  restaura un respaldo viejo, "Sincronizar todo" lo sube sin preguntar. Ideas:
  - avisar y pedir confirmación si la subida es mucho más pequeña o tiene
    menos archivos que la versión del servidor;
  - en Sync All, no subir automáticamente lo que parezca un reinicio de partida.
- [ ] **Android: copias locales antes de sobrescribir**, como ya hace la 3DS
  (`local_backups`). Hoy, si en un conflicto se elige bajar, la versión local
  de Android desaparece salvo que se haya subido antes.
- [ ] **Android: sync automático en segundo plano.** Puede bajar una partida
  mientras Azahar está abierto, y luego el emulador la pisa con lo que tiene
  en memoria. Mantenerlo apagado y, si se usa, avisar de que no corra con el
  emulador abierto.
- [ ] **Respaldo periódico del servidor a otra máquina**: copiar `saves/` a un
  equipo personal cuando esté en la red de casa.
- [ ] **3DS: rediseño de UX** (pedido 26-oct-2026). Hoy es una consola de texto en
  las dos pantallas, sin táctil, con un editor de texto propio letra por letra
  (`config_edit_field`). Por etapas:
  1. **Teclado de serie** (`swkbd` de libctru: el teclado oficial, táctil, con
     sugerencias) en todos los campos de texto: `server_url`, `server_url_alt`,
     `api_key`, `nds_dir` y búsqueda. Alcanza para todo; no hace falta uno propio.
  2. **Búsqueda/filtro** de juegos por nombre con ese teclado.
  2b. **Selector de carpetas estilo FBI** para toda ruta (`nds_dir` y futuras):
     - navegar la SD con la cruceta o el táctil: entrar con A, subir con B,
       elegir la carpeta actual con X/START;
     - al elegir, **validar y mostrar qué encontró**, p. ej. "12 ROMs .nds, 10
       partidas en saves/". Si no encuentra nada, avisa antes de guardar;
     - la ruta escrita a mano (teclado) queda solo como opción avanzada.
  3. **Pantalla táctil:** tocar un juego, botones grandes de acción, desplazar
     la lista arrastrando.
  4. **Interfaz gráfica con `citro2d`** (devkitPro, licencia zlib) y la fuente
     del sistema, siguiendo la skill `apple-design`:
     - una acción principal clara ("Sincronizar"); huellas e IDs en "detalles";
     - el conflicto como dos tarjetas lado a lado (fecha, tamaño, aparato);
     - progreso visible y respuesta inmediata al tocar;
     - movimiento suave en la lista.
  - Mantener los atajos de botones actuales, para usar la app sin táctil.
  - La UI vive en `3ds/source/ui.c` y `main.c`; separar primero la lógica de la
    presentación facilita el cambio.

## Seguimiento de PRs al autor

| Rama | PR | Estado |
|---|---|---|
| `fix/server-hardening` | — | pendiente de abrir |
| `fix/3ds-client` | — | pendiente de abrir |
| `fix/android-client` | — | pendiente de abrir |
