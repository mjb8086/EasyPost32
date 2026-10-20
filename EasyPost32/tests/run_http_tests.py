"""Start private loopback fixtures, run the native tests, then clean up."""
import argparse
import multiprocessing
import os
from pathlib import Path
import socket
import socketserver
import subprocess
import sys
import tempfile
import threading
import time

from LocalHttpServer.fixture import run_server
from LocalHttpServer.certificates import generate
from validate_postman import validate_exports


class TruncatedResponse(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.recv(65536)
        self.request.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 20\r\n\r\nshort")


def loopback_socket():
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    if hasattr(socket, "SO_EXCLUSIVEADDRUSE"):
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
    listener.bind(("127.0.0.1", 0))
    return listener


def run_gui_checks(command, directory):
    environment = os.environ.copy()
    environment["LOCALAPPDATA"] = str(directory / "local-app-data")
    environment["EASYPOST_TEST_DIRECTORY"] = str(directory)
    process = subprocess.Popen(command, env=environment)
    try:
        return process.wait(timeout=90)
    finally:
        if process.poll() is None:
            # Timeout/interruption can bypass PowerShell's finally block. Kill
            # only this test process tree, including its private GUI instance.
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           timeout=10, check=False)
            process.wait(timeout=10)


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, default=root / "x64/Debug/HttpTests.exe")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--gui", action="store_true", help="Drive the MFC GUI using the same private fixtures")
    mode.add_argument("--manual", action="store_true", help="Keep fixtures alive until the manually tested GUI exits")
    parser.add_argument("--screenshot", type=Path)
    parser.add_argument("--interop-only", action="store_true", help="Run just GUI interoperability checks")
    args = parser.parse_args()
    if args.interop_only and not args.gui:
        parser.error("--interop-only requires --gui")
    executable = args.executable.resolve(strict=True)
    build = root / "build"
    build.mkdir(exist_ok=True)
    context = multiprocessing.get_context("spawn")
    processes = []

    # Socket remains bound but does not listen, reserving a deterministic refused port.
    with loopback_socket() as refused, loopback_socket() as http_listener, tempfile.TemporaryDirectory(prefix="http-fixture-", dir=build) as temporary:
        directory = Path(temporary)
        generate(directory, http_listener.getsockname()[1])
        ca_path = directory / "ca.pem"

        def start(tls=False, wrong_name=False):
            cert_path = key_path = None
            if tls:
                name = "wrong" if wrong_name else "server"
                cert_path, key_path = directory / f"{name}.pem", directory / f"{name}-key.pem"
            with (loopback_socket() if tls else http_listener) as listener:
                port = listener.getsockname()[1]
                listener.listen(128)
                ready = context.Event()
                process = context.Process(target=run_server, args=(listener, ready,
                    str(cert_path) if tls else None, str(key_path) if tls else None, str(directory / "ca.crl")))
                process.start()
                processes.append(process)
                deadline = time.monotonic() + 15
                while not ready.wait(0.05):
                    if not process.is_alive():
                        raise RuntimeError("Loopback fixture exited before readiness")
                    if time.monotonic() >= deadline:
                        raise TimeoutError("Loopback fixture readiness timed out")
            return f"{'https' if tls else 'http'}://127.0.0.1:{port}"

        try:
            http = start()
            https = start(tls=True)
            wrong = start(tls=True, wrong_name=True)
            with socketserver.ThreadingTCPServer(("127.0.0.1", 0), TruncatedResponse) as truncated:
                truncated.daemon_threads = True
                thread = threading.Thread(target=truncated.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True)
                thread.start()
                try:
                    command = [str(executable), http, https, wrong, str(ca_path),
                               f"http://127.0.0.1:{refused.getsockname()[1]}",
                               f"http://127.0.0.1:{truncated.server_address[1]}"]
                    if args.gui:
                        command = ["powershell.exe", "-NoProfile", "-File", str(root / "tests/SmokeMdi.ps1"),
                                   "-Executable", str(executable), "-Editor", "-HttpBase", http,
                                   "-TlsBase", https, "-RefusedBase", command[5], "-TruncatedBase", command[6]]
                        if args.screenshot:
                            command.extend(["-Screenshot", str(args.screenshot.resolve())])
                        if args.interop_only:
                            command.append("-InteropOnly")
                    elif args.manual:
                        print(f"HTTP fixture: {http}\nUntrusted TLS fixture: {https}\nRefused connection: {command[5]}", flush=True)
                        print("Try /inspect, /echo, /status/400, /status/500, /delay/3000 and /binary. Close this test GUI to stop the fixtures.", flush=True)
                        command = [str(executable)]
                    # No dependency restoration, certificate-store changes, or public endpoints.
                    if args.gui:
                        result = run_gui_checks(command, directory)
                    else:
                        environment = os.environ.copy()
                        environment["EASYPOST_INTEROP_OUTPUT"] = str(directory)
                        result = subprocess.run(command, env=environment, timeout=None if args.manual else 90, check=False).returncode
                    if result == 0 and not args.manual:
                        validate_exports(directory)
                    return result
                finally:
                    truncated.shutdown()
                    thread.join(timeout=3)
        finally:
            for process in processes:
                if process.is_alive():
                    process.terminate()
            for process in processes:
                process.join(timeout=3)
                if process.is_alive():
                    process.kill()
                    process.join(timeout=3)
                process.close()


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, TimeoutError, subprocess.TimeoutExpired) as error:
        print(f"HTTP test runner failed: {error}", file=sys.stderr)
        sys.exit(1)
