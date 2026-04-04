import asyncio
import logging
import random
import shutil
import subprocess
from pathlib import Path

from contract.contract import ton
from pytoniq_core import (
    Address,
    Cell,
    ExternalMsgInfo,
    InternalMsgInfo,
    MessageAny,
    StateInit,
    WalletMessage,
    begin_cell,
)
from tontester.install import Install
from tontester.network import FullNode, Network
from tontester.zerostate import SimplexConsensusConfig

CONTRACTS_DIR = Path(__file__).parent / "contracts"
BATCH_SIZE = 250
TOTAL_CHILDREN = 10000
NUM_BATCHES = TOTAL_CHILDREN // BATCH_SIZE
SPAM_TPS = 2000  # target externals per second during spam phase
SIG_ITERATIONS = 0  # signature verifications per internal message


def compile_tolk(install: Install, tolk_source: Path, working_dir: Path) -> Cell:
    working_dir.mkdir(parents=True, exist_ok=True)
    boc_path = working_dir / "output.boc"
    fif_path = working_dir / "output.fif"

    tolk_exe = install.build_dir / "tolk/tolk"
    stdlib_path = install.source_dir / "crypto/smartcont/tolk-stdlib"
    fift_exe = install.build_dir / "crypto/fift"
    fift_lib = install.source_dir / "crypto/fift/lib"

    env = {"TOLKSTDLIB": str(stdlib_path)}
    subprocess.run(
        [str(tolk_exe), "--boc-output", str(boc_path), "-o", str(fif_path), str(tolk_source)],
        check=True,
        env=env,
    )
    subprocess.run(
        [str(fift_exe), "-I", str(fift_lib), "-s", str(fif_path)],
        check=True,
    )

    return Cell.one_from_boc(boc_path.read_bytes())


def make_child_data(child_idx: int) -> Cell:
    return begin_cell().store_uint(0, 32).store_uint(child_idx, 64).end_cell()


def compute_child_address(child_code: Cell, child_idx: int) -> Address:
    child_data = make_child_data(child_idx)
    state_init = StateInit(code=child_code, data=child_data)
    return Address((0, state_init.serialize().hash))


