"""UID + ordinal-name helpers."""

import uuid


def new_uid() -> str:
    """Allocate a fresh, never-recurring object UID.

    UUID4 is overkill for cardinality (we'll never have 2^122 resources)
    but matches the format k8s uses, which keeps logs uniform.
    """
    return str(uuid.uuid4())


def child_name(set_name: str, ordinal: int) -> str:
    """Compose a stable child name from set name + ordinal.

    Mirrors k8s StatefulSet (``foo-0``, ``foo-1``, ...). Ordinal is the
    only thing that must be stable across restarts — names are otherwise
    just convenience.
    """
    return f"{set_name}-{ordinal}"
