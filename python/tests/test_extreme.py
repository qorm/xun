import unittest
from xun import Tagged, XunError, decode, encode

MUST_TAG = [
    "0",
    "00",
    "012",
    "08",
    "8080",
    "+123",
    "-123",
    "-0",
    "+0",
    "3.10",
    "3.",
    ".5",
    ".0",
    "0.",
    "0.0",
    "00.1",
    "-.5",
    "+.5",
    "+0.0",
    "-0.0",
    "1e3",
    "1E-3",
    "1e+10",
    "0e0",
    "1E+0",
    "1e-0",
    "+1.5e-10",
    "5.e2",
    "+.5e2",
    "-.5E-1",
    "0xFF",
    "0Xff",
    "0x0",
    "0xabcdef",
    "0XABCDEF",
    "0b10",
    "0B10",
    "0b0",
    "0b01",
    "0o755",
    "0O7",
    "0o0",
    "0o07",
    "Infinity",
    "+Infinity",
    "-Infinity",
    "9007199254740991",
    "9007199254740993",
    "-0x10",
    "+0x10",
    "-0b1",
    "+0b10",
    "-0o10",
    " 123",
]

MUST_NOT_TAG = [
    "hello",
    "123abc",
    "abc123",
    "1.2.3",
    "3.1.0",
    "1e",
    "1e+",
    "e3",
    "e10",
    "5.e",
    ".",
    "+",
    "-",
    "0x",
    "0b",
    "0o",
    "0xg",
    "0xG",
    "0b2",
    "0o8",
    "0x10n",
    "123n",
    "infinity",
    "INFINITY",
    "Inf",
    "NaN",
    "true",
    "false",
    "null",
    "1_000",
    "1_2",
    "0xFF_AA",
    "127.0.0.1",
    "2026-08-14",
    "::1",
]


def nest_encode(levels: int) -> dict:
    o: dict = {"v": "leaf"}
    for _ in range(levels):
        o = {"c": o}
    return o


def nest_source(levels: int) -> str:
    parts = [f"{'  ' * i}k{i}:" for i in range(levels)]
    parts.append(f"{'  ' * levels}v: leaf")
    return "\n".join(parts) + "\n"


