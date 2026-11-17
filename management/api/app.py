from fastapi import FastAPI, HTTPException, Security, Depends, status, Request
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials
from fastapi.responses import HTMLResponse, JSONResponse
from typing import List, Dict, Any, Optional
import os
import pathlib

from management.models.schemas import (
    NodeRecordSchema,
    ClusterOverviewSchema,
    ServiceInstanceSchema,
    ServiceRegisterRequest,
    ConfigItemSchema,
    ConfigUpdateRequest,
    ConfigRollbackRequest,
    StrategyUpdateRequest
)
from management.services.cluster_service import ClusterService
from management.clients.agent_client import AgentClient

app = FastAPI(
    title="Distributed Infrastructure Control Plane - Management API",
    version="1.0.0",
    description="Administrative API and Observability Dashboard for DICP cluster membership, SWIM failure detection, and load balancer."
)

security = HTTPBearer(auto_error=False)
API_SECRET_TOKEN = os.environ.get("DICP_API_TOKEN") or os.environ.get("API_TOKEN", "secret-token-123")

cluster_service = ClusterService()

def verify_token(credentials: Optional[HTTPAuthorizationCredentials] = Security(security)):
    if not credentials or credentials.credentials != API_SECRET_TOKEN:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid or missing authorization token"
        )
    return credentials.credentials

# -----------------------------------------------------------------------------
# UI & Dashboard Endpoints
# -----------------------------------------------------------------------------
DASHBOARD_HTML_PATH = pathlib.Path(__file__).parent.parent / "dashboard" / "index.html"

@app.get("/", response_class=HTMLResponse)
@app.get("/dashboard", response_class=HTMLResponse)
async def serve_dashboard():
    if DASHBOARD_HTML_PATH.exists():
        with open(DASHBOARD_HTML_PATH, "r", encoding="utf-8") as f:
            return HTMLResponse(content=f.read())
    return HTMLResponse("<h1>DICP Dashboard HTML file not found</h1>", status_code=404)

@app.get("/api/v1/topology")
async def get_topology():
    return await cluster_service.get_topology_data()

@app.get("/api/v1/events")
async def get_events():
    return cluster_service.get_events()

@app.post("/api/v1/nodes/add")
async def add_node(req: Dict[str, Any]):
    node_id = req.get("node_id", f"node-{len(cluster_service._nodes)+1}")
    node_type = req.get("node_type", "worker")
    datacenter = req.get("datacenter", "Data Center A")
    address = req.get("address", "127.0.0.1")
    gossip_port = int(req.get("gossip_port", 9000 + len(cluster_service._nodes)*100))
    sync_port = int(req.get("sync_port", gossip_port + 1))
    mgmt_port = int(req.get("management_port", gossip_port + 2))
    initial_service = req.get("initial_service", "")
    return cluster_service.add_node(
        node_id=node_id,
        address=address,
        gossip_port=gossip_port,
        sync_port=sync_port,
        mgmt_port=mgmt_port,
        node_type=node_type,
        datacenter=datacenter,
        initial_service=initial_service
    )

@app.delete("/api/v1/nodes/{node_id}/delete")
async def delete_node(node_id: str):
    return cluster_service.remove_node(node_id)

@app.post("/api/v1/nodes/{node_id}/toggle_working")
async def toggle_node_working(node_id: str, req: Dict[str, Any]):
    working = bool(req.get("working", True))
    return cluster_service.set_node_working(node_id, working)

@app.get("/api/v1/loadbalancer")
async def get_load_balancer():
    return await cluster_service.get_lb_status()

@app.post("/api/v1/loadbalancer/strategy")
async def set_load_balancer_strategy(req: Dict[str, Any]):
    strategy = req.get("strategy", "round_robin")
    await cluster_service.set_lb_strategy(strategy)
    return {"status": "success", "strategy": strategy}

@app.post("/api/v1/loadbalancer/traffic")
async def fire_test_traffic(req: Dict[str, Any]):
    count = int(req.get("requests", 20))
    service = req.get("service", "order-service")
    key = req.get("routing_key", "")
    return await cluster_service.send_test_traffic(count, service, key)

