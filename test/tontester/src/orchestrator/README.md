# orchestrator

Hand-rolled k8s-shape control plane for tontester. In-memory typed store, watch bus, declarative resources, controllers, in-process agent. Designed for laptop-scale dev orchestration with a clear seam for fleet deployment later.

The full design rationale (why not k3s/k8s/Nomad, why crun, why a non-distributed control plane) is in `../../../tontester-design-snapshot.md`.

The async-resource model — why `running()` + `shutdown(deadline)`, why sync `__aexit__`, why no `force_kill` — is in [`DESIGN.md`](DESIGN.md). Read it before adding new resource types.

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
                       │  ─ reconciler│  │  ─ watch loops       │
                       │    runner    │  │  ─ workqueue         │
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
  DESIGN.md          # Resource Protocol design rationale
  README.md          # this file
  lifecycle.py       # Resource Protocol, StopToken, CheckedExitStack, GracefulAbort
  broadcast.py       # BroadcastQueue (overflow-safe fan-out, used by WatchBus + Runtime)
  errors.py          # typed exceptions
  ids.py             # uid + ordinal naming
  testing.py         # wait_for_event / wait_for_state / wait_for_asyncio_idle
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
    watch.py         # WatchBus (wraps BroadcastQueue + WatchOverflow conversion)
  control/
    manager.py       # Owns store + runners; AsyncExitStack lifecycle
    controller.py    # Controller Protocol + ControllerRunner (workqueue + tasks, TaskGroup-based)
    workqueue.py     # Heap-backed dedup queue with backoff + supersede support
    cascading.py     # CascadingReconciler base for finalizer + cascade-delete
    reconcilers/
      namespace.py   # NamespaceReconciler (cascade-delete via base class)
      workloadset.py # WorkloadSetReconciler (replicas + ordinals + recreate; finalizer via base)
  runtime/
    protocol.py      # Runtime Protocol (Resource shape: running/shutdown/stop_token + apply/delete/get/list/watch)
    subprocess_runtime.py  # asyncio.subprocess + supervisor task per workload + cgroup
    fakes.py         # FakeRuntime for tests
  agent/
    agent.py         # In-process kubelet equivalent (Resource shape; reconciler-based)
    cleanup.py       # Startup FS cleanup
  client/
    client.py        # Client Protocol
    local.py         # LocalClient (direct method calls)
```

### Resource lifecycle (current shape)

Every async-stateful component implements (or will implement) the
`Resource` Protocol from `lifecycle.py`:

```python
async with resource.running() as r:
    ...                                              # use r
    await r.shutdown()                               # explicit graceful drain
    # or: await asyncio.wait_for(r.shutdown(), 30)   # bounded
