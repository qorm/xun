import unittest
from xun import (
    Tagged,
    XunError,
    decode,
    dump,
    dumps,
    encode,
    load,
    loads,
    parse,
    parse_duration,
    parse_size,
    parse_version,
    unpack,
)


class TestComprehensive(unittest.TestCase):
    def test_unicode_and_chinese_keys_values(self):
        data = {
            "服务名称": "订单处理系统",
            "版本号": Tagged("ver", "2.1.0"),
            "端口": 8080,
            "配置项": {
                "超时时间": Tagged("du", "30s"),
                "允许跨域": True,
                "白名单IP": [Tagged("ip", "127.0.0.1"), Tagged("ip", "192.168.1.1")],
            },
        }
        text = encode(data)
        doc = decode(text)
        self.assertEqual(doc["服务名称"], "订单处理系统")
        self.assertEqual(doc["版本号"], Tagged("ver", "2.1.0"))
        self.assertEqual(doc["端口"], 8080)
        self.assertEqual(doc["配置项"]["超时时间"], Tagged("du", "30s"))
        self.assertEqual(doc["配置项"]["允许跨域"], True)
        self.assertEqual(doc["配置项"]["白名单IP"][0], Tagged("ip", "127.0.0.1"))

    def test_extreme_indentation_errors(self):
        # 3 spaces indent
        with self.assertRaises(XunError) as cm:
            decode("a:\n   b: 1\n")
        self.assertEqual(cm.exception.line, 2)
        self.assertIn("multiple of 2", cm.exception.message)

        # Tab indent
        with self.assertRaises(XunError) as cm:
            decode("a:\n\tb: 1\n")
        self.assertEqual(cm.exception.line, 2)
        self.assertIn("tab", cm.exception.message)

        # Indent jump (0 to 4)
        with self.assertRaises(XunError) as cm:
            decode("a:\n    b: 1\n")
        self.assertEqual(cm.exception.line, 2)
        self.assertIn("indent", cm.exception.message)

        # Mixing dict and list
        with self.assertRaises(XunError) as cm:
            decode("server:\n  host: localhost\n  - item1\n")
        self.assertEqual(cm.exception.line, 3)

        with self.assertRaises(XunError) as cm:
            decode("items:\n  - item1\n  key: val\n")
        self.assertEqual(cm.exception.line, 3)

    def test_full_20_core_tags(self):
        raw = """
str_plain: hello world
str_special: !s !not_a_tag
str_empty:
num_int: !i 42
num_float: !f 3.14159
num_hex: !x DEAD_BEEF
num_oct: !o 755
flag_t: !b true
flag_f: !b false
date_v: !d 2026-08-14
time_v: !t 16:54:00.123
dt_v: !dt 2026-08-14T16:54:00+08:00
tz_v: !tz Asia/Shanghai
dur_v: !du 1d2h30m15s
sz_v: !sz 10GiB
unix_v: !unix 1700000000
ver_v: !ver 3.10.1
uuid_v: !uuid 12345678-1234-5678-1234-567812345678
ip4_v: !ip 127.0.0.1
ip6_v: !ip ::1
bytes_v: !xb FF00AA
b64_v: !b64 SGVsbG8=
char_v: !c A
char_cp: !c U+4E2D
custom_v: !sql SELECT * FROM users
"""
        doc = decode(raw)
        self.assertEqual(doc["str_plain"], "hello world")
        self.assertEqual(doc["str_special"], "!not_a_tag")
        self.assertEqual(doc["str_empty"], "")
        self.assertEqual(doc["num_int"], 42)
        self.assertEqual(doc["num_float"], 3.14159)
        self.assertEqual(doc["num_hex"], 0xDEADBEEF)
        self.assertEqual(doc["num_oct"], 0o755)
        self.assertEqual(doc["flag_t"], True)
        self.assertEqual(doc["flag_f"], False)
        self.assertEqual(doc["date_v"], Tagged("d", "2026-08-14"))
        self.assertEqual(doc["time_v"], Tagged("t", "16:54:00.123"))
        self.assertEqual(doc["dt_v"], Tagged("dt", "2026-08-14T16:54:00+08:00"))
        self.assertEqual(doc["tz_v"], Tagged("tz", "Asia/Shanghai"))
        self.assertEqual(doc["dur_v"], Tagged("du", "1d2h30m15s"))
        self.assertEqual(doc["sz_v"], Tagged("sz", "10GiB"))
        self.assertEqual(doc["unix_v"], 1700000000)
        self.assertEqual(doc["ver_v"], Tagged("ver", "3.10.1"))
        self.assertEqual(doc["uuid_v"], Tagged("uuid", "12345678-1234-5678-1234-567812345678"))
        self.assertEqual(doc["ip4_v"], Tagged("ip", "127.0.0.1"))
        self.assertEqual(doc["ip6_v"], Tagged("ip", "::1"))
        self.assertEqual(doc["bytes_v"], bytes([0xFF, 0x00, 0xAA]))
        self.assertEqual(doc["b64_v"], b"Hello")
        self.assertEqual(doc["char_v"], Tagged("c", "A"))
        self.assertEqual(doc["char_cp"], Tagged("c", "中"))
        self.assertEqual(doc["custom_v"], Tagged("sql", "SELECT * FROM users"))

    def test_compact_arrays_and_errors(self):
        src = """
numbers: !n[1, 2, 3, 4]
floats: !f[1.1, 2.2, 3.3]
chars: !c[a, b, c]
ips: !ip[10.0.0.1, 10.0.0.2]
versions: !ver[1.0, 2.0, 3.10]
"""
        doc = decode(src)
        self.assertEqual(doc["numbers"], [1, 2, 3, 4])
        self.assertEqual(doc["floats"], [1.1, 2.2, 3.3])
        self.assertEqual(doc["chars"], [Tagged("c", "a"), Tagged("c", "b"), Tagged("c", "c")])
        self.assertEqual(doc["ips"], [Tagged("ip", "10.0.0.1"), Tagged("ip", "10.0.0.2")])
        self.assertEqual(doc["versions"], [Tagged("ver", "1.0"), Tagged("ver", "2.0"), Tagged("ver", "3.10")])

        with self.assertRaises(XunError):
            decode("ports: !n[80, 443\n")

    def test_multiline_blocks(self):
        src = """
sql_query: |SQL
  SELECT id, name, email
  FROM users
  WHERE status = 'active'
  # This is not a comment line
  AND age > 18;
SQL
empty_block: |
|
"""
        doc = decode(src)
        self.assertEqual(
            doc["sql_query"],
            "SELECT id, name, email\nFROM users\nWHERE status = 'active'\n# This is not a comment line\nAND age > 18;",
        )
        self.assertEqual(doc["empty_block"], "")

        with self.assertRaises(XunError):
            decode("a: |\n  Line 1\n  Line 2\n")

    def test_unpack_all_formats(self):
        raw = """
str_plain: hello world
num_int: !i 42
num_float: !f 3.14
num_hex: !x DEAD_BEEF
num_oct: !o 755
flag: !b true
date_v: !d 2026-08-14
time_v: !t 16:54:00.123
dt_v: !dt 2026-08-14T16:54:00+08:00
tz_v: !tz Asia/Shanghai
dur_v: !du 1d2h30m15s
sz_v: !sz 10MiB
unix_v: !unix 1700000000
ver_v: !ver 3.10.1
uuid_v: !uuid 12345678-1234-5678-1234-567812345678
ip_v: !ip ::1
bytes_v: !xb FF00AA
b64_v: !b64 SGVsbG8=
char_v: !c A
char_cp: !c U+4E2D
custom_v: !sql SELECT 1
"""
        import datetime
        import ipaddress
        import uuid

        native = unpack(decode(raw))
        self.assertEqual(native["str_plain"], "hello world")
        self.assertEqual(native["num_int"], 42)
        self.assertEqual(native["num_float"], 3.14)
        self.assertEqual(native["num_hex"], 0xDEADBEEF)
        self.assertEqual(native["num_oct"], 0o755)
        self.assertIs(native["flag"], True)
        self.assertEqual(native["date_v"], datetime.date(2026, 8, 14))
        self.assertEqual(native["time_v"], datetime.time(16, 54, 0, 123000))
        self.assertEqual(
            native["dt_v"],
            datetime.datetime(2026, 8, 14, 16, 54, tzinfo=datetime.timezone(datetime.timedelta(hours=8))),
        )
        self.assertEqual(native["tz_v"].key, "Asia/Shanghai")
        self.assertEqual(native["dur_v"], 1 * 86400 + 2 * 3600 + 30 * 60 + 15)
        self.assertEqual(native["sz_v"], 10 * 1024 * 1024)
        self.assertEqual(native["unix_v"], 1700000000)
        self.assertEqual(native["ver_v"], (3, 10, 1))
        self.assertEqual(native["uuid_v"], uuid.UUID("12345678-1234-5678-1234-567812345678"))
        self.assertEqual(native["ip_v"], ipaddress.ip_address("::1"))
        self.assertEqual(native["bytes_v"], bytes([0xFF, 0x00, 0xAA]))
        self.assertEqual(native["b64_v"], b"Hello")
        self.assertEqual(native["char_v"], "A")
        self.assertEqual(native["char_cp"], "中")
        self.assertEqual(native["custom_v"], "SELECT 1")

        # Round-trip: encoding the unpacked natives back preserves values.
        text = encode(native)
        reparsed = decode(text)
        self.assertEqual(reparsed["dt_v"], Tagged("dt", "2026-08-14T16:54:00+08:00"))
        self.assertEqual(reparsed["ip_v"], Tagged("ip", "::1"))
        self.assertEqual(reparsed["uuid_v"], Tagged("uuid", "12345678-1234-5678-1234-567812345678"))
        self.assertEqual(reparsed["sz_v"], 10 * 1024 * 1024)
        self.assertEqual(reparsed["dur_v"], 1 * 86400 + 2 * 3600 + 30 * 60 + 15)
        self.assertEqual(reparsed["ver_v"], Tagged("ver", "3.10.1"))
        self.assertEqual(reparsed["tz_v"], Tagged("tz", "Asia/Shanghai"))
        self.assertEqual(reparsed["char_cp"], "中")

    def test_to_timezone_and_to_char(self):
        import datetime
        from zoneinfo import ZoneInfo

        self.assertEqual(Tagged("tz", "Z").to_timezone(), datetime.timezone.utc)
        self.assertEqual(Tagged("tz", "UTC").to_timezone(), datetime.timezone.utc)
        self.assertEqual(
            Tagged("tz", "+08:00").to_timezone(),
            datetime.timezone(datetime.timedelta(hours=8)),
        )
        self.assertEqual(Tagged("tz", "Asia/Shanghai").to_timezone().key, ZoneInfo("Asia/Shanghai").key)
        with self.assertRaises(XunError):
            Tagged("tz", "Not/AZone").to_timezone()

        self.assertEqual(Tagged("c", "A").to_char(), "A")
        self.assertEqual(Tagged("c", "U+4E2D").to_char(), "中")
        with self.assertRaises(XunError):
            Tagged("c", "ab").to_char()

    def test_invalid_glyphs_all_tags(self):
        invalid = [
            "a: !i 1.5\n",
            "a: !i 99999999999999999999999999\n",
            "a: !f 8080\n",
            "a: !x XYZ\n",
            "a: !xb F0A\n",
            "a: !o 89\n",
            "a: !b yes\n",
            "a: !d 2026/08/14\n",
            "a: !t 4pm\n",
            "a: !dt 2026-08-14T16:54:00\n",
            "a: !tz CST\n",
            "a: !du 90 minutes\n",
            "a: !sz 10m\n",
            "a: !unix 01692000000\n",
            "a: !ver 3.10.beta\n",
            "a: !uuid 12345678-1234-5678-1234-5678123456\n",
            "a: !ip 127.0.0.1:80\n",
            "a: !b64 not_base64!!\n",
            "a: !c ab\n",
        ]
        for src in invalid:
            with self.assertRaises(XunError, msg=f"should reject: {src!r}"):
                decode(src)

    def test_encode_roundtrip_all_20_tags(self):
        raw = """
str_plain: hello world
num_int: !i 42
num_float: !f 3.14159
num_hex: !x DEAD_BEEF
num_oct: !o 755
flag: !b true
date_v: !d 2026-08-14
time_v: !t 16:54:00.123
dt_v: !dt 2026-08-14T16:54:00+08:00
tz_v: !tz Asia/Shanghai
dur_v: !du 1d2h30m15s
sz_v: !sz 10GiB
unix_v: !unix 1700000000
ver_v: !ver 3.10.1
uuid_v: !uuid 12345678-1234-5678-1234-567812345678
ip4_v: !ip 127.0.0.1
ip6_v: !ip ::1
bytes_v: !xb FF00AA
b64_v: !b64 SGVsbG8=
char_v: !c A
char_cp: !c U+4E2D
"""
        doc = decode(raw)
        text = encode(doc)
        reparsed = decode(text)
        self.assertEqual(reparsed, doc)

    def test_deep_nesting(self):
        nested_obj: dict = {"value": "deepest"}
        for i in range(20):
            nested_obj = {f"level_{i}": nested_obj}
        text = encode(nested_obj)
        doc = decode(text)

        cur = doc
        for i in range(19, -1, -1):
            cur = cur[f"level_{i}"]
        self.assertEqual(cur["value"], "deepest")


if __name__ == "__main__":
    unittest.main()
