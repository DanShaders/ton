import base64
import json
import logging
import re
import sqlite3
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import cast

from pydantic import BaseModel
from tonapi.ton_api import (
    TonNode_blockIdExt,
    ValidatorStats_collatedBlock,
    ValidatorStats_collatorNodeResponse,
    ValidatorStats_stats,
    ValidatorStats_stats_producer,
    ValidatorStats_validatedBlock,
)

from tl import JSONSerializable

LOCAL_DB_FILENAME = "session-stats-accel.sqlite"

max_ts: int | None = None


class Config(BaseModel):
    title: str
    frontend_build_dir: Path
    test_runs_file: str
    private_net: bool
    validate_actual_work_time: bool


type StatLine = (
    ValidatorStats_collatedBlock
    | ValidatorStats_validatedBlock
    | ValidatorStats_collatorNodeResponse
    | ValidatorStats_stats
)


@dataclass(frozen=True)
class BlockId:
    @staticmethod
    def create(bid: TonNode_blockIdExt) -> "BlockId":
        return BlockId(
            workchain=bid.workchain,
            shard=bid.shard,
            seqno=bid.seqno,
            root_hash=bid.root_hash,
            file_hash=bid.file_hash,
        )

    workchain: int
    shard: int
    seqno: int
    root_hash: bytes
    file_hash: bytes


config = Config.model_validate_json(Path("config.json").read_bytes())


conn = sqlite3.connect(LOCAL_DB_FILENAME)
cursor = conn.cursor()


def init_schema() -> None:
    _ = cursor.execute("""
        CREATE TABLE IF NOT EXISTS processed_blocks_from (
            workchain INTEGER NOT NULL,
            shard INTEGER NOT NULL,
            seqno INTEGER NOT NULL,
            root_hash TEXT NOT NULL,
            file_hash TEXT NOT NULL,
            self TEXT NOT NULL,
            PRIMARY KEY (workchain, shard, seqno, root_hash, file_hash, self)
        )
    """)
    _ = cursor.execute("""
        CREATE TABLE IF NOT EXISTS stats (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE
        )
    """)
    _ = cursor.execute("""
        CREATE TABLE IF NOT EXISTS data (
            stat_id INTEGER NOT NULL,
            workchain INTEGER NOT NULL,
            timestamp INTEGER NOT NULL,
            v_count INTEGER NOT NULL,
            v_sum REAL NOT NULL,
            v_min REAL NOT NULL,
            v_max REAL NOT NULL,
            PRIMARY KEY (stat_id, workchain, timestamp),
            FOREIGN KEY (stat_id) REFERENCES stats(id)
        )
    """)
    conn.commit()


def try_add_block_from(block_id: TonNode_blockIdExt, node: str) -> bool:
    _ = cursor.execute(
        """
            INSERT or ignore into processed_blocks_from (workchain, shard, seqno, root_hash, file_hash, self)
            values (?, ?, ?, ?, ?, ?)
        """,
        (
            block_id.workchain,
            block_id.shard,
            block_id.seqno,
            base64.b64encode(block_id.root_hash),
            base64.b64encode(block_id.file_hash),
            node,
        ),
    )
    return cursor.rowcount == 1


def parse_source_file(s: str) -> str:
    ss = s.split("-")
    if len(ss) < 3:
        return ""
    return "-".join(ss[:3])


stat_id_to_name: dict[str, int] = {}

process_all_collates = not config.private_net


def get_stat_id(name: str) -> int:
    if name in stat_id_to_name:
        return stat_id_to_name[name]
    _ = cursor.execute("SELECT id from stats where name = ?", (name,))
    rows = cursor.fetchall()
    if len(rows) == 1:
        idx = cast(int, rows[0][0])
    else:
        _ = cursor.execute("INSERT into stats (name) values (?)", (name,))
        idx = cursor.lastrowid
        assert idx is not None
    stat_id_to_name[name] = idx
    return idx


