"""Resource-protocol primitives: StopToken, CheckedExitStack, GracefulAbort.

These exercise the building blocks before the Resource shape is
actually adopted by Agent / Runtime. Each test pins one specific
property of the contract so a future implementation that breaks it
fails loud.
"""

import asyncio
from collections.abc import AsyncGenerator
from contextlib import asynccontextmanager

import pytest
from orchestrator.lifecycle import CheckedExitStack, GracefulAbort, StopToken

pytestmark = [pytest.mark.asyncio, pytest.mark.usefixtures("virtual_clock")]


async def test_stop_token_set_is_synchronous():
    """``set()`` must complete before any other task observes the
    transition — no ``await``, atomic from a sibling task's POV."""
    token = StopToken()
    assert not token.is_set
    token.set()
    assert token.is_set


async def test_stop_token_wait_returns_when_set():
    token = StopToken()

    async def _waiter() -> None:
        await token.wait()

    task = asyncio.create_task(_waiter())
    # Not yet set — task should not be done.
    await asyncio.sleep(0)
    assert not task.done()
    token.set()
    _ = await asyncio.wait_for(task, timeout=2.0)


async def test_stop_token_wait_returns_immediately_if_already_set():
    token = StopToken()
    token.set()
    # If wait() doesn't short-circuit when already set, this hangs.
    await asyncio.wait_for(token.wait(), timeout=2.0)


async def test_stop_token_set_cascades_to_children():
    parent = StopToken()
    child = parent.child()
    grandchild = child.child()
    parent.set()
    assert parent.is_set
    assert child.is_set
    assert grandchild.is_set


async def test_child_created_after_parent_set_inherits_state():
    """If a child is created from an already-set parent, the child
    starts in the set state. Otherwise the parent's cancel signal
    would be silently lost for late-bound children."""
    parent = StopToken()
    parent.set()
    child = parent.child()
    assert child.is_set


async def test_child_set_does_not_propagate_to_parent():
    parent = StopToken()
    child = parent.child()
    child.set()
    assert child.is_set
    assert not parent.is_set


async def test_checked_exit_stack_blocks_enter_when_token_set():
    """The whole point: every enter is a safe-point check."""
    token = StopToken()
    entered: list[str] = []

    @asynccontextmanager
    async def _named(name: str) -> AsyncGenerator[str]:
        entered.append(f"enter:{name}")
        try:
            yield name
        finally:
            entered.append(f"exit:{name}")

    async with CheckedExitStack(token) as stack:
        _ = await stack.enter_async_context(_named("first"))
        token.set()
        with pytest.raises(GracefulAbort):
            _ = await stack.enter_async_context(_named("second"))

    # First context's __aexit__ ran (cleanup); second never entered.
    assert entered == ["enter:first", "exit:first"]


async def test_checked_exit_stack_runs_rollback_on_graceful_abort():
    """When GracefulAbort fires inside the with-block, the stack's
    own __aexit__ runs all registered cleanups (in LIFO)."""
    token = StopToken()
    entered: list[str] = []

    @asynccontextmanager
    async def _named(name: str) -> AsyncGenerator[str]:
        entered.append(f"enter:{name}")
        try:
            yield name
        finally:
            entered.append(f"exit:{name}")

    with pytest.raises(GracefulAbort):
        async with CheckedExitStack(token) as stack:
            _ = await stack.enter_async_context(_named("a"))
            _ = await stack.enter_async_context(_named("b"))
            token.set()
            _ = await stack.enter_async_context(_named("c"))  # raises

    # a and b entered + exited; c never entered. Exit order is LIFO.
    assert entered == ["enter:a", "enter:b", "exit:b", "exit:a"]


async def test_checked_exit_stack_callback_also_blocks_when_token_set():
    """``stack.callback`` is a safe-point too — not just
    ``enter_async_context``."""
    token = StopToken()
    fired: list[str] = []

    async with CheckedExitStack(token) as stack:
        _ = stack.callback(fired.append, "first")
        token.set()
        with pytest.raises(GracefulAbort):
            _ = stack.callback(fired.append, "second")

    # First callback fired on unwind; second was never registered.
    assert fired == ["first"]


