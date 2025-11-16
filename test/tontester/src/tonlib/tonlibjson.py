import json
import traceback
from typing import override

import random
import asyncio
import time
import functools
import logging
import typing

from tl import TLObject, JSONSerializable

import ctypes

logger = logging.getLogger(__name__)


class TonlibException(Exception):
    pass


class TonlibNoResponse(TonlibException):
    @override
    def __str__(self):
        return 'tonlibjson did not respond'


class TonlibError(TonlibException):
    def __init__(self, result: dict[str, JSONSerializable]):
        self.result: dict[str, JSONSerializable] = result
        super().__init__()

    @property
    def code(self):
        return int(typing.cast(int, self.result.get('code', 0)))

    @override
    def __str__(self) -> str:
        return typing.cast(str, self.result.get('message', ''))


class LiteServerTimeout(TonlibError):
    pass


class BlockNotFound(TonlibError):
    pass


class BlockDeleted(TonlibError):
    pass


class ExternalMessageNotAccepted(TonlibError):
    pass


def parse_tonlib_error(result: dict[str, JSONSerializable]):
    if result.get('@type') == 'error':
        message = typing.cast(str, result['message'])
        if 'not in db' in message:
            return BlockNotFound(result)
        if "state already gc'd" in message:
            return BlockDeleted(result)
        if 'cannot apply external message to current state' in message:
            return ExternalMessageNotAccepted(result)
        if 'adnl query timeout' in message:
            return LiteServerTimeout(result)
        return TonlibError(result)
    return None


