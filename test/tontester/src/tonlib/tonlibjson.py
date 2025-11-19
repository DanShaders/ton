import json
import traceback
from typing import override

import random
import asyncio
import time
import functools
import logging
import typing
from enum import Enum, auto

from tontester.tl import tonlib_api
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
    def __init__(self, result: tonlib_api.Error):
        self.result: tonlib_api.Error = result
        super().__init__()

    @property
    def code(self):
        return self.result.code

    @override
    def __str__(self) -> str:
        return self.result.message


def parse_tonlib_error(result: dict[str, JSONSerializable]):
    if result.get('@type') == 'error':
        er = tonlib_api.Error.from_dict(result)
        return TonlibError(er)
    return None


class Status(Enum):
    NONE = auto()
    FINISHED = auto()
    CRASHED = auto()
    STUCK = auto()


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

        self._futures: dict[str, asyncio.Future[JSONSerializable]] = {}
        self._loop: asyncio.AbstractEventLoop = loop
        self._ls_index: int = ls_index
        self._state: Status = Status.NONE

        self.is_dead: bool = False

        # creating tasks
        self._read_results_task: asyncio.Task[None] = self._loop.create_task(self.read_results())

    def __del__(self):
        try:
            self._tonlib_json_client_destroy(self._client)
        except Exception as ee:
            logger.error(f"Exception in tonlibjson.__del__: {traceback.format_exc()}")
            raise RuntimeError(f'Error in tonlibjson.__del__: {ee}')

    def send(self, query: JSONSerializable):
        assert self._is_working, f"TonLib failed with state: {self._state}"
        q = json.dumps(query).encode('utf-8')
        self._tonlib_json_client_send(self._client, q)

    def receive(self, timeout: int = 10) -> JSONSerializable:
        result = self._tonlib_json_client_receive(self._client, timeout)
        if result is not None:
            result = typing.cast(JSONSerializable, json.loads(result.decode('utf-8')))
        return result

    async def execute(self, query: TLObject, timeout: int = 10) -> JSONSerializable:
        assert self._is_working, f"TonLib failed with state: {self._state}"

        extra_id = "%s:%s:%s" % (time.time() + timeout, self._ls_index, random.random())
        query_d = query.to_dict()
        query_d["@extra"] = extra_id

        future: asyncio.Future[JSONSerializable] = self._loop.create_future()
        self._futures[extra_id] = future

        _ = self._loop.run_in_executor(None, lambda: self.send(query_d))
        try:
            result = await asyncio.wait_for(future, timeout + 0.5)  # add extra time since in case timeout == self.tonlib_timeout, it's better to wait for tonlib to answer timeout error
        except asyncio.TimeoutError:
            if extra_id in self._futures:
                _ = self._futures.pop(extra_id)
            raise TonlibNoResponse()
        return result

    @property
    def _is_working(self):
        return self._state not in (Status.CRASHED, Status.STUCK, Status.FINISHED)

    async def aclose(self):
        try:
            self._state = Status.FINISHED
            await self._read_results_task
            for f in self._futures.values():
                if not f.done():
                    f.set_exception(TonlibNoResponse())
        except Exception as ee:
            logger.error(f"Exception in tonlibjson.close: {traceback.format_exc()}")
            raise RuntimeError(f'Error in tonlibjson.close: {ee}')

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
                    f: asyncio.Future[JSONSerializable | None] = self._loop.run_in_executor(None, receive_func)
                    result = await asyncio.wait_for(f, timeout=timeout + delta)
                except asyncio.TimeoutError:
                    logger.critical(f"Tonlib #{self._ls_index:03d} stuck (timeout error)")
                    self._state = Status.STUCK
                except:
                    logger.critical(f"Tonlib #{self._ls_index:03d} crashed: {traceback.format_exc()}")
                    self._state = Status.CRASHED

                if isinstance(result, dict) and ("@extra" in result) and (result["@extra"] in self._futures):
                    assert isinstance(result["@extra"], str)
                    extra_id: str = result["@extra"]
                    try:
                        if not self._futures[extra_id].done():
                            tonlib_error = parse_tonlib_error(result)
                            if tonlib_error is not None:
                                self._futures[extra_id].set_exception(tonlib_error)
                            else:
                                self._futures[extra_id].set_result(result)
                        _ = self._futures.pop(extra_id)
                    except Exception as e:
                        logger.error(f'Tonlib #{self._ls_index:03d} receiving result exception: {e}')
        except Exception as ee:
            logger.critical(f'Task read_results failed: {ee}')
