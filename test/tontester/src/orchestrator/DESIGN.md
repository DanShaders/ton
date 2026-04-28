# Resource Protocol — design rationale

This document captures the design decisions behind the orchestrator's
async-resource model, derived through several audit rounds and a
concentrated session on cleanup-and-cancellation correctness. It's
the "why" behind `lifecycle.py`. New contributors should read it
before adding new resource types.

## The problem we converged on

The recurring bug shape across 5 audit rounds was **cleanup logic
that gets cut short by cancellation**. Concretely:

- `await self._close()` in an `@asynccontextmanager` `finally` block.
- `for task in tasks: try: await task except CancelledError: pass`.
- "Cancel old workload, start new" patterns where the old's cleanup
  awaits got interrupted before the process was actually dead.

The *underlying* issue is that **async cleanup mixed with
cancellation has no universally correct policy**. When a caller
cancels a task that's mid-cleanup, three sensible answers all
exist:

- **Fast sync**: just signal stop, return. No graceful drain. Never
  hangs.
- **Bounded graceful**: try graceful for T seconds, then escalate.
- **Indefinite graceful**: wait until done; ignore caller-cancel.

Async `__aexit__` picks one of these implicitly, and the
implementer rarely thought about which. That's the root muddle.

## How the ecosystem solves this

The same problem appears in Go, Rust/Tokio, Erlang OTP, and
Kubernetes. The common pattern is: **separate "release the handle"
(sync, fast, always works) from "drain the work" (async, optional,
caller picks the policy)**.

| System | Sync release | Async drain | Force escalation |
|--------|--------------|-------------|------------------|
| Go HTTP server | `Close()` | `Shutdown(ctx)` | (use shorter ctx) |
| Tokio task | `Drop` (detaches) | `JoinHandle.await` | `JoinHandle::abort()` |
| Erlang supervisor | `brutal_kill` | `shutdown=N ms` | `shutdown=brutal_kill` |
| k8s pod | SIGTERM (immediate) | `terminationGracePeriodSeconds` | SIGKILL after grace |

In each case, the resource's *release* is unconditional and synchronous;
the *drain* is the caller's policy decision (deadline, indefinite,
fire-and-forget). Rust's language design (no async drop) forces this
split; Go's and Erlang's libraries impose it by convention.

We adopt the same split in Python via the `Resource` Protocol below.

## The Resource Protocol

```python
class Resource(Protocol):
    @property
    def stop_token(self) -> StopToken: ...
    def running(self) -> AbstractAsyncContextManager[Self]: ...
    async def shutdown(self) -> None: ...
```

### `running()` — the lifecycle context manager

- `__aenter__` does setup. Async; can fail; rolls back via
  `CheckedExitStack` if cancelled mid-init.
- `__aexit__` is **synchronous** (no `await`). Sets `stop_token`,
  marks `_is_running = False`, requests cancellation of any owned
  tasks, closes any owned synchronous resources.
- After `__aexit__` returns, the resource is "not running"; further
  `shutdown()` calls raise `ResourceNotRunning`.

The sync `__aexit__` rule is the load-bearing discipline. It
guarantees that cancellation cannot interrupt cleanup *because there
is no await to interrupt*. The reconciliation logic that handles
half-state from a prior cancelled `shutdown()` lives here, runs
deterministically, completes in microseconds.

### `shutdown()` — the explicit graceful drain

- Must be called inside `running()`; raises `ResourceNotRunning`
  otherwise.
- Sends sync stop signals (idempotent) at the top of the function,
  then awaits in-flight work to finish.
- **Takes no time budget.** Callers control the budget by wrapping
  with `asyncio.wait_for(rt.shutdown(), timeout=N)`. Any "deadline"
  argument here would be redundant — `wait_for` already does it,
  and there's nothing the runtime could *act on* differently with
  internal deadline knowledge: the workload-level
  `termination_grace_s` controls SIGTERM→SIGKILL escalation inside
  `owned_process`; runtime-level shutdown just observes that
  cleanup.
- Caller-cancel of `shutdown()` propagates as `CancelledError` —
  caller has decided to give up waiting.
- After cancellation, the resource is in a **half-cancelled state**;
  see "Half-cancellation contract" below.

The four caller modes for shutdown:

| Operation | How |
|-----------|-----|
| Indefinite graceful | `await rt.shutdown()` |
| Bounded graceful | `await asyncio.wait_for(rt.shutdown(), N)` |
| Forced (immediate) | exit `running()` block; `__aexit__` does sync emergency |
| Mixed (graceful then force) | `wait_for` with timeout; on TimeoutError, exit with-block |

