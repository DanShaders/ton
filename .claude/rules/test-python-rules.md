# Python test rules

These apply on top of `python-general-rules.md` for `tests/` files.

## No real-time waits in tests

**Don't write polling or sleep-based "settle" loops in tests.** The repository has the primitives to make tests instant and deterministic — use them.

The two antipatterns:

```python
# Polling — slow, flaky, ignores the watch primitive.
deadline = asyncio.get_running_loop().time() + 2.0
while asyncio.get_running_loop().time() < deadline:
    if predicate():
        break
    await asyncio.sleep(0.01)
else:
    pytest.fail(...)

# "Let things settle" — arbitrary, doesn't actually verify settlement.
await asyncio.sleep(0.05)  # let the agent process the event
```

The two correct patterns:

```python
# 1. State-aware: subscribe to the watch bus, wait for an event matching
# a predicate. Use orchestrator.testing.wait_for_event /
# orchestrator.testing.wait_for_state.
result = await wait_for_event(
    store, Workload, lambda w: w.status.phase == "Running"
)

# 2. Schedule-aware: yield until the asyncio loop becomes idle (no
# pending work). Use orchestrator.testing.wait_for_asyncio_idle.
# Replaces "asyncio.sleep(0.05) to let things settle".
await wait_for_asyncio_idle()
```

Both helpers live in `orchestrator.testing` (per the `<pkg>.testing` convention from `python-general-rules.md`).

## Use VirtualClock for instant deterministic tests

Add at the top of every test module that doesn't depend on wall-clock timing:

```python
import pytest

pytestmark = [pytest.mark.asyncio, pytest.mark.usefixtures("virtual_clock")]
```

`virtual_clock` is an opt-in fixture in `tests/orchestrator/conftest.py` that wraps the test in `aiotools.VirtualClock().patch_loop()`. With it:

- `asyncio.sleep(N)` returns instantly while advancing virtual loop time.
- `asyncio.wait_for(..., timeout=N)` still raises `TimeoutError` correctly because virtual time advances when the loop has nothing else to do.
- A 2-second test timeout costs ~milliseconds of real wall clock.

**Don't use it for tests that touch real wall-clock-sensitive code** (real subprocesses with their own startup latency, real network sockets). The current orchestrator tests all qualify because subprocess interactions are either mocked via `monkeypatch.setattr(asyncio, "create_subprocess_exec", ...)` or never hit a real spawn.

## Don't write `virtual_clock: None` parameter annotations

Pytest fixtures decorated with `pytest.mark.usefixtures` don't pass the fixture value into the test as a parameter. Don't fake an unused-parameter:

```python
# wrong — pytest doesn't actually pass anything; the type is a lie.
async def test_thing(virtual_clock: None):
    _ = virtual_clock
    ...

# right — module-level pytestmark applies the fixture without injecting it.
pytestmark = [pytest.mark.asyncio, pytest.mark.usefixtures("virtual_clock")]

async def test_thing():
    ...
```

The exception: when a test genuinely needs the fixture's *return value*, take it as a parameter with the real type:

```python
# returns a Manager — take it as a parameter, type it.
async def test_thing(manager: Manager):
    ...
```

## When to use which helper

| Goal                                                      | Helper                       |
|-----------------------------------------------------------|------------------------------|
| Wait for a workload's status.phase to reach "Running"     | `wait_for_event`             |
| Wait for "all replicas have finalizers" across many rows  | `wait_for_state`             |
| Wait for an event of any kind (e.g. cascade-delete event) | `wait_for_event` w/ predicate|
| Just give the agent's task a chance to run                | `wait_for_asyncio_idle`      |
| Wait for an apply task to block on a held lock            | `wait_for_asyncio_idle`      |
| Real wall-clock timing under test                         | drop `virtual_clock` for that file |

Subscription-based helpers (`wait_for_event` / `wait_for_state`) open a `store.subscription(Kind)` synchronously before iterating, so events fired immediately after the call returns are observed without a race window. Use `async with store.subscription(...) as sub: async for event in sub: ...` directly when you need both the subscription handle and the iteration in the same scope (e.g. open before the action that fires the event you're waiting for).

## Override notes carry over

From `python-general-rules.md`: when overriding a method (or satisfying a Protocol via a class that inherits the Protocol's signature), unused parameters don't need the `_` prefix and don't need `_ = arg` discards. Use the base's parameter names verbatim.