# class TonLib for single liteserver
class TonLib:
    def __init__(self, loop: asyncio.AbstractEventLoop, ls_index: int, cdll_path: str, verbosity_level: int = 0):
        tonlib = ctypes.CDLL(cdll_path)

        tonlib_client_set_verbosity_level = tonlib.tonlib_client_set_verbosity_level
        tonlib_client_set_verbosity_level.restype = None
        tonlib_client_set_verbosity_level.argtypes = [ctypes.c_int]

        try:
            tonlib_client_set_verbosity_level(verbosity_level)
        except Exception as ee:
            raise RuntimeError(f"Failed to set verbosity level: {ee}")

        tonlib_json_client_create = tonlib.tonlib_client_json_create
        tonlib_json_client_create.restype = ctypes.c_void_p
        tonlib_json_client_create.argtypes = []
        try:
            self._client: int = tonlib_json_client_create()
        except Exception as ee:
            raise RuntimeError(f"Failed to create tonlibjson client: {ee}")
        tonlib_json_client_receive = tonlib.tonlib_client_json_receive
        tonlib_json_client_receive.restype = ctypes.c_char_p
        tonlib_json_client_receive.argtypes = [ctypes.c_void_p, ctypes.c_double]
        self._tonlib_json_client_receive: typing.Callable[[int, int], bytes | None] = tonlib_json_client_receive

        tonlib_json_client_send = tonlib.tonlib_client_json_send
        tonlib_json_client_send.restype = None
        tonlib_json_client_send.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        self._tonlib_json_client_send: typing.Callable[[int, bytes], None] = tonlib_json_client_send

        tonlib_json_client_destroy = tonlib.tonlib_client_json_destroy
        tonlib_json_client_destroy.restype = None
        tonlib_json_client_destroy.argtypes = [ctypes.c_void_p]
        self._tonlib_json_client_destroy: typing.Callable[[int], None] = tonlib_json_client_destroy

        self.futures: dict[str, asyncio.Future[JSONSerializable]] = {}
        self.loop: asyncio.AbstractEventLoop = loop
        self.ls_index: int = ls_index
        self._state: str | None = None  # None, "finished", "crashed", "stuck"

        self.is_dead: bool = False

        # creating tasks
        self.read_results_task: asyncio.Task[None] = self.loop.create_task(self.read_results())
        self.del_expired_futures_task: asyncio.Task[None] = self.loop.create_task(self.del_expired_futures_loop())

    def __del__(self):
        try:
            self._tonlib_json_client_destroy(self._client)
        except Exception as ee:
            logger.error(f"Exception in tonlibjson.__del__: {traceback.format_exc()}")
            raise RuntimeError(f'Error in tonlibjson.__del__: {ee}')

    def send(self, query: JSONSerializable):
        if not self._is_working:
            raise RuntimeError(f"TonLib failed with state: {self._state}")

        q = json.dumps(query).encode('utf-8')
        try:
            self._tonlib_json_client_send(self._client, q)
        except Exception as ee:
            logger.error(f"Exception in tonlibjson.send: {traceback.format_exc()}")
            raise RuntimeError(f'Error in tonlibjson.send: {ee}')

    def receive(self, timeout: int = 10) -> JSONSerializable | None:
        try:
            result = self._tonlib_json_client_receive(self._client, timeout)
        except Exception as ee:
            logger.error(f"Exception in tonlibjson.receive: {traceback.format_exc()}")
            raise RuntimeError(f'Error in tonlibjson.receive: {ee}')
        if result is not None:
            result = typing.cast(JSONSerializable, json.loads(result.decode('utf-8')))
        return result

    def execute(self, query: TLObject, timeout: int = 10) -> asyncio.Future[JSONSerializable]:
        if not self._is_working:
            raise RuntimeError(f"TonLib failed with state: {self._state}")

        extra_id = "%s:%s:%s" % (time.time() + timeout, self.ls_index, random.random())
        query_d = query.to_dict()
        query_d["@extra"] = extra_id

        future_result = self.loop.create_future()
        self.futures[extra_id] = future_result

        _ = self.loop.run_in_executor(None, lambda: self.send(query_d))
        return future_result

    @property
    def _is_working(self):
        return self._state not in ('crashed', 'stuck', 'finished')

    async def close(self):
        try:
            self._state = 'finished'
            await self.read_results_task
            await self.del_expired_futures_task
        except Exception as ee:
            logger.error(f"Exception in tonlibjson.close: {traceback.format_exc()}")
            raise RuntimeError(f'Error in tonlibjson.close: {ee}')

    def cancel_futures(self, cancel_all: bool = False):
        now = time.time()
        to_del: list[str] = []
        for i in self.futures:
            if float(i.split(":")[0]) <= now or cancel_all:
                to_del.append(i)
        logger.debug(f'Pruning {len(to_del)} tasks')
        for i in to_del:
            self.futures[i].set_exception(TonlibNoResponse())
            _ = self.futures.pop(i)

    # tasks
    async def read_results(self):
        timeout = 1
        delta = 5
        receive_func = functools.partial(self.receive, timeout)
        try:
            while self._is_working:
                # return reading result
                result: JSONSerializable | None = None
                try:
                    f: asyncio.Future[JSONSerializable | None] = self.loop.run_in_executor(None, receive_func)
                    result = await asyncio.wait_for(f, timeout=timeout + delta)
                except asyncio.TimeoutError:
                    logger.critical(f"Tonlib #{self.ls_index:03d} stuck (timeout error)")
                    self._state = "stuck"
                except:
                    logger.critical(f"Tonlib #{self.ls_index:03d} crashed: {traceback.format_exc()}")
                    self._state = "crashed"

                if isinstance(result, dict) and ("@extra" in result) and (result["@extra"] in self.futures):
                    extra_id: str = typing.cast(str, result["@extra"])
                    try:
                        if not self.futures[extra_id].done():
                            tonlib_error = parse_tonlib_error(result)
                            if tonlib_error is not None:
                                self.futures[extra_id].set_exception(tonlib_error)
                            else:
                                self.futures[extra_id].set_result(result)
                        _ = self.futures.pop(extra_id)
                    except Exception as e:
                        logger.error(f'Tonlib #{self.ls_index:03d} receiving result exception: {e}')
        except Exception as ee:
            logger.critical(f'Task read_results failed: {ee}')

    async def del_expired_futures_loop(self):
        try:
            while self._is_working:
                self.cancel_futures()
                await asyncio.sleep(1)

            self.cancel_futures(cancel_all=True)
        except Exception as ee:
            logger.critical(f'Task del_expired_futures_loop failed: {ee}')
