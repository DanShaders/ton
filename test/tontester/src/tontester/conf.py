import os

NODE_IP_ADDRESS: str = os.getenv("NODE_IP_ADDRESS", '127.0.42.239')
TONLIBJSON_BIN_PATH: str = os.getenv("TONLIBJSON_BIN_PATH", 'tonlib/libtonlibjson.so')