async def main() -> None:
    repo_root = Path(__file__).resolve().parents[2]
    working_dir = repo_root / "test/integration/.network"
    shutil.rmtree(working_dir, ignore_errors=True)
    working_dir.mkdir(exist_ok=True)

    install = Install(repo_root / "build", repo_root)
    install.tonlibjson.client_set_verbosity_level(0)

    logging.basicConfig(
        level=logging.WARNING,
        format="[%(levelname)s][%(asctime)s][%(name)s] %(message)s",
    )

    # Compile contracts
    print("Compiling contracts...")
    receiver_code = compile_tolk(
        install, CONTRACTS_DIR / "receiver.tolk", working_dir / "compile-receiver"
    )
    deployer_code = compile_tolk(
        install, CONTRACTS_DIR / "deployer.tolk", working_dir / "compile-deployer"
    )
    print("Contracts compiled.")

    # Prepare deployer StateInit
    deployer_data = (
        begin_cell().store_bit(0).store_ref(receiver_code).end_cell()
    )  # empty map + childCode ref
    deployer_state_init = StateInit(code=deployer_code, data=deployer_data)
    deployer_address = Address((0, deployer_state_init.serialize().hash))
    print(f"Deployer address: {deployer_address.to_str()}")

    async with Network(install, working_dir) as network:
        dht = network.create_dht_node()

        NODES_COUNT = 2

        network.config.shard_valgroup_lifetime = 250
        # network.config.shard_consensus = SimplexConsensusConfig(
        #     target_block_rate_ms=400,
        #     slots_per_leader_window=4,
        #     first_block_timeout_ms=700,
        #     max_leader_window_desync=250,
        # )
        network.config.mc_valgroup_lifetime = 250
        # network.config.mc_consensus = SimplexConsensusConfig(
        #     target_block_rate_ms=400,
        #     slots_per_leader_window=4,
        #     first_block_timeout_ms=800,
        #     max_leader_window_desync=250,
        # )
        network.config.shard_validators = NODES_COUNT

        nodes: list[FullNode] = []
        for _ in range(NODES_COUNT):
            node = network.create_full_node()
            node.make_initial_validator()
            node.announce_to(dht)
            nodes.append(node)

        async with asyncio.TaskGroup() as start_group:
            _ = start_group.create_task(dht.run())
            for node in nodes:
                _ = start_group.create_task(node.run())

        await network.wait_mc_block(seqno=1)
        await network.wait_block(workchain=0, shard=-(2**63), seqno=1)

        nodes[0].enable_blockchain_explorer()

        # Background task: record actor stats every 15 seconds
        stats_dir = working_dir / "actor_stats"
        stats_dir.mkdir(exist_ok=True)

        async def record_actor_stats() -> None:
            while True:
                await asyncio.sleep(15)
                for node in nodes:
                    try:
                        stats = await node.engine_console.get_actor_stats()
                        ts = int(asyncio.get_event_loop().time())
                        path = stats_dir / f"{node.name}_{ts}.txt"
                        path.write_text(stats)
                    except Exception:
                        pass

        stats_task = asyncio.create_task(record_actor_stats())

        clients = [await node.tonlib_client() for node in nodes]
        client = clients[0]
        main_wallet = network.zerostate.main_wallet(client)

        # Deploy deployer contract via main wallet
        print("Deploying deployer contract...")
        deploy_msg = WalletMessage(
            send_mode=3,
            message=MessageAny(
                info=InternalMsgInfo(
                    ihr_disabled=True,
                    bounce=False,
                    bounced=False,
                    src=main_wallet.address,
                    dest=deployer_address,
                    value=ton(1_000_000_000),
                    ihr_fee=0,
                    fwd_fee=0,
                    created_lt=0,
                    created_at=0,
                ),
                init=deployer_state_init,
                body=Cell.empty(),
            ),
        )
        await main_wallet.send(deploy_msg)

        # Wait for deployer to appear on-chain
        async def wait_for_account(address: Address) -> None:
            while True:
                state = await client.raw_get_account_state(address)
                if state.balance > 0:
                    return
                await asyncio.sleep(0.5)

        await asyncio.wait_for(wait_for_account(deployer_address), timeout=30)
        print("Deployer deployed.")

        # Deploy children: send all batch externals in parallel, then wait
        print(f"Deploying {TOTAL_CHILDREN} children in {NUM_BATCHES} batches of {BATCH_SIZE}...")

        async def send_deploy_batch(batch: int) -> None:
            start_idx = batch * BATCH_SIZE
            last_in_batch = compute_child_address(receiver_code, start_idx + BATCH_SIZE - 1)
            attempt = 0
            while True:
                body = (
                    begin_cell()
                    .store_uint(start_idx, 64)
                    .store_uint(BATCH_SIZE, 32)
                    .store_uint(attempt, 32)  # nonce for dedup
                    .end_cell()
                )
                ext_msg = MessageAny(
                    info=ExternalMsgInfo(None, deployer_address, 0),  # pyright: ignore [reportArgumentType]
                    init=None,
                    body=body,
                )
                try:
                    await client.send_external(ext_msg)
                except Exception:
                    pass
                try:
                    await asyncio.wait_for(wait_for_account(last_in_batch), timeout=5)
                    print(f"  Batch {batch + 1}/{NUM_BATCHES} deployed")
                    return
                except TimeoutError:
                    attempt += 1

        await asyncio.gather(*(send_deploy_batch(b) for b in range(NUM_BATCHES)))
        print("All children deployed.")

        # Spot check a few children
        for idx in [0, TOTAL_CHILDREN // 2, TOTAL_CHILDREN - 1]:
            addr = compute_child_address(receiver_code, idx)
            state = await client.raw_get_account_state(addr)
            print(f"  Child {idx}: balance={state.balance}")

        # Compute all child addresses
        child_addresses = [compute_child_address(receiver_code, i) for i in range(TOTAL_CHILDREN)]

        # Background block monitor: prints on-chain TPS averaged over ~5s
        async def monitor_blocks() -> None:
            last_mc_seqno = 0
            # Rolling window of (utime, txs) for averaging
            recent: list[tuple[int, int]] = []
            while True:
                try:
                    mc_info = await client.get_masterchain_info()
                    mc_seqno = mc_info.last.seqno
                    if mc_seqno <= last_mc_seqno:
                        await asyncio.sleep(0.3)
                        continue

                    # Only process the latest few blocks, skip if too far behind
                    start_seqno = max(last_mc_seqno + 1, mc_seqno - 5)
                    for seqno in range(start_seqno, mc_seqno + 1):
                        try:
                            mc_block = await client.lookup_block(
                                workchain=-1, shard=-(2**63), seqno=seqno
                            )
                            mc_header = await client.get_block_header(mc_block)
                            shards_info = await client.get_shards(mc_block)

                            total_txs = 0
                            for shard_block in shards_info.shards:
                                txs = await client.get_block_transactions(shard_block)
                                total_txs += len(txs)

                            utime = mc_header.gen_utime
                            recent.append((utime, total_txs))
                            # Keep only entries within last 5 seconds
                            recent = [(t, n) for t, n in recent if utime - t < 5]

                            if len(recent) >= 2 and recent[-1][0] > recent[0][0]:
                                dt = recent[-1][0] - recent[0][0]
                                window_txs = sum(n for _, n in recent[:-1])
                                avg_tps = window_txs / dt
                                print(
                                    f"  [monitor] mc#{seqno} shards={len(shards_info.shards)}"
                                    f" txs={total_txs} avg_tps={avg_tps:.0f} (over {dt}s)"
                                )
                            else:
                                print(
                                    f"  [monitor] mc#{seqno} shards={len(shards_info.shards)}"
                                    f" txs={total_txs}"
                                )
                        except Exception:
                            # Block not yet synced to liteserver, skip it
                            continue

                    last_mc_seqno = mc_seqno
                except Exception:
                    pass
                await asyncio.sleep(0.3)

        monitor_task = asyncio.create_task(monitor_blocks())

        # Spam all children with externals forever (^C to stop)
        print(f"Spamming {TOTAL_CHILDREN} children at ~{SPAM_TPS} TPS (^C to stop)...")

        successful_sends = 0
        failed_sends = 0

        async def send_external_to(address: Address, nonce: int) -> bool:
            nonlocal successful_sends, failed_sends
            body = begin_cell().store_uint(nonce, 32).store_uint(SIG_ITERATIONS, 32).end_cell()
            msg = MessageAny(
                info=ExternalMsgInfo(None, address, 0),  # pyright: ignore [reportArgumentType]
                init=None,
                body=body,
            )
            try:
                await random.choice(clients).send_external(msg)
                successful_sends += 1
                return True
            except Exception:
                failed_sends += 1
                return False

        spam_round = 0
        spam_start = asyncio.get_event_loop().time()
        while True:
            round_ok = 0
            round_fail = 0
            for i in range(0, TOTAL_CHILDREN, SPAM_TPS):
                batch_start = asyncio.get_event_loop().time()
                batch = child_addresses[i : i + SPAM_TPS]
                before_ok = successful_sends
                before_fail = failed_sends
                await asyncio.gather(*(send_external_to(addr, spam_round) for addr in batch))
                batch_ok = successful_sends - before_ok
                batch_fail = failed_sends - before_fail
                round_ok += batch_ok
                round_fail += batch_fail
                elapsed = asyncio.get_event_loop().time() - batch_start
                sleep_for = max(0.0, 1.0 - elapsed)
                total_elapsed = asyncio.get_event_loop().time() - spam_start
                print(
                    f"  round {spam_round}: {successful_sends} ok {failed_sends} fail"
                    f" ({successful_sends / total_elapsed:.0f} avg TPS,"
                    f" {batch_ok}/{len(batch)} in {elapsed:.2f}s)"
                )
                if sleep_for > 0:
                    await asyncio.sleep(sleep_for)
            spam_round += 1


if __name__ == "__main__":
    asyncio.run(main())
