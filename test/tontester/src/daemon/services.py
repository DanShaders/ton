"""Service factories — how the daemon composes Grafana/Prometheus."""

import json
from pathlib import Path
from typing import final

import yaml
from pydantic import BaseModel

from .compose import HttpReadyProbe, Service, VolumeMount


class ScrapeTarget(BaseModel):
    targets: list[str]
    labels: dict[str, str] = {}


def prometheus_container_name(run_id: str) -> str:
    return f"tontester-prom-{run_id}"


def grafana_container_name() -> str:
    return "tontester-grafana"


def prometheus_service(
    *,
    run_id: str,
    host_port: int,
    config_dir: Path,
    data_dir: Path,
) -> Service:
    """Single factory for both live and archive Prometheus instances."""
    return Service(
        name=prometheus_container_name(run_id),
        image="docker.io/prom/prometheus:latest",
        ports={host_port: 9090},
        volumes=[
            VolumeMount(host_path=config_dir, container_path="/etc/prometheus"),
            VolumeMount(host_path=data_dir, container_path="/prometheus"),
        ],
        command=[
            "--config.file=/etc/prometheus/prometheus.yml",
            "--storage.tsdb.path=/prometheus",
            # The daemon owns lifecycle: we never want Prometheus to delete blocks.
            "--storage.tsdb.retention.time=0s",
            "--storage.tsdb.retention.size=0B",
            "--web.enable-lifecycle",
            "--web.enable-admin-api",
        ],
        ready_probe=HttpReadyProbe(path="/-/ready", container_port=9090),
    )


def write_prometheus_config(config_dir: Path, targets: list[ScrapeTarget]) -> None:
    """Write ``prometheus.yml`` + ``targets.json`` for the given run.

    file_sd is used so the daemon can edit targets without touching the
    running container.
    """
    config_dir.mkdir(parents=True, exist_ok=True)
    config = {
        "global": {
            "scrape_interval": "5s",
            "evaluation_interval": "5s",
        },
        "scrape_configs": [
            {
                "job_name": "ton-validator",
                "file_sd_configs": [
                    {
                        "files": ["/etc/prometheus/targets.json"],
                        "refresh_interval": "5s",
                    }
                ],
            }
        ],
    }
    _atomic_write(
        config_dir / "prometheus.yml",
        yaml.safe_dump(config, default_flow_style=False),
    )
    _atomic_write(
        config_dir / "targets.json",
        json.dumps([t.model_dump() for t in targets], indent=2),
    )


def _atomic_write(path: Path, content: str) -> None:
    tmp = path.with_suffix(path.suffix + ".tmp")
    _ = tmp.write_text(content)
    _ = tmp.replace(path)


def grafana_service(
    *,
    host_port: int,
    data_dir: Path,
    provisioning_dir: Path,
) -> Service:
    return Service(
        name=grafana_container_name(),
        image="docker.io/grafana/grafana:latest",
        ports={host_port: 3000},
        volumes=[
            VolumeMount(host_path=data_dir, container_path="/var/lib/grafana"),
            VolumeMount(host_path=provisioning_dir, container_path="/etc/grafana/provisioning"),
        ],
        env={
            "GF_SECURITY_ADMIN_PASSWORD": "admin",
            "GF_USERS_ALLOW_SIGN_UP": "false",
            "GF_AUTH_ANONYMOUS_ENABLED": "true",
            "GF_AUTH_ANONYMOUS_ORG_ROLE": "Admin",
            "GF_AUTH_DISABLE_LOGIN_FORM": "true",
        },
        ready_probe=HttpReadyProbe(path="/api/health", container_port=3000),
    )


