#!/usr/bin/env python3

import hashlib
import json
import sys

from btstest import RPCClient

known_output_list = [
[2112000, 2112199, "680afecbc1d3856937e8aa4c00cc24cf638863373eca473af3a850ea8caafd82"],
[2112200, 2112399, "a2bdecc62ea17f04b3be3a7ece0ef331d46c894b6bc5d1d536dc5a33bbbc2ae6"],
[2112400, 2112599, "3d03216d3061ad5f2e773c610c24209de6b77d25136622a28078e9f917a1a97b"],
[2112600, 2112799, "f091d0217da6fc101ab8f0abb5821bb6ffbd0df94720765d1ffe6d6fc39c7757"],
[2112800, 2112999, "138dd5c627f7e90755c96073e3b7f1aa5bee3dcf5be5249571b47f115b1dc760"],
[2113000, 2113199, "76e2542d758eecc9f9483efd83adb66ea298b2910813325d715faa8e66b86efd"],
[2113200, 2113399, "a07cabda242a6a3ab8f478e662c41966034ba499f6df276b0fc082ace4388eb1"],
[2113400, 2113599, "6f726d5dbb7eafc2bef3aeb45b5b57ca0e9ba3d87194fe4a17d679a5f6cecd45"],
[2113600, 2113799, "138dd5c627f7e90755c96073e3b7f1aa5bee3dcf5be5249571b47f115b1dc760"],
[2113800, 2113999, "94f19b61c591ba7303dae7d48d024d6c9654606148dddc5138155450f35c94c4"],
[2114000, 2114199, "162b1c6cbac4442808586ce7ae667124887957ab2f5dd28e57fafe71a5d73c0c"],
[2114200, 2114399, "965ca8dd0a741c8e06c15fb5eda2afeefe2613f41843e2dbe3486e6d5a39f06a"],
[2114400, 2114599, "cd0acbc1e2cf5c614941ed19d8346d307893d39e709cedb00fba2a5142eb87a3"],
[2114600, 2114799, "40d1b40c0cc68f047caaec6074c6a5e6f9cc8a8dab1e586f5546aa23fd72c909"],
[2114800, 2114999, "f8b16896d09283538e4d994bb0e0536279a19de73db04cbac57c7989cc97cc74"],
[2115000, 2115199, "7ad22534fd3fdc081a890973751a331ba756917f0df9f281ed08dd818253f2e8"],
[2115200, 2115399, "5bcf06ef819a2e1830586dc177c7ec9b5c009ef1c9ecb7d0c6312ed1314d5643"],
[2115400, 2115599, "78b99b2839f01c481cd25a82684615f41282292c049cf3fe50008e85627f871e"],
[2115600, 2115799, "710fab41e98c545d135597e0622b903c460e8244601868069b7c8ad9384c8c49"],
[2115800, 2115999, "1a63b68c46b480d89292d2db44e604373f9f2ded51781acda38009d14c5f2743"],
[2116000, 2116199, "5a738c091175922b9a8b8bf704832fb18eb3c56fe12db1d6d0707395035cc977"],
[2116200, 2116399, "69c05e9d38d828da2ae215e6c972281fb7999cdc8c655b42f13e4036a703e3e6"],
[2116400, 2116599, "6e7de80528ff24df26f48eae29c01897d026ca3ec04652914a45ea35432ba2a0"],
[2116600, 2116799, "ec7725612755cfeb3ca2575af1ebc226b1990fc4e5a4e6b5b25689b4d5d2b79e"],
[2116800, 2116999, "6492853bf2c50ba89c6b41d9f2381ae7b1fd1e94b01a4a0d25610061fb843d71"],
[2117000, 2117199, "138dd5c627f7e90755c96073e3b7f1aa5bee3dcf5be5249571b47f115b1dc760"],
[2117200, 2117399, "1fb20ff1a7a7be6356cef54a247f3c05246bb59e98afdc1a3d95f143845e942b"],
[2117400, 2117599, "3a41f9ab192aa93298a4c795b1b5eebddc468f3809dbf12b475d5465e513ba41"],
[2117600, 2117799, "42457dca424de7444daae4e39fe27230ddee7a819d829406248973db91a20659"],
[2117800, 2117999, "58c9a1e053fe985476ca848f93f24b9498df93bb26a8cbc130cc3979adc5655a"],
[2118000, 2118199, "a0e7250c2957212760fba75cc4f3942db9ff040913f94984add9c8354149bc5c"],
[2118200, 2118399, "6c0e1f77007f3d909fbafca0a4bfe8bbdd62d16da1cb1d2ec7e14873eba77c4a"],
[2118400, 2118599, "b660adeaa519b9fa382447c5eb5369e717a46b015646a051c20c96a7374da7ff"],
[2118600, 2118799, "17af751688d04e6f7150fab8c0cfbc248978de81f953eebe341b15ebe1231b12"],
[2118800, 2118999, "cb28ed897a3394f8b29cdf7cf9b710804b0c8095d00ae8fd6a98b9b78633f68a"],
[2119000, 2119199, "7096b8bbcb60f5642537d209dfc429f6807c9c6bacafe487fc425f7fa6475054"],
[2119200, 2119399, "3efa3c7799e1c53170c47cf6dbb947e91f32f35285bdd9bb51763d361137253b"],
[2119400, 2119599, "93867518a11d7a7a0a7ea749c47c83179c424b3c9cf9195ad4697da2a1ddcb07"],
[2119600, 2119799, "ea289578ba20d5470617e0bda115afdc6b8ba41dfae9bbfed5148b897b9cbb13"],
[2119800, 2119999, "9bea1b6873e4b25d61af38d306d4a7e9a9d689b9d6355e2eaf459b7b23f76ea2"],
[2120000, 2120199, "49af645b98d73dd161b3dd8382f8127aa69850850914676c4a148c4b1de0e9ff"],
[2120200, 2120399, "52a9f5fcc516a5b3855d217fda711682590d2ae2d3959afea4d06311e4f005b5"],
[2120400, 2120599, "65503dd0d61e06799e0864a845e6f7ce8e81fd96c99113ee3c9d665025fa2358"],
[2120600, 2120799, "4d73b2b0e9ba456fc1cf96768a4ab1efbc24347bd55a4149f6bca64c8017e231"],
[2120800, 2120999, "362ef17622f26292e719b7dbddaa8f7f662ce8e7fa25b84eece62b3d7e0fe57e"],
[2121000, 2121199, "d873b5719ae0488945b7024a2e525c3506d21fde8437f812a39285d6accdceef"],
[2121200, 2121399, "150a7d80af136011b010c3df4ff513510fbbb4bfdf23c9fa4bf61800adebeb03"],
[2121400, 2121599, "0edf4fc68e73e48f0edd1bacd4221b7c6cb7c1a3ddf60bc806089b9ae71e397f"],
[2121600, 2121799, "7f8a4970e5cfbceb686d72b57ace39a04278ac2044e41150bf7f9873d7ce824a"],
[2121800, 2121999, "115da2f7c92e6532da176cfc177620f0ffd19b5da66be7e47258eb1c6bcbd810"],
[2122000, 2122199, "c59aa9fe50eb37253afb5f305179a05c809c95d6074ddfbb5daedf53ae36cea8"],
[2122200, 2122399, "3bd957325a338c7456a598f7ad68b619ff5057501b25b18efc6ad58924066aaf"],
[2122400, 2122599, "95c4500d6d958496f0343b9e1f8efd1231bcd0e8b1abccd695cc41a8bcf4ebf3"],
[2122600, 2122600, "cf1cbb66a638b4860a516671fb74850e6ccf787fe6c4c8d29e9c04efe880bd05"],
[2112000, 2122600, "bdf651bbe722622539514a3b55451b55259619af83fcedde76f7045f907cb61d"]
]

known_output_dict = {(start_block, end_block) : value for start_block, end_block, value in known_output_list}

error_ranges = []
unchecked_ranges = []

def check(x):
    start_block, end_block, current_hash = x
    if (start_block, end_block) not in known_output_dict:
        unchecked_ranges.append([start_block, end_block])
    elif known_output_dict[(start_block, end_block)] != current_hash:
        error_ranges.append([start_block, end_block])
    return x

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

    print('{"result":')

    first = True
    first_h200 = True
    for block_num in range(start_block, end_block+1):
        if (not first) and ((block_num % 200) == 0):
            h200_end = block_num-1
            if h200_begin != h200_end:
                h200.update(b"]")
                print(json.dumps(check([h200_begin, h200_end, h200.hexdigest()]))+",")
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
        print(json.dumps(check([h200_begin, end_block, h200.hexdigest()]))+",")
    print(json.dumps([start_block, end_block, h.hexdigest()]))
    print('],\n"error_ranges":')
    print(json.dumps(error_ranges), end="")
    print(',\n"unchecked_ranges":')
    print(json.dumps(unchecked_ranges))
    print("}")

