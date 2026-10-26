"""Web UI route.

GET  /   — Serves the library web interface (no auth required).
          The server injects the API key and admin flag into the page.
          nginx must forward the authenticated username via X-Remote-User.
"""

from pathlib import Path

from fastapi import APIRouter, Request
from fastapi.responses import FileResponse, HTMLResponse

from app.config import settings
from app.middleware.auth import trusted_remote_user

router = APIRouter()

_TEMPLATE = Path(__file__).parent.parent / "templates" / "index.html"
_FAVICON = Path(__file__).parent.parent / "templates" / "favicon.png"


@router.get("/favicon.png", include_in_schema=False)
async def favicon():
    """The GameSync logo for the browser tab (tools/brand_art.py)."""
    return FileResponse(_FAVICON, media_type="image/png")


@router.get("/", response_class=HTMLResponse, include_in_schema=False)
async def web_ui(request: Request):
    """Serve the GameSync web UI.

    The API key is injected into the page ONLY for an authenticated request,
    so a random visitor who can reach this unauthenticated route never
    receives it.  Authentication comes from one of two sources:

      * a trusted reverse proxy (nginx Basic Auth) forwarding the verified
        username in ``X-Remote-User`` — honoured only when
        ``SYNC_TRUST_PROXY_AUTH=true`` (otherwise the header is forgeable and
        is ignored), or
      * ``SYNC_LAN_ADMIN=true`` — an explicit opt-in for a trusted LAN with
        no proxy, where any local request is treated as admin.

    When neither applies the page loads WITHOUT a key and the client-side JS
    prompts the user to paste it (kept in their browser's localStorage).
    """
    # Only trust the proxy-forwarded username when a trusted proxy is
    # actually configured; otherwise a direct client could forge it.
    remote_user = trusted_remote_user(request)

    if remote_user:
        authenticated = True
        is_admin = remote_user in settings.admin_users_set
    elif settings.lan_admin:
        authenticated = True
        is_admin = True
    else:
        authenticated = False
        is_admin = False

    # Never bake the key into a page served to an unauthenticated caller.
    injected_key = settings.api_key if authenticated else ""

    html = _TEMPLATE.read_text(encoding="utf-8")
    html = html.replace("__API_KEY__",   injected_key,              1)
    html = html.replace("__SITE_TITLE__", settings.site_title)
    html = html.replace("__IS_ADMIN__",  "true" if is_admin else "false", 1)
    return HTMLResponse(html)
