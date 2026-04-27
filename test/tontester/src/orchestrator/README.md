# orchestrator

Hand-rolled k8s-shape control plane for tontester. In-memory typed store, watch bus, declarative resources, controllers, in-process agent. Designed for laptop-scale dev orchestration with a clear seam for fleet deployment later.

The full design rationale (why not k3s/k8s/Nomad, why crun, why a non-distributed control plane) is in `../../../tontester-design-snapshot.md`. This README is the operational reference: what's here, how it fits together, and the rules that emerged from 3 audit rounds.

## Architecture

```
                          ┌──────────────────────────┐
   client.apply(workload) │                          │  client.subscription
        ─────────────►    │   InMemoryStore          │     ◄─────
                          │   (per-kind asyncio.Lock,│
                          │    optimistic conc,      │
                          │    watch bus per kind)   │
                          └────┬───────────────┬─────┘
                               │ events        │ events
                               ▼               ▼
                       ┌──────────────┐  ┌──────────────────────┐
                       │ Agent        │  │ ControllerRunner     │
                       │ (per host)   │  │ (per controller)     │
                       │  ─ desired   │  │  ─ watch loops       │
                       │    loop      │  │  ─ workqueue         │
                       │  ─ runtime   │  │  ─ worker tasks      │
                       │    event loop│  │                      │
                       └──────┬───────┘  └──────────┬───────────┘
                              │                      │
                              ▼                      ▼
                       ┌──────────────┐       ┌──────────┐
                       │  Runtime     │       │ store.   │
                       │ (subprocess /│       │ apply/   │
                       │  fake / runc)│       │ delete/  │
                       │              │       │ patch    │
                       └──────────────┘       └──────────┘
```

### File map

```
src/orchestrator/
  resources/         # pydantic resource shapes (Workload, Namespace, ...)
    base.py          # Metadata, Condition, OwnerRef, LabelSelector, Resource, ResourceLike
    namespace.py
    workload.py
    workloadset.py
    networkpolicy.py
    portforward.py
    matching.py      # label-selector evaluation
  store/
    store.py         # InMemoryStore: single-writer, per-kind lock, optimistic concurrency
    watch.py         # WatchBus + Subscription (sync register, async iter)
  control/
    manager.py       # Owns store + runners; AsyncExitStack lifecycle
    controller.py    # Controller Protocol + ControllerRunner (workqueue + tasks)
    workqueue.py     # Heap-backed dedup queue with backoff
    reconcilers/
      namespace.py   # Cascade-delete reconciler
      workloadset.py # Replicas + ordinals + recreate strategy
  runtime/
    protocol.py      # Runtime Protocol + RuntimeEvent
    subprocess_runtime.py   # asyncio.subprocess + per-key locks + cgroup
    fakes.py         # FakeRuntime for tests
  agent/
    agent.py         # In-process kubelet equivalent
    cleanup.py       # Startup FS cleanup
  client/
    client.py        # Client Protocol
    local.py         # LocalClient (direct method calls)
  errors.py          # Typed exceptions
  ids.py             # uid + ordinal naming
  testing.py         # wait_for_event / wait_for_state / wait_for_asyncio_idle
```

### Lifecycle

- **Construction is inert.** `Manager()`, `Agent(...)` allocate; nothing runs. Register kinds, add controllers via sync setup methods.
- **`Manager.start()` and `Agent.start()` use AsyncExitStack `pop_all`.** Resources push cleanups onto a *bootstrap* stack as they come up; on success, the stack is `pop_all()`-transferred to the long-lived `self._stack`. Any exception escaping the `async with bootstrap:` (including `CancelledError`) runs the bootstrap's cleanups via `__aexit__`. No `except BaseException` needed.
- **`shutdown()` order is explicit.** Manager: `await stack.aclose()` (stops runners) → `_safe_close_store_sync()` (closes bus). Encoded as separate phases, not via stack push order.
- **Crash semantics: orchestrator crash = payload crash.** No durable state. The daemon re-applies its desired state on reconnect. Agents would (when fleet support lands) suicide on heartbeat loss + SIGKILL their workers.

## Rules learned through 3 audit rounds

These are the recurring shapes of bugs the audits found. Read these before adding new code in this package.

### Cancellation safety