def add_stat_data(stat: str | int, workchain: int, timestamp: float, value: int | float) -> None:
    if isinstance(stat, str):
        stat = get_stat_id(stat)
    timestamp_int = int(timestamp) // 60 * 60
    _ = cursor.execute(
        """
            INSERT into data (stat_id, workchain, timestamp, v_count, v_sum, v_min, v_max)
            values (?, ?, ?, ?, ?, ?, ?)
            on conflict (stat_id, workchain, timestamp) do update
            set
                v_count = v_count + excluded.v_count,
                v_sum = v_sum + excluded.v_sum,
                v_min = min(v_min, excluded.v_min),
                v_max = max(v_max, excluded.v_max)
        """,
        (stat, workchain, timestamp_int, 1, value, value, value),
    )


def ts_from_stat_line(line: StatLine) -> float:
    match line:
        case ValidatorStats_collatedBlock():
            return line.collated_at
        case ValidatorStats_validatedBlock():
            return line.validated_at
        case ValidatorStats_collatorNodeResponse():
            return line.timestamp
        case ValidatorStats_stats():
            return line.timestamp


def parse_time_stats(ss: str) -> dict[str, float]:
    res: dict[str, float] = {}
    for s in ss.split("\n"):
        s = s.strip()
        m = re.match(r"^\{([^:]+):[-0-9.]+->[-0-9.]+\(([-0-9.]+)\)}", s)
        if m:
            res[m.group(1)] = float(m.group(2))
    return res


def parse_work_time_stats(wt: str) -> dict[str, float]:
    if wt.endswith('"'):
        wt = wt[:-1]
    if wt.startswith('"'):
        wt = wt[1:]
    res: dict[str, float] = {}
    for s in wt.split():
        if "=" not in s:
            continue
        key, val = s.split("=")
        res[key] = float(val)
    return res


