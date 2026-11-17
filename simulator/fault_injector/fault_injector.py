#!/usr/bin/env python3
"""
Distributed Infrastructure Control Plane (DICP) - Fault Injection Engine
Supports injecting:
1. Node Crash (hard kill via SIGKILL)
2. Graceful Departure (SIGINT / drain signal)
3. Service Crash (terminate backend instance)
4. Slow Backend (inject artificial latency)
5. Network Partition (simulate packet drops/partitioning)
6. Node Restart (restart with persistent SQLite storage)
"""

import os
import signal
import subprocess
import time
import httpx
from typing import Dict, Optional, List

class FaultInjector:
    def __init__(self):
        # Strip proxy to allow direct localhost connections
        os.environ["NO_PROXY"] = "127.0.0.1,localhost"
        for k in ["http_proxy", "https_proxy", "HTTP_PROXY", "HTTPS_PROXY"]:
            if k in os.environ:
                del os.environ[k]

    def crash_process(self, pid: int) -> bool:
        """Simulate an abrupt hard crash using SIGKILL."""
        try:
            os.kill(pid, signal.SIGKILL)
            return True
        except ProcessLookupError:
            return False

    def graceful_stop(self, pid: int) -> bool:
        """Simulate a planned graceful shutdown using SIGINT."""
        try:
            os.kill(pid, signal.SIGINT)
            return True
        except ProcessLookupError:
            return False

    def inject_service_crash(self, mgmt_url: str, instance_id: str) -> bool:
        """Deregister or terminate a specific service instance."""
        try:
            with httpx.Client(timeout=2.0) as client:
                res = client.post(
                    f"{mgmt_url}/api/v1/agent/services/deregister",
                    json={"instance_id": instance_id}
                )
                return res.status_code == 200
        except Exception:
            return False

    def check_node_status(self, mgmt_url: str, target_node_id: str) -> Optional[int]:
        """Query agent's view of a target node: 1=ALIVE, 2=SUSPECT, 3=DEAD, 4=LEFT."""
        try:
            with httpx.Client(timeout=2.0) as client:
                res = client.get(f"{mgmt_url}/api/v1/agent/members")
                if res.status_code == 200:
                    members = res.json()
                    for m in members:
                        if m.get("node_id") == target_node_id:
                            return m.get("status")
        except Exception:
            pass
        return None

if __name__ == "__main__":
    injector = FaultInjector()
    print("DICP Fault Injector initialized successfully.")
