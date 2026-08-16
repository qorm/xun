package xun

import (
	"bytes"
	"net"
	"os"
	"path/filepath"
	"runtime"
	"testing"
	"time"
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
		"app":     "go-xun",
		"version": Tagged{Tag: "ver", Value: "0.1.4"},
		"server": map[string]any{
			"host": "0.0.0.0",
			"port": int64(9000),
		},
		"tags":      []any{"production", "cluster"},
		"bytes":     []byte{0x01, 0x02, 0xFE, 0xFF},
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
	if m["version"].(Tagged) != (Tagged{Tag: "ver", Value: "0.1.4"}) {
		t.Fatalf("version mismatch: %v", m["version"])
	}
	if m["multiline"] != "First\nSecond\nThird" {
		t.Fatalf("multiline mismatch: %v", m["multiline"])
	}
	if !bytes.Equal(m["bytes"].([]byte), []byte{0x01, 0x02, 0xFE, 0xFF}) {
		t.Fatalf("bytes mismatch: %v", m["bytes"])
	}
}

func TestSymmetricAPIAndUnpack(t *testing.T) {
	now := time.Date(2026, 8, 14, 16, 54, 0, 0, time.UTC)
	ip := net.ParseIP("192.168.1.1")

	data := map[string]any{
		"time":     now,
		"ip":       ip,
		"size":     Tagged{Tag: "sz", Value: "10MiB"},
		"duration": Tagged{Tag: "du", Value: "1h30m"},
		"version":  Tagged{Tag: "ver", Value: "3.10.1"},
	}

	marshaled, err := Marshal(data)
	if err != nil {
		t.Fatalf("marshal error: %v", err)
	}

	var result map[string]any
	if err := Unmarshal(marshaled, &result); err != nil {
		t.Fatalf("unmarshal error: %v", err)
	}

	tVal, ok := result["time"].(Tagged)
	if !ok {
		t.Fatalf("expected Tagged for time, got %T", result["time"])
	}
	parsedTime, err := tVal.AsTime()
	if err != nil || !parsedTime.Equal(now) {
		t.Fatalf("time mismatch: %v, err=%v", parsedTime, err)
	}

	szVal := result["size"].(Tagged)
	szBytes, err := szVal.AsSizeBytes()
	if err != nil || szBytes != 10485760 {
		t.Fatalf("size mismatch: %v, err=%v", szBytes, err)
	}

	duVal := result["duration"].(Tagged)
	du, err := duVal.AsDuration()
	if err != nil || du != 90*time.Minute {
		t.Fatalf("duration mismatch: %v, err=%v", du, err)
	}

	verVal := result["version"].(Tagged)
	verParts, err := verVal.AsVersion()
	if err != nil || len(verParts) != 3 || verParts[0] != 3 || verParts[1] != 10 || verParts[2] != 1 {
		t.Fatalf("version mismatch: %v, err=%v", verParts, err)
	}
}

func TestUnicodeAndChinese(t *testing.T) {
	data := map[string]any{
		"服务名称": "订单处理系统",
		"版本号":  Tagged{Tag: "ver", Value: "2.1.0"},
		"端口":   int64(8080),
		"配置项": map[string]any{
			"超时时间": Tagged{Tag: "du", Value: "30s"},
			"允许跨域": true,
		},
	}
	encoded, err := Encode(data)
	if err != nil {
		t.Fatal(err)
	}
	doc, err := Decode(encoded)
	if err != nil {
		t.Fatal(err)
	}
	m := doc.(map[string]any)
	if m["服务名称"] != "订单处理系统" {
		t.Fatalf("mismatch: %v", m["服务名称"])
	}
	if m["端口"] != int64(8080) {
		t.Fatalf("mismatch: %v", m["端口"])
	}
}

