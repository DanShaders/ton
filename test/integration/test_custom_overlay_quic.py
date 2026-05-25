import asyncio
import logging
import shutil
from pathlib import Path

from tonapi import ton_api
from tontester.install import Install
from tontester.network import FullNode, Network, StartOptions
from tontester.zerostate import SimplexConsensusConfig


async def _add_custom_overlay(node: FullNode, overlay: ton_api.Engine_validator_customOverlay):
    request = ton_api.Engine_validator_addCustomOverlayRequest(overlay=overlay)
    _ = await node.engine_console.request(request)


async def _log_mc_state(nodes: list[FullNode], interval: float):
    clients = [await node.tonlib_client() for node in nodes]
    while True:
        await asyncio.sleep(interval)
        parts: list[str] = []
        for node, client in zip(nodes, clients):
            try:
                info = await client.get_masterchain_info()
                assert info.last is not None
                parts.append(f"{node.name}: seqno={info.last.seqno}")
            except Exception as e:
                parts.append(f"{node.name}: <error: {type(e).__name__}>")
        print("[mc state] " + " | ".join(parts))


async def main():
    repo_root = Path(__file__).resolve().parents[2]
    working_dir = repo_root / "test/integration/.network-quic"
    shutil.rmtree(working_dir, ignore_errors=True)
    working_dir.mkdir(exist_ok=True)

    install = Install(repo_root / "build", repo_root)
    install.tonlibjson.client_set_verbosity_level(3)

    logging.basicConfig(
        level=logging.INFO,
        format="[%(levelname)s][%(asctime)s][%(name)s] %(message)s",
        datefmt="%Y-%m-%d %H-%M-%S",
    )

    async with Network(install, working_dir) as network:
        dht = network.create_dht_node()
        network.config.shard_validators = 2
        network.config.shard_consensus = SimplexConsensusConfig(
            target_block_rate_ms=500,
            use_quic=True,
        )
        network.config.shard_valgroup_lifetime = 10
        network.config.mc_consensus = SimplexConsensusConfig(
            target_block_rate_ms=500,
            use_quic=True,
        )
        network.config.mc_valgroup_lifetime = 10

        validators: list[FullNode] = []
        for _ in range(2):
            v = network.create_full_node()
            v.make_initial_validator()
            v.announce_to(dht)
            validators.append(v)

        third = network.create_full_node()
        third.announce_to(dht)

        async with asyncio.TaskGroup() as start_group:
            _ = start_group.create_task(dht.run())
            for node in (*validators, third):
                _ = start_group.create_task(
                    node.run(
                        StartOptions(
                            console_verbosity=1,
                            install=Install(repo_root / "build-old", repo_root)
                            if node == validators[1]
                            else None,
                        )
                    )
                )

        await network.wait_mc_block(seqno=1)

        v1 = validators[0]
        v1_fn_adnl = v1.fullnode_key.id
        v1_consensus = v1.validator_key.id
        third_fn_adnl = third.fullnode_key.id

        print(f"validator 1 full-node ADNL: {v1_fn_adnl.hex().upper()}")
        print(f"validator 1 consensus key:  {v1_consensus.hex().upper()}")
        print(f"third node    full-node ADNL: {third_fn_adnl.hex().upper()}")

        overlay = ton_api.Engine_validator_customOverlay(
            name="quic-demo",
            nodes=[
                ton_api.Engine_validator_customOverlayNode(
                    adnl_id=v1_fn_adnl,
                    msg_sender=False,
                    msg_sender_priority=0,
                    block_sender=True,
                ),
                ton_api.Engine_validator_customOverlayNode(
                    adnl_id=third_fn_adnl,
                    msg_sender=False,
                    msg_sender_priority=0,
                    block_sender=False,
                ),
            ],
            sender_shards=[],
            skip_public_msg_send=False,
            use_quic=True,
        )

        await _add_custom_overlay(v1, overlay)
        await _add_custom_overlay(third, overlay)

        logger = asyncio.create_task(_log_mc_state([*validators, third], interval=2.0))

        await validators[1].stop()
        await validators[1].run(StartOptions(console_verbosity=1))

        await logger


if __name__ == "__main__":
    asyncio.run(asyncio.wait_for(main(), 5 * 60))
