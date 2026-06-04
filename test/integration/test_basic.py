import asyncio
import logging
import shutil
from pathlib import Path

from contract import WalletV1Blueprint, ton
from tonapi import ton_api
from tontester.install import Install
from tontester.network import FullNode, Network
from tontester.zerostate import SimplexConsensusConfig


async def _add_custom_overlay(node: FullNode, overlay: ton_api.Engine_validator_customOverlay):
    request = ton_api.Engine_validator_addCustomOverlayRequest(overlay=overlay)
    _ = await node.engine_console.request(request)


async def main():
    repo_root = Path(__file__).resolve().parents[2]
    working_dir = repo_root / "test/integration/.network"
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
        network.config.shard_consensus = SimplexConsensusConfig(use_quic=True)

        nodes: list[FullNode] = []
        for i in range(3):
            node = network.create_full_node()
            if i != 0:
                node.make_initial_validator()
            node.announce_to(dht)
            nodes.append(node)

        async with asyncio.TaskGroup() as start_group:
            _ = start_group.create_task(dht.run())
            for node in nodes:
                _ = start_group.create_task(node.run())

        await network.wait_mc_block(seqno=1)

        actor_stats = await nodes[0].engine_console.get_actor_stats()
        assert "= ACTORS STATS =" in actor_stats and "= PERF COUNTERS =" in actor_stats

        _ = await network.wait_block(workchain=0, shard=-(2**63), seqno=1)

        client = await nodes[0].tonlib_client()
        main_wallet = network.zerostate.main_wallet(client)

        new_wallet = await main_wallet.deploy(WalletV1Blueprint(workchain=0), ton(1))

        async def balance_changed():
            while True:
                state = await client.raw_get_account_state(new_wallet.address)
                if state.balance > 0:
                    break
                await asyncio.sleep(0.5)

        await asyncio.wait_for(balance_changed(), timeout=30)

        wallet_state = await main_wallet.current
        assert wallet_state.seqno == 1

        # After ~10 masterchain blocks, wire up a custom overlay between
        # node 0 (non-validator) and node 2 (validator). Node 2 is a block
        # producer, so it acts as the block sender; node 0 only receives.
        await network.wait_mc_block(seqno=10)

        overlay = ton_api.Engine_validator_customOverlay(
            name="test-basic-custom",
            nodes=[
                ton_api.Engine_validator_customOverlayNode(
                    adnl_id=nodes[0].fullnode_key.id,
                    msg_sender=False,
                    msg_sender_priority=0,
                    block_sender=False,
                ),
                ton_api.Engine_validator_customOverlayNode(
                    adnl_id=nodes[2].fullnode_key.id,
                    msg_sender=False,
                    msg_sender_priority=0,
                    block_sender=True,
                ),
            ],
            sender_shards=[],
            skip_public_msg_send=False,
            use_quic=False,
        )

        await _add_custom_overlay(nodes[0], overlay)
        await _add_custom_overlay(nodes[2], overlay)

        await asyncio.Future()


if __name__ == "__main__":
    asyncio.run(asyncio.wait_for(main(), 5 * 60))