async def test_checked_exit_stack_does_not_block_after_enter():
    """The safe-point is the ``enter_*_context`` call, not the
    body of an already-entered context. Once a context is entered,
    code inside it runs even if the token fires later — only the
    *next* enter is checked.
    """
    token = StopToken()
    body_ran: list[str] = []

    @asynccontextmanager
    async def _ctx(name: str) -> AsyncGenerator[str]:
        try:
            yield name
        finally:
            pass

    async with CheckedExitStack(token) as stack:
        ctx = await stack.enter_async_context(_ctx("first"))
        token.set()
        body_ran.append(ctx)
        with pytest.raises(GracefulAbort):
            _ = await stack.enter_async_context(_ctx("second"))

    assert body_ran == ["first"]


async def test_stop_token_set_is_idempotent():
    """Calling ``set()`` on an already-set token is a no-op."""
    token = StopToken()
    token.set()
    assert token.is_set
    token.set()
    assert token.is_set


async def test_stop_token_multiple_waiters_all_wake():
    """Every pending ``wait()`` future resolves on a single ``set()``."""
    token = StopToken()
    waiters = [asyncio.create_task(token.wait()) for _ in range(5)]
    await asyncio.sleep(0)
    for w in waiters:
        assert not w.done()
    token.set()
    _ = await asyncio.gather(*waiters)
    for w in waiters:
        assert w.done() and w.exception() is None


async def test_stop_token_grandchild_inherits_set_at_construction():
    """Grandchildren created from a set ancestor start in set state."""
    parent = StopToken()
    parent.set()
    child = parent.child()
    grandchild = child.child()
    assert child.is_set
    assert grandchild.is_set


async def test_stop_token_grandparent_set_cascades_to_grandchild():
    """Cascade walks the whole subtree, not just direct children."""
    grandparent = StopToken()
    parent = grandparent.child()
    child = parent.child()
    assert not (grandparent.is_set or parent.is_set or child.is_set)
    grandparent.set()
    assert grandparent.is_set
    assert parent.is_set
    assert child.is_set


async def test_reaper_offload_runs_in_background():
    """Reaper offload schedules a coroutine; await wait() drains it."""
    from orchestrator.lifecycle import Reaper

    reaper = Reaper()
    seen: list[int] = []

    async def _work() -> None:
        seen.append(1)

    reaper.offload(_work())
    assert len(reaper) == 1, "reaper should hold the offloaded task"
    await reaper.wait()
    assert seen == [1]
    assert len(reaper) == 0


async def test_reaper_wait_drains_multiple_tasks_added_during_wait():
    """If a reaper task offloads more work, ``wait()`` keeps draining
    until the set is empty — not just one round."""
    from orchestrator.lifecycle import Reaper

    reaper = Reaper()
    seen: list[str] = []

    async def _grandchild() -> None:
        seen.append("grandchild")

    async def _child() -> None:
        seen.append("child")
        reaper.offload(_grandchild())

    reaper.offload(_child())
    await reaper.wait()
    assert seen == ["child", "grandchild"]


async def test_reaper_wait_with_no_offloads_returns_immediately():
    """The orderly-shutdown invariant: if no offloads, wait is fast."""
    from orchestrator.lifecycle import Reaper

    reaper = Reaper()
    await reaper.wait()
    assert len(reaper) == 0


async def test_reaper_swallows_exception_in_offloaded_task():
    """A reaper task crashing must not break the reaper itself.
    Other pending tasks still drain."""
    from orchestrator.lifecycle import Reaper

    reaper = Reaper()
    seen: list[str] = []

    async def _crashes() -> None:
        raise RuntimeError("reaper task boom")

    async def _ok() -> None:
        seen.append("ok")

    reaper.offload(_crashes())
    reaper.offload(_ok())
    await reaper.wait()
    assert "ok" in seen


async def test_stop_token_sibling_isolation():
    """Setting one child does not affect a sibling child of the same
    parent."""
    parent = StopToken()
    a = parent.child()
    b = parent.child()
    a.set()
    assert a.is_set
    assert not b.is_set
    assert not parent.is_set