def process_accepted_block_collate(
    line: ValidatorStats_stats,
    collate: ValidatorStats_collatedBlock,
    prod: ValidatorStats_stats_producer,
):
    timestamp = line.timestamp
    block_id = line.block_id
    assert block_id is not None

    workchain = block_id.workchain
    add_stat_data("BLOCK_size", workchain, timestamp, collate.bytes_)
    add_stat_data("BLOCK_collated_data_size", workchain, timestamp, collate.collated_data_bytes)
    assert collate.block_limits is not None
    add_stat_data("BLOCK_size_est", workchain, timestamp, collate.block_limits.bytes_)
    add_stat_data(
        "BLOCK_collated_data_size_est",
        workchain,
        timestamp,
        collate.block_limits.collated_data_bytes,
    )

    if collate.block_stats is not None:
        add_stat_data("BLOCK_transactions", workchain, timestamp, collate.block_stats.transactions)
        add_stat_data(
            "BLOCK_msg_queue_size",
            workchain,
            timestamp,
            collate.block_stats.new_out_msg_queue_size,
        )
        add_stat_data(
            "BLOCK_msg_queue_cleaned",
            workchain,
            timestamp,
            collate.block_stats.msg_queue_cleaned,
        )
        add_stat_data(
            "BLOCK_queue_total_processed",
            workchain,
            timestamp,
            sum(x.processed_msgs for x in collate.block_stats.neighbors),
        )
        add_stat_data(
            "BLOCK_queue_total_skipped",
            workchain,
            timestamp,
            sum(x.skipped_msgs for x in collate.block_stats.neighbors),
        )
        add_stat_data(
            "BLOCK_queue_limit_reached",
            workchain,
            timestamp,
            1 if any(x.limit_reached for x in collate.block_stats.neighbors) else 0,
        )
        if collate.block_stats.neighbors:
            add_stat_data(
                "BLOCK_max_neighbor_processed",
                workchain,
                timestamp,
                max(x.processed_msgs for x in collate.block_stats.neighbors),
            )
        for x in collate.block_stats.neighbors:
            if x.msg_limit >= 0:
                add_stat_data("BLOCK_neighbor_msg_limit", workchain, timestamp, x.msg_limit)

        if workchain == -1:
            add_stat_data(
                "shards_count",
                workchain,
                timestamp,
                len(collate.block_stats.shard_configuration),
            )

    add_stat_data(
        "BLOCK_load_fraction_queue_cleanup",
        workchain,
        timestamp,
        collate.block_limits.load_fraction_queue_cleanup,
    )
    add_stat_data(
        "BLOCK_load_fraction_dispatch",
        workchain,
        timestamp,
        collate.block_limits.load_fraction_dispatch,
    )
    add_stat_data(
        "BLOCK_load_fraction_internals",
        workchain,
        timestamp,
        collate.block_limits.load_fraction_internals,
    )
    add_stat_data(
        "BLOCK_load_fraction_externals",
        workchain,
        timestamp,
        collate.block_limits.load_fraction_externals,
    )
    add_stat_data(
        "BLOCK_load_fraction_new_msgs",
        workchain,
        timestamp,
        collate.block_limits.load_fraction_new_msgs,
    )
    if prod.serialize_time > 0:
        add_stat_data("BLOCK_serialize_time", workchain, timestamp, prod.serialize_time)
    if prod.serialized_size > 0:
        add_stat_data("BLOCK_serialized_size", workchain, timestamp, prod.serialized_size)
        add_stat_data(
            "BLOCK_serialized_size_frac",
            workchain,
            timestamp,
            prod.serialized_size / (collate.bytes_ + collate.collated_data_bytes),
        )

    time_stats = parse_time_stats(collate.time_stats)
    other_wait = collate.total_time - collate.work_time
    for name in ["set_block_candidate"]:
        if name in time_stats:
            add_stat_data(f"BLOCK_collate_time_{name}", workchain, timestamp, time_stats[name])
            other_wait -= time_stats[name]
    add_stat_data("BLOCK_collate_time_other_wait", workchain, timestamp, other_wait)
    for t, stats in (("real", collate.work_time_real_stats), ("cpu", collate.work_time_cpu_stats)):
        wt = parse_work_time_stats(stats)
        other = 0.0
        for s in wt:
            v = wt[s]
            if s == "total":
                other += v
            else:
                if not s.startswith("*"):
                    other -= v
                add_stat_data(f"BLOCK_collate_work_time_{t}_{s}", workchain, timestamp, v)
        add_stat_data(f"BLOCK_collate_work_time_{t}_other", workchain, timestamp, other)

    if collate.storage_stat_cache is not None:
        ss = collate.storage_stat_cache
        for metric_name, metric_value in [
            ("small_cnt", ss.small_cnt),
            ("small_cells", ss.small_cells),
            ("hit_cnt", ss.hit_cnt),
            ("hit_cells", ss.hit_cells),
            ("miss_cnt", ss.miss_cnt),
            ("miss_cells", ss.miss_cells),
        ]:
            add_stat_data(
                f"BLOCK_collate_storage_stat_cache_{metric_name}",
                workchain,
                timestamp,
                metric_value,
            )


def add_consensus_stats_collate(
    line: ValidatorStats_stats,
    line_prev: ValidatorStats_stats | None,
    request: ValidatorStats_collatorNodeResponse | None,
    collate: ValidatorStats_collatedBlock | None,
    producer: ValidatorStats_stats_producer,
    first_candidate: bool,
):
    if not first_candidate:
        return
    if collate is None:
        return
    if line_prev is None:
        return
    self_collated = collate.is_validator
    request_timestamp = collate.collated_at
    if not self_collated:
        if request is None:
            return
        request_timestamp = request.timestamp

    timestamp = line.timestamp
    block_id = line.block_id
    assert block_id is not None
    workchain = block_id.workchain

    tss = [
        line_prev.timestamp,
        collate.collated_at - collate.total_time,
        collate.collated_at - collate.work_time,
        collate.collated_at,
        request_timestamp,
        producer.got_block_at if not self_collated else collate.collated_at,
        producer.approved_33pct_at,
        producer.approved_66pct_at,
        producer.signed_33pct_at,
        producer.signed_66pct_at,
    ]
    if any(x <= 0 for x in tss):
        return
    for i in range(1, len(tss) - 1):
        tss[i + 1] = max(tss[i + 1], tss[i])
    tss = [tss[i + 1] - tss[i] for i in range(len(tss) - 1)]
    names = [
        "before_collate",
        "collate_wait",
        "collate_work",
        "before_request",
        "download",
        "approve33",
        "approve66",
        "sign33",
        "sign66",
    ]
    for name, x in zip(names, tss):
        name1 = "COLLATE_" + name
        add_stat_data(name1, workchain, timestamp, x)
        name2 = "COLLATE_" + ("self_" if self_collated else "collator_") + name
        add_stat_data(name2, workchain, timestamp, x)