# __aexit__ is sync; reconciles any half-state from an interrupted shutdown.
```

Three primitives back this:

- **`StopToken`** — hierarchical cancellation. Children cascade from parents; tree mirrors resource ownership; never re-parented.
- **`CheckedExitStack`** — `AsyncExitStack` with safe-point checks against a stop_token. Multi-step `__aenter__` chains automatically abort gracefully if the token fires mid-init.
- **Sync `__aexit__`** — the load-bearing rule. No `await` in `running()`'s exit path means cancellation can never cut cleanup short.

Forced shutdown is the cancellation path: cancel the task running `async with running():` and the resulting sync `__aexit__` is the emergency stop. There is no separate `force_kill()` API.

Migration status:
- ✅ `Runtime` Protocol extends `Resource` directly.
- ✅ `SubprocessRuntime` — full Resource shape, sync `__aexit__`. Per-workload `_ProcessSupervisor` is itself a `Resource` (sync release on cancel; graceful SIGTERM→SIGKILL→reap on `shutdown()`).
- ✅ `FakeRuntime` — full Resource shape.
- ✅ `Agent` — Resource shape, fully sync `__aexit__`. `shutdown()` awaits runner + runtime + event-loop task in dependency order.
- ✅ `ControllerRunner` — Resource shape (sync `__aexit__` cancels tasks + closes queue, `shutdown()` awaits drain).
- ✅ `Manager` — Resource shape (sync `__aexit__` signals stop, `shutdown()` awaits each runner in reverse).

### Crash semantics

Orchestrator crash = payload crash. No durable state. The daemon re-applies its desired state on reconnect. Agents would (when fleet support lands) suicide on heartbeat loss + SIGKILL their workers.

## Audit findings — what's been fixed

The package has been through 5 audit rounds plus a focused session on resource lifecycle. Findings consolidated below.

### Rounds 1-4 (legacy, all fixed)

| Round | Finding shape | Status |
|-------|---------------|--------|
| 1 | Subscription token leak / overflow handling | Structurally fixed: `BroadcastQueue` |
| 2 | `_spawn` cgroup leak on cancel; subscription single-use | Structurally fixed: `owned_path` / `async with subscription` |
| 3 | Cleanup ordering (close-before-cancel-tasks) | Structurally fixed: `AsyncExitStack` + nested resources |
| 3 | `_already_reconciled` namespacing | Fixed: check on `(type, status, reason, observed_generation)` with reason guard |
| 4 | WorkQueue busy-spin; runtime watch overflow strands consumer; subprocess restart-on-zombie path | Structurally fixed: per-call wakeup futures; `BroadcastQueue` everywhere; supervisor pattern |
| 4 | WorkloadSet orphans on direct delete | Structurally fixed: `CascadingReconciler` base class enforces finalizer + child enumeration |
| 4 | `Manager.shutdown` permanent no-op after failure; `stop()` swallows external cancel | Structurally fixed: `running()` async context manager pattern; no `_stopping` flag |

### Round 5

| Finding | Severity | Status |
|---------|----------|--------|
| 1. `_restart_locked` skips reaper + probe registration | HIGH | Eliminated by supervisor pattern (no separate reaper to forget) |
| 2. `_close()` races in-flight `apply()` | HIGH | Reduced via supervisor pattern; remaining edge cases are theoretical (test-only paths) |
| 3. `ControllerRunner.running()` hangs forever on watch-loop setup failure | HIGH | Fixed: ready is `Future[None]` with set_exception on first-time failure |
| 4. `_wait_exit` leaks `_procs` entry + cgroup_dir on natural exit | MED | Eliminated by supervisor pattern |
| 5. Duplicate EXITED on graceful stop | MED | Eliminated by supervisor pattern |
| 6. WorkQueue.add_after deadline lost during in-flight | MED | Fixed: `_dirty_in_flight` tracks deadline as dict[ref, float] |
| 7. `_wait_exit` reads stale `proc.process` after `_restart_locked` mutation | MED | Eliminated by supervisor pattern (no in-place mutation) |
| 8. Agent has no per-workload serialization | MED | Fixed structurally: agent is a Controller; WorkQueue's `_in_flight` provides per-key serialization |
| 9. Dead `except BroadcastOverflow` in `WatchBus.subscribe` | LOW | Fixed: removed |
| 10. `clean_state_dir` symlink handling | LOW | Verified working as-is; tests added |

### Resource Protocol session

- Initially: `running()` had an async `__aexit__` (calling `_close` via `await`); cancellation could cut cleanup short. Identified during the design discussion that followed.
- Replaced with: sync `__aexit__` (signals only) + explicit `shutdown(deadline)`. Caller-cancel of `shutdown` is "stop waiting"; `__aexit__` is the deterministic reconciler.
- Converted `Runtime` (both backends) and `Agent` to the new shape. `ControllerRunner` and `Manager` are the documented seam for future cleanup.

### Round 6 — protocol completion + Reaper

Two-phase: an audit after the full `Resource` Protocol rollout (every component now satisfies the protocol), then a structural pass introducing the `Reaper` for kernel-state cleanup that doesn't fit sync `__aexit__`.

| Finding | Severity | Status |
|---------|----------|--------|
| 1. `except (CancelledError, Exception): pass` in supervisor / agent shutdown swallows the outer caller-cancel; violates Resource Protocol's "caller-cancel propagates" contract | HIGH | Structurally fixed: `cancel_and_collect()` helper distinguishes inner cancel (issued by us) from outer cancel (`current_task().cancelling() > 0`); migrated `_ProcessSupervisor.shutdown` and `Agent.shutdown` to it |
| 2. `_safe_rmtree` had `_ = time.sleep` (assigning the function reference instead of calling it) — sloppy AI cruft from when the discipline forbade `await` and someone realized sync `time.sleep` blocks the loop | HIGH | Eliminated: cgroup retry-until-empty migrated to `Reaper.offload(_async_rm_cgroup_with_retry)`; the bogus line is gone |
| 3. `_safe_rmtree` parses `cgroup.procs` as ints under `except OSError:` — kernel race writing partial pid lines causes `ValueError` to escape the sync stack-callback during unwind | HIGH | Fixed: catch `(OSError, ValueError)` |
| 4. `GracefulAbort` raised by `CheckedExitStack` before the first `yield` makes the `@asynccontextmanager` generator return without yielding → `RuntimeError("generator didn't yield")` instead of a clean `ResourceNotRunning` | HIGH | Fixed in all 4 Resource impls: `except GracefulAbort:` → `raise ResourceNotRunning(...) from None` |
| 5. Cascade-delete orphan window: a child resource applied between the cascade reconciler's `enumerate_children` scan and its `patch_metadata` finalizer-drop creates an orphan whose namespace just hard-deleted | HIGH | Structurally fixed: `InMemoryStore.apply` / `create` reject writes into a namespace with `deletion_timestamp` set, raising new `NamespaceTerminating` exception |
| 6. `Agent._runtime_event_loop` retries persistent runtime failure forever; `runtime_ready` (an `asyncio.Event`) is never set, so `Agent.running().__aenter__` blocks indefinitely on `runtime_ready.wait()` | MED | Structurally fixed: ported to `asyncio.Future[None]` with `set_exception` on first-time setup failure (matches `ControllerRunner._watch_loop`); `await runtime_ready` raises the underlying error instead of hanging |
| 7. `Manager.shutdown` bails out of the reverse-runner-shutdown loop on any non-`ResourceNotRunning` exception; remaining runners go undrained | MED | Fixed: log-and-continue on `Exception`; `__aexit__` still tears down everything via `CheckedExitStack` |
| 8. Forceful runtime tear-down (no `shutdown()`) published an `EXITED` event with `phase="Running"` — synthetic-Failed-with-`-SIGKILL` was a workaround | MED | Eliminated by `Reaper`: `_sync_kill_process` SIGKILLs sync and offloads `_async_reap_and_publish` to the reaper; the reaper task awaits real `process.wait()` and publishes the real exit code |
| 9. Predecessor handoff (v1→v2 supersede) lived in `runtime.apply` orchestration; cancellation mid-handoff could leave v1 partially torn down with no clear ownership | MED | Structurally fixed: `_ProcessSupervisor` takes `predecessor` + `predecessor_cm` at construction; `_drain_predecessor` runs *before* its own `CheckedExitStack` opens; refs are dropped before the `try/finally` await so "shutdown after aexit" is structurally impossible |
| 10. `BroadcastQueue.subscribe()` after `close()` adds the subscriber to (re-populated) `_subs` and never delivers a None sentinel; consumer's `next()` blocks forever | LOW | **Open** (deferred — flagged as "easy to remember `_closed` is a state"; reflagged in the next audit round) |

### Round 7 — post-Reaper audit

Smaller pass, mostly state-machine bugs the language can't catch. None on the cancellation/exception-safety axis.

| Finding | Severity | Status |
|---------|----------|--------|
| 1. `_build_argv(primary)` runs *before* `_drain_predecessor()` in `_ProcessSupervisor.running().__aenter__`. For a `TarballImage` workload it raises `RuntimeError`; the runtime has already removed the predecessor from its dicts; the partial supervisor's `_predecessor` ref is dropped without draining → orphan process + leaked cm | HIGH | Fixed: `_drain_predecessor()` is now the first statement of `__aenter__`, before any other prelude work that can raise |
| 2. `_reject_if_namespace_terminating` runs outside the workload-kind lock; the subsequent `async with state.lock:` may yield (lock contended); during the yield another task can mark the namespace terminating; we then write the workload row anyway, recreating the race the barrier was supposed to close | HIGH | Fixed: check moved inside the lock for both `apply` and `create` |
| 3. `FakeRuntime.running()` is the only Resource impl missing the re-entry guard (`if self._is_running: raise ResourceNotRunning(...)`). Concurrent `async with fake.running():` blocks corrupt state silently | HIGH | Fixed: guard added; re-entry test in `test_resource_protocol.py` covers all Runtime impls |
| 4. `BroadcastQueue.subscribe()` after `close()` (re-flagged from Round 6 #10) | LOW | Fixed: explicit `_State` enum; `subscribe` raises new `BroadcastClosed` on closed state, `publish` becomes a no-op (intentional — tear-down sync callbacks may publish into a bus the runtime already closed) |

## Discipline rules

These are the durable rules — they describe the structural invariants that keep recurring bug shapes from coming back. New code in this package must follow them.

### Lifecycle / async resources

- **Implement `Resource`.** New stateful components expose `running()` + `shutdown(deadline)` + `stop_token`. Construction inert.
- **`__aexit__` has no `await`.** Cleanup that needs awaits goes in `shutdown()` (caller-policied) or in a body task (cooperative via `stop_token`).
- **Use `CheckedExitStack` for multi-step init.** Each entered context is an automatic safe-point against the stop_token.
- **Stop_token tree mirrors resource ownership.** Never re-parent. Resource-replacement (supersede) cascades through `await existing.shutdown(...)` calls, not through token tree mutation.
- **Half-cancellation is `__aexit__`'s problem.** If `shutdown` is cancelled mid-flight, the resource is in a half-state; `__aexit__` reconciles. Caller is responsible for not accessing the resource between cancel and `__aexit__`.

### Store / spec / status invariants

- **Store is the only writer.** Every mutation goes through `apply` / `patch_status` / `patch_metadata` / `delete`, all under the per-kind lock. No yields between read-modify-write. Watch publish happens inside the lock so events arrive in resource_version order.
- **Spec ↔ status is enforced.** `apply` writes spec + writable user metadata (labels, annotations); `patch_status` writes status + a small subset of metadata; `patch_metadata` writes finalizers/labels/annotations. Mutators that try to change the wrong half raise `ValidationError`.
- **`_update_spec` preserves controller-managed metadata.** Finalizers and owner_refs come from `existing`, not `desired`. Otherwise a user re-applying a workload silently disables cleanup finalizers and breaks cascade-delete.
- **`generation` bumps only on spec change. `resource_version` bumps on every write.** `observed_generation` on conditions is what controllers use to detect "fresh vs stale" status.
- **Returned objects are deep-copied.** Callers can mutate them without affecting the stored row.

### Subscription discipline

- **`async with store.subscription(Kind) as events:`** is the only sanctioned way to subscribe. The `BroadcastQueue` underneath registers synchronously on `__aenter__`; events fired immediately after the with-block enters are observed.
- **Open subscription before issuing the action that fires the event.** Synchronous registration means events are observed without race windows.
- **`runtime.watch()` is also an async context manager** with the same shape. No registration sentinel needed.

### Reconciler pattern

- **Read current state at reconcile time.** The workqueue dedups bursts; the reconciler sees only the *latest* state when it runs. v1→v2→v3 spec churn collapses to one reconcile against v3.
- **Event-driven, no `requeue_after_s` for polling.** Watch-driven controllers wake when state changes. `requeue_after_s` is for explicit debounce, not periodic polling.
- **`Controller` is covariant in its owned kind.** Lets Manager hold a heterogeneous `list[ControllerRunner[_AnyResource]]` while concrete callers see narrowed parameters.
- **Don't claim Reconciled from the runtime event loop.** Only the desired loop knows what generation it actually applied. The runtime event loop writes phase/host/Available; if it also wrote Reconciled, a stale STARTED for an old generation would falsely mark a newer spec as already-reconciled.
- **`_already_reconciled` checks `(type, status, reason, observed_generation)`.** Reason `"ApplyOk"` is the agent's own marker; foreign Reconciled writes are ignored.
- **CascadingReconciler base for parent-child resources.** Subclasses declare `finalizer_name`, `enumerate_children`, `reconcile_active`. Base class handles finalizer install + cascade-delete uniformly. Don't reimplement the cascade dance per reconciler.

### Runtime backend

- **Supervisor pattern: one Resource per workload.** `_ProcessSupervisor` implements `Resource`. Setup uses a single `CheckedExitStack` with sync callbacks (`_sync_kill_process`, `_sync_rm_cgroup`, `_sync_cancel_monitor`). Sync `__aexit__` runs the stack — every cleanup is unconditional. No separate reaper task; no per-key lock; no two-phase shutdown.
- **`apply`'s spec-change path: `await existing.shutdown()` then start new.** Sequential. No coexistence.
- **Process cleanup split.** Sync stack callback: `process.kill()` (a sync syscall — fires unconditionally). Async drain in `shutdown()`: SIGTERM → wait → SIGKILL → reap. No `__aexit__` ever awaits.

### WorkQueue

- **`close()` is authoritative.** `get()` raises `QueueClosed` even with future-deadline items.
- **`done(success=True)` + dirty deadline → requeue at dirty deadline.** Earliest deadline wins (`add()` urgency trumps `add_after()` debounce).
- **Per-call wakeup futures, not a shared `asyncio.Event`.** Avoids busy-spin on permanently-set events.

### Watch bus

- **`BroadcastQueue` is the single primitive.** WatchBus and every Runtime backend's event stream wrap it. Overflow is detected at the top of every iter loop, not just after the None sentinel; subscribers get `BroadcastOverflow` and must re-list.

### Variance / typing

- **`Resource` is non-generic** (just `metadata`); concrete subclasses (Workload, Namespace) declare their own `spec`/`status`/`api_version`/`kind` fields directly. `ResourceLike[Spec_co, Status_co]` is a covariant Protocol used at the store boundary; concrete pydantic classes satisfy it structurally.
- **No `# type: ignore`, no `cast()`, no `Any`** anywhere except the one Protocol declaration that has to mirror pydantic's `Any` returns. Encapsulated.

