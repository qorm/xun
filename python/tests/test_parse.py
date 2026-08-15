import unittest
from pathlib import Path

from xun import Tagged, XunError, parse

ROOT = Path(__file__).resolve().parents[2]


class TestParse(unittest.TestCase):
    def test_readme_example(self):
        src = (ROOT / "testdata" / "example.xun").read_text(encoding="utf-8")
        doc = parse(src)
        self.assertEqual(doc["server"]["host"], "localhost")
        self.assertEqual(doc["server"]["port"], 8080)
        self.assertEqual(doc["server"]["bind"], Tagged("ip", "::1"))
        self.assertEqual(doc["server"]["tls"]["mode"], 0o755)
        self.assertEqual(doc["features"], ["auth", "cache"])
        self.assertEqual(doc["ports"], [80, 443, 8080])
        self.assertEqual(doc["endpoint"], "https://api.example.com/v2/orders")
        self.assertEqual(doc["tz"], Tagged("tz", "Asia/Shanghai"))
        self.assertEqual(doc["py"], Tagged("ver", "3.10"))
        self.assertEqual(doc["color"], bytes.fromhex("ff00aa"))
        self.assertEqual(doc["roles"], ["admin", "ops"])
        self.assertEqual(doc["banner"], "Welcome\nto XUN")

    def test_empty_file(self):
        self.assertEqual(parse(""), {})
        self.assertEqual(parse("# only\n"), {})

    def test_untyped_strings(self):
        doc = parse("a: 8080\nb: true\nc: 3.10\n")
        self.assertEqual(doc, {"a": "8080", "b": "true", "c": "3.10"})

    def test_duplicate_keys(self):
        with self.assertRaises(XunError):
            parse("a: 1\na: 2\n")

    def test_version_not_float(self):
        self.assertEqual(parse("py: !ver 3.10\n")["py"], Tagged("ver", "3.10"))


if __name__ == "__main__":
    unittest.main()
