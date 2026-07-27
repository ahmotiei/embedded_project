"""Thin FastAPI/Swagger gateway for Smart Guard Section 2.

All telemetry, person counting, history, command execution, and MJPEG
production are implemented by the C service. This module only validates the
HTTP shape, publishes OpenAPI documentation, and forwards requests to the
loopback C API.
"""

from __future__ import annotations

import os
from collections.abc import AsyncIterator
from typing import Any

import httpx
from fastapi import FastAPI, Header, HTTPException, Query, Security, status
from fastapi.responses import RedirectResponse, StreamingResponse
from fastapi.security import APIKeyHeader
from pydantic import BaseModel, ConfigDict, Field

STUDENT_NAME = "Amir Hossein Motiei"
STUDENT_ID = "401102553"
C_CORE_URL = os.getenv("SMART_GUARD_C_CORE_URL", "http://127.0.0.1:18080").rstrip("/")
JSON_TIMEOUT_SECONDS = float(os.getenv("SMART_GUARD_PROXY_TIMEOUT_SECONDS", "5"))

command_token_scheme = APIKeyHeader(
    name="X-Command-Token",
    scheme_name="CommandToken",
    description=(
        "Token stored on the VM in /etc/smart-guard/section2.env. "
        "It is required only for POST /api/v1/command."
    ),
    auto_error=False,
)

app = FastAPI(
    title="Smart Guard REST API",
    version="2.0.0",
    description=(
        "Section 2 REST API for Amir Hossein Motiei (401102553). "
        "FastAPI is only the Swagger/documentation gateway; the operational "
        "logic runs in the C core service."
    ),
    contact={"name": STUDENT_NAME},
    docs_url="/docs",
    redoc_url="/redoc",
    openapi_url="/openapi.json",
)


class CommandRequest(BaseModel):
    model_config = ConfigDict(
        json_schema_extra={"examples": [{"cmd": "ping"}, {"cmd": "reboot"}]}
    )

    cmd: str = Field(
        min_length=1,
        max_length=64,
        description="Extensible command name handled by the C command registry.",
        examples=["ping", "history_clear", "reboot"],
    )


class HealthResponse(BaseModel):
    status: str = Field(examples=["ok"])
    component: str = Field(examples=["smart_guard_c_core"])


class PersonsResponse(BaseModel):
    student_id: str = Field(examples=[STUDENT_ID])
    timestamp: str = Field(examples=["2026-07-27T16:54:50+0330"])
    persons: int = Field(ge=0, examples=[1])


class TelemetryResponse(BaseModel):
    student_name: str = Field(examples=[STUDENT_NAME])
    student_id: str = Field(examples=[STUDENT_ID])
    timestamp: str = Field(examples=["2026-07-27T16:54:50+0330"])
    cpu_usage_percent: float = Field(ge=0, le=100, examples=[17.961])
    memory_total_kb: int = Field(ge=0, examples=[1494600])
    memory_free_kb: int = Field(ge=0, examples=[70076])
    memory_available_kb: int = Field(ge=0, examples=[1037336])
    cpu_temperature_available: bool = Field(examples=[True])
    cpu_temperature_stale: bool = Field(examples=[False])
    cpu_temperature_c: float | None = Field(default=None, examples=[54.0])
    temperature_source: str = Field(examples=["host_sysfs_udp"])
    persons: int = Field(ge=0, examples=[1])
    camera_connected: bool = Field(examples=[True])
    last_frame_age_seconds: float = Field(examples=[0.024])


class HistoryRecord(BaseModel):
    id: int = Field(ge=1, examples=[62])
    timestamp: str = Field(examples=["2026-07-27T16:54:50+0330"])
    persons: int = Field(ge=0, examples=[1])


class HistoryResponse(BaseModel):
    student_id: str = Field(examples=[STUDENT_ID])
    count: int = Field(ge=0, le=5, examples=[5])
    records: list[HistoryRecord] = Field(max_length=5)


class CommandResponse(BaseModel):
    accepted: bool = Field(examples=[True])
    cmd: str = Field(examples=["ping"])
    status: str = Field(examples=["ok"])
    timestamp: str | None = Field(
        default=None,
        description="Present for commands such as ping; omitted when not applicable.",
        examples=["2026-07-27T16:30:35+0330"],
    )


class ErrorResponse(BaseModel):
    detail: str = Field(examples=["C core is unavailable"])


async def _json_request(
    method: str,
    path: str,
    *,
    json_body: dict[str, Any] | None = None,
    headers: dict[str, str] | None = None,
) -> Any:
    try:
        async with httpx.AsyncClient(timeout=JSON_TIMEOUT_SECONDS) as client:
            response = await client.request(
                method,
                f"{C_CORE_URL}{path}",
                json=json_body,
                headers=headers,
            )
    except httpx.RequestError as exc:
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail=f"C core is unavailable: {exc}",
        ) from exc

    try:
        payload = response.json()
    except ValueError:
        payload = {"error": response.text or "non-JSON response from C core"}

    if response.status_code >= 400:
        detail = payload.get("error", payload) if isinstance(payload, dict) else payload
        raise HTTPException(status_code=response.status_code, detail=detail)
    return payload


@app.get("/", include_in_schema=False)
async def root() -> RedirectResponse:
    return RedirectResponse(url="/docs", status_code=status.HTTP_307_TEMPORARY_REDIRECT)


@app.get(
    "/health",
    response_model=HealthResponse,
    tags=["Service"],
    summary="C core health check",
    responses={
        503: {
            "model": ErrorResponse,
            "description": "The C core service is unavailable.",
        }
    },
)
async def health() -> Any:
    return await _json_request("GET", "/health")


