import datetime
import ipaddress
import os
import tempfile
import unittest
import uuid
from pathlib import Path
from xun import (
    Tagged,
    XunError,
    dump,
    encode,
    parse,
    parse_duration,
    parse_size,
    parse_version,
    unpack,
)


class TestEncode(unittest.TestCase):
    def test_encode_empty(self):
        self.assertEqual(encode({}), "")

    def test_encode_non_dict_root(self):
        with self.assertRaises(XunError):
            encode(["item1", "item2"])
        with self.assertRaises(XunError):
            encode("string")

    def test_encode_basic_types(self):
        data = {
            "str_plain": "hello world",
            "str_with_tag": "!not_a_tag",
            "str_empty": "",
            "number_int": 42,
            "number_float": 3.14,
            "flag_true": True,
            "flag_false": False,
            "bytes_val": bytes([0xDE, 0xAD, 0xBE, 0xEF]),
            "tagged_val": Tagged("ver", "3.10"),
        }
        text = encode(data)
        parsed = parse(text)
        self.assertEqual(parsed["str_plain"], "hello world")
        self.assertEqual(parsed["str_with_tag"], "!not_a_tag")
        self.assertEqual(parsed["str_empty"], "")
        self.assertEqual(parsed["number_int"], 42)
        self.assertEqual(parsed["number_float"], 3.14)
        self.assertEqual(parsed["flag_true"], True)
        self.assertEqual(parsed["flag_false"], False)
        self.assertEqual(parsed["bytes_val"], bytes([0xDE, 0xAD, 0xBE, 0xEF]))
        self.assertEqual(parsed["tagged_val"], Tagged("ver", "3.10"))

    def test_encode_native_types(self):
        now = datetime.datetime(2026, 8, 14, 16, 54, 0, tzinfo=datetime.timezone.utc)
        today = datetime.date(2026, 8, 14)
        time_val = datetime.time(16, 54, 0)
        u = uuid.UUID("12345678-1234-5678-1234-567812345678")
        ip = ipaddress.ip_address("192.168.1.1")

        data = {
            "datetime": now,
            "date": today,
            "time": time_val,
            "uuid": u,
            "ip": ip,
        }
        text = encode(data)
        parsed = parse(text)
        self.assertEqual(parsed["datetime"].to_datetime(), now)
        self.assertEqual(parsed["date"].to_date(), today)
        self.assertEqual(parsed["time"].to_time(), time_val)
        self.assertEqual(parsed["uuid"].to_uuid(), u)
        self.assertEqual(parsed["ip"].to_ip(), ip)

    def test_format_unpack_helpers(self):
        self.assertEqual(parse_size("10MiB"), 10 * 1024 * 1024)
        self.assertEqual(parse_size("3KB"), 3000)
        self.assertEqual(parse_duration("1d2h30m"), 86400 + 7200 + 1800)
        self.assertEqual(parse_version("3.10.1"), (3, 10, 1))

        t_sz = Tagged("sz", "10MiB")
        self.assertEqual(t_sz.to_size_bytes(), 10485760)

        t_du = Tagged("du", "1h30s")
        self.assertEqual(t_du.to_duration_seconds(), 3630.0)

        t_ver = Tagged("ver", "2.1.0")
        self.assertEqual(t_ver.to_version_parts(), (2, 1, 0))

        doc = parse("sz: !sz 10MiB\nver: !ver 3.10\n")
        unpacked = unpack(doc)
        self.assertEqual(unpacked["sz"], 10485760)
        self.assertEqual(unpacked["ver"], (3, 10))

    def test_circular_reference_error(self):
        a: dict = {}
        b: dict = {"a": a}
        a["b"] = b
        with self.assertRaises(XunError) as cm:
            encode(a)
        self.assertIn("circular reference detected", str(cm.exception))

    def test_encode_nested_dict_and_list(self):
        data = {
            "server": {
                "host": "127.0.0.1",
                "port": 8080,
                "tls": {
                    "enabled": True,
                },
            },
            "empty_dict": {},
            "empty_list": [],
            "items": [
                "first",
                42,
                True,
                {"sub": "value"},
                ["nested", "list"],
            ],
        }
        text = encode(data)
        parsed = parse(text)
        self.assertEqual(parsed["server"]["host"], "127.0.0.1")
        self.assertEqual(parsed["server"]["port"], 8080)
        self.assertEqual(parsed["server"]["tls"]["enabled"], True)
        self.assertEqual(parsed["empty_dict"], {})
        self.assertEqual(parsed["empty_list"], [])
        self.assertEqual(parsed["items"][0], "first")
        self.assertEqual(parsed["items"][1], 42)
        self.assertEqual(parsed["items"][2], True)
        self.assertEqual(parsed["items"][3], {"sub": "value"})
        self.assertEqual(parsed["items"][4], ["nested", "list"])

    def test_encode_strips_surrounding_quotes(self):
        self.assertEqual(encode({"a": '"hello"'}), "a: hello\n")
        self.assertEqual(encode({"a": '""hello world""'}), "a: hello world\n")
        self.assertEqual(encode({"a": '""'}), "a:\n")
        self.assertEqual(encode({"a": '"!x"'}), "a: !s !x\n")
        self.assertEqual(encode({"a": '"'}), 'a: "\\""\n')
        self.assertEqual(encode({"a": '"unclosed'}), 'a: "\\"unclosed"\n')
        self.assertEqual(encode({"items": ['"a"', '"b"']}), "items:\n  - a\n  - b\n")
        # Tagged values are never stripped.
        self.assertEqual(encode({"a": Tagged("s", '"keep"')}), 'a: !s "keep"\n')

    def test_encode_numeric_looking_strings_with_s_tag(self):
        self.assertEqual(encode({"a": "123"}), "a: !s 123\n")
        self.assertEqual(encode({"a": "3.10"}), "a: !s 3.10\n")
        self.assertEqual(encode({"a": "-1.5"}), "a: !s -1.5\n")
        self.assertEqual(encode({"a": "1e-3"}), "a: !s 1e-3\n")
        self.assertEqual(encode({"a": "0xFF"}), "a: !s 0xFF\n")
        self.assertEqual(encode({"a": "0b10"}), "a: !s 0b10\n")
        self.assertEqual(encode({"a": "0o755"}), "a: !s 0o755\n")
        self.assertEqual(encode({"a": "Infinity"}), "a: !s Infinity\n")
        self.assertEqual(encode({"a": '"8080"'}), "a: !s 8080\n")
        self.assertEqual(encode({"a": "123 "}), 'a: !s "123 "\n')
        self.assertEqual(encode({"items": ["80", "443"]}), "items:\n  - !s 80\n  - !s 443\n")
        self.assertEqual(encode({"a": 123}), "a: !i 123\n")
        self.assertEqual(encode({"a": "hello"}), "a: hello\n")
        self.assertEqual(encode({"a": "123abc"}), "a: 123abc\n")
        self.assertEqual(encode({"a": "1.2.3"}), "a: 1.2.3\n")
        self.assertEqual(parse("a: 123\n")["a"], "123")
        self.assertEqual(encode(parse("a: 123\n")), "a: !s 123\n")
        self.assertEqual(parse('a: "Alice"\n')["a"], "Alice")
        self.assertEqual(parse('a: !s "3.10 "\n')["a"], "3.10 ")

    def test_encode_quoted_strings(self):
        self.assertEqual(encode({"a": 'say "hi"'}), 'a: "say \\"hi\\""\n')
        self.assertEqual(parse('a: "say \\"hi\\""\n')["a"], 'say "hi"')
        self.assertEqual(parse('a: ""\n')["a"], "")
        self.assertEqual(encode(parse('a: "123 "\n')), 'a: !s "123 "\n')

    def test_file_write_and_read(self):
        data = {
            "app": "python-xun",
            "version": Tagged("ver", "0.1.5"),
            "server": {
                "host": "127.0.0.1",
                "port": 8080,
            },
            "features": ["auth", "rate-limit"],
            "raw": bytes([0xCA, 0xFE, 0xBA, 0xBE]),
            "description": "Multi-line\nDescription\nTest",
        }
        with tempfile.NamedTemporaryFile(mode="w+", encoding="utf-8", delete=False) as tf:
            path = tf.name
            dump(data, tf)

        try:
            content = Path(path).read_text(encoding="utf-8")
            doc = parse(content)
            self.assertEqual(doc["app"], "python-xun")
            self.assertEqual(doc["version"], Tagged("ver", "0.1.5"))
            self.assertEqual(doc["server"]["host"], "127.0.0.1")
            self.assertEqual(doc["server"]["port"], 8080)
            self.assertEqual(doc["features"], ["auth", "rate-limit"])
            self.assertEqual(doc["raw"], bytes([0xCA, 0xFE, 0xBA, 0xBE]))
            self.assertEqual(doc["description"], "Multi-line\nDescription\nTest")
        finally:
            if os.path.exists(path):
                os.remove(path)


if __name__ == "__main__":
    unittest.main()
