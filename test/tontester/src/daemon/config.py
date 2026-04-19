from pydantic import BaseModel


class DaemonConfig(BaseModel):
    host: str = "127.0.0.1"
    dashboard_port: int = 8080
    grafana_port: int = 3000
    # Per-run Prometheus instances bind one port each from this inclusive range.
    prometheus_port_range: tuple[int, int] = (9100, 9499)
