"""Resource-shape sanity tests.

These don't exercise behavior — just that the pydantic models round-
trip and that label selectors match the expected combinations.
"""

import pytest
from orchestrator import (
    Container,
    HostBinaryImage,
    LabelSelector,
    Metadata,
    Namespace,
    NamespaceSpec,
    NamespaceStatus,
    Workload,
    WorkloadSpec,
)
from orchestrator.resources import (
    LabelExpression,
    matches,
)


def test_workload_roundtrips_through_json():
    wl = Workload(
        metadata=Metadata(name="val-0", namespace="ns1", labels={"role": "validator"}),
        spec=WorkloadSpec(
            containers=[
                Container(name="ve", image=HostBinaryImage(path="/usr/local/bin/validator-engine"))
            ],
        ),
    )
    blob = wl.model_dump_json()
    parsed = Workload.model_validate_json(blob)
    assert parsed.metadata.name == "val-0"
    assert parsed.spec.containers[0].image.kind == "host_binary"
    assert parsed.spec.containers[0].image.path == "/usr/local/bin/validator-engine"


def test_namespace_default_spec_and_status():
    ns = Namespace(metadata=Metadata(name="run-abc"))
    assert isinstance(ns.spec, NamespaceSpec)
    assert isinstance(ns.status, NamespaceStatus)
    assert ns.status.phase == "Active"
    assert ns.api_version == "orchestrator/v1"
    assert ns.kind == "Namespace"


def test_workload_requires_at_least_one_container():
    with pytest.raises(Exception):
        _ = Workload(
            metadata=Metadata(name="x"),
            spec=WorkloadSpec(containers=[]),
        )


def test_label_selector_matches_match_labels_AND():
    sel = LabelSelector(match_labels={"role": "validator", "tier": "test"})
    assert matches(sel, {"role": "validator", "tier": "test", "extra": "x"})
    assert not matches(sel, {"role": "validator"})
    assert not matches(sel, {"role": "validator", "tier": "prod"})


def test_label_selector_match_expressions_in():
    sel = LabelSelector(
        match_expressions=[LabelExpression(key="env", operator="In", values=["dev", "test"])]
    )
    assert matches(sel, {"env": "dev"})
    assert matches(sel, {"env": "test"})
    assert not matches(sel, {"env": "prod"})


def test_label_selector_exists_doesnotexist():
    sel_exists = LabelSelector(
        match_expressions=[LabelExpression(key="canary", operator="Exists", values=[])]
    )
    sel_dne = LabelSelector(
        match_expressions=[LabelExpression(key="canary", operator="DoesNotExist", values=[])]
    )
    assert matches(sel_exists, {"canary": "true"})
    assert not matches(sel_exists, {"role": "x"})
    assert not matches(sel_dne, {"canary": "x"})
    assert matches(sel_dne, {"role": "x"})


def test_empty_selector_matches_everything():
    assert matches(LabelSelector(), {})
    assert matches(LabelSelector(), {"a": "b"})


def test_workload_status_default_phase_is_pending():
    wl = Workload(
        metadata=Metadata(name="x"),
        spec=WorkloadSpec(
            containers=[Container(name="c", image=HostBinaryImage(path="/bin/true"))]
        ),
    )
    assert wl.status.phase == "Pending"
    assert wl.status.host is None
    assert wl.metadata.uid == ""  # store assigns later
    assert wl.metadata.generation == 0
