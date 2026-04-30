"""Async-resource lifecycle primitives.

The package's recurring bug shape was: hand-rolled cleanup either
(a) swallowed external cancellation, or (b) missed cleanup on a
not-yet-handled exception class. The structural fix is a set of
primitives + a discipline:

**The discipline.** Every async resource follows the
:class:`Resource` Protocol shape:

- ``running()`` is an async context manager. ``__aenter__`` does
  setup; ``__aexit__`` is **synchronous** (no ``await``) and only
  signals the resource's ``stop_token``.
- ``shutdown(deadline)`` is the explicit graceful-drain method.
  Called *inside* ``running()``, awaits in-flight work to complete
  with a caller-provided deadline; escalates via internal
  ``task.cancel()`` if cancelled or out of time.
- After ``running()``'s context exits, the resource is "not
  running"; further ``shutdown()`` calls raise
  :exc:`ResourceNotRunning`.

This split (sync release in ``__aexit__``, async drain in
``shutdown``) is the same shape Tokio / Go / k8s use: the resource
gives up its handle synchronously and the caller chooses whether to
wait for completion. Because ``__aexit__`` has no ``await``,
cancellation can't cut cleanup short — there's nothing to cut.

**Hierarchical cancellation.** :class:`StopToken` carries a
fixed-parent relationship: setting a parent cascades to all
descendant tokens. The token tree mirrors resource ownership; it's
write-once (no re-parenting). For *resource-replacement*
transitions (one supervisor superseding another), the cascade
happens through explicit ``shutdown()`` calls in the wrapping
constructor, not through token re-parenting.

**Safe-point checks.** :class:`CheckedExitStack` is an
:class:`contextlib.AsyncExitStack` whose every ``enter_*_context``
call checks the bound stop_token. If set, raises
:exc:`GracefulAbort` (a ``BaseException`` subclass that bypasses
``except Exception`` clauses) which the surrounding ``running()``
catches. So an init-sequence written as a chain of
``stack.enter_async_context(...)`` automatically gets a safe-point
between every step — no manual ``if stop_token.is_set: raise``
sprinkled through.

**Process / path RAII.** Concrete resources own subprocesses and
filesystem state directly via a :class:`CheckedExitStack` whose
callbacks are sync (``stack.callback(self._sync_kill)``,
``stack.callback(self._sync_rm_cgroup)``). The previous
``owned_process`` / ``owned_path`` async-with helpers are gone —
they had async cleanup in their ``finally`` blocks, which the sync-
``__aexit__`` discipline forbids.
"""

import asyncio
import logging
import weakref
from collections.abc import Callable, Coroutine
from contextlib import (
    AbstractAsyncContextManager,
    AbstractContextManager,
    AsyncExitStack,
)
from types import TracebackType
from typing import Protocol, Self, final

logger = logging.getLogger(__name__)


# ---- StopToken -----------------------------------------------------------


@final
class StopToken:
    """Hierarchical cancellation signal.

    Each token has a (possibly None) parent fixed at construction.
    ``set()`` on a parent cascades to all descendants. Tokens cannot
    be re-parented. The tree mirrors resource-ownership; cascading
    cancellations through *resource-replacement* transitions
    happen via explicit ``shutdown()`` calls, not via tree mutation.

    Single-threaded asyncio means no locking is needed for the flag
    or the waiter list; reads and writes are atomic between awaits.
    """

    def __init__(self, *, parent: StopToken | None = None):
        self._is_set: bool = False
        self._waiters: list[asyncio.Future[None]] = []
        self._children: weakref.WeakSet[StopToken] = weakref.WeakSet()
        if parent is not None:
            parent._children.add(self)
            if parent._is_set:
                # Parent already fired before we could register; inherit.
                self._is_set = True

    @property
    def is_set(self) -> bool:
        return self._is_set

    def set(self) -> None:
        """Mark this token (and all descendants) as set. Idempotent.

        Synchronous: no ``await``. Resolves any pending ``wait()``
        futures atomically before returning.
        """
        if self._is_set:
            return
        self._is_set = True
        for w in self._waiters:
            if not w.done():
                w.set_result(None)
        self._waiters.clear()
        for child in list(self._children):
            child.set()

    async def wait(self) -> None:
        """Block until this token is set."""
        if self._is_set:
            return
        loop = asyncio.get_running_loop()
        fut: asyncio.Future[None] = loop.create_future()
        self._waiters.append(fut)
        try:
            _ = await fut
        finally:
            if fut in self._waiters:
                self._waiters.remove(fut)

    def child(self) -> StopToken:
        """Create a child token. Cancelling self cancels the child."""
        return StopToken(parent=self)


