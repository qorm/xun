import os
import tempfile
import unittest
from pathlib import Path
from xun import Tagged, XunError, dump, encode, parse


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

    def test_encode_multiline_string(self):
        data = {
            "banner": "Hello\nWorld\nXUN",
        }
        text = encode(data)
        parsed = parse(text)
        self.assertEqual(parsed["banner"], "Hello\nWorld\nXUN")

    def test_file_write_and_read(self):
        data = {
            "app": "python-xun",
            "version": Tagged("ver", "0.1.1"),
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
            self.assertEqual(doc["version"], Tagged("ver", "0.1.1"))
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
