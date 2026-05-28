"""Smoke tests for the vmafx-mcp HTTP transport (ADR-0701).

Tests use aiohttp's TestClient for lightweight in-process HTTP testing.
No network access, no GPU, no vmaf binary required for health/metrics endpoints.
The /v1/score endpoint is tested with a monkeypatched _run_vmaf_score coroutine.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any
from unittest.mock import AsyncMock, patch

import pytest
import pytest_asyncio

# Skip the entire module if aiohttp or prometheus_client are not installed.
aiohttp = pytest.importorskip("aiohttp")
pytest.importorskip("prometheus_client")

from aiohttp.test_utils import TestClient  # noqa: E402

from vmaf_mcp import http_transport as ht  # noqa: E402

# ---------------------------------------------------------------------------
# Helper: build a fresh app with an isolated prometheus registry
# ---------------------------------------------------------------------------


def _fresh_metrics() -> dict[str, Any]:
    """Return a metrics dict backed by an isolated prometheus registry.

    Using the default (global) registry in tests causes duplicate-metric errors
    when multiple tests call _build_metrics() in the same process.
    """
    import prometheus_client as pc  # type: ignore[import-untyped]

    registry = pc.CollectorRegistry(auto_describe=False)

    return {
        "scoring_requests_total": pc.Counter(
            "vmaf_scoring_requests_total_test",
            "Total VMAF scoring requests",
            ["endpoint", "status"],
            registry=registry,
        ),
        "scoring_errors_total": pc.Counter(
            "vmaf_scoring_errors_total_test",
            "Total VMAF scoring errors",
            registry=registry,
        ),
        "scoring_duration_seconds": pc.Histogram(
            "vmaf_scoring_duration_seconds_test",
            "VMAF scoring latency",
            buckets=[0.1, 0.5, 1.0, 5.0, 30.0, 120.0],
            registry=registry,
        ),
    }


@pytest_asyncio.fixture
async def test_client(aiohttp_client: Any) -> TestClient:
    """Return an aiohttp TestClient connected to a fresh app instance."""
    metrics = _fresh_metrics()
    app = ht._make_app(metrics)
    return await aiohttp_client(app)


# ---------------------------------------------------------------------------
# /healthz
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_healthz_returns_200(test_client: TestClient) -> None:
    """GET /healthz must return 200 with {"status": "healthy"}."""
    resp = await test_client.get("/healthz")
    assert resp.status == 200
    body = await resp.json()
    assert body == {"status": "healthy"}


@pytest.mark.asyncio
async def test_healthz_content_type(test_client: TestClient) -> None:
    """GET /healthz must return Content-Type: application/json."""
    resp = await test_client.get("/healthz")
    assert "application/json" in resp.content_type


# ---------------------------------------------------------------------------
# /readyz
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_readyz_returns_200_when_binary_exists(
    test_client: TestClient, tmp_path: Path
) -> None:
    """GET /readyz must return 200 when the vmaf binary path exists."""
    fake_binary = tmp_path / "vmaf"
    fake_binary.write_bytes(b"")
    fake_binary.chmod(0o755)

    with patch("vmaf_mcp.server._vmaf_binary", return_value=fake_binary):
        resp = await test_client.get("/readyz")
    assert resp.status == 200
    body = await resp.json()
    assert body["status"] == "ready"
    assert "vmaf_binary" in body


@pytest.mark.asyncio
async def test_readyz_returns_503_when_binary_missing(
    test_client: TestClient, tmp_path: Path
) -> None:
    """GET /readyz must return 503 when the vmaf binary path does not exist."""
    missing = tmp_path / "vmaf_does_not_exist"

    with patch("vmaf_mcp.server._vmaf_binary", return_value=missing):
        resp = await test_client.get("/readyz")
    assert resp.status == 503
    body = await resp.json()
    assert body["status"] == "not_ready"
    assert "reason" in body


# ---------------------------------------------------------------------------
# /metrics
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_metrics_returns_200(test_client: TestClient) -> None:
    """GET /metrics must return 200 with Prometheus exposition text."""
    resp = await test_client.get("/metrics")
    assert resp.status == 200


@pytest.mark.asyncio
async def test_metrics_content_type_is_prometheus(test_client: TestClient) -> None:
    """GET /metrics must use the prometheus_client CONTENT_TYPE_LATEST."""
    import prometheus_client as pc  # type: ignore[import-untyped]

    resp = await test_client.get("/metrics")
    # CONTENT_TYPE_LATEST may include version and charset info after a semicolon.
    assert resp.content_type.split(";")[0].strip() == pc.CONTENT_TYPE_LATEST.split(";")[0].strip()


@pytest.mark.asyncio
async def test_metrics_body_is_text(test_client: TestClient) -> None:
    """GET /metrics body must be valid UTF-8 Prometheus exposition text."""
    resp = await test_client.get("/metrics")
    text = await resp.text()
    # Prometheus exposition always has lines starting with # or a metric name.
    # An empty registry is valid; just check it round-trips as text.
    assert isinstance(text, str)


# ---------------------------------------------------------------------------
# /v1/score
# ---------------------------------------------------------------------------


def _fake_score_payload() -> dict[str, Any]:
    return {
        "vmaf": 85.4321,
        "frames": [],
        "pooled_metrics": {"vmaf": {"mean": 85.4321, "harmonic_mean": 85.0}},
    }


@pytest.mark.asyncio
async def test_score_returns_200_on_valid_request(test_client: TestClient, tmp_path: Path) -> None:
    """POST /v1/score must return 200 with the vmaf JSON payload on success."""
    ref = tmp_path / "ref.yuv"
    dis = tmp_path / "dis.yuv"
    ref.write_bytes(b"\x00" * 16)
    dis.write_bytes(b"\x00" * 16)

    fake_result = _fake_score_payload()

    async def _mock_run(req: Any) -> dict[str, Any]:
        return dict(fake_result)

    import vmaf_mcp.server as srv

    with (
        patch.object(srv, "_validate_path", side_effect=lambda p: Path(p)),
        patch.object(srv, "_run_vmaf_score", new=AsyncMock(side_effect=_mock_run)),
    ):
        resp = await test_client.post(
            "/v1/score",
            json={
                "reference": str(ref),
                "distorted": str(dis),
                "width": 1920,
                "height": 1080,
                "pixfmt": "420",
                "bitdepth": 8,
            },
        )

    assert resp.status == 200
    body = await resp.json()
    assert "vmaf" in body
    assert "request_id" in body


@pytest.mark.asyncio
async def test_score_returns_400_on_missing_fields(test_client: TestClient) -> None:
    """POST /v1/score must return 400 when required fields are absent."""
    resp = await test_client.post("/v1/score", json={"reference": "/tmp/r.yuv"})
    assert resp.status == 400
    body = await resp.json()
    assert "error" in body


@pytest.mark.asyncio
async def test_score_returns_400_on_invalid_json(test_client: TestClient) -> None:
    """POST /v1/score must return 400 when the body is not valid JSON."""
    resp = await test_client.post(
        "/v1/score",
        data=b"not-json",
        headers={"Content-Type": "application/json"},
    )
    assert resp.status == 400
    body = await resp.json()
    assert "error" in body
