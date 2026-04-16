"""Extract compiled contract BOC hex from Fift and patch into contract wrapper files."""

import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "src"))

# Import Install directly to avoid loading contract modules with PLACEHOLDER BOCs
from tontester.install import Install  # noqa: E402

CONTRACT_DIR = Path(__file__).parent / "src" / "contract"
ZEROSTATE_FILE = Path(__file__).parent / "src" / "tontester" / "zerostate.py"


def main():
    repo_root = Path(__file__).resolve().parents[2]
    build_dir = repo_root / "build"
    install = Install(build_dir, repo_root)

    with tempfile.TemporaryDirectory() as td:
        state_dir = Path(td)

        script = r"""
"TonUtil.fif" include
"Asm.fif" include
"Lists.fif" include

// Elector code
"auto/elector-code.fif" include
2 boc+>B Bx. cr

// Config code
"auto/config-code.fif" include
2 boc+>B Bx. cr

// SMC#3 (tick-tock test contract) code
PROGRAM{
  recv_internal x{} PROC
  run_ticktock PROC:<{
    c4 PUSHCTR CTOS 32 LDU 256 LDU ENDS
    NEWC ROT INC 32 STUR OVER 256 STUR ENDC
    c4 POPCTR
    NEWC b{00100010011111111} STSLICECONST TUCK 256 STU
    100000000 INT STGRAMS
    1 4 + 4 + 64 + 32 + 1+ 1+ INT STZEROES ENDC
    ZERO SENDRAWMSG
    -17 INT 256 STIR 130000000 INT STGRAMS
    107 INT STZEROES ENDC
    ZERO
    NEWC b{11000100100000} "test" $>s |+ STSLICECONST
    123456789 INT STGRAMS
    107 INT STZEROES "Hello, world!" $>s STSLICECONST ENDC
    ZERO SENDRAWMSG SENDRAWMSG
  }>
}END>c
2 boc+>B Bx. cr

// Wallet library
Libs{
  x{ABACABADABACABA} s>c public_lib
  x{1234} x{5678} |_ s>c private_lib
}Libs
2 boc+>B Bx. cr

// SMC#3 library
Libs{
  x{ABACABADABACABA} s>c public_lib
  x{1234} x{5678} |_ s>c public_lib
}Libs
2 boc+>B Bx. cr
"""
        script_file = state_dir / "extract.fif"
        _ = script_file.write_text(script)

        args = [str(install.fift_exe)]
        for include_dir in install.fift_include_dirs:
            args += ["-I", str(include_dir)]
        args += ["-s", str(script_file)]

        result = subprocess.run(args, cwd=str(state_dir), check=True, capture_output=True)
        lines = result.stdout.decode().strip().split("\n")
        hex_lines = [
            l.strip()
            for l in lines
            if all(c in "0123456789abcdefABCDEF" for c in l.strip()) and len(l.strip()) > 10
        ]

        if len(hex_lines) != 5:
            print(f"Expected 5 BOC hex lines, got {len(hex_lines)}")
            print("Raw output:")
            print(result.stdout.decode())
            return 1

        elector_hex, config_hex, smc3_hex, wallet_lib_hex, smc3_lib_hex = hex_lines

        patches: list[tuple[Path, str, str, str]] = [
            (CONTRACT_DIR / "elector.py", "ELECTOR_CODE = None", elector_hex, "elector.py"),
            (CONTRACT_DIR / "config.py", "CONFIG_CODE = None", config_hex, "config.py"),
            (ZEROSTATE_FILE, "SMC3_CODE = None", smc3_hex, "zerostate.py SMC3_CODE"),
            (
                ZEROSTATE_FILE,
                "WALLET_LIBRARY = None",
                wallet_lib_hex,
                "zerostate.py WALLET_LIBRARY",
            ),
            (ZEROSTATE_FILE, "SMC3_LIBRARY = None", smc3_lib_hex, "zerostate.py SMC3_LIBRARY"),
        ]

        for filepath, placeholder, hex_val, label in patches:
            text = filepath.read_text()
            replacement = f'Cell.one_from_boc("{hex_val}")'
            text = text.replace(placeholder, f"{placeholder.split(' = ')[0]} = {replacement}")
            _ = filepath.write_text(text)
            print(f"Patched {label} ({len(hex_val) // 2} bytes)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
