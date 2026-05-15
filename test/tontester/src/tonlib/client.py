import traceback
from collections.abc import Mapping
from dataclasses import dataclass
from typing import Callable, Protocol, cast, final

from pytoniq_core import Account, Address, Cell, MessageAny, Slice, deserialize_shard_hashes
from pytoniq_core.tlb.block import BlockInfo
from tonapi import lite_api, ton_api

from .event_loop import TonlibEventLoop
from .lite_client import LiteClient
from .tonlib_cdll import TonlibCDLL


class _ExtBlkRefView(Protocol):
    seqno: int
    root_hash: bytes
    file_hash: bytes


class _PrevBlkInfoView(Protocol):
    type_: str
    prev: _ExtBlkRefView


class _ShardDescrView(Protocol):
    seq_no: int
    root_hash: bytes
    file_hash: bytes
    next_validator_shard: int


class _BinTreeView(Protocol):
    list: list[_ShardDescrView | None]


@dataclass(frozen=True)
class AccountState:
    block_id: lite_api.TonNode_blockIdExt
    balance: int
    data: Cell | None


@dataclass(frozen=True)
class BlockHeader:
    id: lite_api.TonNode_blockIdExt
    gen_utime: int
    start_lt: int
    end_lt: int
    prev_blocks: list[lite_api.TonNode_blockIdExt]


@dataclass(frozen=True)
class Shards:
    id: lite_api.TonNode_blockIdExt
    shards: list[lite_api.TonNode_blockIdExt]


def _signed_int(value: int, bits: int) -> int:
    """Reinterpret the low `bits` of `value` as a signed integer."""
    mask = (1 << bits) - 1
    sign_bit = 1 << (bits - 1)
    return ((value & mask) ^ sign_bit) - sign_bit


def _lite_account_id(address: Address) -> lite_api.LiteServer_accountId:
    return lite_api.LiteServer_accountId(workchain=address.wc, id=address.hash_part)


def _parse_account_state(state_boc: bytes) -> tuple[int, Cell | None]:
    """Returns (balance_nanotons, data_cell). data is None for uninitialized accounts."""
    if not state_boc:
        return 0, None
    account = Account.deserialize(Slice.one_from_boc(state_boc))
    if account is None:
        return 0, None
    storage = account.storage
    balance: int = storage.balance.grams
    state = storage.state
    # AccountState can be: account_uninit, account_active(state_init), account_frozen.
    state_init = getattr(state, "state_init", None)
    data = cast(Cell | None, state_init.data) if state_init is not None else None
    return balance, data


def _prev_blocks_from_info(
    block_id: lite_api.TonNode_blockIdExt,
    info: BlockInfo,
) -> list[lite_api.TonNode_blockIdExt]:
    """Reconstruct prev blockIdExt list from a parsed BlockInfo."""
    prev_ref = cast(_PrevBlkInfoView, cast(object, info.prev_ref))
    if prev_ref.type_ == "prev_blks_info":
        # After merge: two prev blocks with child shards. Tests we support do not
        # currently exercise merges; surface a clear error if encountered.
        raise NotImplementedError("prev_blocks reconstruction for after_merge blocks")
    ref = prev_ref.prev
    return [
        lite_api.TonNode_blockIdExt(
            workchain=block_id.workchain,
            shard=block_id.shard,
            seqno=ref.seqno,
            root_hash=ref.root_hash,
            file_hash=ref.file_hash,
        )
    ]


@final
class TonlibStateReader:
    def __init__(self, client: "TonlibClient"):
        self._client: TonlibClient = client
        self._cache: dict[tuple[int, int, int, Address], Cell] = {}

    async def fetch[T](self, address: Address, parser: Callable[[Cell], T]) -> T:
        state = await self._client.raw_get_account_state(address)
        block_id = state.block_id
        key = (block_id.workchain, block_id.shard, block_id.seqno, address)
        if key not in self._cache:
            assert state.data is not None, f"account {address} has no data"
            self._cache[key] = state.data
        return parser(self._cache[key])


