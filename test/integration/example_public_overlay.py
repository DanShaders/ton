import asyncio
import logging
import tempfile
from pathlib import Path

from tonapi import ton_api
from tonlib.event_loop import TonlibEventLoop
from tonlib.public_overlay import PublicOverlayClient
from tonlib.tonlib_cdll import TonlibCDLL
from tontester.install import Install
from tontester.key import Key

# TESTNET_CONFIG = ton_api.Config_global.from_json(
#     """
# {
#     "liteservers": [
#         {
#             "ip": 822907680,
#             "port": 27842,
#             "provided":"Swiss",
#             "id": {
#                 "@type": "pub.ed25519",
#                 "key": "sU7QavX2F964iI9oToP9gffQpCQIoOLppeqL/pdPvpM="
#             }
#         },
#         {
#             "ip": -1468571697,
#             "port": 27787,
#             "provided":"Swiss",
#             "id": {
#                 "@type": "pub.ed25519",
#                 "key": "Y/QVf6G5VDiKTZOKitbFVm067WsuocTN8Vg036A4zGk="
#             }
#         },
#         {
#             "ip": -1468575011,
#             "port": 51088,
#             "provided":"Swiss",
#             "id": {
#                 "@type": "pub.ed25519",
#                 "key": "Sy5ghr3EahQd/1rDayzZXt5+inlfF+7kLfkZDJcU/ek="
#             }
#         },
#         {
#             "ip": 1844203589,
#             "port": 49913,
#             "provided":"Neo",
#             "id": {
#                 "@type": "pub.ed25519",
#                 "key": "AxFZRHVD1qIO9Fyva52P4vC3tRvk8ac1KKOG0c6IVio="
#             }
#         },
#         {
#             "ip": 1844203537,
#             "port": 4330,
#             "provided":"Neo",
#             "id": {
#                 "@type": "pub.ed25519",
#                 "key": "IraRAKcsECFUwFRKXF23YeXAxrDMVWobbw8Hpb2m7Aw="
#             }
#         },
#         {
#             "ip": 1047529523,
#             "port": 36005,
#             "provided":"Neo",
#             "id": {
#                 "@type": "pub.ed25519",
#                 "key": "39EIFy/IaCjgJSu1WvHJjD/mNbGI0c+bM/hrkCjzfrw="
#             }
#         },
#         {
#             "ip": 1097633201,
#             "port": 17439,
#             "id": {
#                 "@type": "pub.ed25519",
#                 "key": "0MIADpLH4VQn+INHfm0FxGiuZZAA8JfTujRqQugkkA8="
#             }
#         },
#         {
#             "ip": 1091956407,
#             "port": 16351,
#             "id": {
#                 "@type": "pub.ed25519",
#                 "key": "Mf/JGvcWAvcrN3oheze8RF/ps6p7oL6ifrIzFmGQFQ8="
#             }
#         }
#     ],
#     "dht": {
#         "a": 3,
#         "k": 3,
#         "static_nodes": {
#             "nodes": [
#                 {
#                     "@type": "dht.node",
#                     "id": {
#                         "@type": "pub.ed25519",
#                         "key": "MKaJGWvZ8AOX/VWV2rJglcEJh07MKjgZiy5Mel6L2Xk="
#                     },
#                     "addr_list": {
#                         "@type": "adnl.addressList",
#                         "addrs": [
#                             {
#                                 "@type": "adnl.address.udp",
#                                 "ip": 1053990413,
#                                 "port": 7694
#                             }
#                         ],
#                         "version": 0,
#                         "reinit_date": 0,
#                         "priority": 0,
#                         "expire_at": 0
#                     },
#                     "version": -1,
#                     "signature": "GJiGFKV5sfOJuKNj13Bo7TfEk8A7NMyAruzj1nwWvfSlGSmnYUhUa9LmyHU7XyrlKcLmYC+MU0h5SctUkctsCA=="
#                 },
#                 {
#                     "@type": "dht.node",
#                     "id": {
#                         "@type": "pub.ed25519",
#                         "key": "+htkM588jJXXidOs64fuGj/jiZiCY/AG3EljcugfOs0="
#                     },
#                     "addr_list": {
#                         "@type": "adnl.addressList",
#                         "addrs": [
#                             {
#                                 "@type": "adnl.address.udp",
#                                 "ip": -2087062611,
#                                 "port": 17750
#                             }
#                         ],
#                         "version": 0,
#                         "reinit_date": 0,
#                         "priority": 0,
#                         "expire_at": 0
#                     },
#                     "version": -1,
#                     "signature": "K9zywmobUDVsYlXdJwTg1b9xtbSNueX6cizpI26xD71ntQbAURyd6TLXUzEezdYZYSzTvK0NJoL2VojqBgFBAw=="
#                 },
#                 {
#                     "@type": "dht.node",
#                     "id": {
#                         "@type": "pub.ed25519",
#                         "key": "LFnKVKTO+GYsOBrTH2xaVAGsOGEgSNGo0TRdDZmBeL4="
#                     },
#                     "addr_list": {
#                         "@type": "adnl.addressList",
#                         "addrs": [
#                             {
#                                 "@type": "adnl.address.udp",
#                                 "ip": -1874058059,
#                                 "port": 21533
#                             }
#                         ],
#                         "version": 0,
#                         "reinit_date": 0,
#                         "priority": 0,
#                         "expire_at": 0
#                     },
#                     "version": -1,
#                     "signature": "UqP2Mvgtu4f/70NdsFekQ3jarIpcBQDamGn2jYhocw4yWTfHaxnP6m4FMh+qSe07q7e0DbkBjwKnGxu+YfIBCQ=="
#                 },
#                 {
#                     "@type": "dht.node",
#                     "id": {
#                         "@type": "pub.ed25519",
#                         "key": "pdeuI0a/RhBqgkQxn+6+J2EMdcpTi0WhhaR8Q3/+u+4="
#                     },
#                     "addr_list": {
#                         "@type": "adnl.addressList",
#                         "addrs": [
#                             {
#                                 "@type": "adnl.address.udp",
#                                 "ip": -2087065405,
#                                 "port": 13654
#                             }
#                         ],
#                         "version": 0,
#                         "reinit_date": 0,
#                         "priority": 0,
#                         "expire_at": 0
#                     },
#                     "version": -1,
#                     "signature": "Ph5LdoX9NyBFDqF5YLS2jNdb/omco3pgmCt0iI99tS95Mcic+WFUsH9nt0zyzy1dd8D75vR952go2HMHpKYaBA=="
#                 },
#                 {
#                     "@type": "dht.node",
#                     "id": {
#                         "@type": "pub.ed25519",
#                         "key": "GIpxz5qHuKu/kNZl7Zc/w8LbxYamzCBKpbT9+FAnIJU="
#                     },
#                     "addr_list": {
#                         "@type": "adnl.addressList",
#                         "addrs": [
#                             {
#                                 "@type": "adnl.address.udp",
#                                 "ip": 1495755553,
#                                 "port": 2810
#                             }
#                         ],
#                         "version": 0,
#                         "reinit_date": 0,
#                         "priority": 0,
#                         "expire_at": 0
#                     },
#                     "version": -1,
#                     "signature": "RvSTi5eaZ7wH1ap0EfffnT66CccrJA2JZtwb8tYOlxhaxtlRqwPOr0Om1pWBmbItwwAGScCxoYZFtdGbEImbDw=="
#                 }
#             ],
#             "@type": "dht.nodes"
#         },
#         "@type": "dht.config.global"
#     },
#     "@type": "config.global",
#     "validator": {
#         "zero_state": {
#             "file_hash": "Z+IKwYS54DmmJmesw/nAD5DzWadnOCMzee+kdgSYDOg=",
#             "seqno": 0,
#             "root_hash": "gj+B8wb/AmlPk1z1AhVI484rhrUpgSr2oSFIh56VoSg=",
#             "workchain": -1,
#             "shard": -9223372036854775808
#         },
#         "@type": "validator.config.global",
#         "init_block": {
#             "workchain": -1,
#             "shard": -9223372036854775808,
#             "seqno": 17908219,
#             "root_hash": "y6qWqhCnLgzWHjUFmXysaiOljuK5xVoCRMLzUwGInVM=",
#             "file_hash": "Y/GziXxwuYte0AM4WT7tTWsCx+6rcfLpGmRaEQwhUKI="
#         },
#         "hardforks": [
#             {
#                 "file_hash": "jF3RTD+OyOoP+OI9oIjdV6M8EaOh9E+8+c3m5JkPYdg=",
#                 "seqno": 5141579,
#                 "root_hash": "6JSqIYIkW7y8IorxfbQBoXiuY3kXjcoYgQOxTJpjXXA=",
#                 "workchain": -1,
#                 "shard": -9223372036854775808
#             },
#             {
#                 "file_hash": "WrNoMrn5UIVPDV/ug/VPjYatvde8TPvz5v1VYHCLPh8=",
#                 "seqno": 5172980,
#                 "root_hash": "054VCNNtUEwYGoRe1zjH+9b1q21/MeM+3fOo76Vcjes=",
#                 "workchain": -1,
#                 "shard": -9223372036854775808
#             },
#             {
#                 "file_hash": "xRaxgUwgTXYFb16YnR+Q+VVsczLl6jmYwvzhQ/ncrh4=",
#                 "seqno": 5176527,
#                 "root_hash": "SoPLqMe9Dz26YJPOGDOHApTSe5i0kXFtRmRh/zPMGuI=",
#                 "workchain": -1,
#                 "shard": -9223372036854775808
#             }
#         ]
#     }
# }
# """
# )


