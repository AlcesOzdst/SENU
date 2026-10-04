from fastapi import FastAPI
import psutil

app = FastAPI(title="SENU Desktop Agent", version="0.1.0")


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok", "service": "senu-desktop-agent"}


@app.get("/metrics/system")
def system_metrics() -> dict[str, float]:
    memory = psutil.virtual_memory()
    return {
        "cpu_percent": float(psutil.cpu_percent(interval=None)),
        "memory_percent": float(memory.percent),
        "memory_used_bytes": float(memory.used),
        "memory_total_bytes": float(memory.total),
    }
