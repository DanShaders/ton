"""Label-selector evaluation.

Pure functions — no resource-store coupling — so tests can exercise the
selector matrix without booting a manager.
"""

from .base import LabelExpression, LabelSelector


def matches(selector: LabelSelector, labels: dict[str, str]) -> bool:
    """Return True iff ``labels`` satisfies every clause of ``selector``.

    An empty selector (no match_labels, no match_expressions) matches
    everything — the same convention k8s uses for ``Selector{}``. Each
    ``match_labels`` entry is an equality requirement; each
    ``match_expressions`` entry is one of the four set-shaped operators.
    All clauses must hold (logical AND).
    """
    for k, v in selector.match_labels.items():
        if labels.get(k) != v:
            return False
    for expr in selector.match_expressions:
        if not _matches_expression(expr, labels):
            return False
    return True


def _matches_expression(expr: LabelExpression, labels: dict[str, str]) -> bool:
    match expr.operator:
        case "In":
            return labels.get(expr.key) in expr.values
        case "NotIn":
            return labels.get(expr.key) not in expr.values
        case "Exists":
            return expr.key in labels
        case "DoesNotExist":
            return expr.key not in labels


def select[T: dict[str, str]](
    selector: LabelSelector, items: list[tuple[T, object]]
) -> list[object]:
    """Return objects whose paired label dict matches ``selector``.

    The pair shape lets callers carry whatever they want alongside the
    labels (e.g. the resource itself) without this function knowing the
    resource model.
    """
    return [obj for labels, obj in items if matches(selector, labels)]
