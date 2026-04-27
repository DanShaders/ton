"""Top-level orchestrator process glue.

The :class:`Manager` is the process-wide owner of:

- the in-memory :class:`InMemoryStore`,
- every registered :class:`ControllerRunner`.

Lifecycle is the :meth:`running` async context manager. Internally it
uses :class:`contextlib.AsyncExitStack` and nested
``enter_async_context`` calls — sub-resources are entered in
dependency order; cleanup runs in LIFO order automatically. There is
no ``start()`` / ``shutdown()`` pair, no ``_started`` /
``_stopping`` flags, and no chance to mis-order cleanup callbacks
because the cleanup *is* the resource's ``__aexit__``.
"""

import contextlib
import logging
from collections.abc import AsyncGenerator
from typing import Self, final

from pydantic import BaseModel

from ..errors import ManagerStopped
from ..resources import ResourceLike
from ..store import InMemoryStore
from .controller import Controller, ControllerRunner
from .workqueue import Clock, RealClock

logger = logging.getLogger(__name__)

_AnyResource = ResourceLike[BaseModel, BaseModel]


@final
class Manager:
    """Process-wide orchestrator host.

    Construction is cheap and inert: ``register_kind`` /
    ``add_controller`` are sync setup. The :meth:`running` async
    context manager spawns the runners and tears them down in
    reverse order on exit. Single-tenant: a given manager hosts one
    logical "control plane".
    """

    def __init__(self, *, clock: Clock | None = None):
        self._store: InMemoryStore = InMemoryStore()
        self._clock: Clock = clock or RealClock()
        # Controllers/runners are covariant in their owned kind, so the
        # heterogeneous list types cleanly under the base alias.
        self._runners: list[ControllerRunner[_AnyResource]] = []
        # Becomes True only while inside ``running()``. Used to refuse
        # late ``register_kind`` / ``add_controller`` calls — those
        # are setup-time only.
        self._live: bool = False

    @property
    def store(self) -> InMemoryStore:
        return self._store

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
            )
        )

    # ---- lifecycle ------------------------------------------------------

    @contextlib.asynccontextmanager
    async def running(self) -> AsyncGenerator[Self]:
        """Enter every runner; on exit, tear them all down in reverse.

        Sub-resources are pushed onto an ``AsyncExitStack`` via
        ``enter_async_context`` so their ``__aexit__`` callbacks
        run in LIFO order — the runners stop first, then the store
        closes. There is no place to register a free-floating
        cleanup callback (no ``push_async_callback``), so cleanup
        ordering is structurally tied to acquisition ordering.

        ``CancelledError`` propagates correctly because that is what
        ``async with AsyncExitStack`` does.
        """
        self._live = True
        try:
            async with contextlib.AsyncExitStack() as stack:
                # Store first: every runner reads from it. LIFO exit
                # → runners stop, then store closes.
                _ = stack.enter_context(_store_session(self._store))
                for runner in self._runners:
                    _ = await stack.enter_async_context(runner.running())
                yield self
        finally:
            self._live = False


@contextlib.contextmanager
def _store_session(store: InMemoryStore):
    """Sync context manager — close the store on exit.

    ``InMemoryStore.close`` is sync (it closes per-kind buses); we
    expose it as a context manager so ``AsyncExitStack`` can own it
    via ``enter_context``.
    """
    try:
        yield store
    finally:
        try:
            store.close()
        except Exception:
            logger.exception("store close raised; swallowing")
