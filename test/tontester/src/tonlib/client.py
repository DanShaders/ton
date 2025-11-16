import asyncio
import logging
import os

import tl
from .tonlibjson import TonLib

from tontester.tl import tonlib_api, ton_api
from .utils import is_hex

logger = logging.getLogger(__name__)


class TonlibClient:
    def __init__(self,
                 ls_index: int,
                 config: ton_api.Liteclient_config_global,
                 keystore: str,
                 cdll_path: str,
                 loop: asyncio.AbstractEventLoop | None = None,
                 verbosity_level: int = 0,
                 tonlib_timeout: int = 10
                 ):
        if not os.access(keystore, os.F_OK):
            raise FileNotFoundError(f'Keystore directory {keystore} does not exist')
        if not os.access(keystore, os.W_OK | os.R_OK | os.X_OK):
            raise PermissionError(f'Keystore directory {keystore} does not have required permissions (rwx)')

        self.ls_index: int = ls_index
        self.config: ton_api.Liteclient_config_global = config
        self.keystore: str = keystore
        self.cdll_path: str = cdll_path
        self.loop: asyncio.AbstractEventLoop | None = loop
        self.verbosity_level: int = verbosity_level
        self.tonlib_wrapper: TonLib | None = None
        self.tonlib_timeout: int = tonlib_timeout

    @property
    def local_config(self) -> ton_api.Liteclient_config_global:
        local = ton_api.Liteclient_config_global.from_json(self.config.to_json())
        local.liteservers = [local.liteservers[self.ls_index]]
        return local

    async def init(self) -> None:
        if self.tonlib_wrapper:
            logger.warning(f'init is already done')
            return
        event_loop = self.loop or asyncio.get_running_loop()
        self.tonlib_wrapper = TonLib(event_loop, self.ls_index, self.cdll_path, self.verbosity_level)
        keystore = tonlib_api.KeyStoreTypeDirectory(directory=self.keystore)

        config = tonlib_api.Config(
            config=self.local_config.to_json(),
            blockchain_name='',
            use_callbacks_for_network=False,
            ignore_cache=False
        )
        options = tonlib_api.Options(config=config, keystore_type=keystore)
        request = tonlib_api.InitRequest(options=options)

        _ = await self.tonlib_wrapper.execute(request)

        logger.info(F"TonLib #{self.ls_index:03d} inited successfully")

    async def close(self):
        if self.tonlib_wrapper is not None:
            await self.tonlib_wrapper.close()
            self.tonlib_wrapper = None

    async def __aenter__(self):
        await self.init()
        return self

    async def __aexit__(self):
        await self.close()

    def __await__(self):
        return self.init().__await__()

    async def make_request[T: tl.TLObject](self, request: tl.TLRequest, result: type[T]) -> T:
        if self.tonlib_wrapper is None:
            raise Exception('TonlibClient is not initialized. Call init() before making requests.')
        return result.from_dict(await self.tonlib_wrapper.execute(request, timeout=self.tonlib_timeout))

    async def sync_tonlib(self) -> tonlib_api.Ton_blockIdExt:
        return await self.make_request(tonlib_api.SyncRequest(), tonlib_api.Ton_blockIdExt)

    async def get_masterchain_info(self) -> tonlib_api.Blocks_masterchainInfo:
        return await self.make_request(tonlib_api.Blocks_getMasterchainInfoRequest(), tonlib_api.Blocks_masterchainInfo)

    async def raw_send_message(self, serialized_boc: bytes):
        request = tonlib_api.Raw_sendMessageRequest(body=serialized_boc)
        return await self.make_request(request, tonlib_api.Ok)

    async def get_libraries(self, library_list: list[bytes]) -> tonlib_api.Smc_libraryResult:
        request = tonlib_api.Smc_getLibrariesRequest(library_list)
        return await self.make_request(request, tonlib_api.Smc_libraryResult)

    async def raw_get_transactions(self, account_address: str, from_transaction_lt: int, from_transaction_hash: str):
        assert len(account_address) == 48, 'account address must be serialized'
        assert is_hex(from_transaction_hash), 'from_transaction_hash must be hex'
        request = tonlib_api.Raw_getTransactionsRequest(
            account_address=tonlib_api.AccountAddress(account_address),
            from_transaction_id=tonlib_api.Internal_transactionId(
                lt=from_transaction_lt,
                hash=bytes.fromhex(from_transaction_hash)
            )
        )
        return await self.make_request(request, tonlib_api.Raw_transactions)

    async def raw_get_account_state(self, account_address: str):
        assert len(account_address) == 48, 'account address must be serialized'
        request = tonlib_api.Raw_getAccountStateRequest(
            account_address=tonlib_api.AccountAddress(account_address)
        )
        return await self.make_request(request, tonlib_api.Raw_fullAccountState)
