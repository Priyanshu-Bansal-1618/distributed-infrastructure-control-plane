import pytest
from httpx import AsyncClient, ASGITransport
from management.api.app import app, API_SECRET_TOKEN

@pytest.mark.asyncio
async def test_health_endpoint():
    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as ac:
        response = await ac.get("/health")
    assert response.status_code == 200
    assert response.json() == {"status": "healthy"}

@pytest.mark.asyncio
async def test_cluster_overview_fallback():
    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as ac:
        response = await ac.get("/api/v1/cluster")
    assert response.status_code == 200
    data = response.json()
    assert "total_nodes" in data
    assert "alive_nodes" in data
    assert "load_balancer_status" in data

@pytest.mark.asyncio
async def test_unauthorized_mutation_fails():
    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as ac:
        response = await ac.post("/api/v1/nodes/node-1/drain")
    assert response.status_code == 401

@pytest.mark.asyncio
async def test_authorized_mutation_succeeds():
    headers = {"Authorization": f"Bearer {API_SECRET_TOKEN}"}
    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as ac:
        response = await ac.post("/api/v1/nodes/node-1/drain", headers=headers)
    assert response.status_code == 200
    assert response.json()["status"] == "draining"

@pytest.mark.asyncio
async def test_heterogeneous_node_lifecycle_and_swim():
    async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as ac:
        # 1. Add heterogeneous node (Gateway in Data Center B with initial service)
        add_res = await ac.post("/api/v1/nodes/add", json={
            "node_id": "test-gw-1",
            "node_type": "gateway",
            "datacenter": "Data Center B",
            "address": "127.0.0.1",
            "gossip_port": 9800,
            "sync_port": 9801,
            "management_port": 9802,
            "initial_service": "test-edge-svc"
        })
        assert add_res.status_code == 200
        assert add_res.json()["status"] == "success"
        assert add_res.json()["node"]["node_type"] == "gateway"
        assert add_res.json()["node"]["datacenter"] == "Data Center B"

        # 2. Check topology contains the node, datacenter hub, and service
        topo_res = await ac.get("/api/v1/topology")
        assert topo_res.status_code == 200
        topo_data = topo_res.json()
        node_entry = next((n for n in topo_data["nodes"] if n["id"] == "test-gw-1"), None)
        assert node_entry is not None
        assert node_entry["node_type"] == "gateway"
        assert node_entry["datacenter"] == "Data Center B"
        assert any(s["service_name"] == "test-edge-svc" for s in node_entry["services"])

        # 3. Simulate failure (cut network / unworking) -> protocol triggers SUSPECT
        toggle_res = await ac.post("/api/v1/nodes/test-gw-1/toggle_working", json={"working": False})
        assert toggle_res.status_code == 200
        assert toggle_res.json()["current_status"] == "suspect"

        # 4. Simulate recovery (set working) -> node refutes with higher incarnation
        refute_res = await ac.post("/api/v1/nodes/test-gw-1/toggle_working", json={"working": True})
        assert refute_res.status_code == 200
        assert refute_res.json()["current_status"] == "alive"
        assert refute_res.json()["incarnation"] >= 2
