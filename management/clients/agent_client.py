import httpx
from typing import List, Dict, Any, Optional

class AgentClient:
    def __init__(self, base_url: str = "http://127.0.0.1:8080", timeout: float = 3.0):
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout

    async def get_self(self) -> Dict[str, Any]:
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.get(f"{self.base_url}/api/v1/agent/self")
            resp.raise_for_status()
            return resp.json()

    async def get_members(self) -> List[Dict[str, Any]]:
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.get(f"{self.base_url}/api/v1/agent/members")
            resp.raise_for_status()
            return resp.json()

    async def get_services(self) -> List[Dict[str, Any]]:
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.get(f"{self.base_url}/api/v1/agent/services")
            resp.raise_for_status()
            return resp.json()

    async def register_service(self, payload: Dict[str, Any]) -> Dict[str, Any]:
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.post(f"{self.base_url}/api/v1/agent/services/register", json=payload)
            resp.raise_for_status()
            return resp.json()

    async def deregister_service(self, service_name: str, instance_id: str) -> Dict[str, Any]:
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.post(
                f"{self.base_url}/api/v1/agent/services/deregister",
                json={"service_name": service_name, "instance_id": instance_id}
            )
            resp.raise_for_status()
            return resp.json()

    async def get_configs(self) -> List[Dict[str, Any]]:
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.get(f"{self.base_url}/api/v1/config")
            resp.raise_for_status()
            return resp.json()

    async def set_config(self, config_id: str, key: str, value: str, author: str = "admin") -> Dict[str, Any]:
        async with httpx.AsyncClient(timeout=self.timeout) as client:
            resp = await client.post(
                f"{self.base_url}/api/v1/config",
                json={"config_id": config_id, "key": key, "value": value, "author": author}
            )
            resp.raise_for_status()
            return resp.json()
