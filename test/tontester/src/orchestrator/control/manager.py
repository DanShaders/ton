"""Top-level orchestrator process glue.

The :class:`Manager` is the process-wide owner of:

- the in-memory :class:`InMemoryStore`,
- every registered :class:`ControllerRunner`.

Implements the :class:`~orchestrator.lifecycle.Resource` Protocol —
``running()`` is the lifecycle context manager (sync ``__aexit__`` sets
``stop_token`` and marks not-running), ``shutdown()`` is the explicit
graceful drain that awaits each runner's ``shutdown()`` in reverse
order. One-shot: a Manager cannot be re-entered after ``running()``
exits.
"""

import contextlib
import logging
from collections.abc import AsyncGenerator
from contextlib import asynccontextmanager
from typing import Self, final, override

from pydantic import BaseModel

from ..errors import ManagerStopped
from ..lifecycle import (
    CheckedExitStack,
    GracefulAbort,
    Resource,
    ResourceNotRunning,
    StopToken,
)
from ..resources import ResourceLike
from ..store import InMemoryStore
from .controller import Controller, ControllerRunner
from .workqueue import Clock, RealClock

logger = logging.getLogger(__name__)

_AnyResource = ResourceLike[BaseModel, BaseModel]


@final
class Manager(Resource):
    """Process-wide orchestrator host.

    Construction is cheap and inert: ``register_kind`` /
    ``add_controller`` are sync setup. The :meth:`running` async
    context manager spawns every registered runner and tears them
    down on exit.

    Implements :class:`~orchestrator.lifecycle.Resource`: sync
    ``__aexit__`` only signals stop; ``shutdown()`` is the explicit
    async drain that awaits each runner's ``shutdown()`` in reverse.
    Single-tenant: a given manager hosts one logical "control plane"
    and is one-shot — construct a fresh Manager to re-run.
    """

    def __init__(
        self,
        *,
        clock: Clock | None = None,
        parent_token: StopToken | None = None,
    ):
        self._store: InMemoryStore = InMemoryStore()
        self._clock: Clock = clock or RealClock()
        # Controllers/runners are covariant in their owned kind, so the
        # heterogeneous list types cleanly under the base alias.
        self._runners: list[ControllerRunner[_AnyResource]] = []
        self._stop_token: StopToken = (
            parent_token.child() if parent_token is not None else StopToken()
        )
        self._is_running: bool = False
        # Becomes True only while inside ``running()``. Used to refuse
        # late ``register_kind`` / ``add_controller`` calls — those
        # are setup-time only.
        self._live: bool = False

    @property
    def store(self) -> InMemoryStore:
        return self._store

    @property
    @override
    def stop_token(self) -> StopToken:
        return self._stop_token

    # ---- setup (pre-running) -------------------------------------------

    def register_kind(self, resource_type: type[_AnyResource]) -> None:
        if self._live:
            raise ManagerStopped()
        self._store.register_kind(resource_type)

    def add_controller(
        self,
        controller: Controller[_AnyResource],
        *,
        worker_count: int = 1,
    ) -> None:
        if self._live:
            raise ManagerStopped()
        self._runners.append(
            ControllerRunner(
                controller,
                store=self._store,
                worker_count=worker_count,
                clock=self._clock,
                # Child of ours: cancellation cascades through the
                # token tree as a backup channel; the primary cancel
                # path is the explicit ``shutdown`` call chain.
                parent_token=self._stop_token,
            )
        )

    # ---- lifecycle ------------------------------------------------------

    @override
    @asynccontextmanager
    async def running(self) -> AsyncGenerator[Self]:
        """Enter every runner; on exit signal stop synchronously.

        Sub-resources are entered via :class:`CheckedExitStack` so
        a stop_token fired mid-init aborts gracefully via
        :exc:`GracefulAbort`. On the body's exit (sync ``finally``):

        1. ``_is_running`` flips to False, ``_live`` to False.
        2. ``stop_token.set()`` cascades to every runner's child token.

        No awaits in cleanup — Resource discipline. Sub-resource
        cleanup happens via the exit stack's LIFO unwind: each
        runner's own sync ``__aexit__`` runs and signals its tasks.
        For graceful drain of the runners' in-flight reconciles,
        call ``await manager.shutdown()`` inside the with-block.
        """
        if self._is_running:
            raise ResourceNotRunning("Manager.running re-entered while already running")
        self._live = True
        try:
            try:
                async with CheckedExitStack(self._stop_token) as stack:
                    # Store first: every runner reads from it. LIFO exit
                    # → runners stop, then store closes.
                    _ = stack.enter_context(_store_session(self._store))
                    for runner in self._runners:
                        _ = await stack.enter_async_context(runner.running())
                    self._is_running = True
                    try:
                        yield self
                    finally:
                        # SYNC: signal stop. Sub-resources' sync
                        # ``__aexit__`` runs as the stack unwinds
                        # (also sync). No awaits.
                        self._is_running = False
                        self._stop_token.set()
            except GracefulAbort:
                # stop_token fired mid-init. Re-raise as a clear
                # caller-facing error; otherwise @asynccontextmanager
                # would surface RuntimeError("generator didn't yield").
                raise ResourceNotRunning(
                    "Manager.running entered with stop_token already set"
                ) from None
        finally:
            self._live = False

    @override
    async def shutdown(self) -> None:
        """Graceful drain: signal stop, await each runner in reverse.

        Must be called inside ``running()``; raises
        :exc:`ResourceNotRunning` otherwise.

        Caller controls the budget by wrapping with
        :func:`asyncio.wait_for`. Caller-cancel propagates through
        each runner's ``shutdown`` and the surrounding ``__aexit__``
        reconciles any half-state.
        """
        if not self._is_running:
            raise ResourceNotRunning("Manager.shutdown called outside running()")
        self._stop_token.set()
        # Drain runners in reverse-acquisition order. Sequential rather
        # than ``gather`` because individual reconcilers may have
        # cross-runner ordering expectations (e.g. WorkloadSet
        # depending on Workload reconciler having drained).
        for runner in reversed(self._runners):
            try:
                await runner.shutdown()
            except ResourceNotRunning:
                # Runner was already torn down — its body task
                # finished on its own before we got to it.
                continue
            except Exception:
                # A bug in one runner's shutdown shouldn't strand
                # the rest. Log and continue; the caller's
                # ``__aexit__`` will still tear down everything via
                # the CheckedExitStack unwind.
                logger.exception(
                    (
                        "runner shutdown raised during Manager.shutdown; "
                        "continuing to drain remaining runners"
                    )
                )
                continue


@contextlib.contextmanager
def _store_session(store: InMemoryStore):
    """Sync context manager — close the store on exit.

    ``InMemoryStore.close`` is sync (it closes per-kind buses); we
    expose it as a context manager so :class:`CheckedExitStack` can
    own it via ``enter_context``.
    """
    try:
        yield store
    finally:
        try:
            store.close()
        except Exception:
            logger.exception("store close raised; swallowing")
