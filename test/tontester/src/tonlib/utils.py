def is_hex(s: str) -> bool:
    try:
        _ = bytes.fromhex(s)
        return True
    except ValueError:
        return False


def is_b64(s: str) -> bool:
    import base64
    try:
        _ = base64.b64decode(s, validate=True)
        return True
    except Exception:
        return False


def hex_to_b64str(s: str) -> str:
    if is_hex(s):
        import base64
        bytes_data = bytes.fromhex(s)
        return base64.b64encode(bytes_data).decode('utf-8')
    elif not is_b64(s):
        raise ValueError("Input string is neither valid hex nor valid base64.")
    return s
