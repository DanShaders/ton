import asyncio
import logging
import shutil
from pathlib import Path

from tonapi import ton_api
from tontester.install import Install
from tontester.network import FullNode, Network
from tontester.zerostate import SimplexConsensusConfig

# Basechain shard (0:8000000000000000). Collators are only permitted for non-masterchain shards.
_BASECHAIN_SHARD_PREFIX = -(2**63)
_BASECHAIN = ton_api.TonNode_shardId(workchain=0, shard=_BASECHAIN_SHARD_PREFIX)


async def main():
    repo_root = Path(__file__).resolve().parents[2]
    working_dir = repo_root / "test/integration/.network-collator"
    shutil.rmtree(working_dir, ignore_errors=True)
    working_dir.mkdir(parents=True, exist_ok=True)

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

        # protocol_version >= 2 turns on the collator/validator split. A short catchain (validator group)
        # lifetime makes rotated_all_shards rotations frequent so the collator enters the deterministic
        # "second-previous rotation" snapshot within the test's runtime.
        network.config.mc_consensus = SimplexConsensusConfig(protocol_version=2, target_block_rate_ms=600)
        network.config.shard_consensus = SimplexConsensusConfig(protocol_version=2, target_block_rate_ms=600)
        # Catchain (validator group) lifetime: long enough that each group is stable for a while (so the
        # late-joining collator can establish overlay connectivity before the group rotates), but short enough
        # that the two-rotation warmup completes within the test.
        network.config.mc_valgroup_lifetime = 25
        network.config.shard_valgroup_lifetime = 25

        validators: list[FullNode] = []
        for _ in range(2):
            node = network.create_full_node()
            node.make_initial_validator()
            node.announce_to(dht)
            validators.append(node)

        # A dedicated collator node — a full node that is NOT in the validator set.
        collator = network.create_full_node()
        collator.announce_to(dht)

        async with asyncio.TaskGroup() as start_group:
            _ = start_group.create_task(dht.run())
            for node in validators:
                _ = start_group.create_task(node.run())
            _ = start_group.create_task(collator.run())

        await network.wait_mc_block(seqno=1)

        # Use the collator's full-node key as its collator adnl id: it is the one announced to DHT, so the
        # validators can resolve its address and route candidate broadcasts / pleaseCollate to it.
        collator_adnl = collator.fullnode_key.id

        # Each validator publishes the collator on-chain (validator-registry contract) via set-collators-list.
        collators_list = ton_api.Engine_validator_collatorsList(
            shards=[
                ton_api.Engine_validator_collatorsList_shard(
                    shard_id=_BASECHAIN,
                    collators=[ton_api.Engine_validator_collatorsList_collator(adnl_id=collator_adnl)],
                    self_collate=False,
                    select_mode="random",
                )
            ]
        )
        for node in validators:
            _ = await node.engine_console.request(
                ton_api.Engine_validator_setCollatorsListRequest(list_=collators_list)
            )

        # The collator node agrees to serve as a collator for the basechain.
        _ = await collator.engine_console.request(
            ton_api.Engine_validator_addCollatorRequest(adnl_id=collator_adnl, shard=_BASECHAIN)
        )

        # Let the chain run. Collators only enter the deterministic snapshot after two rotated_all_shards
        # rotations, after which the validators delegate their windows to the collator. Each block the collator
        # produces must be notarized (accepted) before it can build the next one (ResolveState needs the notar
        # cert), so *sustained* production by the collator directly proves its blocks are accepted as the
        # leaders' own. Poll the collator's log until it has produced several delegated blocks.
        marker = "Collator produced block for delegated window"
        needed = 5

        async def collator_produced_enough():
            while collator.log_path.read_text().count(marker) < needed:
                await asyncio.sleep(1.0)

        await asyncio.wait_for(collator_produced_enough(), timeout=240)

        delegated = any("delegating window" in node.log_path.read_text() for node in validators)
        assert delegated, "no validator delegated a window to the collator"
        produced = collator.log_path.read_text().count(marker)
        print(
            f"PASS: collator/validator split engaged — collator produced {produced} delegated blocks, "
            "each accepted as the leader's own"
        )


if __name__ == "__main__":
    asyncio.run(asyncio.wait_for(main(), 8 * 60))
