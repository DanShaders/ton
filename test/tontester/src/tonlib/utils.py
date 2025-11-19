def is_hex(s: str) -> bool:
    try:
        _ = bytes.fromhex(s)
        return True
    except ValueError:
        return False