class TestExtreme(unittest.TestCase):
    def test_numeric_looking_strings_must_tag(self):
        for s in MUST_TAG:
            with self.subTest(s=s):
                quoted = s.strip() != s or '"' in s or "\\" in s
                body = f'"{s.replace(chr(92), chr(92)*2).replace(chr(34), chr(92)+chr(34))}"' if quoted else s
                text = encode({"a": s})
                self.assertEqual(text, f"a: !s {body}\n")
                doc = decode(text)
                self.assertIsInstance(doc["a"], str)
                self.assertEqual(doc["a"], s)

    def test_quoted_strings_preserve_spaces(self):
        self.assertEqual(encode({"a": "123 "}), 'a: !s "123 "\n')
        self.assertEqual(decode('a: "123 "\n')["a"], "123 ")
        self.assertEqual(decode('a: ""\n')["a"], "")
        self.assertEqual(encode(decode('a: "123 "\n')), 'a: !s "123 "\n')

    def test_non_numeric_strings_stay_untagged(self):
        for s in MUST_NOT_TAG:
            with self.subTest(s=s):
                self.assertEqual(encode({"a": s}), f"a: {s}\n")
                self.assertEqual(decode(f"a: {s}\n")["a"], s)

    def test_syntactic_specials(self):
        self.assertEqual(encode({"a": "!x"}), "a: !s !x\n")
        self.assertEqual(encode({"a": "[]"}), "a: !s []\n")
        self.assertEqual(encode({"a": "{}"}), "a: !s {}\n")
        self.assertEqual(encode({"a": "|foo"}), "a: !s |foo\n")
        self.assertEqual(encode({"a": "|"}), "a: !s |\n")

    def test_quote_strip_then_numeric(self):
        self.assertEqual(encode({"a": '"8080"'}), "a: !s 8080\n")
        self.assertEqual(encode({"a": '""3.10""'}), "a: !s 3.10\n")
        self.assertEqual(encode({"a": '"0xFF"'}), "a: !s 0xFF\n")
        self.assertEqual(encode({"a": '"Infinity"'}), "a: !s Infinity\n")
        self.assertEqual(encode({"items": ['"80"', '"443"']}), "items:\n  - !s 80\n  - !s 443\n")

    def test_list_and_nested_preserve_string(self):
        data = {
            "ports": ["80", "443", "8080"],
            "mixed": ["1", 1, "x"],
            "deep": {"inner": {"code": "007"}},
        }
        text = encode(data)
        self.assertEqual(
            text,
            "ports:\n  - !s 80\n  - !s 443\n  - !s 8080\nmixed:\n  - !s 1\n  - !i 1\n  - x\ndeep:\n  inner:\n    code: !s 007\n",
        )
        doc = decode(text)
        self.assertEqual(doc["ports"], ["80", "443", "8080"])
        self.assertIsInstance(doc["ports"][0], str)
        self.assertEqual(doc["mixed"][0], "1")
        self.assertEqual(doc["mixed"][1], 1)
        self.assertEqual(doc["deep"]["inner"]["code"], "007")

    def test_untagged_decode_then_explicit_reencode(self):
        src = "a: 123\nb: 3.10\nc: 0xFF\nd: Infinity\ne: true\n"
        doc = decode(src)
        self.assertEqual(doc["a"], "123")
        self.assertEqual(doc["b"], "3.10")
        self.assertEqual(doc["c"], "0xFF")
        self.assertEqual(doc["d"], "Infinity")
        self.assertEqual(doc["e"], "true")
        self.assertEqual(encode(doc), "a: !s 123\nb: !s 3.10\nc: !s 0xFF\nd: !s Infinity\ne: true\n")

    def test_real_numbers_keep_numeric_tags(self):
        self.assertEqual(encode({"a": 0}), "a: !i 0\n")
        self.assertEqual(encode({"a": 123}), "a: !i 123\n")
        self.assertEqual(encode({"a": -7}), "a: !i -7\n")
        self.assertEqual(encode({"a": 3.14}), "a: !f 3.14\n")
        self.assertEqual(encode({"a": True}), "a: !b true\n")
        doc = decode(encode({"a": 123, "b": "123"}))
        self.assertEqual(doc["a"], 123)
        self.assertEqual(doc["b"], "123")

    def test_empty_blank_comments(self):
        self.assertEqual(decode(""), {})
        self.assertEqual(decode("\n\n"), {})
        self.assertEqual(decode("# only\n# comments\n"), {})
        self.assertEqual(encode({}), "")
        self.assertEqual(encode({"a": ""}), "a:\n")
        self.assertEqual(decode("a:\n")["a"], "")

    def test_bom_nul_oversize(self):
        self.assertEqual(decode("\ufeffa: hello\n")["a"], "hello")
        with self.assertRaises(XunError):
            decode("a: ok\0no\n")
        with self.assertRaises(XunError):
            decode("x" * (1024 * 1024 + 1))
        self.assertEqual(decode("a: " + "x" * 100 + "\n")["a"], "x" * 100)

    def test_decode_nesting_boundary(self):
        decode(nest_source(64))
        with self.assertRaises(XunError) as cm:
            decode(nest_source(65))
        self.assertIn("nesting", str(cm.exception))

    def test_encode_nesting_boundary(self):
        text = encode(nest_encode(64))
        doc = decode(text)
        cur = doc
        for _ in range(64):
            cur = cur["c"]
        self.assertEqual(cur["v"], "leaf")
        with self.assertRaises(XunError) as cm:
            encode(nest_encode(65))
        self.assertIn("nesting", str(cm.exception))

    def test_illegal_keys(self):
        with self.assertRaises(XunError):
            encode({"": "x"})
        with self.assertRaises(XunError):
            encode({"a: b": "x"})
        with self.assertRaises(XunError):
            encode({"a:": "x"})
        with self.assertRaises(XunError):
            encode({"a\nb": "x"})

    def test_numeric_looking_keys(self):
        text = encode({"8080": "8080", "3.10": "3.10"})
        doc = decode(text)
        self.assertEqual(doc["8080"], "8080")
        self.assertEqual(doc["3.10"], "3.10")
        self.assertIn("8080: !s 8080", text)
        self.assertIn("3.10: !s 3.10", text)

    def test_multiline_numeric_text(self):
        text = encode({"a": "123\n456"})
        self.assertEqual(text, "a: |\n  123\n  456\n|\n")
        self.assertEqual(decode(text)["a"], "123\n456")

    def test_crlf_and_explicit_s_tag(self):
        self.assertEqual(decode("a: 123\r\nb: x\r\n")["a"], "123")
        self.assertEqual(decode("a: 123\rb: x\r")["a"], "123")
        self.assertEqual(decode("a: !s 123\n")["a"], "123")
        self.assertEqual(decode("a: !s 3.10\n")["a"], "3.10")
        self.assertEqual(decode("v: !ver 3.10\n")["v"], Tagged("ver", "3.10"))


if __name__ == "__main__":
    unittest.main()
