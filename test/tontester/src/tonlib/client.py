import asyncio
import logging
import os
import traceback


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
        self._config: ton_api.Liteclient_config_global = config
        self._keystore: str = keystore
        self._cdll_path: str = cdll_path
        self._loop: asyncio.AbstractEventLoop | None = loop
        self._verbosity_level: int = verbosity_level
        self._tonlib_wrapper: TonLib | None = None
        self.tonlib_timeout: int = tonlib_timeout

    @property
    def local_config(self) -> ton_api.Liteclient_config_global:
        local = ton_api.Liteclient_config_global.from_json(self._config.to_json())
        local.liteservers = [local.liteservers[self.ls_index]]
        return local

    async def init(self) -> None:
        if self._tonlib_wrapper:
            logger.warning(f'init is already done')
            return
        event_loop = self._loop or asyncio.get_running_loop()
        self._tonlib_wrapper = TonLib(event_loop, self.ls_index, self._cdll_path, self._verbosity_level)
        keystore = tonlib_api.KeyStoreTypeDirectory(directory=self._keystore)

        config = tonlib_api.Config(
            config=self.local_config.to_json(),
            blockchain_name='',
            use_callbacks_for_network=False,
            ignore_cache=False
        )
        options = tonlib_api.Options(config=config, keystore_type=keystore)
        request = tonlib_api.InitRequest(options=options)

        _ = await self._tonlib_wrapper.execute(request)

        logger.info(F"TonLib #{self.ls_index:03d} inited successfully")

    async def aclose(self):
        if self._tonlib_wrapper is not None:
            await self._tonlib_wrapper.aclose()
            self._tonlib_wrapper = None

    async def __aenter__(self):
        await self.init()
        return self

    async def __aexit__(
            self,
            exc_type: type[BaseException] | None,
            exc_val: BaseException | None,
            exc_tb: traceback.TracebackException | None
    ):
        await self.aclose()

    def __await__(self):
        return self.init().__await__()

    async def sync_tonlib(self) -> tonlib_api.Ton_blockIdExt:
        assert self._tonlib_wrapper is not None
        request = tonlib_api.SyncRequest()
        return request.parse_result(await self._tonlib_wrapper.execute(request, timeout=self.tonlib_timeout))

    async def get_masterchain_info(self) -> tonlib_api.Blocks_masterchainInfo:
        assert self._tonlib_wrapper is not None
        request = tonlib_api.Blocks_getMasterchainInfoRequest()
        return request.parse_result(await self._tonlib_wrapper.execute(request, timeout=self.tonlib_timeout))

    async def raw_send_message(self, serialized_boc: bytes) -> tonlib_api.TypeOk:
        assert self._tonlib_wrapper is not None
        request = tonlib_api.Raw_sendMessageRequest(body=serialized_boc)
        return request.parse_result(await self._tonlib_wrapper.execute(request, timeout=self.tonlib_timeout))

    async def get_libraries(self, library_list: list[bytes]) -> tonlib_api.Smc_libraryResult:
        assert self._tonlib_wrapper is not None
        request = tonlib_api.Smc_getLibrariesRequest(library_list)
        return request.parse_result(await self._tonlib_wrapper.execute(request, timeout=self.tonlib_timeout))

    async def raw_get_transactions(self, account_address: str, from_transaction_lt: int, from_transaction_hash: str) -> tonlib_api.Raw_transactions:
        assert self._tonlib_wrapper is not None
        assert len(account_address) == 48, 'account address must be serialized'
        assert is_hex(from_transaction_hash), 'from_transaction_hash must be hex'
        request = tonlib_api.Raw_getTransactionsRequest(
            account_address=tonlib_api.AccountAddress(account_address),
            from_transaction_id=tonlib_api.Internal_transactionId(
                lt=from_transaction_lt,
                hash=bytes.fromhex(from_transaction_hash)
            )
        )
        return request.parse_result(await self._tonlib_wrapper.execute(request, timeout=self.tonlib_timeout))

    async def raw_get_account_state(self, account_address: str) -> tonlib_api.Raw_fullAccountState:
        assert self._tonlib_wrapper is not None
        assert len(account_address) == 48, 'account address must be serialized'
        request = tonlib_api.Raw_getAccountStateRequest(
            account_address=tonlib_api.AccountAddress(account_address)
        )
        return request.parse_result(await self._tonlib_wrapper.execute(request, timeout=self.tonlib_timeout))
