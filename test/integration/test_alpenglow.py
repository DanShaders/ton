import asyncio
import logging
import shutil
from pathlib import Path

from tontester.install import Install
from tontester.network import FullNode, Network
from tontester.zerostate import SimplexConsensusConfig


async def main() -> None:
    repo_root = Path(__file__).resolve().parents[2]
    working_dir = repo_root / "test/integration/.network"
    shutil.rmtree(working_dir)
    working_dir.mkdir(exist_ok=True)

    install = Install(repo_root / "build", repo_root)

    logging.basicConfig(
        level=logging.INFO,
        format="[%(levelname)s][%(asctime)s][%(name)s] %(message)s",
    )

    async with Network(install, working_dir) as network:
        dht = network.create_dht_node()

        NODES_COUNT = 2

        network.config.shard_valgroup_lifetime = 10
        network.config.shard_consensus = SimplexConsensusConfig(
            target_block_rate_ms=1000,
            slots_per_leader_window=4,
            first_block_timeout_ms=700,
            max_leader_window_desync=250,
        )
        network.config.mc_valgroup_lifetime = 10
        network.config.mc_consensus = SimplexConsensusConfig(
            target_block_rate_ms=1000,
            slots_per_leader_window=4,
            first_block_timeout_ms=800,
            max_leader_window_desync=250,
        )
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

        await asyncio.sleep(30)

        # for node in nodes:
        #     await node.engine_console.set_consensus_noncritical_params_overrides(
        #         ton_api.Consensus_noncriticalParamsOverrideList(
        #             overrides=[
        #                 ton_api.Consensus_noncriticalParamsOverride(
        #                     workchain=-1,
        #                     shard=-9223372036854775808,
        #                     from_seqno=0,
        #                     to_seqno=(1 << 31) - 1,
        #                     override=ton_api.Consensus_simplex_noncriticalParams(
        #                         flags=1 << 9,
        #                         standstill_max_egress_bytes_per_s=1000,
        #                     ),
        #                 )
        #             ],
        #         )
        #     )

        client = await nodes[0].tonlib_client()
        mc_last = (await client.get_masterchain_info()).last
        assert mc_last is not None
        mc_block = await client.get_block_header(mc_last)
        cc_seqno = mc_block.catchain_seqno

        flag = f"{mc_last.seqno}:{cc_seqno}:1"
        print(f"-F {flag}")

        await nodes[0].stop()

        await nodes[0].run(additional_args=["-F", flag])
        print("RESTARTED WITH ROTATE")

        await asyncio.sleep(30)

        await nodes[1].stop()
        await nodes[1].run(additional_args=["-F", flag])

        # for node in nodes:
        #     await node.stop()

        # print("RESTARTTTTT")
        # await asyncio.sleep(1)

        # for node in nodes:
        #     await node.run()

        await asyncio.sleep(3600)


if __name__ == "__main__":
    asyncio.run(main())