@app.get(
    "/api/v1/persons",
    response_model=PersonsResponse,
    tags=["Monitoring"],
    summary="Current person count",
    responses={
        503: {
            "model": ErrorResponse,
            "description": "The C core service is unavailable.",
        }
    },
)
async def persons() -> Any:
    """Return the current number of detected people and a live timestamp."""
    return await _json_request("GET", "/api/v1/persons")


@app.get(
    "/api/v1/telemetry",
    response_model=TelemetryResponse,
    tags=["Monitoring"],
    summary="Live CPU, memory, and temperature telemetry",
    responses={
        503: {
            "model": ErrorResponse,
            "description": "The C core service is unavailable.",
        }
    },
)
async def telemetry() -> Any:
    """Return values sampled by C from /proc and /sys (with host sysfs fallback)."""
    return await _json_request("GET", "/api/v1/telemetry")


@app.get(
    "/api/v1/history",
    response_model=HistoryResponse,
    tags=["Monitoring"],
    summary="Last five detection events",
    responses={
        503: {
            "model": ErrorResponse,
            "description": "The C core service is unavailable.",
        }
    },
)
async def history() -> Any:
    return await _json_request("GET", "/api/v1/history")


@app.post(
    "/api/v1/command",
    response_model=CommandResponse,
    response_model_exclude_none=True,
    tags=["Command"],
    summary="Execute an extensible C command",
    status_code=status.HTTP_202_ACCEPTED,
    responses={
        400: {
            "model": ErrorResponse,
            "description": "The command name is not registered in the C core.",
            "content": {
                "application/json": {"example": {"detail": "unknown command"}}
            },
        },
        401: {
            "model": ErrorResponse,
            "description": "X-Command-Token is missing or invalid.",
            "content": {
                "application/json": {
                    "example": {"detail": "invalid or missing X-Command-Token"}
                }
            },
        },
        500: {
            "model": ErrorResponse,
            "description": "The C core recognized the command but execution failed.",
            "content": {
                "application/json": {
                    "example": {"detail": "cannot schedule reboot"}
                }
            },
        },
        503: {
            "model": ErrorResponse,
            "description": "The C core is unavailable or the command token is not configured.",
            "content": {
                "application/json": {
                    "examples": {
                        "c_core_unavailable": {
                            "summary": "C core unavailable",
                            "value": {"detail": "C core is unavailable"},
                        },
                        "token_not_configured": {
                            "summary": "Command token not configured",
                            "value": {
                                "detail": "SMART_GUARD_COMMAND_TOKEN is not configured"
                            },
                        },
                    }
                }
            },
        },
    },
)
async def command(
    request: CommandRequest,
    command_token: str | None = Security(command_token_scheme),
) -> Any:
    headers: dict[str, str] = {}
    if command_token:
        headers["X-Command-Token"] = command_token
    return await _json_request(
        "POST",
        "/api/v1/command",
        json_body=request.model_dump(),
        headers=headers,
    )


@app.get(
    "/api/v1/stream",
    tags=["Video"],
    summary="Live MJPEG stream generated by C",
    response_class=StreamingResponse,
    responses={
        200: {
            "description": "multipart/x-mixed-replace MJPEG stream",
            "content": {"multipart/x-mixed-replace": {}},
        },
        503: {
            "model": ErrorResponse,
            "description": "The C stream service is unavailable.",
        },
    },
)
async def stream(
    frames: int = Query(
        default=0,
        ge=0,
        le=100,
        description=(
            "0 keeps the stream open. For a finite Swagger test, set frames=1 "
            "so the response ends after one real JPEG frame."
        ),
    ),
) -> StreamingResponse:
    client = httpx.AsyncClient(timeout=None)
    request = client.build_request(
        "GET",
        f"{C_CORE_URL}/api/v1/stream",
        params={"frames": frames},
    )
    try:
        response = await client.send(request, stream=True)
    except httpx.RequestError as exc:
        await client.aclose()
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail=f"C stream is unavailable: {exc}",
        ) from exc

    if response.status_code != status.HTTP_200_OK:
        body = await response.aread()
        await response.aclose()
        await client.aclose()
        raise HTTPException(
            status_code=response.status_code,
            detail=body.decode(errors="replace"),
        )

    async def iterator() -> AsyncIterator[bytes]:
        try:
            async for chunk in response.aiter_raw():
                if chunk:
                    yield chunk
        finally:
            await response.aclose()
            await client.aclose()

    media_type = response.headers.get(
        "content-type",
        "multipart/x-mixed-replace; boundary=smartguardframe",
    )
    return StreamingResponse(
        iterator(),
        status_code=status.HTTP_200_OK,
        media_type=media_type,
        headers={"Cache-Control": "no-store, no-cache, must-revalidate"},
    )


# Upper-case aliases match the spelling shown in the assignment PDF.
# They are intentionally hidden from OpenAPI to keep Swagger uncluttered.
@app.get("/API/V1/PERSONS", include_in_schema=False)
async def persons_upper() -> Any:
    return await persons()


@app.get("/API/V1/TELEMETRY", include_in_schema=False)
async def telemetry_upper() -> Any:
    return await telemetry()


@app.get("/API/V1/HISTORY", include_in_schema=False)
async def history_upper() -> Any:
    return await history()


@app.post("/API/V1/COMMAND", include_in_schema=False)
async def command_upper(
    request: CommandRequest,
    x_command_token: str | None = Header(default=None, alias="X-Command-Token"),
) -> Any:
    headers = {"X-Command-Token": x_command_token} if x_command_token else {}
    return await _json_request(
        "POST",
        "/api/v1/command",
        json_body=request.model_dump(),
        headers=headers,
    )


@app.get("/API/V1/STREAM", include_in_schema=False)
async def stream_upper(frames: int = Query(default=0, ge=0, le=100)) -> StreamingResponse:
    return await stream(frames=frames)
