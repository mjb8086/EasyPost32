"""Deterministic loopback fixture. All endpoints use synthetic data only."""
import asyncio
import logging
from pathlib import Path

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse, PlainTextResponse, RedirectResponse, Response, StreamingResponse


app = FastAPI(docs_url=None, redoc_url=None, openapi_url=None)
METHODS = ["GET", "POST", "PUT", "PATCH", "DELETE", "HEAD", "OPTIONS"]


@app.get("/ready")
async def ready():
    return PlainTextResponse("ready")


@app.get("/fixture-ca.crl")
async def certificate_revocations():
    return Response(app.state.crl_bytes, media_type="application/pkix-crl")


@app.api_route("/inspect", methods=METHODS)
async def inspect(request: Request):
    response = JSONResponse({
        "method": request.method,
        "path": request.scope["raw_path"].decode("ascii"),
        "query": request.scope["query_string"].decode("ascii"),
        "headers": [[name.decode("latin1"), value.decode("latin1")]
                    for name, value in request.scope["headers"]],
        "body_hex": (await request.body()).hex(),
    })
    response.raw_headers.extend([(b"x-reply", b"one"), (b"x-reply", b"two")])
    return response


@app.api_route("/slow-inspect", methods=METHODS)
async def slow_inspect(request: Request):
    app.state.send_count = getattr(app.state, "send_count", 0) + 1
    await asyncio.sleep(2)
    return await inspect(request)


@app.get("/send-count")
async def send_count():
    return PlainTextResponse(str(getattr(app.state, "send_count", 0)))


@app.api_route("/echo", methods=METHODS)
async def echo(request: Request):
    return Response(await request.body(), media_type="application/octet-stream")


@app.get("/status/{code}")
async def status(code: int):
    return PlainTextResponse(f"fixture status {code}", status_code=code)


@app.get("/delay/{milliseconds}")
async def delay(milliseconds: int):
    await asyncio.sleep(min(max(milliseconds, 0), 3000) / 1000)
    return PlainTextResponse("completed")


@app.api_route("/redirect/{remaining}", methods=METHODS)
async def redirect(remaining: int):
    return RedirectResponse(f"/redirect/{remaining - 1}" if remaining > 1 else "/inspect", status_code=307)


@app.api_route("/redirect303", methods=METHODS)
async def redirect303():
    return RedirectResponse("/inspect", status_code=303)


@app.get("/redirect-loop")
async def redirect_loop():
    return RedirectResponse("/redirect-loop", status_code=302)


@app.get("/redirect-cross")
async def redirect_cross(port: int):
    return RedirectResponse(f"http://127.0.0.1:{port}/inspect", status_code=302)


@app.get("/redirect-file")
async def redirect_file():
    return RedirectResponse("file:///not-a-real-file", status_code=302)


@app.get("/large/{size}")
async def large(size: int, request: Request):
    async def chunks():
        remaining = min(max(size, 0), 2 * 1024 * 1024)
        while remaining:
            if await request.is_disconnected():
                return
            count = min(remaining, 1024)
            yield b"x" * count
            remaining -= count
            await asyncio.sleep(0)
    return StreamingResponse(chunks(), media_type="text/plain")


@app.get("/headers/{size}")
async def headers(size: int):
    return PlainTextResponse("headers", headers={"X-Large": "x" * min(size, 16384)})


@app.get("/binary")
async def binary():
    return Response(b"\x00\x01\xffhello", media_type="application/octet-stream")


@app.get("/invalid-utf8")
async def invalid_utf8():
    return Response(b"\xc0\xaf", media_type="text/plain")


def run_server(listener, ready_event, cert=None, key=None, crl=None):
    import uvicorn
    # Capture-limit tests intentionally close sockets while a stream is active.
    logging.getLogger("asyncio").setLevel(logging.CRITICAL)
    app.state.crl_bytes = Path(crl).read_bytes() if crl else b""

    class ReadyServer(uvicorn.Server):
        async def startup(self, sockets=None):
            await super().startup(sockets=sockets)
            if self.started:
                ready_event.set()

    config = uvicorn.Config(app, host="127.0.0.1", log_config=None, log_level="critical",
                            access_log=False, lifespan="off", http="h11", ws="none",
                            ssl_certfile=cert, ssl_keyfile=key)
    try:
        ReadyServer(config).run(sockets=[listener])
    finally:
        listener.close()
