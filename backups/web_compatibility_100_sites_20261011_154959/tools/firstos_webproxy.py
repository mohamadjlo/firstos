#!/usr/bin/env python3
"""Small host-side HTTPS bridge for the FirstOS QEMU network.

The guest browser speaks ordinary HTTP to 10.0.2.2:18080 and places the
original URL in X-FirstOS-Target.  urllib performs TLS, redirects, and normal
HTTP response handling on the host; FirstOS still parses and renders the
returned document itself.
"""

from __future__ import annotations

import argparse
import sys
import urllib.error
import urllib.parse
import urllib.request
import ssl
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


try:
    import certifi
    ssl_context = ssl.create_default_context(cafile=certifi.where())
except Exception:
    ssl_context = ssl.create_default_context()
    ssl_context.check_hostname = False
    ssl_context.verify_mode = ssl.CERT_NONE

# Create an unverified fallback opener in case certificates fail verification on macOS
unverified_context = ssl.create_default_context()
unverified_context.check_hostname = False
unverified_context.verify_mode = ssl.CERT_NONE

MAX_TARGET = 4096
MAX_REQUEST_BODY = 8 * 1024 * 1024
MAX_RESPONSE_BODY = 32 * 1024 * 1024


class FirstOSProxyHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def do_GET(self) -> None:
        self._forward(False)

    def do_HEAD(self) -> None:
        self._forward(False, head_only=True)

    def do_POST(self) -> None:
        self._forward(True)

    def _send_error(self, status: int, message: str) -> None:
        payload = (message + "\n").encode("utf-8", "replace")
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(payload)

    def _forward(self, has_body: bool, head_only: bool = False) -> None:
        target = self.headers.get("X-FirstOS-Target", "").strip()
        if not target or len(target) > MAX_TARGET:
            self._send_error(400, "Missing or oversized X-FirstOS-Target")
            return

        parsed = urllib.parse.urlsplit(target)
        if parsed.scheme not in ("http", "https") or not parsed.hostname:
            self._send_error(400, "Unsupported target URL")
            return

        body = None
        if has_body:
            try:
                length = int(self.headers.get("Content-Length", "0"))
            except ValueError:
                self._send_error(400, "Invalid request length")
                return
            if length < 0 or length > MAX_REQUEST_BODY:
                self._send_error(413, "Request body is too large")
                return
            body = self.rfile.read(length)

        headers = {
            "User-Agent": "Mozilla/5.0 (X11; FirstOS x86_64; rv:109.0) Gecko/20100101 FirstOS-WebBrowser/1.0",
            "Accept": self.headers.get(
                "Accept",
                "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8",
            ),
            "Accept-Language": "en-US,en;q=0.5",
            "Accept-Encoding": "identity",
            "Upgrade-Insecure-Requests": "1",
            "Sec-Fetch-Dest": "document",
            "Sec-Fetch-Mode": "navigate",
            "Sec-Fetch-Site": "cross-site",
        }
        for key, value in self.headers.items():
            lower = key.lower()
            if lower in {
                "host",
                "connection",
                "content-length",
                "accept-encoding",
                "x-firstos-target",
            }:
                continue
            if lower in {"content-type", "cookie", "referer"}:
                headers[key] = value

        request = urllib.request.Request(
            target,
            data=body,
            headers=headers,
            method="POST" if has_body else ("HEAD" if head_only else "GET"),
        )

        status = 502
        response_headers = {}
        response_body = b""
        try:
            try:
                opener = urllib.request.urlopen(request, timeout=30, context=ssl_context)
            except Exception:
                opener = urllib.request.urlopen(request, timeout=30, context=unverified_context)
            with opener as response:
                status = response.status
                response_headers = dict(response.headers.items())
                if not head_only:
                    response_body = response.read(MAX_RESPONSE_BODY + 1)
        except urllib.error.HTTPError as error:
            status = error.code
            response_headers = dict(error.headers.items()) if error.headers else {}
            if not head_only:
                response_body = error.read(MAX_RESPONSE_BODY + 1)
        except (urllib.error.URLError, TimeoutError, OSError) as error:
            self._send_error(502, "Host fetch failed: " + str(error))
            return

        if len(response_body) > MAX_RESPONSE_BODY:
            self._send_error(502, "Response body is too large")
            return

        self.send_response(status)
        for key in ("Content-Type", "Location", "Set-Cookie", "Cache-Control"):
            value = response_headers.get(key)
            if value:
                self.send_header(key, value)
        self.send_header("Content-Length", str(0 if head_only else len(response_body)))
        self.send_header("Connection", "close")
        self.end_headers()
        if not head_only:
            self.wfile.write(response_body)

        self.log_message("%s %s -> %s (%d bytes)", self.command, target, status, len(response_body))

    def log_message(self, fmt: str, *args: object) -> None:
        sys.stderr.write("[firstos-webproxy] " + (fmt % args) + "\n")
        sys.stderr.flush()


class ReusableThreadingHTTPServer(ThreadingHTTPServer):
    allow_reuse_address = True
    daemon_threads = True


def main() -> int:
    parser = argparse.ArgumentParser(description="FirstOS QEMU HTTPS bridge")
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18080)
    args = parser.parse_args()

    server = ReusableThreadingHTTPServer((args.bind, args.port), FirstOSProxyHandler)
    print(f"[firstos-webproxy] listening on {args.bind}:{args.port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
