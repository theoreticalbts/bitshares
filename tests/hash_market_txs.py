#!/usr/bin/env python3

import hashlib
import json
import sys

from btstest import RPCClient

if __name__ == "__main__":
    if (len(sys.argv) != 4) or sys.argv[1].startswith("-"):
        print("syntax:  hash_market_txs.py ~/.BitShares/config.json 2112000 2122600")
        sys.exit(1)

    with open(sys.argv[1]) as f:
        config = json.load(f)
    if not config["rpc"]["enable"]:
        print("please enable RPC in config file")
        sys.exit(1)

    credentials = {}
    credentials["host"], credentials["port"] = config["rpc"]["httpd_endpoint"].split(":")
    credentials["rpc_user"] = config["rpc"]["rpc_user"]
    credentials["rpc_password"] = config["rpc"]["rpc_password"]

    client = RPCClient(credentials)
    h = hashlib.sha256(b"[")
    h200 = hashlib.sha256(b"[")

    start_block = int(sys.argv[2])
    end_block = int(sys.argv[3])

    h200_begin = start_block

    print("[")

    first = True
    first_h200 = True
    for block_num in range(start_block, end_block+1):
        if (not first) and ((block_num % 200) == 0):
            h200_end = block_num-1
            if h200_begin != h200_end:
                h200.update(b"]")
                print(json.dumps([h200_begin, h200_end, h200.hexdigest()])+",")
                h200 = hashlib.sha256(b"[")
                first_h200 = True
            h200_begin = block_num
        if first:
            first = False
        else:
            h.update(b",")
        if first_h200:
            first_h200 = False
        else:
            h200.update(b",")

        result = client.call("blockchain_list_market_transactions", block_num)
        str_result = json.dumps(result, separators=(",", ":"), sort_keys=True).encode("utf-8")
        h.update(str_result)
        h200.update(str_result)

    h.update(b"]")
    h200.update(b"]")

    if h200_begin != start_block:
        print(json.dumps([h200_begin, end_block, h200.hexdigest()])+",")
    print(json.dumps([start_block, end_block, h.hexdigest()]))
    print("]")

