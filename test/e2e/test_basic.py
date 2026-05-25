import asyncio
from collections.abc import AsyncGenerator
from dataclasses import dataclass
from typing import final

import pytest
import pytest_asyncio
from contract import WalletV1, WalletV1Blueprint, ton
from tontester.install import Install
from tontester.network import FullNode, Network

from tonlib import TonlibClient
from tontester.zerostate import SimplexConsensusConfig

pytestmark = pytest.mark.asyncio(loop_scope="module")


@final
@dataclass(frozen=True)
class _RunningBasic:
    network: Network
    nodes: list[FullNode]
    client: TonlibClient
    main_wallet: WalletV1
    new_wallet: WalletV1


@pytest_asyncio.fixture(scope="module", loop_scope="module")
async def running(
    install: Install,
    tmp_path_factory: pytest.TempPathFactory,
) -> AsyncGenerator[_RunningBasic]:
    working_dir = tmp_path_factory.mktemp("basic")
    async with Network(install, working_dir) as network:
        dht = network.create_dht_node()

        network.config.shard_validators = 2
        network.config.shard_consensus = SimplexConsensusConfig(
            target_block_rate_ms=500,
            use_quic=True,
            use_block_sync=True,
        )
        network.config.mc_consensus = SimplexConsensusConfig(
            target_block_rate_ms=500,
            use_quic=True,
            use_block_sync=True,
        )

        nodes: list[FullNode] = []
        for _ in range(2):
            node = network.create_full_node()
            node.make_initial_validator()
            node.announce_to(dht)
            nodes.append(node)

        async with asyncio.TaskGroup() as start_group:
            _ = start_group.create_task(dht.run())
            for node in nodes:
                _ = start_group.create_task(node.run())

        await network.wait_mc_block(seqno=1)
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

        yield _RunningBasic(
            network=network,
            nodes=nodes,
            client=client,
            main_wallet=main_wallet,
            new_wallet=new_wallet,
        )


async def test_actor_stats(running: _RunningBasic):
    stats = await running.nodes[0].engine_console.get_actor_stats()
    assert "= ACTORS STATS =" in stats
    assert "= PERF COUNTERS =" in stats


async def test_new_wallet_has_positive_balance(running: _RunningBasic):
    state = await running.client.raw_get_account_state(running.new_wallet.address)
    assert state.balance == 999_999_000
