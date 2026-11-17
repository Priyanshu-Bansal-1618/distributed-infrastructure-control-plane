#!/usr/bin/env python3
import os
import sys
import json
import time
from http.server import HTTPServer, BaseHTTPRequestHandler

SERVICE_NAME = os.getenv("SERVICE_NAME", "demo-service")
INSTANCE_ID = os.getenv("INSTANCE_ID", f"{SERVICE_NAME}-{os.getpid()}")
PORT = int(os.getenv("PORT", "8080"))

class BackendHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/health":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"status": "healthy", "instance_id": INSTANCE_ID}).encode())
            return

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("X-Backend-ID", INSTANCE_ID)
        self.send_header("X-Service-Name", SERVICE_NAME)
        self.end_headers()
        body = {
            "service": SERVICE_NAME,
            "instance_id": INSTANCE_ID,
            "path": self.path,
            "timestamp": time.time()
        }
        self.wfile.write(json.dumps(body).encode())

    def log_message(self, format, *args):
        sys.stderr.write(f"[{INSTANCE_ID}] {self.address_string()} - {format % args}\n")

if __name__ == "__main__":
    server = HTTPServer(("0.0.0.0", PORT), BackendHandler)
    print(f"[{INSTANCE_ID}] Serving '{SERVICE_NAME}' on 0.0.0.0:{PORT}")
    server.serve_forever()
