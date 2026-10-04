from fastapi.testclient import TestClient

from app.main import app


client = TestClient(app)


def test_health_endpoint() -> None:
    response = client.get("/health")
    assert response.status_code == 200
    assert response.json() == {"status": "ok", "service": "senu-desktop-agent"}


def test_metrics_endpoint_shape_and_ranges() -> None:
    response = client.get("/metrics/system")
    assert response.status_code == 200

    payload = response.json()
    assert "cpu_percent" in payload
    assert "memory_percent" in payload
    assert "memory_used_bytes" in payload
    assert "memory_total_bytes" in payload

    assert 0.0 <= payload["cpu_percent"] <= 100.0
    assert 0.0 <= payload["memory_percent"] <= 100.0
    assert payload["memory_total_bytes"] > 0
    assert payload["memory_used_bytes"] >= 0
