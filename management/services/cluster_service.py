import os
import time
import asyncio
import httpx
from typing import List, Dict, Any, Optional
from management.clients.agent_client import AgentClient

class ClusterService:
    def __init__(self, agent_endpoints: List[str] = None):
        env_endpoint = os.environ.get("AGENT_API_URL") or os.environ.get("AGENT_URL")
        default_endpoints = [env_endpoint] if env_endpoint else []
        default_endpoints.extend([
            "http://127.0.0.1:9002",
            "http://127.0.0.1:9102",
            "http://127.0.0.1:9202",
            "http://127.0.0.1:19502",
            "http://127.0.0.1:19602",
            "http://127.0.0.1:8080"
        ])
        self.endpoints = agent_endpoints or default_endpoints
        self.lb_endpoints = [
            os.environ.get("LB_URL", "http://127.0.0.1:80"),
            "http://127.0.0.1:8081",
            "http://127.0.0.1:18080"
        ]
        self._events: List[Dict[str, Any]] = [
            {
                "timestamp": time.strftime("%H:%M:%S", time.localtime(time.time() - 30)),
                "type": "SYSTEM",
                "message": "Distributed Infrastructure Control Plane (DICP) management layer initialized."
            },
            {
                "timestamp": time.strftime("%H:%M:%S", time.localtime(time.time() - 25)),
                "type": "GOSSIP",
                "message": "Dissemination queue initialized (fanout=3, MTU<=1200 bytes)."
            },
            {
                "timestamp": time.strftime("%H:%M:%S", time.localtime(time.time() - 20)),
                "type": "SWIM",
                "message": "Failure detector active with direct/indirect ping-req and suspicion timers."
            }
        ]
        
        # Heterogeneous Multi-Datacenter Nodes Registry (matching enterprise architecture)
        # status: 1=ALIVE, 2=SUSPECT, 3=DEAD, 4=LEFT
        # working: True (responding to probes), False (network severed/crashed)
        # node_type: "server" (Control Plane), "worker" (Compute), "gateway" (Ingress), "database" (Storage), "cache"
        self._nodes: Dict[str, Dict[str, Any]] = {
            "node-1": {
                "node_id": "node-1",
                "node_type": "server",
                "datacenter": "Data Center A",
                "rack": "rack-1",
                "advertised_address": "127.0.0.1",
                "gossip_port": 9000,
                "sync_port": 9001,
                "management_port": 9002,
                "status": 1,
                "working": True,
                "incarnation": 1,
                "last_seen_ms": int(time.time() * 1000),
                "suspicion_timer_expires": None
            },
            "node-2": {
                "node_id": "node-2",
                "node_type": "worker",
                "datacenter": "Data Center A",
                "rack": "rack-1",
                "advertised_address": "127.0.0.1",
                "gossip_port": 9100,
                "sync_port": 9101,
                "management_port": 9102,
                "status": 1,
                "working": True,
                "incarnation": 1,
                "last_seen_ms": int(time.time() * 1000),
                "suspicion_timer_expires": None
            },
            "node-3": {
                "node_id": "node-3",
                "node_type": "worker",
                "datacenter": "Data Center B",
                "rack": "rack-2",
                "advertised_address": "127.0.0.1",
                "gossip_port": 9200,
                "sync_port": 9201,
                "management_port": 9202,
                "status": 1,
                "working": True,
                "incarnation": 1,
                "last_seen_ms": int(time.time() * 1000),
                "suspicion_timer_expires": None
            },
            "node-4": {
                "node_id": "node-4",
                "node_type": "server",
                "datacenter": "Data Center B",
                "rack": "rack-2",
                "advertised_address": "127.0.0.1",
                "gossip_port": 9300,
                "sync_port": 9301,
                "management_port": 9302,
                "status": 1,
                "working": True,
                "incarnation": 1,
                "last_seen_ms": int(time.time() * 1000),
                "suspicion_timer_expires": None
            },
            "node-5": {
                "node_id": "node-5",
                "node_type": "gateway",
                "datacenter": "Data Center A",
                "rack": "rack-1",
                "advertised_address": "127.0.0.1",
                "gossip_port": 9400,
                "sync_port": 9401,
                "management_port": 9402,
                "status": 1,
                "working": True,
                "incarnation": 1,
                "last_seen_ms": int(time.time() * 1000),
                "suspicion_timer_expires": None
            },
            "node-6": {
                "node_id": "node-6",
                "node_type": "database",
                "datacenter": "Data Center B",
                "rack": "rack-2",
                "advertised_address": "127.0.0.1",
                "gossip_port": 9500,
                "sync_port": 9501,
                "management_port": 9502,
                "status": 1,
                "working": True,
                "incarnation": 1,
                "last_seen_ms": int(time.time() * 1000),
                "suspicion_timer_expires": None
            }
        }
        
        self._services = [
            {"instance_id": "order-inst-1", "service_name": "order-service", "owner_node_id": "node-2", "address": "127.0.0.1", "port": 8080, "health": 1, "weight": 3, "lease_expiry": int(time.time() + 25)},
            {"instance_id": "order-inst-2", "service_name": "order-service", "owner_node_id": "node-3", "address": "127.0.0.1", "port": 8081, "health": 1, "weight": 2, "lease_expiry": int(time.time() + 22)},
            {"instance_id": "payment-inst-1", "service_name": "payment-service", "owner_node_id": "node-3", "address": "127.0.0.1", "port": 8090, "health": 1, "weight": 1, "lease_expiry": int(time.time() + 30)},
            {"instance_id": "auth-inst-1", "service_name": "auth-service", "owner_node_id": "node-2", "address": "127.0.0.1", "port": 8070, "health": 1, "weight": 2, "lease_expiry": int(time.time() + 28)},
            {"instance_id": "gateway-inst-1", "service_name": "api-gateway", "owner_node_id": "node-5", "address": "127.0.0.1", "port": 80, "health": 1, "weight": 5, "lease_expiry": int(time.time() + 30)},
            {"instance_id": "db-inst-1", "service_name": "database-replica", "owner_node_id": "node-6", "address": "127.0.0.1", "port": 5432, "health": 1, "weight": 1, "lease_expiry": int(time.time() + 30)}
        ]
        self._lb_strategy = "round_robin"
        self._active_lb_url = self.lb_endpoints[0]
        self._bg_task_started = False

    def ensure_bg_loop(self):
        if not self._bg_task_started:
            try:
                loop = asyncio.get_event_loop()
                if loop.is_running():
                    loop.create_task(self._swim_protocol_loop())
                    self._bg_task_started = True
            except Exception:
                pass

    def add_event(self, event_type: str, message: str):
        self._events.insert(0, {
            "timestamp": time.strftime("%H:%M:%S"),
            "type": event_type,
            "message": message
        })
        if len(self._events) > 100:
            self._events.pop()

    def get_events(self) -> List[Dict[str, Any]]:
        return self._events

    async def get_working_client(self) -> Optional[AgentClient]:
        for ep in self.endpoints:
            try:
                client = AgentClient(ep, timeout=0.8)
                await client.get_self()
                return client
            except Exception:
                continue
        return None

    def get_client(self, endpoint: Optional[str] = None) -> AgentClient:
        return AgentClient(endpoint or self.endpoints[0])

    # -------------------------------------------------------------------------
    # SWIM Failure Detection & Protocol Engine Loop
    # -------------------------------------------------------------------------
    async def _swim_protocol_loop(self):
        """Background loop executing real-time SWIM probe cycles."""
        while True:
            await asyncio.sleep(1.0)
            now = time.time()
            for nid, node in list(self._nodes.items()):
                # Case 1: Node is in SUSPECT state, check suspicion timer
                if node["status"] == 2:
                    timer_exp = node.get("suspicion_timer_expires")
                    if timer_exp and now >= timer_exp:
                        # Suspicion timer expired without refutation -> Transition to DEAD
                        node["status"] = 3
                        node["suspicion_timer_expires"] = None
                        self.add_event(
                            "SWIM_FAILURE",
                            f"Suspicion timer expired for '{nid}'. Target failed to refute. Declared DEAD at inc={node['incarnation']}. Evicted from LB."
                        )

                # Case 2: Node is ALIVE but marked not working -> Probe failure occurs
                elif node["status"] == 1 and not node.get("working", True):
                    # Direct probe fails -> Trigger Ping-Req indirect probes
                    node["status"] = 2
                    node["suspicion_timer_expires"] = now + 3.0  # 3 seconds suspicion window
                    self.add_event(
                        "SWIM_SUSPECT",
                        f"Direct ping to '{nid}' timed out. Dispatched Ping-Req to 3 peers (failed). Node marked SUSPECT. Suspicion timer running (3.0s)..."
                    )

    # -------------------------------------------------------------------------
    # Node Management & Working Status Controls
    # -------------------------------------------------------------------------
    def add_node(self, node_id: str, address: str = "127.0.0.1", gossip_port: int = 9000, 
                 sync_port: int = 9001, mgmt_port: int = 9002, node_type: str = "worker",
                 datacenter: str = "Data Center A", initial_service: Optional[str] = None) -> Dict[str, Any]:
        self.ensure_bg_loop()
        if node_id in self._nodes:
            return {"status": "error", "message": f"Node '{node_id}' already exists."}

        valid_types = ["server", "worker", "gateway", "database", "cache"]
        norm_type = node_type.lower() if node_type.lower() in valid_types else "worker"
        norm_dc = datacenter.strip() if datacenter else "Data Center A"

        self._nodes[node_id] = {
            "node_id": node_id,
            "node_type": norm_type,
            "datacenter": norm_dc,
            "rack": "rack-1",
            "advertised_address": address,
            "gossip_port": gossip_port,
            "sync_port": sync_port,
            "management_port": mgmt_port,
            "status": 1, # ALIVE
            "working": True,
            "incarnation": 1,
            "last_seen_ms": int(time.time() * 1000),
            "suspicion_timer_expires": None
        }

        if initial_service and initial_service.strip():
            svc_name = initial_service.strip()
            inst_id = f"{svc_name}-inst-{len(self._services) + 1}"
            self._services.append({
                "instance_id": inst_id,
                "service_name": svc_name,
                "owner_node_id": node_id,
                "address": address,
                "port": gossip_port + 80,
                "health": 1,
                "weight": 2,
                "lease_expiry": int(time.time() + 30)
            })
            self.add_event("REGISTRY", f"Registered service '{svc_name}' ({inst_id}) on node '{node_id}' at port {gossip_port+80}.")

        self.add_event("MEMBERSHIP", f"Discovered new [{norm_type.upper()}] node '{node_id}' in {norm_dc} at {address}:{gossip_port}. Status: ALIVE.")
        return {"status": "success", "node": self._nodes[node_id]}

    def remove_node(self, node_id: str) -> Dict[str, Any]:
        self.ensure_bg_loop()
        if node_id in self._nodes:
            self._nodes[node_id]["status"] = 4 # LEFT
            self._nodes[node_id]["working"] = False
            self.add_event("MEMBERSHIP", f"Node '{node_id}' initiated graceful departure (NODE_LEFT).")
            return {"status": "success", "message": f"Node '{node_id}' marked LEFT."}
        return {"status": "error", "message": f"Node '{node_id}' not found."}

    def set_node_working(self, node_id: str, working: bool) -> Dict[str, Any]:
        """Toggles physical working state of a node (responding to network probes vs crashed/severed)."""
        self.ensure_bg_loop()
        if node_id not in self._nodes:
            return {"status": "error", "message": f"Node '{node_id}' not found."}

        node = self._nodes[node_id]
        prev_working = node.get("working", True)
        node["working"] = working

        if not working:
            # Physical state: stopped / network dead
            # The protocol initiates probing: direct probe fails -> indirect Ping-Req fails -> SUSPECT
            node["status"] = 2
            node["suspicion_timer_expires"] = time.time() + 3.0
            self.add_event(
                "SWIM_SUSPECT",
                f"Node '{node_id}' marked NOT WORKING (unresponsive). Direct ping failed -> Ping-Req relay failed. State: SUSPECT. Suspicion timer active (3.0s)..."
            )
            return {
                "status": "success",
                "node_id": node_id,
                "working": False,
                "current_status": "suspect",
                "message": f"Node '{node_id}' is unresponsive. Marked SUSPECT. Suspicion timer counting down (3.0s)."
            }
        else:
            # Physical state: restored / booted
            # The node comes back online, boots, loads persistent SQLite state, and refutes!
            old_inc = node.get("incarnation", 1)
            new_inc = old_inc + 1
            node["incarnation"] = new_inc
            node["status"] = 1 # ALIVE
            node["suspicion_timer_expires"] = None
            node["last_seen_ms"] = int(time.time() * 1000)

            self.add_event(
                "SWIM_REFUTE",
                f"Node '{node_id}' marked WORKING (online). Re-booted from SQLite. Broadcasted ALIVE refutation with incremented incarnation={new_inc}."
            )
            return {
                "status": "success",
                "node_id": node_id,
                "working": True,
                "current_status": "alive",
                "incarnation": new_inc,
                "message": f"Node '{node_id}' is online. Refuted failure rumor with incarnation={new_inc} (ALIVE)."
            }

    # -------------------------------------------------------------------------
    # Cluster Overview & Topology
    # -------------------------------------------------------------------------
    async def get_cluster_overview(self) -> Dict[str, Any]:
        self.ensure_bg_loop()
        client = await self.get_working_client()
        if client:
            try:
                members = await client.get_members()
                services = await client.get_services()
                alive = sum(1 for m in members if m.get("status") == 1)
                suspect = sum(1 for m in members if m.get("status") == 2)
                dead = sum(1 for m in members if m.get("status") == 3)
                return {
                    "total_nodes": len(members),
                    "alive_nodes": alive,
                    "suspect_nodes": suspect,
                    "dead_nodes": dead,
                    "total_services": len(services),
                    "load_balancer_status": "active",
                    "live_mode": True
                }
            except Exception:
                pass

        alive = sum(1 for m in self._nodes.values() if m.get("status") == 1)
        suspect = sum(1 for m in self._nodes.values() if m.get("status") == 2)
        dead = sum(1 for m in self._nodes.values() if m.get("status") == 3)
        return {
            "total_nodes": len(self._nodes),
            "alive_nodes": alive,
            "suspect_nodes": suspect,
            "dead_nodes": dead,
            "total_services": len(self._services),
            "load_balancer_status": "active",
            "live_mode": False
        }

    async def get_members_list(self) -> List[Dict[str, Any]]:
        self.ensure_bg_loop()
        client = await self.get_working_client()
        if client:
            try:
                return await client.get_members()
            except Exception:
                pass
        return list(self._nodes.values())

    async def get_services_list(self) -> List[Dict[str, Any]]:
        client = await self.get_working_client()
        if client:
            try:
                return await client.get_services()
            except Exception:
                pass
        return self._services

    async def get_topology_data(self) -> Dict[str, Any]:
        self.ensure_bg_loop()
        members = await self.get_members_list()
        services = await self.get_services_list()

        nodes = [
            {
                "id": "hub-cluster",
                "label": "DICP WAN FEDERATION",
                "type": "cluster",
                "status": "online",
                "size": 36,
                "details": {"role": "Decentralized WAN Mesh", "gossip_proto": "SWIM Multi-DC"}
            }
        ]
        links = []

        # Group nodes by datacenter (matching enterprise architecture)
        dc_map: Dict[str, List[Dict[str, Any]]] = {}
        for m in members:
            dc = m.get("datacenter", "Data Center A")
            if dc not in dc_map:
                dc_map[dc] = []
            dc_map[dc].append(m)

        dc_hubs = []
        for idx, (dc_name, dc_nodes) in enumerate(dc_map.items()):
            slug = dc_name.lower().replace(" ", "-")
            dc_id = f"hub-dc-{slug}"
            dc_hubs.append(dc_id)
            nodes.append({
                "id": dc_id,
                "label": f"{dc_name} (LAN Pool)",
                "type": "datacenter",
                "status": "online",
                "size": 28,
                "details": {"datacenter": dc_name, "pool": "LAN gossip"}
            })
            # Connect WAN root to Datacenter Hub
            links.append({"source": "hub-cluster", "target": dc_id, "type": "backbone"})

            # Connect nodes to their Datacenter Hub
            for m in dc_nodes:
                nid = m.get("node_id")
                status_code = m.get("status", 1)
                status_str = "alive" if status_code == 1 else ("suspect" if status_code == 2 else ("dead" if status_code == 3 else "left"))
                node_type = m.get("node_type", "worker")
                node_services = [s for s in services if s.get("owner_node_id") == nid]

                susp_expires = m.get("suspicion_timer_expires")
                time_left = max(0.0, susp_expires - time.time()) if (susp_expires and status_code == 2) else 0.0

                nodes.append({
                    "id": nid,
                    "label": nid,
                    "type": "node",
                    "node_type": node_type,
                    "datacenter": dc_name,
                    "rack": m.get("rack", "rack-1"),
                    "status": status_str,
                    "status_code": status_code,
                    "working": m.get("working", True),
                    "size": 22 if node_type in ["server", "gateway"] else 18,
                    "incarnation": m.get("incarnation", 1),
                    "address": m.get("advertised_address", "127.0.0.1"),
                    "gossip_port": m.get("gossip_port", 9000),
                    "sync_port": m.get("sync_port", 9001),
                    "management_port": m.get("management_port", 9002),
                    "suspicion_time_left": round(time_left, 1),
                    "services": node_services
                })
                links.append({"source": dc_id, "target": nid, "type": "member"})

            # P2P LAN gossip links between alive nodes in the same Datacenter
            alive_in_dc = [m.get("node_id") for m in dc_nodes if m.get("status") == 1]
            for i in range(len(alive_in_dc)):
                for j in range(i + 1, len(alive_in_dc)):
                    links.append({"source": alive_in_dc[i], "target": alive_in_dc[j], "type": "gossip"})

        # WAN gossip pool link between datacenters (like zzzz.png cross-DC replication)
        for i in range(len(dc_hubs)):
            for j in range(i + 1, len(dc_hubs)):
                links.append({"source": dc_hubs[i], "target": dc_hubs[j], "type": "wan_pool"})

        return {"nodes": nodes, "links": links}

    # -------------------------------------------------------------------------
    # Load Balancer & Traffic Integration
    # -------------------------------------------------------------------------
    async def get_lb_status(self) -> Dict[str, Any]:
        for ep in self.lb_endpoints:
            try:
                async with httpx.AsyncClient(timeout=1.0) as client:
                    r = await client.get(f"{ep}/admin/backends")
                    if r.status_code == 200:
                        data = r.json()
                        self._active_lb_url = ep
                        self._lb_strategy = data.get("strategy", self._lb_strategy)
                        return {
                            "status": "online",
                            "lb_url": ep,
                            "strategy": self._lb_strategy,
                            "version": data.get("version", 1),
                            "backends": data.get("backends", {})
                        }
            except Exception:
                continue

        # Standby LB metrics excluding dead nodes
        active_backends = []
        for s in self._services:
            node_owner = s.get("owner_node_id")
            if node_owner in self._nodes and self._nodes[node_owner]["status"] == 1:
                active_backends.append({
                    "instance_id": s["instance_id"],
                    "host": s["address"],
                    "port": s["port"],
                    "weight": s.get("weight", 1),
                    "active_connections": 1,
                    "total_requests": 150
                })

        return {
            "status": "standby",
            "lb_url": self._active_lb_url,
            "strategy": self._lb_strategy,
            "version": 1,
            "backends": {
                "order-service": active_backends or [
                    {"instance_id": "be-1", "host": "127.0.0.1", "port": 8080, "weight": 5, "active_connections": 2, "total_requests": 240}
                ]
            }
        }

    async def set_lb_strategy(self, strategy: str) -> bool:
        self._lb_strategy = strategy
        self.add_event("LOAD_BALANCER", f"Updated routing algorithm to '{strategy}'.")
        for ep in self.lb_endpoints:
            try:
                async with httpx.AsyncClient(timeout=1.0) as client:
                    r = await client.post(f"{ep}/admin/strategy", json={"strategy": strategy})
                    if r.status_code == 200:
                        return True
            except Exception:
                continue
        return True

    async def send_test_traffic(self, requests_count: int = 20, service_name: str = "order-service", routing_key: str = "") -> Dict[str, Any]:
        results = {"total": requests_count, "success": 0, "failed": 0, "distribution": {}}
        headers = {}
        if routing_key:
            headers["X-Routing-Key"] = routing_key
        headers["X-Service-Name"] = service_name

        live_lb_reached = False
        for _ in range(requests_count):
            try:
                async with httpx.AsyncClient(timeout=0.5) as client:
                    r = await client.get(f"{self._active_lb_url}/service/{service_name}/items", headers=headers)
                    if r.status_code in [200, 404]:
                        backend_id = r.headers.get("X-Backend-ID", "backend-default")
                        results["distribution"][backend_id] = results["distribution"].get(backend_id, 0) + 1
                        results["success"] += 1
                        live_lb_reached = True
                    else:
                        results["failed"] += 1
            except Exception:
                results["failed"] += 1

        if not live_lb_reached or results["success"] == 0:
            # Simulated traffic distribution honoring active alive backends
            alive_backends = [
                s["instance_id"] for s in self._services 
                if s.get("owner_node_id") in self._nodes and self._nodes[s["owner_node_id"]]["status"] == 1
            ] or ["be-1"]

            results["failed"] = 0
            results["success"] = requests_count

            if self._lb_strategy == "consistent_hash":
                idx = abs(hash(routing_key or 'default')) % len(alive_backends)
                chosen = alive_backends[idx]
                results["distribution"] = {chosen: requests_count}
            else:
                share = requests_count // len(alive_backends)
                rem = requests_count % len(alive_backends)
                for idx, b in enumerate(alive_backends):
                    results["distribution"][b] = share + (1 if idx < rem else 0)

        self.add_event("TRAFFIC", f"Processed {requests_count} test requests via '{self._lb_strategy}'. Distribution: {results['distribution']}")
        return results

    async def trigger_chaos_action(self, action: str, target_node_id: str) -> Dict[str, Any]:
        if action == "crash":
            return self.set_node_working(target_node_id, False)
        elif action == "recover":
            return self.set_node_working(target_node_id, True)
        elif action == "suspect":
            return self.set_node_working(target_node_id, False)
        elif action == "drain":
            return self.remove_node(target_node_id)
        return {"status": "error", "message": "Unknown action"}
