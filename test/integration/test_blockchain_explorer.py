import asyncio
import logging
import shutil
from pathlib import Path

import httpx
from tontester.install import Install
from tontester.network import FullNode, Network, StartOptions


async def main():
    repo_root = Path(__file__).resolve().parents[2]
    working_dir = repo_root / "test/integration/.be-network"
    shutil.rmtree(working_dir, ignore_errors=True)
    working_dir.mkdir(exist_ok=True)

    install = Install(repo_root / "build", repo_root)
    install.tonlibjson.client_set_verbosity_level(3)

    logging.basicConfig(
        level=logging.INFO,
        format="[%(levelname)s][%(asctime)s][%(name)s] %(message)s",
        datefmt="%Y-%m-%d %H-%M-%S",
    )
    log = logging.getLogger("test_blockchain_explorer")

    async with Network(install, working_dir) as network:
        dht = network.create_dht_node()
        network.config.shard_validators = 2

        nodes: list[FullNode] = []
        for _ in range(2):
            node = network.create_full_node()
            node.make_initial_validator()
            node.announce_to(dht)
            nodes.append(node)

        async with asyncio.TaskGroup() as start_group:
            _ = start_group.create_task(dht.run(StartOptions(console_verbosity=1)))
            for node in nodes:
                _ = start_group.create_task(node.run(StartOptions(console_verbosity=1)))

        be_node = nodes[0]
        await be_node.enable_blockchain_explorer()
        url = be_node.blockchain_explorer_url
        assert url is not None
        log.info(f"blockchain explorer reachable at {url}")

        await network.wait_mc_block(seqno=2)

        client = await be_node.tonlib_client()
        mc_info = await client.get_masterchain_info()
        assert mc_info.last is not None
        last = mc_info.last

        block_params = {
            "workchain": str(last.workchain),
            "shard": f"{last.shard & ((1 << 64) - 1):016x}",
            "seqno": str(last.seqno),
            "roothash": last.root_hash.hex(),
            "filehash": last.file_hash.hex(),
        }

        async with httpx.AsyncClient(timeout=10.0) as http:
            # GETs that should render an HTML page.
            for path in ("/last", "/", "/status", "/sendform"):
                r = await http.get(f"{url}{path}")
                assert r.status_code == 200, f"{path} -> {r.status_code}"
                assert "<!DOCTYPE html>" in r.text, f"{path} did not return HTML"

            # Endpoint-specific content checks.
            r = await http.get(f"{url}/sendform")
            assert "bag of cells" in r.text

            r = await http.get(f"{url}/status")
            assert "<table" in r.text

            # The masterchain head block must come back with its seqno embedded.
            r = await http.get(f"{url}/block", params=block_params)
            assert r.status_code == 200
            assert str(last.seqno) in r.text

            # /download for the same block: BoC binary, not HTML.
            r = await http.get(f"{url}/download", params=block_params)
            assert r.status_code == 200
            assert r.headers.get("content-type") == "application/octet-stream"
            assert len(r.content) > 0
            assert not r.content.startswith(b"<!DOCTYPE")

            # Dispatcher error paths.
            r = await http.get(f"{url}/no-such-command")
            assert r.status_code == 404

            r = await http.request("DELETE", f"{url}/last")
            assert r.status_code == 405

            # POST body cap is 64 KiB; anything bigger should be rejected.
            # The cap is asynchronous (PostBodyReader keeps reading until either the limit fires
            # or the body is fully buffered), so give httpx a wider read timeout than the default.
            oversized = "filedata=" + "A" * (64 * 1024 + 1)
            r = await http.post(
                f"{url}/send",
                content=oversized,
                headers={"Content-Type": "application/x-www-form-urlencoded"},
                timeout=60.0,
            )
            assert r.status_code == 413, f"oversized POST got {r.status_code}, expected 413"

        log.info("all blockchain explorer assertions passed")


if __name__ == "__main__":
    asyncio.run(asyncio.wait_for(main(), 5 * 60))