## Test discipline

See `/home/danklishch/code/ton/src/.claude/rules/test-python-rules.md` for the full rules. The TL;DR:

- **`pytestmark = [pytest.mark.asyncio, pytest.mark.usefixtures("virtual_clock")]`** at the top of every test module makes timeouts virtual and tests run in milliseconds.
- **Helpers from `orchestrator.testing`:**
  - `wait_for_event(store, Kind, predicate)` — subscribes, returns the first matching resource.
  - `wait_for_state(store, Kind, state_check)` — subscribes, re-evaluates a predicate after each event (use for "all replicas have finalizers" or external side-channels like `fake_runtime.applies`).
  - `wait_for_asyncio_idle()` — yields until the pending task set is stable across N consecutive ticks. Replaces `asyncio.sleep(0.05)` "let things settle" patterns.
- **Don't write polling loops** with `while ... await asyncio.sleep(0.01)`. Don't take `virtual_clock: None` as a parameter.
- **Tests live in `tests/orchestrator/`, helpers in `orchestrator.testing`** (per the `<pkg>.testing` convention from `python-general-rules.md`).

Current suite: 81 tests, ~300ms run time. 0 errors, 0 warnings, 0 lint issues.

## What's deliberately not built

The architecture supports these but they're out of scope for the prototype. Adding them is "labor, not redesign":

- **Lint check for "no `await` in `@asynccontextmanager` after yield."** Mechanical enforcement of the sync-`__aexit__` rule. ~20 LOC.
- **`weakref.finalize` warning** when a `Resource` is GC'd while running. Catches "forgot to use `async with`" bugs.
- **Remote agents.** `Agent` runs in-process today. Fleet deploys would run `Agent` as a separate process on each host, talking to `Manager` over WS+msgpack. The `Runtime` Protocol is already the seam.
- **WireGuard overlay reconciler.** `NetworkPolicy` resource exists; no reconciler yet. For cross-host network games (slow-validator-style chaos).
- **`PortForward` reconciler.** Resource exists; pasta/socat reconciler is ~50 LoC of glue.
- **`NetworkPolicy` reconciler.** nft per netns. ~200 LoC.
- **`RuncRuntime`.** Performance fix (~13ms cold start vs ~370ms podman). `Runtime` Protocol is already there; just write the OCI bundle synthesizer.
- **Heartbeat + suicide-on-disconnect.** For the fleet path: agents kill their workers if they lose the manager. Trivial protocol, ~100 LoC.

The control plane shape is converged. The Resource Protocol session was the last load-bearing design change. Future audits should find smaller, more localized issues.