def add_consensus_stats_validate(
    line: ValidatorStats_stats,
    line_prev: ValidatorStats_stats | None,
    validate: ValidatorStats_validatedBlock | None,
    producer: ValidatorStats_stats_producer,
    first_candidate: bool,
):
    if validate is None:
        return
    timestamp = line.timestamp
    block_id = line.block_id
    assert block_id is not None
    workchain = block_id.workchain

    if validate.valid:
        time_stats = parse_time_stats(validate.time_stats)
        other_wait = validate.total_time - validate.work_time
        for name in ["set_block_candidate"]:
            if name in time_stats:
                add_stat_data(f"BLOCK_validate_time_{name}", workchain, timestamp, time_stats[name])
                other_wait -= time_stats[name]
        add_stat_data("BLOCK_validate_time_other_wait", workchain, timestamp, other_wait)
        for t, stat in (
            ("real", validate.work_time_real_stats),
            ("cpu", validate.work_time_cpu_stats),
        ):
            wt = parse_work_time_stats(stat)
            other = 0.0
            for s in wt:
                v = wt[s]
                if s == "total":
                    other += v
                else:
                    if not s.startswith("*"):
                        other -= v
                    add_stat_data(f"BLOCK_validate_work_time_{t}_{s}", workchain, timestamp, v)
            add_stat_data(f"BLOCK_validate_work_time_{t}_other", workchain, timestamp, other)

        actual_work_time = validate.actual_time if validate.actual_time else validate.work_time
        add_stat_data("BLOCK_validate_actual_work_time", workchain, timestamp, actual_work_time)
        parallel_validation = validate.parallel_accounts_validation
        add_stat_data(
            f"BLOCK_validate_actual_work_time_{'parallel' if parallel_validation else 'singlethread'}",
            workchain,
            timestamp,
            actual_work_time,
        )

    if not first_candidate:
        return
    if line_prev is None:
        return

    tss = [
        line_prev.timestamp,
        min(producer.got_block_at, producer.got_submit_at),
        min(producer.got_block_at, producer.got_submit_at),
        producer.got_block_at,
        validate.validated_at - validate.total_time,
        validate.validated_at
        - (validate.actual_time if validate.actual_time else validate.work_time),
        validate.validated_at,
        producer.approved_33pct_at,
        producer.approved_66pct_at,
        producer.signed_33pct_at,
        producer.signed_66pct_at,
    ]
    if any(x <= 0 for x in tss):
        return
    for i in range(3, len(tss) - 1):
        tss[i + 1] = max(tss[i + 1], tss[i])
    tss = [tss[i + 1] - tss[i] for i in range(len(tss) - 1)]
    names = [
        "get_submit",
        "get_submit_real",
        "get_block",
        "before_validate",
        "validate_wait",
        "validate_work",
        "approve33",
        "approve66",
        "sign33",
        "sign66",
    ]
    for name, x in zip(names, tss):
        name = "VALIDATE_" + name
        add_stat_data(name, workchain, timestamp, x)


mc_blocks_accepted_at: dict[BlockId, float] = dict()


