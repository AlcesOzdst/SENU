import uvicorn

if __name__ == "__main__":
    print("[SENU Desktop Agent] Starting metrics server on http://0.0.0.0:8000 ...")
    print("Accessible locally via http://127.0.0.1:8000/metrics/system")
    print("Or across Wi-Fi network via your PC's IP address (e.g. http://172.20.10.3:8000/metrics/system)")
    uvicorn.run("app.main:app", host="0.0.0.0", port=8000, reload=False)