@final
class GrafanaProvisioning:
    """Manages per-run Grafana datasource YAMLs + the dashboard definition.

    One datasource per run (``run-<run_id>.yml``) points at the daemon's
    proxy URL; Grafana hot-reloads provisioned datasources, so adding/removing
    a file is enough — no restart needed.
    """

    def __init__(self, provisioning_dir: Path, daemon_host_url: str):
        self.provisioning_dir = provisioning_dir
        self.daemon_host_url = daemon_host_url

    @property
    def datasources_dir(self) -> Path:
        return self.provisioning_dir / "datasources"

    @property
    def dashboards_dir(self) -> Path:
        return self.provisioning_dir / "dashboards"

    def ensure_dirs(self) -> None:
        self.datasources_dir.mkdir(parents=True, exist_ok=True)
        self.dashboards_dir.mkdir(parents=True, exist_ok=True)
        (self.dashboards_dir / "definitions").mkdir(parents=True, exist_ok=True)

    def write_dashboard_provider(self) -> None:
        self.ensure_dirs()
        provider = {
            "apiVersion": 1,
            "providers": [
                {
                    "name": "default",
                    "orgId": 1,
                    "folder": "",
                    "type": "file",
                    "disableDeletion": False,
                    "updateIntervalSeconds": 10,
                    "options": {"path": "/etc/grafana/provisioning/dashboards/definitions"},
                }
            ],
        }
        _ = (self.dashboards_dir / "dashboard.yml").write_text(yaml.safe_dump(provider))

    def write_dashboard(self) -> None:
        self.ensure_dirs()
        definitions_dir = self.dashboards_dir / "definitions"
        _ = (definitions_dir / "ton-overview.json").write_text(
            json.dumps(_default_dashboard(), indent=2)
        )

    def datasource_file(self, run_id: str) -> Path:
        return self.datasources_dir / f"run-{run_id}.yml"

    def write_run_datasource(self, run_id: str) -> None:
        self.ensure_dirs()
        datasource_url = f"{self.daemon_host_url}/runs/{run_id}/prom"
        payload = {
            "apiVersion": 1,
            "datasources": [
                {
                    "name": f"run-{run_id}",
                    "uid": f"run-{run_id}",
                    "type": "prometheus",
                    "access": "proxy",
                    "url": datasource_url,
                    "isDefault": False,
                    "editable": False,
                    "jsonData": {"httpMethod": "POST"},
                }
            ],
        }
        # Fixed .tmp suffix is safe only because write_run_datasource is never
        # called concurrently for the same run_id: RunsManager.register holds
        # the runs lock, and recover() runs before uvicorn starts accepting
        # IPC. If either of those invariants changes, use a unique suffix.
        tmp = self.datasource_file(run_id).with_suffix(".yml.tmp")
        _ = tmp.write_text(yaml.safe_dump(payload))
        _ = tmp.replace(self.datasource_file(run_id))

    def remove_run_datasource(self, run_id: str) -> None:
        f = self.datasource_file(run_id)
        if f.exists():
            f.unlink()

    def list_provisioned_runs(self) -> set[str]:
        out: set[str] = set()
        if not self.datasources_dir.exists():
            return out
        for f in self.datasources_dir.iterdir():
            if f.name.startswith("run-") and f.suffix == ".yml":
                out.add(f.stem[len("run-") :])
        return out


