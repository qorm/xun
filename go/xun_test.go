package xun

import (
	"bytes"
	"os"
	"path/filepath"
	"runtime"
	"testing"
)

func testdata(name string) string {
	_, file, _, _ := runtime.Caller(0)
	b, err := os.ReadFile(filepath.Join(filepath.Dir(file), "..", "testdata", name))
	if err != nil {
		panic(err)
	}
	return string(b)
}

func TestExample(t *testing.T) {
	doc, err := Parse(testdata("example.xun"))
	if err != nil {
		t.Fatal(err)
	}
	m := doc.(map[string]any)
	server := m["server"].(map[string]any)
	if server["host"] != "localhost" {
		t.Fatalf("host=%v", server["host"])
	}
	if server["port"] != int64(8080) {
		t.Fatalf("port=%v", server["port"])
	}
	if bind := server["bind"].(Tagged); bind != (Tagged{Tag: "ip", Value: "::1"}) {
		t.Fatalf("bind=%v", bind)
	}
	tls := server["tls"].(map[string]any)
	if tls["mode"] != int64(0755) {
		t.Fatalf("mode=%v", tls["mode"])
	}
	ports := m["ports"].([]any)
	if ports[0] != int64(80) || ports[2] != int64(8080) {
		t.Fatalf("ports=%v", ports)
	}
	if m["endpoint"] != "https://api.example.com/v2/orders" {
		t.Fatalf("endpoint=%v", m["endpoint"])
	}
	if m["py"].(Tagged) != (Tagged{Tag: "ver", Value: "3.10"}) {
		t.Fatalf("py=%v", m["py"])
	}
	color := m["color"].([]byte)
	if !bytes.Equal(color, []byte{0xff, 0x00, 0xaa}) {
		t.Fatalf("color=%x", color)
	}
	if m["banner"] != "Welcome\nto XUN" {
		t.Fatalf("banner=%q", m["banner"])
	}
}

func TestEmpty(t *testing.T) {
	doc, err := Parse("")
	if err != nil {
		t.Fatal(err)
	}
	if len(doc.(map[string]any)) != 0 {
		t.Fatal(doc)
	}
}

func TestUntyped(t *testing.T) {
	doc, err := Parse("a: 8080\nb: true\n")
	if err != nil {
		t.Fatal(err)
	}
	m := doc.(map[string]any)
	if m["a"] != "8080" || m["b"] != "true" {
		t.Fatal(m)
	}
}

func TestDuplicate(t *testing.T) {
	if _, err := Parse("a: 1\na: 2\n"); err == nil {
		t.Fatal("expected error")
	}
}

func TestEncodeAndRoundTrip(t *testing.T) {
	data := map[string]any{
		"server": map[string]any{
			"host": "localhost",
			"port": int64(8080),
			"tls": map[string]any{
				"cert": "/etc/ssl/cert.pem",
			},
		},
		"empty_dict": map[string]any{},
		"empty_list": []any{},
		"features":   []any{"auth", "cache"},
		"banner":     "Welcome\nto XUN",
		"flag":       true,
		"rate":       3.14,
		"color":      []byte{0xDE, 0xAD, 0xBE, 0xEF},
		"py":         Tagged{Tag: "ver", Value: "3.10"},
	}

	encoded, err := Encode(data)
	if err != nil {
		t.Fatalf("encode error: %v", err)
	}

	parsed, err := Parse(encoded)
	if err != nil {
		t.Fatalf("parse error: %v\nencoded text:\n%s", err, encoded)
	}

	m := parsed.(map[string]any)
	server := m["server"].(map[string]any)
	if server["host"] != "localhost" || server["port"] != int64(8080) {
		t.Fatalf("server mismatch: %v", server)
	}
	if m["banner"] != "Welcome\nto XUN" {
		t.Fatalf("banner mismatch: %v", m["banner"])
	}
	if m["flag"] != true {
		t.Fatalf("flag mismatch: %v", m["flag"])
	}
	if !bytes.Equal(m["color"].([]byte), []byte{0xDE, 0xAD, 0xBE, 0xEF}) {
		t.Fatalf("color mismatch: %v", m["color"])
	}
	if m["py"].(Tagged) != (Tagged{Tag: "ver", Value: "3.10"}) {
		t.Fatalf("py mismatch: %v", m["py"])
	}
}

func TestFileWriteAndRead(t *testing.T) {
	data := map[string]any{
		"app": "go-xun",
		"version": Tagged{Tag: "ver", Value: "0.1.1"},
		"server": map[string]any{
			"host": "0.0.0.0",
			"port": int64(9000),
		},
		"tags": []any{"production", "cluster"},
		"bytes": []byte{0x01, 0x02, 0xFE, 0xFF},
		"multiline": "First\nSecond\nThird",
	}

	bytesData, err := Marshal(data)
	if err != nil {
		t.Fatalf("marshal error: %v", err)
	}

	tmpFile := filepath.Join(t.TempDir(), "config.xun")
	if err := os.WriteFile(tmpFile, bytesData, 0644); err != nil {
		t.Fatalf("write file error: %v", err)
	}

	readBytes, err := os.ReadFile(tmpFile)
	if err != nil {
		t.Fatalf("read file error: %v", err)
	}

	parsed, err := Parse(string(readBytes))
	if err != nil {
		t.Fatalf("parse error: %v", err)
	}

	m := parsed.(map[string]any)
	if m["app"] != "go-xun" {
		t.Fatalf("app mismatch: %v", m["app"])
	}
	if m["version"].(Tagged) != (Tagged{Tag: "ver", Value: "0.1.1"}) {
		t.Fatalf("version mismatch: %v", m["version"])
	}
	if m["multiline"] != "First\nSecond\nThird" {
		t.Fatalf("multiline mismatch: %v", m["multiline"])
	}
	if !bytes.Equal(m["bytes"].([]byte), []byte{0x01, 0x02, 0xFE, 0xFF}) {
		t.Fatalf("bytes mismatch: %v", m["bytes"])
	}
}
