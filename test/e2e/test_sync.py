# pyright: reportPrivateUsage=false

import asyncio
import socket
from collections.abc import AsyncGenerator, Awaitable
from pathlib import Path
from typing import Callable, cast

import pytest
import pytest_asyncio
from tonapi import ton_api
from tontester.install import Install
from tontester.key import Key
from tontester.network import FullNode, Network

type SingleNodeNetwork = tuple[FullNode, Callable[[], Awaitable[None]]]


@pytest_asyncio.fixture
async def single_node_network(
    install: Install, tmp_path: Path
) -> AsyncGenerator[SingleNodeNetwork]:
    async with Network(install, tmp_path) as network:
        dht = network.create_dht_node()
        node = network.create_full_node()
        node.make_initial_validator()
        node.announce_to(dht)

        async def start():
            async with asyncio.TaskGroup() as g:
                _ = g.create_task(dht.run())
                _ = g.create_task(node.run())

        yield node, start


@pytest.mark.asyncio
async def test_waitfor_liteserver_blocks_until_initial_port_binds(
    single_node_network: SingleNodeNetwork,
):
    node, start_closure = single_node_network

    wait_query = ton_api.Engine_validator_waitForLiteServerRequest()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as squatter:
        squatter.bind(("127.0.0.1", node._liteserver_addr.port))
        squatter.listen(1)

        await start_closure()

        wait_task = asyncio.create_task(node.engine_console.request(wait_query))
        done, _ = await asyncio.wait({wait_task}, timeout=3.0)
        assert not done, "waitForLiteServer returned while port was busy"

        squatter.close()

        await wait_task


@pytest.mark.asyncio
async def test_dynamic_liteserver_add_does_not_gate_readiness(
    single_node_network: SingleNodeNetwork,
):
    node, start_closure = single_node_network

    await start_closure()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as squatter:
        squatter.bind(("127.0.0.1", 0))
        squatter.listen(1)
        busy_port = cast(int, squatter.getsockname()[1])

        new_key = Key()
        _ = await node.engine_console.request(
            ton_api.Engine_validator_importPrivateKeyRequest(key=new_key.private_key)
        )
        _ = await node.engine_console.request(
            ton_api.Engine_validator_addAdnlIdRequest(key_hash=new_key.id, category=0)
        )
        _ = await node.engine_console.request(
            ton_api.Engine_validator_addLiteserverRequest(key_hash=new_key.id, port=busy_port)
        )

        wait_query = ton_api.Engine_validator_waitForLiteServerRequest()
        _ = await node.engine_console.request(wait_query)
