"""Cleanup primitives shared by every async-stateful component.

The package's recurring bug shape was: hand-rolled cleanup either
(a) swallowed external cancellation, or (b) missed cleanup on a
not-yet-handled exception class. The structural fix is to model
every owned resource as an async context manager, so cleanup is the
``__aexit__`` body and runs on every exit path — success, exception,
``CancelledError`` — without per-site discipline.

Three primitives live here:

- :class:`asyncio.TaskGroup` (stdlib) for "spawn N tasks, cancel them
  all on exit." Components' ``running()`` methods compose this with
  :class:`contextlib.AsyncExitStack` so sub-resources clean up in
  LIFO order automatically. There is no ``cancel_and_drain`` helper
  because TaskGroup *is* the helper.

- :func:`owned_path` for filesystem paths the code creates before
  committing to a wider operation; cleanup runs on every non-released
  exit.

- :func:`owned_process` for ``asyncio.subprocess.Process`` handles.
  Closes the spawn-then-handle window: between
  ``await create_subprocess_exec(...)`` returning and the surrounding
  code taking responsibility for the handle, a ``CancelledError`` would
  otherwise leak the forked-and-running child. The wrapper takes
  responsibility from the moment the spawn returns and guarantees a
  graceful-then-forceful shutdown on exit.
"""

import asyncio
import contextlib
import logging
import shutil
from collections.abc import AsyncGenerator, Callable
from contextlib import asynccontextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import final

logger = logging.getLogger(__name__)


@final
@dataclass
class OwnedPath:
    """Handle returned by :func:`owned_path`.

    The path is removed when the surrounding ``async with`` exits,
    *unless* the caller has called :meth:`release` to transfer
    ownership. ``release()`` returns the path so callers can write::

        async with owned_path(p) as handle:
            do_setup(handle.path)
            transferred = handle.release()  # caller now owns it
    """

    path: Path
    released: bool = False

    def release(self) -> Path:
        """Transfer ownership to the caller; the context exit is a no-op.

        Idempotent.
        """
        self.released = True
        return self.path


def _default_path_cleanup(path: Path) -> None:
    try:
        path.rmdir()
    except OSError:
        shutil.rmtree(path, ignore_errors=True)


@asynccontextmanager
async def owned_path(
    path: Path,
    *,
    cleanup: Callable[[Path], None] | None = None,
) -> AsyncGenerator[OwnedPath]:
    """RAII for a filesystem path that may need to be cleaned up.

    On any exit path — success, exception, ``CancelledError`` — the
    ``finally`` block invokes ``cleanup(path)`` (default:
    ``rmdir`` then ``shutil.rmtree`` fallback) if ``path`` still
    exists and the caller hasn't :meth:`OwnedPath.release`-d it.

    Pass a custom ``cleanup`` for paths with special teardown
    semantics (cgroup dirs need to SIGKILL survivors before
    ``rmdir`` succeeds; the runtime backend supplies that).
    """
    handle = OwnedPath(path=path)
    try:
        yield handle
    finally:
        if not handle.released and path.exists():
            (cleanup or _default_path_cleanup)(path)


@asynccontextmanager
async def owned_process(
    *argv: str,
    env: dict[str, str] | None = None,
    cwd: str | None = None,
    grace_seconds: float = 5.0,
) -> AsyncGenerator[asyncio.subprocess.Process]:
    """RAII for a subprocess: spawn on enter, terminate-and-wait on exit.

    Closes the spawn-then-handle hazard window. Between
    ``await asyncio.create_subprocess_exec(...)`` returning a process
    handle and the surrounding code starting its own ``try/finally``,
    a ``CancelledError`` would leak the live child. Wrapping the
    spawn in an async context manager makes the take-responsibility
    boundary atomic from the caller's perspective: the moment the
    ``async with`` enters, the child is owned.

    On exit the process is terminated gracefully (SIGTERM + wait up
    to ``grace_seconds``), then killed if it didn't honor the grace
    period. Both signals tolerate ``ProcessLookupError`` (the child
    may have already exited on its own).
    """
    process = await asyncio.create_subprocess_exec(
        *argv,
        env=env,
        cwd=cwd,
        stdin=asyncio.subprocess.DEVNULL,
        stdout=asyncio.subprocess.DEVNULL,
        stderr=asyncio.subprocess.DEVNULL,
    )
    try:
        yield process
    finally:
        if process.returncode is None:
            try:
                process.terminate()
            except ProcessLookupError:
                pass
            with contextlib.suppress(TimeoutError):
                _ = await asyncio.wait_for(process.wait(), timeout=grace_seconds)
            if process.returncode is None:
                try:
                    process.kill()
                except ProcessLookupError:
                    pass
                with contextlib.suppress(TimeoutError):
                    _ = await asyncio.wait_for(process.wait(), timeout=2.0)
        if process.returncode is None:
            logger.warning("subprocess %d failed to die on cleanup", process.pid)
