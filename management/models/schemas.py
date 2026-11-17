from pydantic import BaseModel, Field
from typing import List, Dict, Optional

class NodeRecordSchema(BaseModel):
    node_id: str
    advertised_address: str
    gossip_port: int
    sync_port: int
    management_port: int
    status: int
    incarnation: int
    last_seen_ms: Optional[int] = 0

class ClusterOverviewSchema(BaseModel):
    total_nodes: int
    alive_nodes: int
    suspect_nodes: int
    dead_nodes: int
    total_services: int
    load_balancer_status: str

class ServiceInstanceSchema(BaseModel):
    instance_id: str
    service_name: str
    owner_node_id: str
    address: str
    port: int
    health: int
    weight: int = 1
    lease_expiry: int = 0

class ServiceRegisterRequest(BaseModel):
    service_name: str
    instance_id: str
    address: str
    port: int
    weight: int = 1
    health_check_url: Optional[str] = None
    target_node_id: Optional[str] = None

class ConfigItemSchema(BaseModel):
    config_id: str
    key: str
    value: str
    version: int
    author: str

class ConfigUpdateRequest(BaseModel):
    key: str
    value: str
    author: str = "admin"

class ConfigRollbackRequest(BaseModel):
    target_version: int
    author: str = "admin"

class StrategyUpdateRequest(BaseModel):
    strategy: str # round_robin, weighted_round_robin, least_conn, consistent_hash
