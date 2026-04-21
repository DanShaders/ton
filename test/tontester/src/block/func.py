import argparse
import hashlib
import re
import subprocess
import sys
import tempfile
from base64 import b64encode
from dataclasses import dataclass
from pathlib import Path
from typing import cast

import requests
from pytoniq_core import Cell

from block import generated

TESTNET_BASE = "https://test-explorer.toncenter.com"
MAINNET_BASE = "https://explorer.toncenter.com"

MC_WORKCHAIN = -1
MC_SHARD = "8000000000000000"

DEFAULT_SHOW_VSET = Path.home() / "code/ton/src/build/utils/show-validator-set"

BLOCK_ID_RE = re.compile(r"^\(\s*(-?\d+)\s*,\s*([0-9a-fA-F]+)\s*,\s*(\d+)\s*\)$")


@dataclass(frozen=True)
class BlockId:
    workchain: int
    shard: str
    seqno: int


def parse_block_id(s: str) -> BlockId:
    m = BLOCK_ID_RE.match(s.strip())
    if m is None:
        raise argparse.ArgumentTypeError(
            f"Expected canonical block form '(workchain,shard,seqno)', got: {s!r}"
        )
    return BlockId(
        workchain=int(m.group(1)),
        shard=m.group(2).lower(),
        seqno=int(m.group(3)),
    )


def find_download_url(base: str, html: str) -> str:
    m = re.search(r'href="(/download\?[^"]+)"', html)
    if m is None:
        raise RuntimeError("Could not find a /download link on the block page")
    return base + m.group(1).replace("&amp;", "&")


def fetch_block_boc(session: requests.Session, base: str, block: BlockId) -> bytes:
    search_url = (
        f"{base}/search?workchain={block.workchain}&shard={block.shard}"
        f"&seqno={block.seqno}&lt=&utime=&roothash=&filehash="
    )
    r = session.get(search_url, timeout=30)
    _ = r.raise_for_status()
    download_url = find_download_url(base, r.text)
    r = session.get(download_url, timeout=60)
    _ = r.raise_for_status()
    return r.content


def pubkey_short_id(pubkey: bytes) -> str:
    # 0xc6b41348 is the TL id prefix for pub.ed25519, hashed with the key to
    # produce the ADNL short-id.
    key = hashlib.sha256(b"\xc6\xb4\x13\x48" + pubkey).digest()
    return b64encode(key).decode()


@dataclass(frozen=True)
class BlockFacts:
    leader_pubkey_hash: str
    prev_key_block_seqno: int
    gen_catchain_seqno: int


def parse_block_facts(boc: bytes) -> BlockFacts:
    root = Cell.one_from_boc(boc)
    block = generated.Block.deserialize(root)
    info = block.info.ref
    pubkey = block.extra.ref.created_by.tobytes()
    return BlockFacts(
        leader_pubkey_hash=pubkey_short_id(pubkey),
        prev_key_block_seqno=info.prev_key_block_seqno,
        gen_catchain_seqno=info.gen_catchain_seqno,
    )


def run_show_validator_set(
    binary: Path,
    key_block_boc: bytes,
    shard: BlockId,
    cc_seqno: int,
) -> str:
    with tempfile.NamedTemporaryFile(suffix=".boc") as f:
        _ = f.write(key_block_boc)
        f.flush()
        result = subprocess.run(
            [
                str(binary),
                "-f",
                f.name,
                "-w",
                str(shard.workchain),
                "-s",
                shard.shard,
                "-c",
                str(cc_seqno),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
    return result.stdout


def find_adnl_for_pubkey(show_vset_output: str, pubkey_hash_b64: str) -> str:
    for line in show_vset_output.splitlines():
        parts = line.split()
        if len(parts) < 4:
            continue
        if parts[1] == pubkey_hash_b64:
            return parts[2]
    raise RuntimeError(f"Pubkey hash {pubkey_hash_b64} not found in validator set output")


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Download a TON block from the toncenter explorer, print the"
            " leader's pubkey short-id (base64) and the corresponding ADNL hash."
        )
    )
    _ = parser.add_argument(
        "block",
        type=parse_block_id,
        help="Canonical block id, e.g. '(0,8000000000000000,66544366)'",
    )
    _ = parser.add_argument(
        "--mainnet",
        action="store_true",
        help="Use explorer.toncenter.com (default: test-explorer.toncenter.com)",
    )
    _ = parser.add_argument(
        "--show-validator-set",
        default=str(DEFAULT_SHOW_VSET),
        help=f"Path to show-validator-set binary (default: {DEFAULT_SHOW_VSET})",
    )
    args = parser.parse_args()

    block_id = cast(BlockId, args.block)
    use_mainnet = cast(bool, args.mainnet)
    show_vset_bin = Path(cast(str, args.show_validator_set))
    base = MAINNET_BASE if use_mainnet else TESTNET_BASE

    if not show_vset_bin.is_file():
        print(f"show-validator-set not found at {show_vset_bin}", file=sys.stderr)
        return 2

    session = requests.Session()
    session.headers.update({"User-Agent": "ton-leader-extractor/1.0"})

    print(f"downloading block {block_id}...", file=sys.stderr)
    boc = fetch_block_boc(session, base, block_id)
    facts = parse_block_facts(boc)

    key_block_id = BlockId(
        workchain=MC_WORKCHAIN,
        shard=MC_SHARD,
        seqno=facts.prev_key_block_seqno,
    )
    print(
        f"downloading key block {key_block_id} (cc_seqno={facts.gen_catchain_seqno})...",
        file=sys.stderr,
    )
    key_boc = fetch_block_boc(session, base, key_block_id)

    vset_out = run_show_validator_set(show_vset_bin, key_boc, block_id, facts.gen_catchain_seqno)
    adnl_hash = find_adnl_for_pubkey(vset_out, facts.leader_pubkey_hash)

    print(f"pubkey_hash  {facts.leader_pubkey_hash}")
    print(f"adnl_hash    {adnl_hash}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
