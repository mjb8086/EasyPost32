Open a **PowerShell window** and run:

```powershell
cd C:\Users\MJB\src\EasyPost32\EasyPost32
.\build\test-venv\Scripts\python.exe -m uvicorn fixture:app --app-dir .\tests\LocalHttpServer --host 127.0.0.1 --port 8000
```

This runs the FastAPI fixture independently, listening at **`http://127.0.0.1:8000`**. Leave that window open; press **Ctrl+C** to stop the server.

Launch EasyPost32 separately from Visual Studio or your built executable, then try:

| Method | URL | What it tests |
|---|---|---|
| GET | `http://127.0.0.1:8000/ready` | Returns `ready` |
| GET or POST | `http://127.0.0.1:8000/inspect?hello=world` | Returns method, headers, query and body as hex |
| POST | `http://127.0.0.1:8000/echo` | Returns your raw request body |
| GET | `http://127.0.0.1:8000/status/404` | HTTP error response |
| GET | `http://127.0.0.1:8000/delay/3000` | Three-second delay for cancellation tests |
| GET | `http://127.0.0.1:8000/redirect/2` | Redirect handling |
| GET | `http://127.0.0.1:8000/binary` | Binary response handling |

The fixture disables FastAPI’s `/docs` page. 

This command serves plain HTTP; the automated test harness handles the separate HTTPS fixtures and certificates.