"""Combined RFC-0001 + RFC-0002 demo (k8s deployment style).

Mirrors javascript/test/rfc-demo.test.js. Demonstrates that the two
RFCs compose naturally: explicit 'end' blocks for readability plus
inline object/array literals for compact one-liners.
"""
import unittest

from xun import encode, parse


class TestRFCDemo(unittest.TestCase):
    def test_rfc_0001_and_0002_combined_k8s_deployment(self):
        src = (
            "apiVersion: apps/v1\n"
            "kind: Deployment\n"
            "metadata:\n"
            "  name: web\n"
            "  labels:\n"
            "    app: web\n"
            "    tier: frontend\n"
            "end metadata\n"
            "\n"
            "spec:\n"
            "  replicas: !n 3\n"
            "  selector:\n"
            "    matchLabels:\n"
            "      app: web\n"
            "  template:\n"
            "    spec:\n"
            "      containers:\n"
            "        - {name: nginx, image: nginx:1.25, ports: !n[80, 443]}\n"
            "        - {name: sidecar, image: envoy:v1.30, ports: !n[9901]}\n"
            "      end containers\n"
            "    end spec\n"
            "  end template\n"
            "end spec\n"
        )
        doc = parse(src)
        self.assertEqual(doc["apiVersion"], "apps/v1")
        self.assertEqual(doc["kind"], "Deployment")
        self.assertEqual(doc["metadata"]["name"], "web")
        self.assertEqual(doc["spec"]["replicas"], 3)
        containers = doc["spec"]["template"]["spec"]["containers"]
        self.assertEqual(len(containers), 2)
        self.assertEqual(containers[0]["name"], "nginx")
        self.assertEqual(containers[0]["ports"], [80, 443])
        self.assertEqual(containers[1]["name"], "sidecar")

    def test_encoder_stays_pure_no_auto_end_or_inline_emission(self):
        # The encoder must remain free of auto-emitted 'end' / '{...}'.
        # It only produces the canonical block form.
        doc = {
            "server": {"host": "localhost", "port": 8080},
            "replicas": [{"host": "a", "port": 80}, {"host": "b", "port": 81}],
        }
        out = encode(doc)
        self.assertNotIn("end", out)
        self.assertNotIn("{", out)


if __name__ == "__main__":
    unittest.main()