- **Use AsyncExitStack `pop_all` for "acquire several resources, all-or-nothing."** Push cleanups onto a bootstrap as you go; on success do `self._stack = bootstrap.pop_all()`; on any exception (including `CancelledError`) the bootstrap's `__aexit__` fires every cleanup. Don't write `try / except BaseException / await cleanup(); raise` — `BaseException` is a code smell, easy to forget the re-raise, and easy to accidentally catch `KeyboardInterrupt`.
- **`try / finally + fully_started` flag** is the lighter cousin of pop_all when you don't have multiple resources to manage — for `_started` flag reset in particular.
- **`try / finally`** around any individual resource that must be cleaned on abnormal exit. Specifically: `_spawn`'s cgroup_dir cleanup uses a `spawned` flag in finally, not `except OSError` (which misses `CancelledError`).

### Subscription discipline

- **`async with store.subscription(Kind) as sub:` is the only sanctioned way to subscribe.** The Subscription is single-use (raises on second `__aiter__` call). The bus registers synchronously in `__init__`; `__aexit__` calls `WatchBus.unregister(token)` which both closes the queue *and* removes from `_subscriptions` (round-2 finding 1.2: closing the queue alone leaks the token in the bus's set).
- **Open subscription before issuing the action that fires the event** you want to observe. Synchronous registration means events fired immediately after `subscription()` returns are observed.
- **`Runtime.watch()` yields `None` first** as a registration sentinel — same pattern. Agent's `_runtime_event_loop` uses this to set `_runtime_ready`; `agent.start` blocks on both `_desired_ready` and `_runtime_ready`.

### Store / spec / status invariants

- **Store is the only writer.** Every mutation goes through `apply` / `patch_status` / `patch_metadata` / `delete`, all under the per-kind lock. No yields between read-modify-write. Watch publish happens inside the lock so events arrive in resource_version order.
- **Spec ↔ status is enforced.** `apply` writes spec + writable user metadata (labels, annotations); `patch_status` writes status + a small subset of metadata; `patch_metadata` writes finalizers/labels/annotations. Mutators that try to change the wrong half raise `ValidationError`.
- **`_update_spec` preserves controller-managed metadata.** Finalizers and owner_refs come from `existing`, not `desired`. Otherwise a user re-applying a workload silently disables the agent's cleanup finalizer and breaks cascade-delete.
- **`generation` bumps only on spec change. `resource_version` bumps on every write.** `observed_generation` on conditions is what controllers use to detect "fresh vs stale" status.
- **Returned objects are deep-copied.** Callers can mutate them without affecting the stored row.

### Reconciler pattern

- **Event-driven, no `requeue_after_s` for polling.** If a controller cares about a state change, the relevant kind's watch will fire when it happens. `requeue_after_s=0.5` was a bug — combined with the `was_dirty` immediate re-add it created hot reconcile loops that starved other tasks.
- **`Controller` is covariant in its owned kind** (`_TOwned: covariant=True`). Lets the manager hold a heterogeneous `list[ControllerRunner[_AnyResource]]` while concrete callers see the narrowed type.
- **`ControllerRunner.start()` self-cleans on exception.** If start raises after creating tasks (e.g. cancelled mid-await on `ready.wait`), the runner's `try/except BaseException → self.stop()` cancels them. Manager's stack-rollback then doesn't have to know about partial start state.
- **Don't claim Reconciled from the runtime event loop.** Only the desired loop knows what generation it actually applied. The runtime event loop writes phase/host/Available; if it also wrote Reconciled, a stale STARTED for an old generation would write Reconciled with the *current* generation (read from `current.metadata.generation` at event time) and falsely mark a newer spec as already-reconciled. `_already_reconciled` then skips applying it.
- **`_already_reconciled` checks `(type, status, reason, observed_generation)`.** Reason `"ApplyOk"` is the agent's own marker — any other writer of `Reconciled=True` (a future controller, a test fixture) is ignored. Without the reason check, the agent silently disables itself on foreign Reconciled writes.

### Runtime backend

- **`SubprocessRuntime` uses per-key `asyncio.Lock`s.** Acquired before any `_procs[key]` access, on both apply and delete. The previous "lock on `existing._proc.lock`" pattern had a race window during the spawn between `_procs.get` and `_procs[key] = proc`.
- **`apply` re-checks `self._closed` after acquiring the lock.** A concurrent `close()` may have set `_closed` between the cheap top-level check and our lock acquisition; without the re-check we spawn a process that close's drain pass already finished iterating.
- **`_wait_exit` reaper registers immediately after `_procs[key] = proc`.** Before any subsequent `_publish` or probe-loop spawn. A cancellation between `_procs[key] = proc` and `_spawn_bg(_wait_exit(...))` would leak an unreaped child otherwise.
- **`_spawn` uses `try/finally` with `spawned` flag**, not `except OSError`. CancelledError isn't an OSError; the previous narrow catch left cgroup_dir on disk on cancellation.

### WorkQueue

- **`close()` is authoritative.** `get()` raises `QueueClosed` even if the heap still has future-deadline items. The previous "wait for heap empty" path could spin-wait on a permanently-set `_not_empty` event when scheduled items were due in the future.
- **`done(success=False) + was_dirty` flag** schedules a backoff requeue. Don't combine this with `requeue_after_s` to "double-protect" — see the reconciler rule above.

### Watch bus

- **Overflow is checked at the top of every iter loop**, not just after the None sentinel. Under sustained pressure the queue stays full and the None push fails too — without the top-of-loop check, the consumer blocks in `next_event` forever even though its subscription is already broken.
- **`Subscription.close` is idempotent.** Double-close (via context-manager exit + explicit `WatchBus.close()`) is harmless.

### Variance / typing

- **Generic resources had a wall of variance pain.** Resolution: `Resource` is non-generic (just `metadata`); concrete subclasses (Workload, Namespace) declare their own `spec`/`status`/`api_version`/`kind` fields directly. `ResourceLike[Spec_co, Status_co]` is a covariant Protocol used at the store boundary; concrete pydantic classes satisfy it structurally.
- **No `# type: ignore`, no `cast()`, no `Any`** anywhere except the one Protocol declaration that has to mirror pydantic's `Any` returns from `model_copy` / `model_dump`. Encapsulated.

## Test discipline

See `/home/danklishch/code/ton/src/.claude/rules/test-python-rules.md` for the full rules. The TL;DR:

- **`pytestmark = [pytest.mark.asyncio, pytest.mark.usefixtures("virtual_clock")]`** at the top of every test module makes timeouts virtual and tests run in milliseconds.
- **Helpers from `orchestrator.testing`:**
  - `wait_for_event(store, Kind, predicate)` — subscribes, returns the first matching resource.
  - `wait_for_state(store, Kind, state_check)` — subscribes, re-evaluates a predicate after each event (use for "all replicas have finalizers" or external side-channels like `fake_runtime.applies`).
  - `wait_for_asyncio_idle()` — yields until the pending task set is stable across N consecutive ticks. Replaces `asyncio.sleep(0.05)` "let things settle" patterns.
- **Don't write polling loops** with `while ... await asyncio.sleep(0.01)`. Don't take `virtual_clock: None` as a parameter (lie — `usefixtures` doesn't pass anything in).
- **Tests live in `tests/orchestrator/`, helpers in `orchestrator.testing`** (per the `<pkg>.testing` convention from `python-general-rules.md`).

## What's deliberately not built

The architecture supports these but they're out of scope for the prototype. Adding them is "labor, not redesign":

- **Remote agents.** `Agent` runs in-process today. Fleet deploys would run `Agent` as a separate process on each host, talking to `Manager` over WS+msgpack. The `Runtime` Protocol is already the seam.
- **WireGuard overlay reconciler.** `NetworkPolicy` resource exists; no reconciler yet. For cross-host network games (slow-validator-style chaos).
- **`PortForward` reconciler.** Resource exists; pasta/socat reconciler is ~50 LoC of glue.
- **`NetworkPolicy` reconciler.** nft per netns. ~200 LoC.
- **`RuncRuntime`.** Performance fix (~13ms cold start vs ~370ms podman). `Runtime` Protocol is already there; just write the OCI bundle synthesizer.
- **Heartbeat + suicide-on-disconnect.** For the fleet path: agents kill their workers if they lose the manager. Trivial protocol, ~100 LoC.

The control plane shape itself is converged: 3 audit rounds, each finding fewer issues of the same shape, with the round-3 fixes targeting actual root causes (cleanup pairs, generation discipline, subscription registration). The next session should be implementing one of the deferred features above — not re-designing what's here.
