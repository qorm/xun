"""Tests for RFC-0001: optional 'end' block delimiter.

Mirrors the JavaScript end-keyword.test.js matrix to ensure functional
equivalence across language implementations.
"""
import unittest

from xun import XunError, parse


class TestRFC0001EndKeyword(unittest.TestCase):
    def test_bare_end_closes_top_level_dict(self):
        src = (
            "server:\n"
            "  host: localhost\n"
            "  port: 8080\n"
            "end\n"
        )
        doc = parse(src)
        self.assertEqual(doc["server"]["host"], "localhost")
        self.assertEqual(doc["server"]["port"], "8080")

    def test_bare_end_closes_nested_dict(self):
        src = (
            "server:\n"
            "  host: localhost\n"
            "  tls:\n"
            "    cert: /etc/ssl/cert.pem\n"
            "    mode: 755\n"
            "  end\n"
            "  port: 8080\n"
            "end\n"
        )
        doc = parse(src)
        self.assertEqual(doc["server"]["tls"]["cert"], "/etc/ssl/cert.pem")
        self.assertEqual(doc["server"]["port"], "8080")

    def test_end_with_key_closes_named_nested_dict(self):
        src = (
            "server:\n"
            "  host: localhost\n"
            "  tls:\n"
            "    cert: /etc/ssl/cert.pem\n"
            "  end tls\n"
            "  port: 8080\n"
            "end server\n"
        )
        doc = parse(src)
        self.assertEqual(doc["server"]["host"], "localhost")
        self.assertEqual(doc["server"]["tls"]["cert"], "/etc/ssl/cert.pem")
        self.assertEqual(doc["server"]["port"], "8080")

    def test_bare_end_closes_list_block(self):
        src = (
            "servers:\n"
            "  - host: a\n"
            "    port: 80\n"
            "  - host: b\n"
            "    port: 81\n"
            "end\n"
        )
        doc = parse(src)
        self.assertEqual(len(doc["servers"]), 2)
        self.assertEqual(doc["servers"][0]["host"], "a")
        self.assertEqual(doc["servers"][1]["host"], "b")

    def test_bare_end_equivalent_to_dedent(self):
        with_end = "a: 1\nb: 2\nend\n"
        without_end = "a: 1\nb: 2\n"
        self.assertEqual(parse(with_end), parse(without_end))

    def test_end_key_mismatch_throws(self):
        src = (
            "server:\n"
            "  host: localhost\n"
            "  port: 8080\n"
            "end tls\n"
        )
        with self.assertRaises(XunError) as ctx:
            parse(src)
        self.assertIn("end-key mismatch", str(ctx.exception))

    def test_bare_end_on_root_allowed(self):
        src = "a: 1\nend\n"
        doc = parse(src)
        self.assertEqual(doc["a"], "1")

    def test_end_as_dict_key_allowed(self):
        # 'end' as a dict key is just a regular identifier, not a delimiter.
        src = "end: 1\nend2: 2\n"
        doc = parse(src)
        self.assertEqual(doc["end"], "1")
        self.assertEqual(doc["end2"], "2")

    def test_end_inside_multiline_block_is_literal(self):
        src = (
            'script: |\n'
            '  echo "end of script"\n'
            "|\n"
        )
        doc = parse(src)
        self.assertEqual(doc["script"], 'echo "end of script"')

    def test_deeply_nested_end_chains(self):
        src = (
            "a:\n"
            "  b:\n"
            "    c:\n"
            "      d: 1\n"
            "    end c\n"
            "  end b\n"
            "end a\n"
            "e: 2\n"
        )
        doc = parse(src)
        self.assertEqual(doc["a"]["b"]["c"]["d"], "1")
        self.assertEqual(doc["e"], "2")

    def test_end_allows_sibling_content_after(self):
        src = (
            "server:\n"
            "  host: a\n"
            "end server\n"
            "proxy:\n"
            "  host: b\n"
            "end proxy\n"
        )
        doc = parse(src)
        self.assertEqual(doc["server"]["host"], "a")
        self.assertEqual(doc["proxy"]["host"], "b")

    def test_root_end_works_with_complex_dict(self):
        src = (
            "apiVersion: apps/v1\n"
            "kind: Deployment\n"
            "metadata:\n"
            "  name: web\n"
            "  labels:\n"
            "    app: web\n"
            "    tier: frontend\n"
            "end metadata\n"
            "spec:\n"
            "  replicas: !n 3\n"
            "end spec\n"
        )
        doc = parse(src)
        self.assertEqual(doc["apiVersion"], "apps/v1")
        self.assertEqual(doc["kind"], "Deployment")
        self.assertEqual(doc["metadata"]["name"], "web")
        self.assertEqual(doc["spec"]["replicas"], 3)


if __name__ == "__main__":
    unittest.main()