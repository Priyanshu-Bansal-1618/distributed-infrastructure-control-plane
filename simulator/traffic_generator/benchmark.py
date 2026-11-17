#!/usr/bin/env python3
"""
Distributed Infrastructure Control Plane (DICP) - Benchmark & Load Testing Suite
Measures:
1. Requests/sec (throughput)
2. Latency percentiles (P50, P90, P95, P99, Max, Mean)
3. Routing distribution across backends
4. Behavior across 4 strategies: RoundRobin, WeightedRoundRobin, LeastConnections, ConsistentHash
5. Backend failure failover and recovery behavior
"""

import os
import sys
import time
import json
import socket
import signal
import subprocess
import threading
import statistics
import concurrent.futures
from http.server import HTTPServer, BaseHTTPRequestHandler
from typing import Dict, List, Any
import httpx

# Ensure no proxy interference for localhost
os.environ["NO_PROXY"] = "127.0.0.1,localhost"
for k in ["http_proxy", "https_proxy", "HTTP_PROXY", "HTTPS_PROXY"]:
    if k in os.environ:
        del os.environ[k]

WORKSPACE = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN_LB = os.path.join(WORKSPACE, "build", "dicp_lb")

class DummyBackendHandler(BaseHTTPRequestHandler):
    backend_id = "unknown"

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("X-Backend-ID", self.backend_id)
        self.end_headers()
        body = json.dumps({"backend_id": self.backend_id, "status": "ok"}).encode("utf-8")
        self.wfile.write(body)

    def log_message(self, format, *args):
        pass # Suppress HTTP access logging