There is no `force_kill()` method. Forceful escalation happens
through the standard cancellation path: cancel the task running the
`async with` block, and the resulting `__aexit__` is itself the
forceful-shutdown primitive (sync, immediate, can't be interrupted).

### `stop_token` — hierarchical cancellation

`StopToken` carries a fixed-parent relationship:

- Created with optional `parent` parameter at construction time.
- `set()` cascades to all descendants synchronously.
- Tokens cannot be re-parented after construction.
- The token tree mirrors *resource ownership*. Resource-replacement
  transitions (e.g., one supervisor superseding another) propagate
  through the *call chain* of explicit `shutdown()` calls, not
  through token re-parenting.

This avoids the "what if cancel was in flight when we re-parent"
class of bugs that prevents `tokio_util::CancellationToken` from
exposing re-parenting.

## CheckedExitStack and `_GracefulAbort`

When a resource's `__aenter__` does multi-step init using
`AsyncExitStack`, each `enter_async_context(...)` call should
implicitly check the resource's stop_token — so a parent-cancel
during init aborts gracefully without committing to subsequent
steps.

`CheckedExitStack` is a thin wrapper: every `enter_*_context`
checks `stop_token.is_set` first; if set, raises `_GracefulAbort`
(a `BaseException`). The surrounding `running()` catches it and
returns early; the stack's already-entered contexts unwind via
their (sync) `__aexit__`s.

`_GracefulAbort` extends `BaseException` (not `Exception`) so
broad `except Exception:` clauses inside init can't accidentally
swallow it — same reason `CancelledError` is a `BaseException`.

## Wrapping for resource-replacement (supersede)

When a workload is updated (v2 supersedes v1), the natural
implementation is "shutdown old, start new":

```python
async def apply(self, workload):
    existing = self._supervisors.get(key)
    if existing and _spec_equivalent(existing.workload, workload):
        return _build_status(existing)
    if existing:
        # Per-workload supersede budget via wait_for.
        try:
            await asyncio.wait_for(existing.shutdown(), self.supersede_grace)
        except TimeoutError:
            pass  # supervisor was cancelled; cleanup is in flight
        del self._supervisors[key]
    new = await _Supervisor.start(workload, ...)
    self._supervisors[key] = new
    return new.status
```

The cascade for v3 superseding v2 (which is mid-shutdown of v1)
happens **through the call chain**:

- v3.apply calls `existing_v2.shutdown(deadline)`
- v2.shutdown sets v2.stop_token, awaits its body task
- v2's body task is in `await v1.shutdown(...)` — that completes
  naturally (v1 dies properly with full grace)
- v2's body checks v2.stop_token at next safe point, exits
- v2.shutdown returns
- v3 spawns

The token tree stays flat (each supervisor's token is a child of
the runtime's token, never re-parented). The cascade is in the
function-call/return chain, not the token tree. Re-parenting is
unnecessary because v1, by the time v2 cancels, was already going
to die — v2 had already committed to v1.shutdown.

## Half-cancellation contract

If `shutdown(deadline)` is called from a sibling task and that
task is cancelled, the resource ends up in a **half-cancelled
state**:

- `stop_token` is set.
- Some supervisors have been cancelled and are unwinding.
- `_is_running` is still `True` (the running()-task hasn't reached
  `__aexit__`).
- `_closed` may or may not be set (depends on impl; either is OK).

This is intentional. The contract is:

1. **`__aexit__` is the deterministic reconciler.** It runs sync,
   re-issues all stop signals (idempotent), re-cancels any
   already-cancelled tasks (idempotent), closes any owned sync
   resources (idempotent). Whatever state shutdown left in the
   middle of, `__aexit__` finishes cleanly.
2. **The running()-task should exit ASAP after a cancelled
   shutdown.** That triggers `__aexit__`, which reconciles. While
   the task is still in the body, the resource is in an
   indeterminate state.
3. **Callers must not access the resource between "shutdown
   raised" and "`__aexit__` ran."** Doing so will see partial
   state. This is the caller's bug — there's no API contract that
   protects them.

## Why shield isn't needed

`asyncio.shield` would prevent caller-cancel from interrupting
shutdown's await. We deliberately don't use it. Reasoning:

- **The work that matters is sync.** `shutdown()` sets
  `stop_token` and calls `task.cancel()` on supervisors *before*
  awaiting anything. Those signals fire unconditionally — caller-
  cancel can't interrupt them because they're synchronous.
- **The await in shutdown is observation.** It only waits to learn
  that supervisors have finished unwinding. Whether we observe
  that completion or not doesn't change the unwinding itself.
- **`__aexit__` is the deterministic reconciler.** Re-issues every
  signal; finishes whatever shutdown started. Sync, can't be cut
  short.

So shield's only effect would be: shutdown's task continues
running uselessly in background, holding a reference, until it
observes the completion no one's waiting for. No correctness
gain. We let cancel propagate naturally.

## Composition through layers

`Manager.shutdown()` → `Agent.shutdown()` → `Runtime.shutdown()`:
each layer follows the same template:

```python
async def shutdown(self):
    if not self._is_running:
        raise ResourceNotRunning(...)
    self._stop_token.set()
    # Sync signal to sub-resources or own tasks.
    for sub in self._subs:
        sub.request_stop()
    # Await in-flight cleanup. Caller controls budget via wait_for.
    _ = await asyncio.gather(*pending, return_exceptions=True)
```

Caller passes a budget through the call chain via `wait_for` at
the top: `await asyncio.wait_for(manager.shutdown(), 30)`.
`wait_for` cancels manager.shutdown, which cancels its gather,
which cancels each sub's shutdown — propagation is automatic. No
deadline arithmetic at every layer; no per-layer policy
decisions.

Each layer's caller-cancel propagates upward (CancelledError) and
each layer's `__aexit__` reconciles independently. Token-tree
cascade handles "Manager-level cancel → all sub-resource tokens
fire → cooperative loops exit." Call-chain cascade handles
"`apply()` calls `existing.shutdown()` → existing's body sees
stop_token → exits."

The two cascade mechanisms are non-overlapping: tokens for
"system-wide stop"; calls for "I'm replacing this specific
resource."

## "Sync cleanup" really means "sync signals, async observation"

The "no `await` in `__aexit__`" rule is a constraint on *signal
sending*, not on *waiting for completion*. Many real resources
need async work to confirm cleanup finished — e.g., observing a
process actually die requires `process.wait()`. That's not what
goes in `__aexit__`.

The split is:

1. **Sync signals fire unconditionally.** Inside `__aexit__`:
   set stop_token, mark closed, call `task.cancel()` on owned
   tasks, `process.kill()` (sync syscall), close events bus.
   None of these awaits; none can be interrupted by
   cancellation. They *will* run.
2. **Async observation is opt-in via `shutdown()`.** Want to
   know all processes are dead before continuing? Call
   `await rt.shutdown()` inside the with-block. It awaits the
   supervisors' RAII unwind in their own task contexts (no
   cancel-pending on them, so their `process.wait()` calls
   actually wait).

For `SubprocessRuntime` concretely:

- `__aexit__` calls `sup.release_sync()` on every supervisor.
  That runs the supervisor's sync stack callbacks directly:
  `process.kill()` (sync syscall, fires), `_safe_rmtree` (best-
  effort), monitor/probe task cancel. No awaits anywhere; the
  reap happens eventually (kernel + startup-sweep mop up).
- `await rt.shutdown()` calls each supervisor's `shutdown()`:
  SIGTERM → wait `termination_grace_s` → SIGKILL → wait reap.
  The supervisor's own task isn't cancel-pending while we await
  it, so the waits actually wait. Then the surrounding
  `__aexit__` runs the (now-no-op) sync callbacks.

So:

| Caller pattern | Process state when caller resumes |
|---------------|-----------------------------------|
| `async with rt.running(): pass` | SIGKILL sent; not waited for |
| `async with rt.running(): await rt.shutdown()` | Confirmed dead, reaped |
| `async with rt.running(): await wait_for(rt.shutdown(), N)` | Either confirmed dead (within N) or "we gave up waiting; signals fired" |

**The runtime, during normal operation, never cancels its own
`shutdown()`.** Cancellation comes from outside (caller's
`wait_for` timeout, external `task.cancel()` on the daemon).
When the runtime is in charge — e.g., `apply()` superseding
v1 with v2 — it just `await`s the existing supervisor's
shutdown without imposing a budget; the supervisor's own
`termination_grace_s` controls SIGTERM→SIGKILL escalation
inside its `shutdown()` method.

## Sync `__aexit__` discipline — what it costs

The rule "no `await` in `__aexit__`" means:

- Cleanup that needs awaits (e.g., waiting for a process to die)
  cannot live in `__aexit__`. It must live either in a body task
  (which sees `stop_token` and cleans up itself) or in `shutdown`.
- The "kill + wait for reap" pattern lives inside the supervisor's
  `shutdown()` (async), not its `__aexit__`. The supervisor's
  `__aexit__` only sends SIGKILL (sync syscall) via a stack
  callback; it never awaits. The OS reap is best-effort by then
  but cgroup-based startup cleanup catches survivors.
- Any future `@asynccontextmanager` written for this codebase
  must be reviewed against this rule. A small lint check
  ("no `await` between `yield` and the end of an
  `@asynccontextmanager` body") would enforce it mechanically;
  not yet written.

## Signal handling and `BaseException`

The orchestrator is **not** safe under raw `KeyboardInterrupt`. SIGINT
and SIGTERM must be installed as cooperative cancels at the top of
the call stack: the signal handler invokes `task.cancel()` on the
root task, and the resulting `CancelledError` flows through every
await site as a normal cancellation.

```python
async def main():
    loop = asyncio.get_running_loop()
    root = asyncio.current_task()
    assert root is not None
    loop.add_signal_handler(signal.SIGINT, root.cancel)
    loop.add_signal_handler(signal.SIGTERM, root.cancel)

    reaper = Reaper()
    try:
        async with manager.running():
            ...  # body
    finally:
        await reaper.wait()  # hard-block; clean state before exit
```

No code in this package handles arbitrary `BaseException`s raised
between bytecode boundaries. The contract enumerates exactly:

| Exception | What we do |
|-----------|------------|
| `Exception` subclasses | Normal recovery / logging at well-defined points |
| `asyncio.CancelledError` | Cooperative cancellation at every `await`; `cancel_and_collect` distinguishes inner vs outer cancel |
| `KeyboardInterrupt` | Out of scope. Install a SIGINT handler that cancels the root task. |
| `MemoryError` | Out of scope. If you OOM, the orchestrator dying mid-mutation is the least of your problems. |
| `SystemExit` | Doesn't appear — we never `raise SystemExit` ourselves, and the OS doesn't inject it. |

Cleanup paths (sync `__aexit__` callbacks, ``try/finally`` blocks)
assume that "between sync statement A and sync statement B no
exception can fire." This holds under cooperative cancellation
(`CancelledError` is delivered at await points only). It does
**not** hold under raw `KeyboardInterrupt`. Don't try to harden
against the latter — install the signal handler instead.

A second SIGINT after cancel-already-issued unblocks Python's
default handler, raising `KeyboardInterrupt` and aborting in-flight
cleanup. This is the operator's escape hatch from a stuck shutdown
— equivalent to SIGKILL of the orchestrator process. Orphan
processes / cgroups / mount points left behind are then the
operator's responsibility.

## The `Reaper` and async cleanup that can't fit in `__aexit__`

Some cleanup operations are *async by nature* and cannot fit the
sync-`__aexit__` discipline:

- ``waitpid`` after SIGKILL — kernel pace, our zombie until it
  reaps.
- cgroup ``rmdir`` after the kernel reaps survivors — filesystem
  state with kernel timing.
- (future) bind-mount unmount, netns deletion, OCI bundle dir
  cleanup.

The pattern is POSIX-shaped:

| POSIX | Our protocol |
|-------|--------------|
| `fsync(fd)` | `await resource.shutdown()` — flush pending state |
| `close(fd)` | `__aexit__` (sync) — release the handle, can't fail |
| `waitpid(pid)` | `await reaper.wait()` — collect kernel state |

The :class:`~orchestrator.lifecycle.Reaper` is a top-level pool for
those operations. The top-level call site constructs it, threads
it into resources at construction, and drains it in `finally`.
Production contract: drain is **indefinite**. SIGKILL of the
orchestrator process is the only escape; orphan kernel state is
then the operator's problem.

**Orderly shutdown leaves the reaper empty.** Each Resource's sync
`__aexit__` callbacks check whether their work is still needed; in
the orderly path (after `shutdown()` drained in-flight async work),
every callback hits its no-op branch. The reaper only fills when
`__aexit__` runs forcefully — `shutdown()` cancelled, never called,
or unable to complete.

**Currently only `SubprocessRuntime` uses the reaper.** Other
resources don't have kernel-state cleanup. The dependency is
threaded directly to the runtime constructor; agents/managers don't
need to know about it.

## Mapping to the codebase

| File | Role |
|------|------|
| `lifecycle.py` | `StopToken`, `CheckedExitStack`, `GracefulAbort`, `Resource` Protocol, `ResourceNotRunning`, `Reaper`, `cancel_and_collect` |
| `runtime/protocol.py` | `Runtime` Protocol — extends `Resource`, adds apply/delete/get/list/watch |
| `runtime/subprocess_runtime.py` | `SubprocessRuntime` (Resource) + `_ProcessSupervisor` (per-workload Resource) |
| `runtime/fakes.py` | Concrete `Resource` impl (test) |
| `agent/agent.py` | `Agent` — Resource impl, fully sync `__aexit__` |
| `control/controller.py` | `ControllerRunner` — Resource impl (sync `__aexit__` cancels + closes queue) |
| `control/manager.py` | `Manager` — Resource impl (sync `__aexit__` signals; `shutdown()` awaits each runner in reverse) |

## Future work

- Lint check for "no await in `@asynccontextmanager` after yield."
- `weakref.finalize` warning when a `Resource` is GC'd while
  running (catches "forgot to use `async with`" bugs).