def process_applied_block(
    line: ValidatorStats_stats, collate: ValidatorStats_collatedBlock, mc_accepted_at: float
):
    block_id = line.block_id
    assert block_id is not None

    workchain = block_id.workchain
    timestamp = line.timestamp
    add_stat_data("BLOCK_APPLIED_blocks", block_id.workchain, timestamp, 1)
    if collate.block_stats is not None:
        add_stat_data(
            "BLOCK_APPLIED_transactions",
            workchain,
            timestamp,
            collate.block_stats.transactions,
        )
    if block_id.workchain == 0 and mc_accepted_at > 0:
        add_stat_data(
            "BLOCK_APPLIED_shard_latency_a2a",
            block_id.workchain,
            timestamp,
            mc_accepted_at - line.timestamp,
        )
    if collate.block_stats is not None and collate.block_stats.mc_block_id is not None:
        mc_block_id = collate.block_stats.mc_block_id
        if BlockId.create(mc_block_id) in mc_blocks_accepted_at:
            add_stat_data(
                "BLOCK_APPLIED_master_latency_a2a",
                block_id.workchain,
                timestamp,
                line.timestamp - mc_blocks_accepted_at[BlockId.create(mc_block_id)],
            )


def main():
    global max_ts
    logging.basicConfig(format="%(asctime)s %(levelname)s: %(message)s", level=logging.INFO)

    init_schema()

    files: list[str] = []
    for s in sys.argv[1:]:
        if s.startswith("--"):
            if s.startswith("--max-ts="):
                max_ts = int(s[s.find("=") + 1 :])
            else:
                assert False
        else:
            files.append(s)

    all_lines: list[StatLine] = []
    for file_name in files:
        logging.info(f"Reading {file_name}")
        with open(file_name, "r") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    line = cast(JSONSerializable, json.loads(line))
                    assert isinstance(line, dict)
                except json.decoder.JSONDecodeError:
                    continue

                match line["@type"]:
                    case "validatorStats.collatedBlock":
                        line = ValidatorStats_collatedBlock.from_dict(line)
                    case "validatorStats.validatedBlock":
                        line = ValidatorStats_validatedBlock.from_dict(line)
                    case "validatorStats.collatorNodeResponse":
                        line = ValidatorStats_collatorNodeResponse.from_dict(line)
                    case "validatorStats.stats":
                        line = ValidatorStats_stats.from_dict(line)
                    case _:
                        logging.warning(f"Unknown line type: {line['@type']}")
                        continue

                if max_ts is None or ts_from_stat_line(line) < max_ts:
                    all_lines.append(line)

    all_lines.sort(key=ts_from_stat_line)
    logging.info(f"Total lines: {len(all_lines)}")

    stats_collate: dict[BlockId, ValidatorStats_collatedBlock] = dict()
    stats_validate: dict[tuple[str, BlockId], ValidatorStats_validatedBlock] = dict()
    stats_request: dict[BlockId, ValidatorStats_collatorNodeResponse] = dict()
    stats_session_seqno: dict[tuple[str, bytes, int], ValidatorStats_stats] = dict()
    stats_blocks_our: dict[BlockId, tuple[ValidatorStats_stats, ValidatorStats_stats_producer]] = (
        dict()
    )
    stats_session_seqno_our: dict[tuple[bytes, int], ValidatorStats_stats] = dict()
    new_applied_blocks: dict[BlockId, tuple[int, float]] = dict()

    accepted_ours: list[tuple[ValidatorStats_stats, ValidatorStats_stats_producer]] = []

    for line in all_lines:
        node = base64.b64encode(line.self_).decode()

        match line:
            case ValidatorStats_collatedBlock():
                assert line.block_id is not None
                stats_collate[BlockId.create(line.block_id)] = line

            case ValidatorStats_validatedBlock():
                assert line.block_id is not None
                stats_validate[(node, BlockId.create(line.block_id))] = line

            case ValidatorStats_collatorNodeResponse():
                assert line.block_id is not None and line.original_block_id is not None
                new_block_id = BlockId.create(line.block_id)
                old_block_id = BlockId.create(line.original_block_id)

                if old_block_id in stats_collate:
                    stats_collate[new_block_id] = stats_collate[old_block_id]
                stats_request[new_block_id] = line

            case ValidatorStats_stats():
                if not line.success:
                    continue

                assert line.block_id is not None
                block_id = BlockId.create(line.block_id)
                block_seqno = block_id.seqno
                workchain = block_id.workchain
                session_id = line.session_id
                timestamp = line.timestamp

                if workchain == -1 and block_id not in mc_blocks_accepted_at:
                    mc_blocks_accepted_at[block_id] = timestamp

                if not try_add_block_from(line.block_id, node):
                    continue

                stats_session_seqno[(node, session_id, block_seqno)] = line

                collate = stats_collate.get(block_id)
                validate = stats_validate.get((node, block_id))
                request = stats_request.get(block_id)
                line_prev = stats_session_seqno.get((node, session_id, block_seqno - 1))

                prod1 = -1
                prod2 = -1
                producer = None
                for i, rnd in enumerate(line.rounds):
                    for j, prod in enumerate(rnd.producers):
                        if prod.is_accepted:
                            if prod.got_block_by:
                                if prod.got_block_by in (2, 3):
                                    add_stat_data(
                                        "got_block_by_query",
                                        workchain,
                                        timestamp,
                                        1 if prod.got_block_by == 3 else 0,
                                    )

                            prod1 = i
                            prod2 = j
                            producer = prod
                if producer is None:
                    continue

                first_candidate = prod1 == 0 and prod2 == 0
                is_ours = producer.is_ours

                if is_ours:
                    stats_blocks_our[block_id] = (line, producer)
                    stats_session_seqno_our[(session_id, block_seqno)] = line
                if is_ours and collate is not None:
                    add_stat_data(
                        "first_candidate", workchain, timestamp, 1 if first_candidate else 0
                    )
                    process_accepted_block_collate(line, collate, producer)

                    if workchain == -1:
                        new_applied_blocks[block_id] = (
                            block_seqno,
                            collate.collated_at,
                        )
                        assert collate.block_stats is not None
                        for shard_block_id in collate.block_stats.shard_configuration:
                            if BlockId.create(shard_block_id) in new_applied_blocks:
                                new_applied_blocks[BlockId.create(shard_block_id)] = min(
                                    new_applied_blocks[BlockId.create(shard_block_id)],
                                    (block_seqno, collate.collated_at),
                                )
                            else:
                                new_applied_blocks[BlockId.create(shard_block_id)] = (
                                    block_seqno,
                                    timestamp,
                                )
                    accepted_ours.append((line, producer))

                if is_ours:
                    add_consensus_stats_collate(
                        line, line_prev, request, collate, producer, first_candidate
                    )
                else:
                    add_consensus_stats_validate(
                        line, line_prev, validate, producer, first_candidate
                    )

    visited: set[BlockId] = set()

    def dfs(line: ValidatorStats_stats, mc_accepted_at: float):
        block_id = line.block_id
        assert block_id is not None

        if BlockId.create(block_id) in visited:
            return
        visited.add(BlockId.create(block_id))
        key = (line.session_id, block_id.seqno - 1)
        if key in stats_session_seqno_our:
            dfs(stats_session_seqno_our[key], mc_accepted_at)
        collate = stats_collate.get(BlockId.create(block_id))
        if collate is not None:
            process_applied_block(line, collate, mc_accepted_at)

    new_applied_list = list(new_applied_blocks)
    new_applied_list.sort(key=lambda x: new_applied_blocks[x])
    for block_id in new_applied_list:
        _, mc_accepted_at = new_applied_blocks[block_id]
        if block_id in stats_blocks_our:
            line, prod = stats_blocks_our[block_id]
            dfs(line, mc_accepted_at)

    conn.commit()
    conn.close()


if __name__ == "__main__":
    main()