func TestFullCoreTags(t *testing.T) {
	raw := `
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
`
	doc, err := Decode(raw)
	if err != nil {
		t.Fatal(err)
	}
	m := doc.(map[string]any)
	if m["str_plain"] != "hello world" || m["str_special"] != "!not_a_tag" || m["num_int"] != int64(42) {
		t.Fatalf("basic mismatch: %v", m)
	}
	if m["num_hex"] != int64(0xDEADBEEF) || m["num_oct"] != int64(0755) {
		t.Fatalf("hex/oct mismatch: %v", m)
	}
	if m["flag_t"] != true || m["flag_f"] != false {
		t.Fatalf("bool mismatch: %v", m)
	}
	if !bytes.Equal(m["bytes_v"].([]byte), []byte{0xFF, 0x00, 0xAA}) {
		t.Fatalf("bytes mismatch: %v", m["bytes_v"])
	}
	if !bytes.Equal(m["b64_v"].([]byte), []byte("Hello")) {
		t.Fatalf("b64 mismatch: %v", m["b64_v"])
	}
	if m["char_cp"] != (Tagged{Tag: "c", Value: "中"}) {
		t.Fatalf("char_cp mismatch: %v", m["char_cp"])
	}
}

func TestCompactArrays(t *testing.T) {
	src := `
numbers: !n[1, 2, 3, 4]
floats: !f[1.1, 2.2, 3.3]
chars: !c[a, b, c]
ips: !ip[10.0.0.1, 10.0.0.2]
versions: !ver[1.0, 2.0, 3.10]
`
	doc, err := Decode(src)
	if err != nil {
		t.Fatal(err)
	}
	m := doc.(map[string]any)
	nums := m["numbers"].([]any)
	if len(nums) != 4 || nums[0] != int64(1) || nums[3] != int64(4) {
		t.Fatalf("numbers mismatch: %v", nums)
	}
}

func TestExtremeIndentErrors(t *testing.T) {
	if _, err := Decode("a:\n   b: 1\n"); err == nil {
		t.Fatal("expected 3 spaces error")
	}
	if _, err := Decode("a:\n\tb: 1\n"); err == nil {
		t.Fatal("expected tab error")
	}
	if _, err := Decode("a:\n    b: 1\n"); err == nil {
		t.Fatal("expected indent jump error")
	}
	if _, err := Decode("server:\n  host: 1\n  - item1\n"); err == nil {
		t.Fatal("expected mixed dict/list error")
	}
}

func TestEncodeStripsSurroundingQuotes(t *testing.T) {
	cases := []struct {
		in   map[string]any
		want string
	}{
		{map[string]any{"a": `"hello"`}, "a: hello\n"},
		{map[string]any{"a": `""hello world""`}, "a: hello world\n"},
		{map[string]any{"a": `""`}, "a:\n"},
		{map[string]any{"a": `"!x"`}, "a: !s !x\n"},
		{map[string]any{"a": `"`}, "a: \"\n"},
		{map[string]any{"a": `"unclosed`}, "a: \"unclosed\n"},
		{map[string]any{"items": []any{`"a"`, `"b"`}}, "items:\n  - a\n  - b\n"},
		{map[string]any{"a": Tagged{Tag: "s", Value: `"keep"`}}, "a: !s \"keep\"\n"},
	}
	for _, c := range cases {
		got, err := Encode(c.in)
		if err != nil {
			t.Fatalf("encode error: %v", err)
		}
		if got != c.want {
			t.Fatalf("quote strip mismatch:\n got: %q\nwant: %q", got, c.want)
		}
	}
}

