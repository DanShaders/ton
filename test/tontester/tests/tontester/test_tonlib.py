import base64
import json
import os
from pathlib import Path
from typing import cast

import pytest

from tl import JSONSerializable
from tontester.conf import TONLIBJSON_BIN_PATH
from tontester.tl import ton_api
from tonlib import TonlibClient, TonlibNoResponse, TonlibError

config: dict[str, JSONSerializable] = {
    "@type": "liteclient.config.global",
    "dht": {
        "@type": "dht.config.global",
        "k": 6,
        "a": 3,
        "static_nodes": {
            "@type": "dht.nodes",
            "nodes": []
        }
    },
    "liteservers": [
        {
            "ip": 123,
            "port": 123,
            "id": {
                "@type": "pub.ed25519",
                "key": "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="
            }
        }
    ],
    "validator": {
        "@type": "validator.config.global",
        "zero_state": {
            "workchain": -1,
            "shard": -9223372036854775808,
            "seqno": 0,
            "root_hash": "F6OpKZKqvqeFp6CQmFomXNMfMj2EnaUSOXN+Mh+wVWk=",
            "file_hash": "XplPz01CXAps5qeSWUtxcyBfdAo5zVb1N979KLSKD24="
        },
        "init_block": {
            "workchain": -1,
            "shard": -9223372036854775808,
            "seqno": 53986204,
            "root_hash": "uBltuZIIhio2c9EiCTI9cExzf2UhALUDXvUora+eIPg=",
            "file_hash": "JDx4DGuIs+BFo6qhLQnKZmBaYBlKTCeJrO0CgMzjqjQ="
        },
        "hardforks": [
            {
                "workchain": -1,
                "shard": -9223372036854775808,
                "seqno": 8536841,
                "root_hash": "08Kpc9XxrMKC6BF/FeNHPS3MEL1/Vi/fQU/C9ELUrkc=",
                "file_hash": "t/9VBPODF7Zdh4nsnA49dprO69nQNMqYL+zk5bCjV/8="
            }
        ]
    }
}


@pytest.fixture()
def tonlib_client(tmp_path: Path) -> TonlibClient:
    repo_root = Path(__file__).resolve().parents[4]
    os.makedirs(tmp_path / '.tonlib_keystore', exist_ok=True)
    return TonlibClient(
        verbosity_level=5,
        config=ton_api.Liteclient_config_global.from_dict(config),
        keystore=str(tmp_path / '.tonlib_keystore/'),
        ls_index=0,
        cdll_path=str(repo_root / "build" / TONLIBJSON_BIN_PATH)
    )


@pytest.mark.asyncio
async def test_client_init(tonlib_client: TonlibClient):
    await tonlib_client.init()
    assert tonlib_client._tonlib_wrapper is not None  # pyright: ignore[reportPrivateUsage]
    await tonlib_client.aclose()


@pytest.mark.asyncio
async def test_request(tonlib_client: TonlibClient, monkeypatch: pytest.MonkeyPatch):
    h = 'FsRgTb2HymFDIkV82C0aA0CPbdQKAVEzZBgVq5rjPNI='
    def mock_send(_, request_json: str) -> None:
        q: dict[str, JSONSerializable] = cast(dict[str, JSONSerializable], json.loads(request_json))
        assert q['@type'] == 'blocks.getMasterchainInfo'
        res = {"@type": "blocks.masterchainInfo", "@extra": q["@extra"], "last": {"@type": "ton.blockIdExt", "workchain": -1, "shard": -9223372036854775808, "seqno": 1, "root_hash": 'FsRgTb2HymFDIkV82C0aA0CPbdQKAVEzZBgVq5rjPNI=', "file_hash": "JKzTCHqi/c0or9o78mCwo1kigYXdagBQqQ4B2RLXFXY="}, "state_root_hash": "9DcJlDUeelZCBniBzWseg6KyjbjtkB8r6rX4x6BDT9o=", "init": {"@type": "ton.blockIdExt", "workchain": -1, "shard": 0, "seqno": 0, "root_hash": "4DWqijlo0wfJCuJUZeKkWOvENlS3DdL9z2DXkFQ1UtE=", "file_hash": "nv5sO4rQHhrT5PGLTs1f01AtWYuuI5tH41c87vU1zac="}}
        monkeypatch.setattr(tonlib_client._tonlib_wrapper, '_tonlib_json_client_receive', lambda *_: json.dumps(res).encode('utf-8'))  # pyright: ignore[reportUnknownArgumentType, reportUnknownLambdaType, reportPrivateUsage]

    async with tonlib_client:
        monkeypatch.setattr(tonlib_client._tonlib_wrapper, '_tonlib_json_client_send', mock_send)  # pyright: ignore[reportUnknownArgumentType, reportPrivateUsage]
        blk = await tonlib_client.get_masterchain_info()
        assert blk.last is not None
        assert blk.last.workchain == -1
        assert blk.last.seqno == 1
        assert blk.last.root_hash == base64.b64decode(h)


@pytest.mark.asyncio
async def test_timeout(tonlib_client: TonlibClient, monkeypatch: pytest.MonkeyPatch):
    async with tonlib_client:
        monkeypatch.setattr(tonlib_client, 'tonlib_timeout', 1)
        monkeypatch.setattr(tonlib_client._tonlib_wrapper, '_tonlib_json_client_send', lambda *_: None)  # pyright: ignore[reportUnknownArgumentType, reportPrivateUsage, reportUnknownLambdaType]
        with pytest.raises(TonlibNoResponse):
            _ = await tonlib_client.get_masterchain_info()
        assert tonlib_client._tonlib_wrapper is not None  # pyright: ignore[reportPrivateUsage]
        assert not tonlib_client._tonlib_wrapper._futures  # pyright: ignore[reportPrivateUsage]


@pytest.mark.asyncio
async def test_error_response(tonlib_client: TonlibClient, monkeypatch: pytest.MonkeyPatch):
    async with tonlib_client:
        def mock_send(_, request_json: str) -> None:
            q: dict[str, JSONSerializable] = cast(dict[str, JSONSerializable], json.loads(request_json))
            assert q['@type'] == 'blocks.getMasterchainInfo'
            res = {"@type": "error", "@extra": q["@extra"], "code": 504, "message": "Timeout"}
            monkeypatch.setattr(tonlib_client._tonlib_wrapper, '_tonlib_json_client_receive', lambda *_: json.dumps(res).encode('utf-8'))  # pyright: ignore[reportUnknownArgumentType, reportUnknownLambdaType, reportPrivateUsage]

        monkeypatch.setattr(tonlib_client._tonlib_wrapper, '_tonlib_json_client_send', mock_send)  # pyright: ignore[reportUnknownArgumentType, reportPrivateUsage]
        with pytest.raises(TonlibError) as e:
            _ = await tonlib_client.get_masterchain_info()
            assert e.value.code == 504
            assert str(e.value) == 'Timeout'