def _default_dashboard() -> dict[str, object]:
    """Per-run datasource dashboard.

    Every panel queries ``${datasource}``, which is driven by a datasource
    template variable that lists ``run-*`` datasources provisioned by the
    daemon. Selecting a run in the dropdown selects a datasource — the entire
    per-run Prometheus instance — so there is no ``run_id`` label anywhere.
    """

    def panel(
        panel_id: int,
        title: str,
        targets: str | list[tuple[str, str]],
        x: int,
        y: int,
        unit: str = "",
    ) -> dict[str, object]:
        if isinstance(targets, str):
            targets = [(targets, "{{node}} ({{instance}})")]
        p: dict[str, object] = {
            "id": panel_id,
            "title": title,
            "type": "timeseries",
            "datasource": {"type": "prometheus", "uid": "${datasource}"},
            "gridPos": {"h": 8, "w": 12, "x": x, "y": y},
            "targets": [
                {
                    "expr": expr,
                    "refId": chr(ord("A") + i),
                    "legendFormat": legend,
                    "datasource": {"type": "prometheus", "uid": "${datasource}"},
                }
                for i, (expr, legend) in enumerate(targets)
            ],
            "options": {
                "legend": {"displayMode": "list", "placement": "bottom"},
                "tooltip": {"mode": "multi"},
            },
        }
        if unit:
            p["fieldConfig"] = {"defaults": {"unit": unit}, "overrides": []}
        return p

    node_filter = '{node=~"$node"}'

    def app_rate(metric: str) -> str:
        # Sum away the `kind` label so ratios line-match cleanly.
        return f"sum without (kind) (rate({metric}{node_filter}[1m]))"

    def rate(metric: str) -> str:
        return f"rate({metric}{node_filter}[1m])"

    def div(num: str, denom: str) -> str:
        return f"{num} / {denom}"

    def by_tl(metric: str, kind: str) -> str:
        return f'sum by (tl) (rate({metric}{{node=~"$node",kind="{kind}"}}[1m]))'

    def by_type_tl(metric: str) -> str:
        return f'sum by (type, tl) (rate({metric}{{node=~"$node"}}[1m]))'

    panels = [
        panel(1, "Up", f"up{node_filter}", 0, 0),
        panel(2, "Scrape duration", f"scrape_duration_seconds{node_filter}", 12, 0, unit="s"),
        panel(
            3,
            "ADNL ingress bytes/s",
            rate("ton_adnl_net_udp_ingress_bytes_total"),
            0,
            8,
            unit="Bps",
        ),
        panel(
            4, "ADNL egress bytes/s", rate("ton_adnl_net_udp_egress_bytes_total"), 12, 8, unit="Bps"
        ),
        panel(5, "ADNL peers", f"ton_adnl_peers{node_filter}", 0, 16),
        panel(
            6,
            "Exporter collection duration",
            f"ton_exporter_last_collection_duration_seconds{node_filter}",
            12,
            16,
            unit="s",
        ),
        panel(
            10,
            "QUIC TX bytes/s: app vs ngtcp2 vs UDP",
            [
                (app_rate("ton_quic_app_send_bytes_total"), "{{node}} app_send"),
                (rate("ton_quic_summary_tx_bytes_total"), "{{node}} ngtcp2_tx"),
                (rate("ton_quic_udp_egress_bytes_total"), "{{node}} udp_egress"),
            ],
            0,
            24,
            unit="Bps",
        ),
        panel(
            11,
            "QUIC RX bytes/s: app vs stream vs UDP",
            [
                (app_rate("ton_quic_app_deliver_bytes_total"), "{{node}} app_deliver"),
                (rate("ton_quic_summary_stream_bytes_received_total"), "{{node}} stream_rx"),
                (rate("ton_quic_udp_ingress_bytes_total"), "{{node}} udp_ingress"),
            ],
            12,
            24,
            unit="Bps",
        ),
        panel(
            12,
            "QUIC TX expansion ratio (bytes)",
            [
                (
                    div(
                        rate("ton_quic_summary_tx_bytes_total"),
                        app_rate("ton_quic_app_send_bytes_total"),
                    ),
                    "{{node}} ngtcp2/app",
                ),
                (
                    div(
                        rate("ton_quic_udp_egress_bytes_total"),
                        rate("ton_quic_summary_tx_bytes_total"),
                    ),
                    "{{node}} UDP/ngtcp2",
                ),
                (
                    div(
                        rate("ton_quic_udp_egress_bytes_total"),
                        app_rate("ton_quic_app_send_bytes_total"),
                    ),
                    "{{node}} UDP/app",
                ),
            ],
            0,
            32,
        ),
        panel(
            13,
            "QUIC RX expansion ratio (bytes)",
            [
                (
                    div(
                        rate("ton_quic_summary_stream_bytes_received_total"),
                        app_rate("ton_quic_app_deliver_bytes_total"),
                    ),
                    "{{node}} stream/app",
                ),
                (
                    div(
                        rate("ton_quic_udp_ingress_bytes_total"),
                        rate("ton_quic_summary_stream_bytes_received_total"),
                    ),
                    "{{node}} UDP/stream",
                ),
                (
                    div(
                        rate("ton_quic_udp_ingress_bytes_total"),
                        app_rate("ton_quic_app_deliver_bytes_total"),
                    ),
                    "{{node}} UDP/app",
                ),
            ],
            12,
            32,
        ),
        panel(
            14,
            "UDP packets/s",
            [
                (rate("ton_quic_udp_egress_packets_total"), "{{node}} egress"),
                (rate("ton_quic_udp_ingress_packets_total"), "{{node}} ingress"),
            ],
            0,
            40,
            unit="pps",
        ),
        panel(
            15,
            "Avg UDP packet size",
            [
                (
                    div(
                        rate("ton_quic_udp_egress_bytes_total"),
                        rate("ton_quic_udp_egress_packets_total"),
                    ),
                    "{{node}} egress",
                ),
                (
                    div(
                        rate("ton_quic_udp_ingress_bytes_total"),
                        rate("ton_quic_udp_ingress_packets_total"),
                    ),
                    "{{node}} ingress",
                ),
            ],
            12,
            40,
            unit="bytes",
        ),
        panel(
            16,
            "ngtcp2 packets/s (sent / recv / lost)",
            [
                (rate("ton_quic_summary_pkt_sent_total"), "{{node}} sent"),
                (rate("ton_quic_summary_pkt_recv_total"), "{{node}} recv"),
                (rate("ton_quic_summary_pkt_lost_total"), "{{node}} lost"),
            ],
            0,
            48,
            unit="pps",
        ),
        panel(
            17,
            "UDP packets per syscall (batching)",
            [
                (
                    div(
                        rate("ton_quic_udp_egress_packets_total"),
                        rate("ton_quic_udp_egress_syscalls_total"),
                    ),
                    "{{node}} egress",
                ),
                (
                    div(
                        rate("ton_quic_udp_ingress_packets_total"),
                        rate("ton_quic_udp_ingress_syscalls_total"),
                    ),
                    "{{node}} ingress",
                ),
            ],
            12,
            48,
        ),
        panel(
            18,
            "QUIC RTT",
            [
                (f"ton_quic_summary_latest_rtt_seconds{node_filter}", "{{node}} latest"),
                (f"ton_quic_summary_min_rtt_seconds{node_filter}", "{{node}} min"),
                (f"ton_quic_summary_rttvar_seconds{node_filter}", "{{node}} var"),
            ],
            0,
            56,
            unit="s",
        ),
        panel(
            19,
            "QUIC congestion state",
            [
                (f"ton_quic_summary_cwnd_bytes{node_filter}", "{{node}} cwnd"),
                (f"ton_quic_summary_bytes_in_flight{node_filter}", "{{node}} in_flight"),
                (f"ton_quic_summary_unacked_bytes{node_filter}", "{{node}} unacked"),
                (f"ton_quic_summary_unsent_bytes{node_filter}", "{{node}} unsent"),
            ],
            12,
            56,
            unit="bytes",
        ),
        panel(
            20,
            "RLDP queries sent bytes/s by TL",
            [(by_tl("ton_rldp_app_send_bytes_by_tl_total", "query"), "{{tl}}")],
            0,
            64,
            unit="Bps",
        ),
        panel(
            21,
            "RLDP answers received bytes/s by TL",
            [(by_tl("ton_rldp_app_deliver_bytes_by_tl_total", "answer"), "{{tl}}")],
            12,
            64,
            unit="Bps",
        ),
        panel(
            26,
            "RLDP2 queries sent bytes/s by TL",
            [(by_tl("ton_rldp2_app_send_bytes_by_tl_total", "query"), "{{tl}}")],
            0,
            88,
            unit="Bps",
        ),
        panel(
            27,
            "RLDP2 answers received bytes/s by TL",
            [(by_tl("ton_rldp2_app_deliver_bytes_by_tl_total", "answer"), "{{tl}}")],
            12,
            88,
            unit="Bps",
        ),
        panel(
            28,
            "RLDP messages sent bytes/s by TL",
            [(by_tl("ton_rldp_app_send_bytes_by_tl_total", "message"), "{{tl}}")],
            0,
            96,
            unit="Bps",
        ),
        panel(
            29,
            "RLDP messages received bytes/s by TL",
            [(by_tl("ton_rldp_app_deliver_bytes_by_tl_total", "message"), "{{tl}}")],
            12,
            96,
            unit="Bps",
        ),
        panel(
            22,
            "Overlay broadcasts sent bytes/s by type + TL",
            [(by_type_tl("ton_overlay_broadcasts_sent_bytes_by_tl_total"), "{{type}} {{tl}}")],
            0,
            72,
            unit="Bps",
        ),
        panel(
            23,
            "Overlay broadcasts received bytes/s by type + TL",
            [(by_type_tl("ton_overlay_broadcasts_received_bytes_by_tl_total"), "{{type}} {{tl}}")],
            12,
            72,
            unit="Bps",
        ),
        panel(
            24,
            "Overlay messages sent bytes/s by type + TL",
            [(by_type_tl("ton_overlay_messages_sent_bytes_by_tl_total"), "{{type}} {{tl}}")],
            0,
            80,
            unit="Bps",
        ),
        panel(
            25,
            "Overlay messages received bytes/s by type + TL",
            [(by_type_tl("ton_overlay_messages_received_bytes_by_tl_total"), "{{type}} {{tl}}")],
            12,
            80,
            unit="Bps",
        ),
    ]

    datasource_var: dict[str, object] = {
        "name": "datasource",
        "label": "Run",
        "type": "datasource",
        "query": "prometheus",
        # Only the per-run datasources; the dashboard itself doesn't need a global Prometheus.
        "regex": "/^run-/",
        "refresh": 1,
        "multi": False,
        "current": {"text": "", "value": "", "selected": False},
    }
    node_var: dict[str, object] = {
        "name": "node",
        "label": "Node",
        "type": "query",
        "datasource": {"type": "prometheus", "uid": "${datasource}"},
        "query": {
            "query": "label_values(up, node)",
            "refId": "PrometheusVariableQueryEditor",
        },
        "refresh": 2,
        "sort": 1,
        "includeAll": True,
        "allValue": ".*",
        "multi": True,
        "current": {"text": "All", "value": "$__all", "selected": True},
    }

    return {
        "uid": "ton-overview",
        "title": "TON Validator Overview",
        "tags": ["ton"],
        "timezone": "browser",
        "schemaVersion": 38,
        "version": 3,
        "refresh": "5s",
        "time": {"from": "now-15m", "to": "now"},
        "templating": {"list": [datasource_var, node_var]},
        "panels": panels,
    }