async def main() -> None:
    logging.basicConfig(
        level=logging.INFO,
        format="[%(levelname)s][%(asctime)s][%(name)s] %(message)s",
        datefmt="%Y-%m-%d %H-%M-%S",
    )

    repo_root = Path(__file__).resolve().parents[2]
    install = Install(repo_root / "build", repo_root)

    TESTNET_CONFIG = ton_api.Config_global.from_json(
        Path(repo_root / "test/integration/.network/node1/config.global.json").read_text()
    )

    assert TESTNET_CONFIG.validator is not None
    zerostate_block_id = TESTNET_CONFIG.validator.zero_state
    assert zerostate_block_id is not None

    client_key = Key.new(install)

    tonlib_cdll = TonlibCDLL(install.tonlibjson)
    tonlib_cdll.client_set_verbosity_level(3)

    with tempfile.TemporaryDirectory() as db_root:
        async with TonlibEventLoop(tonlib_cdll) as event_loop:
            config = ton_api.PublicOverlayClient_config(
                db_root=db_root,
                bind_address="127.0.0.1:7998",
                client_private_key=client_key.private_key,
                dht_config=TESTNET_CONFIG.dht,
                zerostate=ton_api.TonNode_zeroStateIdExt(
                    workchain=zerostate_block_id.workchain,
                    root_hash=zerostate_block_id.root_hash,
                    file_hash=zerostate_block_id.file_hash,
                ),
                shard=-(1 << 63),
            )

            async with PublicOverlayClient(tonlib_cdll, event_loop, config):
                logging.info("Public overlay client connected successfully")
                logging.info("Listening for broadcasts... Press Ctrl+C to stop")
                await asyncio.sleep(3600)


if __name__ == "__main__":
    asyncio.run(main())