# ---- Resource Protocol + helpers ----------------------------------------


class ResourceNotRunning(RuntimeError):
    """Raised when a Resource method that requires the running()
    context is called outside of it.

    Examples: ``shutdown(deadline)`` after ``running()``'s ``__aexit__``
    has fired, or before ``__aenter__`` has fired.
    """


class GracefulAbort(BaseException):
    """Internal: raised by :class:`CheckedExitStack` safe-point checks
    when a resource's stop_token fired during init. Caught by the
    resource's ``running()`` and treated as "graceful early exit"
    (rollback already done; resource never yields, caller's
    ``async with`` body never runs).

    Subclasses ``BaseException`` (not ``Exception``) for the same
    reason ``CancelledError`` does: ``except Exception`` clauses
    inside init steps shouldn't accidentally swallow it.
    """


@final
class CheckedExitStack:
    """Like :class:`contextlib.AsyncExitStack`, but every
    ``enter_*_context`` call is a safe-point against a
    :class:`StopToken`.

    If the token is set when ``enter_async_context`` /
    ``enter_context`` / ``callback`` is called, the call raises
    :exc:`GracefulAbort` instead of entering. Combined with the
    discipline that all sub-resources have sync ``__aexit__``, the
    rollback is cancellation-safe by construction.

    Resources use this in their ``running()``'s ``__aenter__`` body
    to chain sub-resource entries. Each entry implicitly checks
    "should we keep initializing?" before committing to the next
    step. For manual safe-points between non-stack awaits, just
    write ``if token.is_set: raise GracefulAbort()``.
    """

    def __init__(self, stop_token: StopToken):
        self._stack: AsyncExitStack = AsyncExitStack()
        self._stop_token: StopToken = stop_token

    async def __aenter__(self) -> CheckedExitStack:
        _ = await self._stack.__aenter__()
        return self

    async def __aexit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        tb: TracebackType | None,
    ) -> bool | None:
        return await self._stack.__aexit__(exc_type, exc, tb)

    async def enter_async_context[T](self, cm: AbstractAsyncContextManager[T]) -> T:
        if self._stop_token.is_set:
            raise GracefulAbort
        return await self._stack.enter_async_context(cm)

    def enter_context[T](self, cm: AbstractContextManager[T]) -> T:
        if self._stop_token.is_set:
            raise GracefulAbort
        return self._stack.enter_context(cm)

    def callback(
        self, cb: Callable[..., object], /, *args: object, **kwargs: object
    ) -> Callable[..., object]:
        if self._stop_token.is_set:
            raise GracefulAbort
        return self._stack.callback(cb, *args, **kwargs)


class Resource(Protocol):
    """Shape every async-stateful component in the orchestrator follows.

    Concrete components implement ``running()`` and
    ``shutdown(deadline)``; ``stop_token`` is the cancellation
    handle (typically a child of an outer resource's token).

    The contract:

    1. ``running()`` is the only valid place to use the resource.
    2. ``shutdown()`` is callable only inside ``running()``;
       outside it raises :exc:`ResourceNotRunning`.
    3. ``running()``'s ``__aexit__`` is synchronous: it sets
       ``stop_token`` and returns. No ``await``. If the caller
       didn't call ``shutdown()`` first, body tasks wind down on
       their own (best-effort, no graceful drain).
    4. ``shutdown()`` takes no time budget — caller controls it via
       :func:`asyncio.wait_for`. Inside, ``shutdown`` does its sync
       signal work first (so signals fire unconditionally), then
       awaits in-flight cleanup. Caller-cancel propagates; the
       surrounding ``__aexit__`` is the deterministic reconciler.
    5. Setting ``stop_token`` (e.g. from a parent) makes the
       resource shut itself down cooperatively.
    """

    @property
    def stop_token(self) -> StopToken: ...

    def running(self) -> AbstractAsyncContextManager[Self]: ...

    async def shutdown(self) -> None: ...


# ---- cancel-and-await primitive -----------------------------------------


