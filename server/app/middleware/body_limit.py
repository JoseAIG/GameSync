from fastapi import HTTPException
from fastapi.responses import JSONResponse

from app.config import settings


def _too_large(limit: int) -> str:
    return f"Request body too large (limit {limit} bytes, see SYNC_MAX_UPLOAD_BYTES)"


class _BodyTooLarge(HTTPException):
    def __init__(self, limit: int):
        super().__init__(status_code=413, detail=_too_large(limit))


def _find_too_large(exc: BaseException | None, depth: int = 0) -> bool:
    """True if ``exc`` is, wraps or groups a ``_BodyTooLarge``.

    BaseHTTPMiddleware reads the body in a task group, so the exception can
    reach us wrapped in an ExceptionGroup and/or chained as a cause.
    """
    if exc is None or depth > 10:
        return False
    if isinstance(exc, _BodyTooLarge):
        return True
    if isinstance(exc, BaseExceptionGroup):
        if any(_find_too_large(e, depth + 1) for e in exc.exceptions):
            return True
    return _find_too_large(exc.__cause__ or exc.__context__, depth + 1)


class BodySizeLimitMiddleware:
    """Reject request bodies larger than ``SYNC_MAX_UPLOAD_BYTES``.

    Pure ASGI middleware: a declared Content-Length over the limit is refused
    with 413 before anything is read.  Bodies without a Content-Length (chunked)
    are counted as they stream in; the first chunk that crosses the limit
    aborts the endpoint's ``request.body()`` call and the client gets the
    same 413.
    """

    def __init__(self, app):
        self.app = app

    async def __call__(self, scope, receive, send):
        limit = settings.max_upload_bytes
        if scope["type"] != "http" or limit <= 0:
            await self.app(scope, receive, send)
            return

        for name, value in scope.get("headers", []):
            if name == b"content-length":
                try:
                    declared = int(value)
                except ValueError:
                    declared = -1
                if declared < 0 or declared > limit:
                    status = 413 if declared > limit else 400
                    detail = _too_large(limit) if status == 413 else "Invalid Content-Length"
                    response = JSONResponse(status_code=status, content={"detail": detail})
                    await response(scope, receive, send)
                    return
                break

        received = 0

        async def limited_receive():
            nonlocal received
            message = await receive()
            if message["type"] == "http.request":
                received += len(message.get("body", b""))
                if received > limit:
                    raise _BodyTooLarge(limit)
            return message

        response_started = False

        async def tracking_send(message):
            nonlocal response_started
            if message["type"] == "http.response.start":
                response_started = True
            await send(message)

        try:
            await self.app(scope, limited_receive, tracking_send)
        except Exception as exc:
            if response_started or not _find_too_large(exc):
                raise
            response = JSONResponse(
                status_code=413, content={"detail": _too_large(limit)}
            )
            await response(scope, receive, send)