func TestUnpackHelpersAllFormats(t *testing.T) {
	raw := `
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
`
	doc, err := Decode(raw)
	if err != nil {
		t.Fatal(err)
	}
	m := doc.(map[string]any)

	d, err := m["date_v"].(Tagged).AsTime()
	if err != nil || d.Year() != 2026 || d.Month() != 8 || d.Day() != 14 {
		t.Fatalf("date mismatch: %v, err=%v", d, err)
	}
	tm, err := m["time_v"].(Tagged).AsTime()
	if err != nil || tm.Hour() != 16 || tm.Minute() != 54 || tm.Second() != 0 || tm.Nanosecond() != 123000000 {
		t.Fatalf("time mismatch: %v, err=%v", tm, err)
	}
	dt, err := m["dt_v"].(Tagged).AsTime()
	if err != nil || dt.Year() != 2026 || dt.Hour() != 16 {
		t.Fatalf("dt mismatch: %v, err=%v", dt, err)
	}
	ip, err := m["ip_v"].(Tagged).AsIP()
	if err != nil || !ip.Equal(net.ParseIP("::1")) {
		t.Fatalf("ip mismatch: %v, err=%v", ip, err)
	}
	u, err := m["uuid_v"].(Tagged).AsUUID()
	if err != nil {
		t.Fatalf("uuid err: %v", err)
	}
	want := [16]byte{0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x56, 0x78}
	if u != want {
		t.Fatalf("uuid mismatch: %x", u)
	}
	if _, err := (Tagged{Tag: "uuid", Value: "12345678-1234-5678-1234-56781234567"}).AsUUID(); err == nil {
		t.Fatal("expected invalid uuid error")
	}
	if _, err := (Tagged{Tag: "ver", Value: "3.10"}).AsUUID(); err == nil {
		t.Fatal("expected wrong-tag uuid error")
	}
	c1, err := m["char_v"].(Tagged).AsChar()
	if err != nil || c1 != 'A' {
		t.Fatalf("char mismatch: %c, err=%v", c1, err)
	}
	c2, err := m["char_cp"].(Tagged).AsChar()
	if err != nil || c2 != '中' {
		t.Fatalf("char_cp mismatch: %c, err=%v", c2, err)
	}
	if _, err := (Tagged{Tag: "c", Value: "ab"}).AsChar(); err == nil {
		t.Fatal("expected multi-char error")
	}
	sz, err := m["sz_v"].(Tagged).AsSizeBytes()
	if err != nil || sz != 10*1024*1024 {
		t.Fatalf("size mismatch: %v, err=%v", sz, err)
	}
	du, err := m["dur_v"].(Tagged).AsDuration()
	if err != nil || du != 1*24*time.Hour+2*time.Hour+30*time.Minute+15*time.Second {
		t.Fatalf("duration mismatch: %v, err=%v", du, err)
	}
	ver, err := m["ver_v"].(Tagged).AsVersion()
	if err != nil || len(ver) != 3 || ver[0] != 3 || ver[1] != 10 || ver[2] != 1 {
		t.Fatalf("version mismatch: %v, err=%v", ver, err)
	}
	bb, err := (Tagged{Tag: "xb", Value: "FF00AA"}).AsBytes()
	if err != nil || !bytes.Equal(bb, []byte{0xFF, 0x00, 0xAA}) {
		t.Fatalf("bytes mismatch: %v, err=%v", bb, err)
	}
	b64, err := (Tagged{Tag: "b64", Value: "SGVsbG8="}).AsBytes()
	if err != nil || !bytes.Equal(b64, []byte("Hello")) {
		t.Fatalf("b64 mismatch: %v, err=%v", b64, err)
	}
	if _, err := (Tagged{Tag: "ip", Value: "::1"}).AsBytes(); err == nil {
		t.Fatal("expected wrong-tag bytes error")
	}
}

func TestInvalidGlyphsAllTags(t *testing.T) {
	cases := []string{
		"a: !i 1.5\n",
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
	}
	for _, src := range cases {
		if _, err := Decode(src); err == nil {
			t.Fatalf("expected error for %q", src)
		}
	}
}

func TestEncodeRoundTripAllTags(t *testing.T) {
	raw := `
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
`
	doc, err := Decode(raw)
	if err != nil {
		t.Fatal(err)
	}
	text, err := Encode(doc)
	if err != nil {
		t.Fatal(err)
	}
	reparsed, err := Decode(text)
	if err != nil {
		t.Fatal(err)
	}
	m := reparsed.(map[string]any)
	if m["uuid_v"] != (Tagged{Tag: "uuid", Value: "12345678-1234-5678-1234-567812345678"}) {
		t.Fatalf("uuid round-trip mismatch: %v", m["uuid_v"])
	}
	if m["ip6_v"] != (Tagged{Tag: "ip", Value: "::1"}) {
		t.Fatalf("ip6 round-trip mismatch: %v", m["ip6_v"])
	}
	if m["ver_v"] != (Tagged{Tag: "ver", Value: "3.10.1"}) {
		t.Fatalf("ver round-trip mismatch: %v", m["ver_v"])
	}
	if m["char_cp"] != (Tagged{Tag: "c", Value: "中"}) {
		t.Fatalf("char round-trip mismatch: %v", m["char_cp"])
	}
	if !bytes.Equal(m["bytes_v"].([]byte), []byte{0xFF, 0x00, 0xAA}) {
		t.Fatalf("bytes round-trip mismatch: %v", m["bytes_v"])
	}
	if !bytes.Equal(m["b64_v"].([]byte), []byte("Hello")) {
		t.Fatalf("b64 round-trip mismatch: %v", m["b64_v"])
	}
}
