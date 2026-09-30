import { test } from "node:test";
import assert from "node:assert/strict";
import { parse, encode } from "../src/xun.js";

test("demo: RFC-0001 + RFC-0002 combined (k8s deployment)", () => {
  const src = `apiVersion: apps/v1
kind: Deployment
metadata:
  name: web
  labels:
    app: web
    tier: frontend
end metadata

spec:
  replicas: !n 3
  selector:
    matchLabels:
      app: web
  template:
    spec:
      containers:
        - {name: nginx, image: nginx:1.25, ports: !n[80, 443]}
        - {name: sidecar, image: envoy:v1.30, ports: !n[9901]}
      end containers
    end spec
  end template
end spec
`;
  const doc = parse(src);
  assert.equal(doc.apiVersion, "apps/v1");
  assert.equal(doc.kind, "Deployment");
  assert.equal(doc.metadata.name, "web");
  assert.equal(doc.spec.replicas, 3);
  assert.equal(doc.spec.template.spec.containers.length, 2);
  assert.equal(doc.spec.template.spec.containers[0].name, "nginx");
  assert.deepEqual(doc.spec.template.spec.containers[0].ports, [80, 443]);
  assert.equal(doc.spec.template.spec.containers[1].name, "sidecar");
});

test("demo: encoder stays pure (no auto end / inline emission)", () => {
  const doc = {
    server: { host: "localhost", port: 8080 },
    replicas: [{ host: "a", port: 80 }, { host: "b", port: 81 }],
  };
  const out = encode(doc);
  assert.equal(out.includes("end"), false);
  assert.equal(out.includes("{"), false);
});