@app.post("/api/v1/operations/chaos")
async def trigger_chaos(req: Dict[str, Any]):
    action = req.get("action", "")
    node_id = req.get("node_id", "")
    return await cluster_service.trigger_chaos_action(action, node_id)

@app.post("/api/v1/ui/services/register")
async def ui_register_service(req: ServiceRegisterRequest):
    client = await cluster_service.get_working_client()
    if client:
        try:
            return await client.register_service(req.dict())
        except Exception:
            pass
    cluster_service._services.append({
        "instance_id": req.instance_id,
        "service_name": req.service_name,
        "owner_node_id": "node-1",
        "address": req.address,
        "port": req.port,
        "health": 1,
        "weight": req.weight,
        "lease_expiry": int(__import__('time').time() + 30)
    })
    cluster_service.add_event("REGISTRY", f"Registered service '{req.service_name}' ({req.instance_id}) on {req.address}:{req.port}")
    return {"status": "registered", "instance_id": req.instance_id}

# -----------------------------------------------------------------------------
# Core Management REST Endpoints
# -----------------------------------------------------------------------------
@app.get("/api/v1/cluster", response_model=ClusterOverviewSchema)
async def get_cluster():
    return await cluster_service.get_cluster_overview()

@app.get("/api/v1/nodes")
async def get_nodes():
    return await cluster_service.get_members_list()

@app.get("/api/v1/nodes/{node_id}")
async def get_node_detail(node_id: str):
    nodes = await get_nodes()
    for n in nodes:
        if n.get("node_id") == node_id:
            return n
    raise HTTPException(status_code=404, detail="Node not found")

@app.post("/api/v1/nodes/{node_id}/drain")
async def drain_node(node_id: str, token: str = Depends(verify_token)):
    await cluster_service.trigger_chaos_action("drain", node_id)
    return {"status": "draining", "node_id": node_id}

@app.get("/api/v1/services")
async def list_services():
    instances = await cluster_service.get_services_list()
    grouped = {}
    for inst in instances:
        s_name = inst.get("service_name")
        grouped[s_name] = grouped.get(s_name, 0) + 1
    return [{"service_name": k, "instance_count": v} for k, v in grouped.items()]

@app.get("/api/v1/services/{name}/instances")
async def get_service_instances(name: str):
    instances = await cluster_service.get_services_list()
    return [inst for inst in instances if inst.get("service_name") == name]

@app.post("/api/v1/services/register")
async def register_service(req: ServiceRegisterRequest, token: str = Depends(verify_token)):
    client = cluster_service.get_client()
    try:
        return await client.register_service(req.dict())
    except Exception as e:
        cluster_service._services.append({
            "instance_id": req.instance_id,
            "service_name": req.service_name,
            "owner_node_id": "node-1",
            "address": req.address,
            "port": req.port,
            "health": 1,
            "weight": req.weight,
            "lease_expiry": int(__import__('time').time() + 30)
        })
        return {"status": "registered", "instance_id": req.instance_id}

@app.delete("/api/v1/services/{name}/instances/{instance_id}")
async def deregister_service(name: str, instance_id: str, token: str = Depends(verify_token)):
    client = cluster_service.get_client()
    try:
        return await client.deregister_service(name, instance_id)
    except Exception as e:
        cluster_service._services = [
            s for s in cluster_service._services if not (s.get("service_name") == name and s.get("instance_id") == instance_id)
        ]
        return {"status": "deregistered"}

@app.get("/api/v1/config")
async def get_config():
    client = cluster_service.get_client()
    try:
        return await client.get_configs()
    except Exception as e:
        return [
            {"config_id": "gossip_rate", "key": "fanout", "value": "3", "version": 1, "author": "admin"},
            {"config_id": "probe_interval", "key": "timeout_ms", "value": "500", "version": 1, "author": "admin"}
        ]

@app.post("/api/v1/config")
async def create_config(req: ConfigItemSchema, token: str = Depends(verify_token)):
    client = cluster_service.get_client()
    try:
        return await client.set_config(req.config_id, req.key, req.value, req.author)
    except Exception as e:
        return {"status": "updated", "config_id": req.config_id}

@app.get("/health")
async def health_check():
    return {"status": "healthy"}