@final
class TonlibClient:
    def __init__(
        self,
        config: ton_api.LiteClient_config,
        tonlib: TonlibCDLL,
        event_loop: TonlibEventLoop,
    ):
        self._lite_client: LiteClient = LiteClient(tonlib, event_loop, config)

    async def aclose(self):
        self._lite_client.close()

    async def __aenter__(self):
        return self

    async def __aexit__(
        self,
        exc_type: type[BaseException] | None,
        exc_val: BaseException | None,
        exc_tb: traceback.TracebackException | None,
    ):
        await self.aclose()

    async def get_masterchain_info(self) -> lite_api.LiteServer_masterchainInfo:
        request = lite_api.LiteServer_getMasterchainInfoRequest()
        return request.parse_result(await self._lite_client.request(request))

    async def lookup_block(
        self,
        workchain: int,
        shard: int,
        seqno: int | None = None,
        lt: int | None = None,
        utime: int | None = None,
    ) -> lite_api.TonNode_blockIdExt:
        assert seqno is not None or lt is not None or utime is not None
        mode = 0
        if seqno is not None:
            mode |= 1
        if lt is not None:
            mode |= 2
        if utime is not None:
            mode |= 4
        request = lite_api.LiteServer_lookupBlockRequest(
            mode=mode,
            id=lite_api.TonNode_blockId(workchain=workchain, shard=shard, seqno=seqno or 0),
            lt=lt or 0,
            utime=utime or 0,
        )
        header = request.parse_result(await self._lite_client.request(request))
        assert header.id is not None
        return header.id

    async def raw_get_account_state(self, address: Address) -> AccountState:
        mc_info = await self.get_masterchain_info()
        assert mc_info.last is not None
        return await self._account_state_at(mc_info.last, address)

    async def _account_state_at(
        self,
        block_id: lite_api.TonNode_blockIdExt,
        address: Address,
    ) -> AccountState:
        request = lite_api.LiteServer_getAccountStateRequest(
            id=block_id,
            account=_lite_account_id(address),
        )
        result = request.parse_result(await self._lite_client.request(request))
        balance, data = _parse_account_state(result.state)
        # `result.id` is the masterchain block the response is anchored at; tonlib
        # used to expose the shardblk-based id as block_id, but for the cache key
        # we want the shard block that actually contains the account.
        anchor = result.shardblk if result.shardblk is not None else result.id
        assert anchor is not None
        return AccountState(block_id=anchor, balance=balance, data=data)

    @property
    def latest_state_reader(self) -> TonlibStateReader:
        return TonlibStateReader(self)

    async def send_external(self, message: MessageAny) -> None:
        _ = await self.raw_send_message(message.serialize().to_boc())

    async def raw_send_message(self, serialized_boc: bytes) -> lite_api.LiteServer_sendMsgStatus:
        request = lite_api.LiteServer_sendMessageRequest(body=serialized_boc)
        return request.parse_result(await self._lite_client.request(request))

    async def get_shards(self, block_id: lite_api.TonNode_blockIdExt) -> Shards:
        request = lite_api.LiteServer_getAllShardsInfoRequest(id=block_id)
        result = request.parse_result(await self._lite_client.request(request))
        shard_hashes = cast(
            Mapping[int, _BinTreeView] | None,
            deserialize_shard_hashes(Slice.one_from_boc(result.data)),
        )
        shards: list[lite_api.TonNode_blockIdExt] = []
        if shard_hashes:
            for raw_wc, bin_tree in shard_hashes.items():
                wc = _signed_int(raw_wc, 32)
                for descr in bin_tree.list:
                    if descr is None:
                        continue
                    shards.append(
                        lite_api.TonNode_blockIdExt(
                            workchain=wc,
                            shard=_signed_int(descr.next_validator_shard, 64),
                            seqno=descr.seq_no,
                            root_hash=descr.root_hash,
                            file_hash=descr.file_hash,
                        )
                    )
        assert result.id is not None
        return Shards(id=result.id, shards=shards)

    async def get_block_header(self, block_id: lite_api.TonNode_blockIdExt) -> BlockHeader:
        request = lite_api.LiteServer_getBlockHeaderRequest(id=block_id, mode=0)
        result = request.parse_result(await self._lite_client.request(request))
        # header_proof is a MerkleProof cell whose [0] is the proven Block cell.
        # The Block cell's [0] is the BlockInfo cell (kept un-pruned by the server).
        proof_root = Cell.one_from_boc(result.header_proof)
        block_info_cell = proof_root[0][0]
        info = BlockInfo.deserialize(block_info_cell.begin_parse())
        assert info is not None
        return BlockHeader(
            id=block_id,
            gen_utime=info.gen_utime,
            start_lt=info.start_lt,
            end_lt=info.end_lt,
            prev_blocks=_prev_blocks_from_info(block_id, info),
        )

    async def get_block_transactions(
        self,
        block_id: lite_api.TonNode_blockIdExt,
    ) -> list[lite_api.LiteServer_transactionId]:
        result: list[lite_api.LiteServer_transactionId] = []
        after: lite_api.LiteServer_transactionId3 | None = None
        while True:
            mode = 7  # bit0=want_account, bit1=want_lt, bit2=want_hash
            if after is not None:
                mode |= 1 << 7
            request = lite_api.LiteServer_listBlockTransactionsRequest(
                id=block_id,
                mode=mode,
                count=256,
                after=after,
                reverse_order=True,
                want_proof=True,
            )
            batch = request.parse_result(await self._lite_client.request(request))
            result.extend(batch.ids)
            if not batch.incomplete:
                break
            last = batch.ids[-1]
            after = lite_api.LiteServer_transactionId3(account=last.account, lt=last.lt)
        return result
