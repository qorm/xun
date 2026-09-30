"""Tests for RFC-0002: inline object / array literals.

Mirrors the JavaScript inline-object.test.js matrix to ensure functional
equivalence across language implementations.
"""
import unittest

from xun import Tagged, XunError, parse


class TestRFC0002InlineObject(unittest.TestCase):
    def test_inline_object_as_list_item(self):
        src = (
            "servers:\n"
            "  - {host: a, port: 80}\n"
            "  - {host: b, port: 81}\n"
        )
        doc = parse(src)
        self.assertEqual(len(doc["servers"]), 2)
        self.assertEqual(doc["servers"][0]["host"], "a")
        self.assertEqual(doc["servers"][0]["port"], "80")
        self.assertEqual(doc["servers"][1]["host"], "b")
        self.assertEqual(doc["servers"][1]["port"], "81")

    def test_inline_object_with_quoted_value(self):
        src = (
            "servers:\n"
            '  - {host: "my host", port: 80}\n'
        )
        doc = parse(src)
        self.assertEqual(doc["servers"][0]["host"], "my host")

    def test_inline_object_with_tagged_values(self):
        src = (
            "cfg:\n"
            "  - {port: !n 8080, mode: !o 755, color: !xb FF00AA}\n"
        )
        doc = parse(src)
        self.assertEqual(doc["cfg"][0]["port"], 8080)
        self.assertEqual(doc["cfg"][0]["mode"], Tagged("o", "755"))
        self.assertEqual(doc["cfg"][0]["color"], bytes.fromhex("ff00aa"))

    def test_inline_object_with_empty_value(self):
        src = (
            "cfg:\n"
            '  - {name: ""}\n'
            "  - {empty:}\n"
        )
        doc = parse(src)
        # First has explicit empty string via ""; second uses trailing colon -> empty string.
        self.assertEqual(doc["cfg"][0]["name"], "")
        self.assertEqual(doc["cfg"][1]["empty"], "")

    def test_compact_array_of_inline_objects_no_tag(self):
        src = "peers: [{host: a, port: 80}, {host: b, port: 81}]\n"
        doc = parse(src)
        self.assertEqual(len(doc["peers"]), 2)
        self.assertEqual(doc["peers"][0]["host"], "a")
        self.assertEqual(doc["peers"][1]["host"], "b")

    def test_compact_tagged_array_of_inline_objects(self):
        src = "items: !o[{a: 1, b: 2}, {c: 3}]\n"
        doc = parse(src)
        self.assertEqual(len(doc["items"]), 2)
        self.assertEqual(doc["items"][0]["a"], "1")
        self.assertEqual(doc["items"][0]["b"], "2")
        self.assertEqual(doc["items"][1]["c"], "3")

    def test_mix_block_and_inline_list_items(self):
        src = (
            "servers:\n"
            "  - {host: a, port: 80}\n"
            "  - host: b\n"
            "    port: 81\n"
            "    tls: enabled\n"
        )
        doc = parse(src)
        self.assertEqual(len(doc["servers"]), 2)
        self.assertEqual(doc["servers"][0]["host"], "a")
        self.assertEqual(doc["servers"][1]["host"], "b")
        self.assertEqual(doc["servers"][1]["tls"], "enabled")

    def test_empty_inline_object(self):
        src = (
            "cfg:\n"
            "  - {}\n"
        )
        doc = parse(src)
        self.assertEqual(doc["cfg"][0], {})

    def test_inline_object_in_nested_block(self):
        src = (
            "data:\n"
            "  items:\n"
            "    - {id: 1, name: alice}\n"
            "    - {id: 2, name: bob}\n"
        )
        doc = parse(src)
        self.assertEqual(len(doc["data"]["items"]), 2)
        self.assertEqual(doc["data"]["items"][0]["id"], "1")
        self.assertEqual(doc["data"]["items"][0]["name"], "alice")

    def test_duplicate_keys_in_inline_object_throws(self):
        src = (
            "cfg:\n"
            "  - {a: 1, a: 2}\n"
        )
        with self.assertRaises(XunError) as ctx:
            parse(src)
        self.assertIn("duplicate key 'a'", str(ctx.exception))

    def test_malformed_inline_object_throws(self):
        src = (
            "cfg:\n"
            "  - {a, b: 2}\n"
        )
        with self.assertRaises(XunError):
            parse(src)

    def test_inline_object_preserves_key_order(self):
        src = (
            "cfg:\n"
            "  - {z: 1, a: 2, m: 3}\n"
        )
        doc = parse(src)
        self.assertEqual(list(doc["cfg"][0].keys()), ["z", "a", "m"])

    def test_nested_compact_array_in_inline_object(self):
        src = (
            "cfg:\n"
            "  - {tags: !s[a, b, c], port: !n 80}\n"
        )
        doc = parse(src)
        self.assertEqual(len(doc["cfg"][0]["tags"]), 3)
        self.assertEqual(doc["cfg"][0]["port"], 80)

    def test_traditional_block_list_regression(self):
        src = (
            "items:\n"
            "  - one\n"
            "  - two\n"
        )
        doc = parse(src)
        self.assertEqual(doc["items"], ["one", "two"])

    def test_traditional_compact_array_regression(self):
        src = "ports: !n[80, 443, 8080]\n"
        doc = parse(src)
        self.assertEqual(len(doc["ports"]), 3)
        self.assertEqual(doc["ports"][0], 80)
        self.assertEqual(doc["ports"][1], 443)
        self.assertEqual(doc["ports"][2], 8080)

    def test_end_as_inline_object_value(self):
        # 'end' as a key inside { ... } must not be confused with the block-close keyword.
        src = (
            "cfg:\n"
            "  - {end: foo, value: 1}\n"
        )
        doc = parse(src)
        self.assertEqual(doc["cfg"][0]["end"], "foo")
        self.assertEqual(doc["cfg"][0]["value"], "1")

    def test_inline_object_with_complex_tagged_values(self):
        src = (
            "cfg:\n"
            '  - {port: !n 8080, name: "my server", tags: !s[web, api]}\n'
        )
        doc = parse(src)
        self.assertEqual(doc["cfg"][0]["port"], 8080)
        self.assertEqual(doc["cfg"][0]["name"], "my server")
        self.assertEqual(doc["cfg"][0]["tags"], ["web", "api"])


if __name__ == "__main__":
    unittest.main()