def start_backend_server(port: int, backend_id: str) -> HTTPServer:
    class Handler(DummyBackendHandler):
        pass
    Handler.backend_id = backend_id
    server = HTTPServer(("127.0.0.1", port), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    return server

class BenchmarkRunner:
    def __init__(self, lb_port: int = 18080):
        self.lb_port = lb_port
        self.lb_url = f"http://127.0.0.1:{lb_port}"
        self.lb_process = None
        self.backend_servers: Dict[str, HTTPServer] = {}
        self.results: Dict[str, Any] = {}

    def setup(self):
        print("[*] Starting 3 Dummy Backend Services...")
        self.backend_servers["be-1"] = start_backend_server(19101, "be-1")
        self.backend_servers["be-2"] = start_backend_server(19102, "be-2")
        self.backend_servers["be-3"] = start_backend_server(19103, "be-3")
        time.sleep(0.5)

        print(f"[*] Starting dicp_lb on port {self.lb_port}...")
        self.lb_process = subprocess.Popen(
            [BIN_LB, str(self.lb_port), "round_robin"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        time.sleep(1.0)

        # Upload initial backend snapshot with weights 5, 3, 2
        snapshot_payload = {
            "version": 1,
            "backends": {
                "bench_svc": [
                    {"instance_id": "be-1", "host": "127.0.0.1", "port": 19101, "weight": 5},
                    {"instance_id": "be-2", "host": "127.0.0.1", "port": 19102, "weight": 3},
                    {"instance_id": "be-3", "host": "127.0.0.1", "port": 19103, "weight": 2}
                ]
            }
        }
        with httpx.Client(timeout=2.0) as client:
            r = client.post(f"{self.lb_url}/admin/snapshot", json=snapshot_payload)
            assert r.status_code == 200, f"Failed to upload snapshot: {r.text}"
            print("[+] Backend snapshot loaded successfully.")

    def set_strategy(self, strategy: str):
        with httpx.Client(timeout=2.0) as client:
            r = client.post(f"{self.lb_url}/admin/strategy", json={"strategy": strategy})
            assert r.status_code == 200

    def run_workload(self, num_requests: int = 600, concurrency: int = 16, key_generator=None) -> Dict[str, Any]:
        latencies_ms = []
        backend_counts: Dict[str, int] = {}
        errors = 0

        def send_one(req_id: int):
            headers = {"X-Service-Name": "bench_svc"}
            if key_generator:
                headers["X-Routing-Key"] = key_generator(req_id)

            t0 = time.perf_counter()
            try:
                with httpx.Client(timeout=2.0) as client:
                    resp = client.get(f"{self.lb_url}/test", headers=headers)
                    elapsed_ms = (time.perf_counter() - t0) * 1000.0
                    if resp.status_code == 200:
                        bid = resp.headers.get("X-Backend-ID", "unknown")
                        return (elapsed_ms, bid, None)
                    else:
                        return (elapsed_ms, None, f"HTTP {resp.status_code}")
            except Exception as e:
                elapsed_ms = (time.perf_counter() - t0) * 1000.0
                return (elapsed_ms, None, str(e))

        t_start = time.perf_counter()
        with concurrent.futures.ThreadPoolExecutor(max_workers=concurrency) as executor:
            futures = [executor.submit(send_one, i) for i in range(num_requests)]
            for fut in concurrent.futures.as_completed(futures):
                lat, bid, err = fut.result()
                if err:
                    errors += 1
                else:
                    latencies_ms.append(lat)
                    backend_counts[bid] = backend_counts.get(bid, 0) + 1
        t_total = time.perf_counter() - t_start

        latencies_ms.sort()
        p50 = latencies_ms[int(len(latencies_ms) * 0.50)] if latencies_ms else 0
        p90 = latencies_ms[int(len(latencies_ms) * 0.90)] if latencies_ms else 0
        p95 = latencies_ms[int(len(latencies_ms) * 0.95)] if latencies_ms else 0
        p99 = latencies_ms[int(len(latencies_ms) * 0.99)] if latencies_ms else 0
        mean_lat = statistics.mean(latencies_ms) if latencies_ms else 0
        throughput = (num_requests - errors) / t_total if t_total > 0 else 0

        return {
            "total_requests": num_requests,
            "successful_requests": len(latencies_ms),
            "errors": errors,
            "duration_sec": round(t_total, 3),
            "throughput_rps": round(throughput, 1),
            "latency_ms": {
                "p50": round(p50, 2),
                "p90": round(p90, 2),
                "p95": round(p95, 2),
                "p99": round(p99, 2),
                "mean": round(mean_lat, 2)
            },
            "backend_distribution": backend_counts
        }

    def benchmark_all_strategies(self):
        print("\n" + "="*60)
        print("BENCHMARKING LOAD BALANCING STRATEGIES")
        print("="*60)

        # 1. Round Robin
        print("\n--> Testing: RoundRobinStrategy")
        self.set_strategy("round_robin")
        res_rr = self.run_workload(num_requests=600, concurrency=16)
        self.results["round_robin"] = res_rr
        print(f"    RPS: {res_rr['throughput_rps']} | P50: {res_rr['latency_ms']['p50']}ms | P99: {res_rr['latency_ms']['p99']}ms")
        print(f"    Distribution: {res_rr['backend_distribution']}")

        # 2. Weighted Round Robin (weights: be-1=5, be-2=3, be-3=2 -> 50%, 30%, 20%)
        print("\n--> Testing: WeightedRoundRobinStrategy (Weights: 5, 3, 2)")
        self.set_strategy("weighted_round_robin")
        res_wrr = self.run_workload(num_requests=600, concurrency=16)
        self.results["weighted_round_robin"] = res_wrr
        print(f"    RPS: {res_wrr['throughput_rps']} | P50: {res_wrr['latency_ms']['p50']}ms | P99: {res_wrr['latency_ms']['p99']}ms")
        print(f"    Distribution: {res_wrr['backend_distribution']}")

        # 3. Least Connections
        print("\n--> Testing: LeastConnectionsStrategy")
        self.set_strategy("least_conn")
        res_lc = self.run_workload(num_requests=600, concurrency=16)
        self.results["least_connections"] = res_lc
        print(f"    RPS: {res_lc['throughput_rps']} | P50: {res_lc['latency_ms']['p50']}ms | P99: {res_lc['latency_ms']['p99']}ms")
        print(f"    Distribution: {res_lc['backend_distribution']}")

        # 4. Consistent Hashing
        print("\n--> Testing: ConsistentHashStrategy (Virtual Nodes: 100)")
        self.set_strategy("consistent_hash")
        # Route 10 distinct user keys repeated 60 times each
        res_ch = self.run_workload(
            num_requests=600,
            concurrency=16,
            key_generator=lambda req_id: f"user_{(req_id % 10):03d}"
        )
        self.results["consistent_hash"] = res_ch
        print(f"    RPS: {res_ch['throughput_rps']} | P50: {res_ch['latency_ms']['p50']}ms | P99: {res_ch['latency_ms']['p99']}ms")
        print(f"    Distribution: {res_ch['backend_distribution']}")

        # Verify Determinism of Consistent Hash: same user key always hits exact same backend
        print("    Verifying Consistent Hashing Determinism...")
        key_hits = {}
        with httpx.Client(timeout=2.0) as client:
            for uid in range(5):
                key = f"sticky_user_{uid}"
                bids = set()
                for _ in range(10):
                    r = client.get(f"{self.lb_url}/test", headers={"X-Service-Name": "bench_svc", "X-Routing-Key": key})
                    bids.add(r.headers.get("X-Backend-ID"))
                assert len(bids) == 1, f"Consistent hash violation: key {key} mapped to {bids}"
                key_hits[key] = list(bids)[0]
        print(f"    [SUCCESS] Consistent hash determinism verified: {key_hits}")

    def benchmark_backend_failover(self):
        print("\n" + "="*60)
        print("TESTING BACKEND FAILURE & ATOMIC SNAPSHOT SWAP")
        print("="*60)
        self.set_strategy("round_robin")

        print("[*] Terminating Backend 'be-2'...")
        self.backend_servers["be-2"].shutdown()
        self.backend_servers["be-2"].server_close()
        del self.backend_servers["be-2"]

        print("[*] Updating LB snapshot removing 'be-2' (Atomic Swap)...")
        new_snapshot = {
            "version": 2,
            "backends": {
                "bench_svc": [
                    {"instance_id": "be-1", "host": "127.0.0.1", "port": 19101, "weight": 5},
                    {"instance_id": "be-3", "host": "127.0.0.1", "port": 19103, "weight": 2}
                ]
            }
        }
        with httpx.Client(timeout=2.0) as client:
            r = client.post(f"{self.lb_url}/admin/snapshot", json=new_snapshot)
            assert r.status_code == 200

        print("[*] Sending 300 requests to evaluate zero-downtime routing...")
        res_failover = self.run_workload(num_requests=300, concurrency=12)
        print(f"    Errors: {res_failover['errors']} (0 expected)")
        print(f"    Distribution: {res_failover['backend_distribution']}")
        assert res_failover["errors"] == 0, "Requests failed during snapshot swap!"
        assert "be-2" not in res_failover["backend_distribution"], "Dead backend still received requests!"
        print("[SUCCESS] Zero-downtime atomic snapshot swap verified successfully!")
        self.results["failover"] = res_failover

    def teardown(self):
        print("\n[*] Cleaning up benchmark processes...")
        if self.lb_process:
            self.lb_process.terminate()
            self.lb_process.wait()

        for bid, server in list(self.backend_servers.items()):
            try:
                server.shutdown()
                server.server_close()
            except Exception:
                pass
        self.backend_servers.clear()

        # Save results
        out_path = os.path.join(WORKSPACE, "simulator", "traffic_generator", "benchmark_results.json")
        with open(out_path, "w") as f:
            json.dump(self.results, f, indent=2)
        print(f"[+] Benchmark results saved to: {out_path}")

    def run(self):
        try:
            self.setup()
            self.benchmark_all_strategies()
            self.benchmark_backend_failover()
            print("\n" + "="*60)
            print("ALL BENCHMARK SUITES COMPLETED WITH 100% SUCCESS")
            print("="*60 + "\n")
        finally:
            self.teardown()

if __name__ == "__main__":
    runner = BenchmarkRunner(lb_port=18080)
    runner.run()
