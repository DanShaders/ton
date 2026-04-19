import json
import logging
import sqlite3
from datetime import datetime
from pathlib import Path
from typing import TypedDict, cast, final, override

from tl import JSONSerializable

from .storage import RunMetadata, RunStatus, StorageBackend, TestMetadata

logger = logging.getLogger(__name__)


class _SelectRow(TypedDict):
    run_id: str
    start_time: float
    end_time: float | None
    status: str
    metadata: str
    host_port: int


_SCHEMA_VERSION = 1


@final
class SQLiteStorage(StorageBackend):
    def __init__(self, db_path: Path | None = None):
        self.db_path = str(db_path) if db_path is not None else ":memory:"
        if db_path is not None:
            db_path.parent.mkdir(parents=True, exist_ok=True)
        self.conn = sqlite3.connect(self.db_path, check_same_thread=False)
        self.conn.row_factory = sqlite3.Row
        self._init_schema()

    def _init_schema(self) -> None:
        cursor = self.conn.cursor()
        _ = cursor.execute("PRAGMA journal_mode=WAL")
        _ = cursor.execute("PRAGMA synchronous=NORMAL")
        _ = cursor.execute("PRAGMA foreign_keys=ON")

        _ = cursor.execute("PRAGMA user_version")
        user_version = cast(tuple[int], cursor.fetchone())[0]

        if user_version == 0:
            _ = cursor.execute(
                """
                CREATE TABLE IF NOT EXISTS runs (
                    run_id TEXT PRIMARY KEY,
                    start_time REAL NOT NULL,
                    end_time REAL,
                    status TEXT NOT NULL,
                    metadata TEXT NOT NULL,
                    host_port INTEGER NOT NULL
                )
                """
            )
            _ = cursor.execute(f"PRAGMA user_version={_SCHEMA_VERSION}")
        elif user_version != _SCHEMA_VERSION:
            raise RuntimeError(
                (
                    f"Unsupported schema version {user_version} at {self.db_path}; "
                    f"expected {_SCHEMA_VERSION}. Remove the file to start fresh."
                )
            )

        self.conn.commit()

    @override
    async def register_run(self, run_id: str, metadata: TestMetadata, host_port: int) -> None:
        """Insert a fresh run or resume an existing one.

        For a fresh run: writes all columns. For a resume (row exists):
        preserves ``start_time``, overwrites ``metadata`` / ``host_port``
        (nodes or port may have changed), clears ``end_time``, flips status
        to ``LIVE``.
        """
        cursor = self.conn.cursor()
        now = datetime.now().timestamp()
        _ = cursor.execute(
            """
            INSERT INTO runs (run_id, start_time, end_time, status, metadata, host_port)
                 VALUES (?, ?, NULL, ?, ?, ?)
            ON CONFLICT(run_id) DO UPDATE SET
                status    = excluded.status,
                end_time  = NULL,
                metadata  = excluded.metadata,
                host_port = excluded.host_port
            """,
            (
                run_id,
                now,
                RunStatus.LIVE.value,
                metadata.model_dump_json(),
                host_port,
            ),
        )
        self.conn.commit()

    @override
    async def set_run_status(
        self,
        run_id: str,
        status: RunStatus,
        *,
        if_port: int | None = None,
    ) -> None:
        cursor = self.conn.cursor()
        port_clause = " AND host_port = ?" if if_port is not None else ""
        port_args: tuple[int, ...] = (if_port,) if if_port is not None else ()
        if status == RunStatus.DORMANT:
            _ = cursor.execute(
                f"UPDATE runs SET status = ?, end_time = ? WHERE run_id = ?{port_clause}",
                (status.value, datetime.now().timestamp(), run_id, *port_args),
            )
        else:
            _ = cursor.execute(
                f"UPDATE runs SET status = ?, end_time = NULL WHERE run_id = ?{port_clause}",
                (status.value, run_id, *port_args),
            )
        self.conn.commit()

    @override
    async def list_runs(self, limit: int = 50) -> list[RunMetadata]:
        cursor = self.conn.cursor()
        _ = cursor.execute(
            """
            SELECT run_id, start_time, end_time, status, metadata, host_port
            FROM runs
            ORDER BY start_time DESC
            LIMIT ?
            """,
            (limit,),
        )
        return _collect(cursor)

    @override
    async def get_run_metadata(self, run_id: str) -> RunMetadata | None:
        cursor = self.conn.cursor()
        _ = cursor.execute(
            """
            SELECT run_id, start_time, end_time, status, metadata, host_port
            FROM runs WHERE run_id = ?
            """,
            (run_id,),
        )
        row = cast(_SelectRow | None, cursor.fetchone())
        if row is None:
            return None
        return _row_to_run(row)

    @override
    async def list_runs_with_status(self, status: RunStatus) -> list[RunMetadata]:
        cursor = self.conn.cursor()
        _ = cursor.execute(
            """
            SELECT run_id, start_time, end_time, status, metadata, host_port
            FROM runs WHERE status = ?
            ORDER BY start_time DESC
            """,
            (status.value,),
        )
        return _collect(cursor)

    @override
    async def list_allocated_ports(self) -> set[int]:
        cursor = self.conn.cursor()
        _ = cursor.execute(
            "SELECT host_port FROM runs WHERE status = ?",
            (RunStatus.LIVE.value,),
        )
        rows = cast(list[tuple[int]], cursor.fetchall())
        return {row[0] for row in rows}

    def close(self) -> None:
        self.conn.close()


def _row_to_run(row: _SelectRow) -> RunMetadata | None:
    metadata_json = cast(JSONSerializable, json.loads(row["metadata"]))
    if not isinstance(metadata_json, dict):
        logger.warning(f"Dropping run {row['run_id']}: metadata is not a JSON object")
        return None
    metadata = TestMetadata.model_validate(metadata_json)
    try:
        status = RunStatus(row["status"])
    except ValueError:
        logger.warning(f"Dropping run {row['run_id']}: unknown status {row['status']!r}")
        return None
    return RunMetadata(
        run_id=row["run_id"],
        start_time=datetime.fromtimestamp(row["start_time"]),
        end_time=datetime.fromtimestamp(row["end_time"]) if row["end_time"] else None,
        status=status,
        metadata=metadata,
        host_port=row["host_port"],
    )


def _collect(cursor: sqlite3.Cursor) -> list[RunMetadata]:
    rows = cast(list[_SelectRow], cursor.fetchall())
    runs: list[RunMetadata] = []
    for row in rows:
        run = _row_to_run(row)
        if run is not None:
            runs.append(run)
    return runs