async def cancel_and_collect(task: asyncio.Task[object]) -> None:
    """Cancel a task we own; suppress *its* cancel; propagate *ours*.

    The recurring footgun in cancellation-aware code:

        _ = task.cancel()
        try:
            await task
        except asyncio.CancelledError:
            pass

    The ``await task`` can raise ``CancelledError`` for **either** of
    two reasons:

    1. *Inner cancel*: the task we cancelled is now raising the
       cancel we issued. We want to swallow this — we asked for it.
    2. *Outer cancel*: our own task is itself cancel-pending (a
       caller did ``task.cancel()`` on us, possibly via
       ``asyncio.wait_for``). We must propagate this — the caller
       asked us to stop.

    The discipline is to distinguish via
    ``asyncio.current_task().cancelling() > 0`` after catching: a
    nonzero count means we have a *pending* outer cancel to honor;
    zero means the only cancel in flight was the one we issued.

    This helper bakes the discipline in. Use it everywhere instead
    of hand-rolling — the H1 audit class of bugs comes from getting
    this wrong.

    Already-done tasks are a no-op. Inner exceptions other than
    ``CancelledError`` are swallowed too (we're cleaning up; the
    task's own crash is not actionable here).
    """
    if task.done():
        return
    _ = task.cancel()
    try:
        _ = await task
    except asyncio.CancelledError:
        current = asyncio.current_task()
        if current is not None and current.cancelling() > 0:
            raise
    except Exception:
        # Task crashed naturally; we're tearing down anyway.
        pass


# ---- Reaper -------------------------------------------------------------


@final
class Reaper:
    """Top-level pool for async cleanup that can't be done sync.

    The Resource Protocol's ``__aexit__`` is sync — by contract no
    awaits, so it can't wait on kernel-level operations like
    ``waitpid`` after SIGKILL or cgroup ``rmdir`` after the kernel
    reaps survivors. The reaper accepts those operations as
    fire-and-forget coroutines via :meth:`offload` (sync, callable
    from inside ``__aexit__`` even under cancel-pending).

    The reaper is owned by the *top-level* call site (``main()``,
    test fixtures), threaded into resources at construction. Lifetime
    outlives every resource it serves: the orderly pattern is

    .. code-block:: python

        reaper = Reaper()
        try:
            async with manager.running():
                await manager.shutdown()  # orderly drain → reaper empty
        finally:
            await reaper.wait()  # forceful path → drain anything queued

    **Orderly shutdown leaves the reaper empty.** Each Resource's
    sync ``__aexit__`` callbacks check whether their work is still
    needed; in the orderly path (after ``shutdown()`` drained
    in-flight async work), every callback is a no-op. The reaper
    only fills when ``__aexit__`` runs forcefully — ``shutdown()``
    cancelled, never called, or unable to complete.

    **`wait()` is indefinite by design.** The whole point is "don't
    leave kernel state behind." If a process is stuck in D-state or
    a cgroup permanently busy, the reaper hangs — caller's policy
    decision via ``asyncio.wait_for`` if they want a budget. SIGKILL
    of the orchestrator process is the operator's escape; orphan
    state is then their responsibility.
    """

    def __init__(self, *, name: str = "reaper"):
        self._tasks: set[asyncio.Task[None]] = set()
        self._name: str = name

    def offload(
        self,
        coro: Coroutine[object, object, None],
        *,
        name: str | None = None,
    ) -> None:
        """Sync. Schedule async cleanup; fire-and-forget from caller's POV.

        Safe to call from inside ``__aexit__`` even when the calling
        task is cancel-pending — ``asyncio.create_task`` schedules a
        fresh task that doesn't inherit cancel state.
        """
        task = asyncio.create_task(coro, name=name or f"{self._name}.task")
        self._tasks.add(task)
        task.add_done_callback(self._on_done)

    async def wait(self) -> None:
        """Block until every offloaded task has finished. Indefinite.

        Drains tasks added during the wait too — a reaper coroutine
        may itself offload further work, and ``wait`` keeps draining
        until the set is empty.
        """
        while self._tasks:
            _ = await asyncio.gather(*list(self._tasks), return_exceptions=True)

    def __len__(self) -> int:
        return len(self._tasks)

    def _on_done(self, task: asyncio.Task[None]) -> None:
        self._tasks.discard(task)
        if task.cancelled():
            return
        exc = task.exception()
        if exc is not None:
            logger.error(
                f"reaper {self._name} task {task.get_name()} crashed",
                exc_info=exc,
            )


# NOTE: ``owned_path`` and ``owned_process`` previously lived here as
# RAII helpers using ``@asynccontextmanager``. ``owned_process``'s
# ``finally`` had ``await process.wait()`` calls — async cleanup that
# violates the Resource Protocol's "no await in __aexit__" discipline.
# Concrete resources (``_ProcessSupervisor`` in
# ``runtime/subprocess_runtime.py``) now own these directly via a
# CheckedExitStack with sync callbacks: ``process.kill()`` is a sync
# syscall registered on the stack; the graceful SIGTERM+wait drain
# lives inside the resource's ``shutdown()`` method.
