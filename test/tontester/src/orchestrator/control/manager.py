"""Top-level orchestrator process glue.

The :class:`Manager` is the process-wide owner of:

- the in-memory :class:`InMemoryStore`,
- every registered :class:`ControllerRunner`.

Lifecycle is :meth:`start` / :meth:`shutdown`, both idempotent. Resource
hygiene is enforced via :class:`contextlib.AsyncExitStack`: every
sub-component pushed onto the stack has its teardown registered as
``push_async_callback`` so a partial init rolls back the same way a
clean shutdown would. The contract is identical to the daemon's
``Live.resources``: callbacks pushed to the stack must not raise — wrap
fallible release in a local ``async def`` that swallows + logs.

The manager does not "do work" itself. All behavior comes from
controllers; the manager just hosts them.
"""

import contextlib
import logging
from typing import final

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

    Construction is cheap and inert: ``register_kind`` / ``add_controller``
    are sync setup. ``start()`` flips the live bit and spawns
    controllers. ``shutdown()`` reverses everything in LIFO order via
    the AsyncExitStack.

    Single-tenant: a given manager hosts one logical "control plane".
    Tests use one manager per test; production runs one per process.
    """

    def __init__(self, *, clock: Clock | None = None):
        self._store: InMemoryStore = InMemoryStore()
        self._clock: Clock = clock or RealClock()
        # Controllers/runners are covariant in their owned kind, so the
        # heterogeneous list types cleanly under the base alias.
        self._runners: list[ControllerRunner[_AnyResource]] = []
        self._stack: contextlib.AsyncExitStack = contextlib.AsyncExitStack()
        self._started: bool = False
        self._stopping: bool = False

    @property
    def store(self) -> InMemoryStore:
        return self._store

    # ---- setup (pre-start) ---------------------------------------------

    def register_kind(self, resource_type: type[_AnyResource]) -> None:
        if self._started:
            raise ManagerStopped()
        self._store.register_kind(resource_type)

    def add_controller(
        self,
        controller: Controller[_AnyResource],
        *,
        worker_count: int = 1,
    ) -> None:
        if self._started:
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

    async def start(self) -> None:
        """Start every registered runner; stop them all if any fail.

        Cleanup-on-failure uses AsyncExitStack ``pop_all``: callbacks
        accumulate on a bootstrap stack as runners come up, and on
        success we transfer ownership to the long-lived stack via
        ``pop_all``. Any exception escaping the ``async with bootstrap``
        — including ``CancelledError`` — runs the bootstrap's
        ``__aexit__`` which stops every already-started runner. The
        previous ``except Exception`` missed cancellation and leaked
        the partially-started runners' tasks.
        """
        if self._started:
            return
        self._started = True
        fully_started = False
        bootstrap = contextlib.AsyncExitStack()
        try:
            async with bootstrap:
                for runner in self._runners:
                    await runner.start()
                    _ = bootstrap.push_async_callback(_safe_stop, runner)
                self._stack = bootstrap.pop_all()
                fully_started = True
        finally:
            if not fully_started:
                # bootstrap.__aexit__ already stopped any runners we
                # successfully started. Close the store too so its
                # buses are torn down — runner watch loops would have
                # held subscriptions on them.
                _safe_close_store_sync(self._store)
                self._started = False

    async def shutdown(self) -> None:
        """Stop runners first, then close the store.

        The two phases are explicit (not encoded as AsyncExitStack push
        order) because every runner's watch loop reads from the store —
        closing the store first would leave running watch loops with
        broken subscriptions until ``stop()`` finally cancels them.
        """
        if not self._started or self._stopping:
            return
        self._stopping = True
        try:
            await self._stack.aclose()
        finally:
            # Always close the store, even if a runner stop raised.
            _safe_close_store_sync(self._store)
        self._started = False

    async def __aenter__(self) -> "Manager":
        await self.start()
        return self

    async def __aexit__(
        self,
        _exc_type: object,
        _exc: object,
        _tb: object,
    ) -> None:
        await self.shutdown()


# --- AsyncExitStack-safe wrappers ----------------------------------------
#
# Callbacks pushed onto the stack MUST NOT raise (see daemon's
# Live.resources noexcept-release contract). Each wrapper logs and
# swallows so aclose() can run every registered teardown without an
# early raise hiding a later one.


async def _safe_stop(runner: ControllerRunner[_AnyResource]) -> None:
    try:
        await runner.stop()
    except Exception:
        logger.exception("controller runner stop raised; swallowing")


def _safe_close_store_sync(store: InMemoryStore) -> None:
    """Sync close — store.close is already sync; no need for awaitability.

    Called from :meth:`Manager.shutdown` after the runner-stop stack has
    fully drained.
    """
    try:
        store.close()
    except Exception:
        logger.exception("store close raised; swallowing")
