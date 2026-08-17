package io.github.qorm.xun;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public final class XunTest {
  private static int failed = 0;

  public static void main(String[] args) throws Exception {
    Path root = Path.of(args.length > 0 ? args[0] : "..");
    testExample(root);
    testEmpty();
    testUntyped();
    testDuplicate();
    testVersion();
    testFloatTag();
    testMultilineCloser();
    testKeyNoSpace();
    testRootList();
    testLiteralS();
    testCompactChars();
    testStringCompact();
    testEncodeAndRoundTrip();
    testEncodeStripsSurroundingQuotes();
    testEncodeNumericLookingStrings();
    testExtreme();
    testUnpackNewHelpers();
    testFileWriteAndRead();
    testSymmetricAndUnpack();
    testUnicodeAndChinese();
    testFullCoreTags();
    testExtremeIndentErrors();
    if (failed > 0) {
      System.err.println(failed + " failed");
      System.exit(1);
    }
    System.out.println("ok");
  }

  static void testUnicodeAndChinese() {
    Map<String, Object> data = new LinkedHashMap<>();
    data.put("服务名称", "订单处理系统");
    data.put("版本号", new Xun.Tagged("ver", "2.1.0"));
    data.put("端口", 8080L);

    String text = Xun.encode(data);
    Map<String, Object> doc = Xun.decode(text);
    eq(doc.get("服务名称"), "订单处理系统", "chinese key/val");
    eq(doc.get("端口"), 8080L, "chinese dict port");
  }

  static void testFullCoreTags() {
    String raw = """
str_plain: hello world
str_special: !s !not_a_tag
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
""";
    Map<String, Object> doc = Xun.decode(raw);
    eq(doc.get("str_plain"), "hello world", "core str");
    eq(doc.get("num_int"), 42L, "core int");
    eq(doc.get("num_hex"), 0xDEADBEEFL, "core hex");
    eq(doc.get("num_oct"), 0755L, "core oct");
    eq(doc.get("flag_t"), true, "core bool true");
    eq(doc.get("flag_f"), false, "core bool false");
    eq(doc.get("char_cp"), new Xun.Tagged("c", "中"), "core char cp");
  }

  static void testExtremeIndentErrors() {
    throwsXun("a:\n   b: 1\n", "3 spaces");
    throwsXun("a:\n\tb: 1\n", "tab indent");
    throwsXun("a:\n    b: 1\n", "indent jump");
    throwsXun("server:\n  host: 1\n  - item1\n", "mix dict/list");
  }

  static void testSymmetricAndUnpack() throws Exception {
    java.time.Instant now = java.time.Instant.parse("2026-08-14T16:54:00Z");
    java.util.UUID u = java.util.UUID.fromString("12345678-1234-5678-1234-567812345678");

    Map<String, Object> data = new LinkedHashMap<>();
    data.put("time", now);
    data.put("uuid", u);
    data.put("size", new Xun.Tagged("sz", "10MiB"));
    data.put("duration", new Xun.Tagged("du", "1h30m"));
    data.put("version", new Xun.Tagged("ver", "3.10.1"));

    String encoded = Xun.encode(data);
    Map<String, Object> decoded = Xun.decode(encoded);

    Xun.Tagged tTime = (Xun.Tagged) decoded.get("time");
    eq(tTime.toInstant(), now, "instant unpack");

    Xun.Tagged tUuid = (Xun.Tagged) decoded.get("uuid");
    eq(tUuid.toUUID(), u, "uuid unpack");

    Xun.Tagged tSz = (Xun.Tagged) decoded.get("size");
    eq(tSz.toBytesSize(), 10485760L, "size bytes unpack");

    Xun.Tagged tDu = (Xun.Tagged) decoded.get("duration");
    eq(tDu.toDuration().getSeconds(), 5400L, "duration unpack");

    Xun.Tagged tVer = (Xun.Tagged) decoded.get("version");
    int[] parts = tVer.toVersionParts();
    eq(parts.length, 3, "version parts length");
    eq(parts[0], 3, "version[0]");
    eq(parts[1], 10, "version[1]");
    eq(parts[2], 1, "version[2]");
  }

  static void testEncodeStripsSurroundingQuotes() {
    Map<String, Object> data = new LinkedHashMap<>();
    data.put("a", "\"hello\"");
    data.put("b", "\"\"");
    data.put("c", "\"!x\"");
    data.put("items", List.of("\"p\"", "\"q\""));
    data.put("keep", new Xun.Tagged("s", "\"keep\""));
    eq(
        Xun.encode(data),
        "a: hello\nb:\nc: !s !x\nitems:\n  - p\n  - q\nkeep: !s \"keep\"\n",
        "strip surrounding quotes");
  }

  static void testEncodeNumericLookingStrings() {
    eq(Xun.encode(Map.of("a", "123")), "a: !s 123\n", "str 123");
    eq(Xun.encode(Map.of("a", "3.10")), "a: !s 3.10\n", "str 3.10");
    eq(Xun.encode(Map.of("a", "-1.5")), "a: !s -1.5\n", "str -1.5");
    eq(Xun.encode(Map.of("a", "1e-3")), "a: !s 1e-3\n", "str 1e-3");
    eq(Xun.encode(Map.of("a", "0xFF")), "a: !s 0xFF\n", "str 0xFF");
    eq(Xun.encode(Map.of("a", "0b10")), "a: !s 0b10\n", "str 0b10");
    eq(Xun.encode(Map.of("a", "0o755")), "a: !s 0o755\n", "str 0o755");
    eq(Xun.encode(Map.of("a", "Infinity")), "a: !s Infinity\n", "str Infinity");
    eq(Xun.encode(Map.of("a", "\"8080\"")), "a: !s 8080\n", "quoted 8080");
    eq(Xun.encode(Map.of("a", "123 ")), "a: !s \"123 \"\n", "str trailing space");
    Map<String, Object> items = new LinkedHashMap<>();
    items.put("items", List.of("80", "443"));
    eq(Xun.encode(items), "items:\n  - !s 80\n  - !s 443\n", "list numeric strings");
    eq(Xun.encode(Map.of("a", 123)), "a: !i 123\n", "int 123");
    eq(Xun.encode(Map.of("a", "hello")), "a: hello\n", "plain hello");
    eq(Xun.encode(Map.of("a", "123abc")), "a: 123abc\n", "123abc");
    eq(Xun.encode(Map.of("a", "1.2.3")), "a: 1.2.3\n", "version-like");
    eq(Xun.parse("a: 123\n").get("a"), "123", "untagged decode stays string");
    eq(Xun.encode(Xun.parse("a: 123\n")), "a: !s 123\n", "re-encode untagged number");
    eq(Xun.parse("a: \"123 \"\n").get("a"), "123 ", "quoted trailing space");
    eq(Xun.encode(Xun.parse("a: \"123 \"\n")), "a: !s \"123 \"\n", "quoted round-trip");
    eq(Xun.parse("a: \"\"\n").get("a"), "", "quoted empty");
  }

  static final String[] MUST_TAG = {
    "0", "00", "012", "08", "8080", "+123", "-123", "-0", "+0",
    "3.10", "3.", ".5", ".0", "0.", "0.0", "00.1", "-.5", "+.5", "+0.0", "-0.0",
    "1e3", "1E-3", "1e+10", "0e0", "1E+0", "1e-0", "+1.5e-10", "5.e2", "+.5e2", "-.5E-1",
    "0xFF", "0Xff", "0x0", "0xabcdef", "0XABCDEF",
    "0b10", "0B10", "0b0", "0b01",
    "0o755", "0O7", "0o0", "0o07",
    "Infinity", "+Infinity", "-Infinity",
    "9007199254740991", "9007199254740993",
    "-0x10", "+0x10", "-0b1", "+0b10", "-0o10",
    " 123"
  };

  static final String[] MUST_NOT_TAG = {
    "hello", "123abc", "abc123", "1.2.3", "3.1.0",
    "1e", "1e+", "e3", "e10", "5.e", ".", "+", "-",
    "0x", "0b", "0o", "0xg", "0xG", "0b2", "0o8", "0x10n", "123n",
    "infinity", "INFINITY", "Inf", "NaN", "true", "false", "null",
    "1_000", "1_2", "0xFF_AA", "127.0.0.1", "2026-08-14", "::1"
  };

  static Map<String, Object> nestEncode(int levels) {
    Map<String, Object> o = new LinkedHashMap<>();
    o.put("v", "leaf");
    for (int i = 0; i < levels; i++) {
      Map<String, Object> wrap = new LinkedHashMap<>();
      wrap.put("c", o);
      o = wrap;
    }
    return o;
  }

  static String nestSource(int levels) {
    StringBuilder sb = new StringBuilder();
    for (int i = 0; i < levels; i++) {
      sb.append("  ".repeat(i)).append("k").append(i).append(":\n");
    }
    sb.append("  ".repeat(levels)).append("v: leaf\n");
    return sb.toString();
  }

  static String quoteGlyphForTest(String s) {
    StringBuilder out = new StringBuilder("\"");
    for (int i = 0; i < s.length(); i++) {
      char ch = s.charAt(i);
      if (ch == '\\') out.append("\\\\");
      else if (ch == '"') out.append("\\\"");
      else out.append(ch);
    }
    return out.append('"').toString();
  }

  static void testExtreme() {
    for (String s : MUST_TAG) {
      boolean quoted = !s.equals(s.trim()) || s.indexOf('"') >= 0 || s.indexOf('\\') >= 0;
      String body = quoted ? quoteGlyphForTest(s) : s;
      String text = Xun.encode(Map.of("a", s));
      eq(text, "a: !s " + body + "\n", "encode " + s);
      eq(Xun.parse(text).get("a"), s, "round-trip " + s);
    }
    for (String s : MUST_NOT_TAG) {
      eq(Xun.encode(Map.of("a", s)), "a: " + s + "\n", "untagged " + s);
    }
    eq(Xun.encode(Map.of("a", "!x")), "a: !s !x\n", "special !x");
    eq(Xun.encode(Map.of("a", "[]")), "a: !s []\n", "special []");
    eq(Xun.encode(Map.of("a", "{}")), "a: !s {}\n", "special {}");
    eq(Xun.encode(Map.of("a", "|foo")), "a: !s |foo\n", "special |");
    eq(Xun.encode(Map.of("a", "\"8080\"")), "a: !s 8080\n", "quoted 8080");
    eq(Xun.encode(Map.of("a", 0)), "a: !i 0\n", "int 0");
    eq(Xun.encode(Map.of("a", 3.14)), "a: !f 3.14\n", "float");
    eq(Xun.encode(Map.of("a", true)), "a: !b true\n", "bool");

    Map<String, Object> data = new LinkedHashMap<>();
    data.put("ports", List.of("80", "443", "8080"));
    data.put("mixed", List.of("1", 1, "x"));
    Map<String, Object> inner = new LinkedHashMap<>();
    inner.put("code", "007");
    Map<String, Object> deep = new LinkedHashMap<>();
    deep.put("inner", inner);
    data.put("deep", deep);
    Map<String, Object> doc = Xun.parse(Xun.encode(data));
    eq(doc.get("ports"), List.of("80", "443", "8080"), "ports");
    @SuppressWarnings("unchecked")
    List<Object> mixed = (List<Object>) doc.get("mixed");
    eq(mixed.get(0), "1", "mixed str");
    eq(mixed.get(1), 1L, "mixed int");

    Map<String, Object> untagged = Xun.parse("a: 123\nb: 3.10\nc: 0xFF\nd: Infinity\ne: true\n");
    eq(untagged.get("a"), "123", "untagged a");
    eq(Xun.encode(untagged), "a: !s 123\nb: !s 3.10\nc: !s 0xFF\nd: !s Infinity\ne: true\n", "re-encode");

    eq(Xun.parse(""), Map.of(), "empty");
    eq(Xun.parse("\uFEFFa: hello\n").get("a"), "hello", "BOM");
    throwsXun("a: ok\0no\n", "NUL");
    throwsXun("x".repeat(1024 * 1024 + 1), "oversize");
    Xun.parse(nestSource(64));
    throwsXun(nestSource(65), "decode depth 65");

    String deepText = Xun.encode(nestEncode(64));
    Map<String, Object> cur = Xun.parse(deepText);
    for (int i = 0; i < 64; i++) {
      @SuppressWarnings("unchecked")
      Map<String, Object> next = (Map<String, Object>) cur.get("c");
      cur = next;
    }
    eq(cur.get("v"), "leaf", "encode depth 64 leaf");
    try {
      Xun.encode(nestEncode(65));
      fail("encode depth 65: expected error");
    } catch (Xun.Error ignored) {
    }
    try {
      Xun.encode(Map.of("", "x"));
      fail("empty key: expected error");
    } catch (Xun.Error ignored) {
    }
    try {
      Xun.encode(Map.of("a: b", "x"));
      fail("colon key: expected error");
    } catch (Xun.Error ignored) {
    }

    String keys = Xun.encode(Map.of("8080", "8080", "3.10", "3.10"));
    if (!keys.contains("8080: !s 8080") || !keys.contains("3.10: !s 3.10")) {
      fail("numeric keys: " + keys);
    }
    eq(Xun.encode(Map.of("a", "123\n456")), "a: |\n  123\n  456\n|\n", "multiline");
    eq(Xun.parse("a: 123\r\nb: x\r\n").get("a"), "123", "CRLF");
    eq(Xun.parse("a: !s 3.10\n").get("a"), "3.10", "explicit !s");
  }

  static void testUnpackNewHelpers() throws Exception {
    Xun.Tagged tz = new Xun.Tagged("tz", "Asia/Shanghai");
    eq(tz.toZoneId(), java.time.ZoneId.of("Asia/Shanghai"), "toZoneId");
    eq(new Xun.Tagged("tz", "+08:00").toZoneId(), java.time.ZoneId.of("+08:00"), "toZoneId offset");
    eq(new Xun.Tagged("tz", "Z").toZoneId(), java.time.ZoneOffset.UTC, "toZoneId Z");

    eq(new Xun.Tagged("c", "A").toChar(), 'A', "toChar plain");
    eq(new Xun.Tagged("c", "U+4E2D").toChar(), '中', "toChar code point");
    try {
      new Xun.Tagged("c", "ab").toChar();
      fail("toChar multi: expected error");
    } catch (Xun.Error ignored) {
    }

    // Encode of native ZoneId / Duration / Character / int[] round-trips to tags.
    Map<String, Object> data = new LinkedHashMap<>();
    data.put("zone", java.time.ZoneId.of("Asia/Shanghai"));
    data.put("dur", java.time.Duration.ofMinutes(90));
    data.put("ch", 'Z');
    data.put("ver", new int[] {3, 10, 1});
    Map<String, Object> parsed = Xun.decode(Xun.encode(data));
    eq(parsed.get("zone"), new Xun.Tagged("tz", "Asia/Shanghai"), "zone encode");
    eq(parsed.get("dur"), new Xun.Tagged("du", "1h30m"), "duration encode");
    eq(parsed.get("ch"), new Xun.Tagged("c", "Z"), "char encode");
    eq(parsed.get("ver"), new Xun.Tagged("ver", "3.10.1"), "version parts encode");
  }

  static void testFileWriteAndRead() throws Exception {
    Map<String, Object> data = new LinkedHashMap<>();
    data.put("app", "java-xun");
    data.put("version", new Xun.Tagged("ver", "0.1.5"));
    data.put("port", 8080L);
    data.put("tags", List.of("jvm", "xun"));
    data.put("raw", new byte[] {(byte) 0x12, (byte) 0x34});
    data.put("text", "Line A\nLine B");

    Path tmp = Files.createTempFile("test_java", ".xun");
    try {
      String encoded = Xun.encode(data);
      Files.writeString(tmp, encoded);

      String content = Files.readString(tmp);
      Map<String, Object> parsed = Xun.parse(content);
      eq(parsed.get("app"), "java-xun", "file app");
      eq(parsed.get("version"), new Xun.Tagged("ver", "0.1.5"), "file ver");
      eq(parsed.get("port"), 8080L, "file port");
      eq(parsed.get("tags"), List.of("jvm", "xun"), "file tags");
      eq(parsed.get("raw"), new byte[] {(byte) 0x12, (byte) 0x34}, "file raw");
      eq(parsed.get("text"), "Line A\nLine B", "file text");
    } finally {
      Files.deleteIfExists(tmp);
    }
  }

  static void eq(Object a, Object b, String msg) {
    if (a instanceof byte[] && b instanceof byte[]) {
      if (!Arrays.equals((byte[]) a, (byte[]) b)) {
        fail(msg + ": " + a + " != " + b);
      }
      return;
    }
    if (a == null ? b != null : !a.equals(b)) fail(msg + ": " + a + " != " + b);
  }

  static void fail(String msg) {
    failed++;
    System.err.println("FAIL " + msg);
  }

  static void testExample(Path root) throws Exception {
    String src = Files.readString(root.resolve("testdata/example.xun"));
    Map<String, Object> doc = Xun.parse(src);
    @SuppressWarnings("unchecked")
    Map<String, Object> server = (Map<String, Object>) doc.get("server");
    eq(server.get("host"), "localhost", "host");
    eq(server.get("port"), 8080L, "port");
    eq(server.get("bind"), new Xun.Tagged("ip", "::1"), "bind");
    @SuppressWarnings("unchecked")
    Map<String, Object> tls = (Map<String, Object>) server.get("tls");
    eq(tls.get("mode"), 0755L, "mode");
    eq(doc.get("features"), List.of("auth", "cache"), "features");
    eq(doc.get("ports"), List.of(80L, 443L, 8080L), "ports");
    eq(doc.get("endpoint"), "https://api.example.com/v2/orders", "endpoint");
    eq(doc.get("tz"), new Xun.Tagged("tz", "Asia/Shanghai"), "tz");
    eq(doc.get("py"), new Xun.Tagged("ver", "3.10"), "py");
    eq(doc.get("color"), new byte[] {(byte) 0xff, 0x00, (byte) 0xaa}, "color");
    eq(doc.get("roles"), List.of("admin", "ops"), "roles");
    eq(doc.get("banner"), "Welcome\nto XUN", "banner");
  }

  static void testEmpty() {
    eq(Xun.parse(""), Map.of(), "empty");
    eq(Xun.parse("# only\n"), Map.of(), "comment only");
  }

  static void testUntyped() {
    Map<String, Object> doc = Xun.parse("a: 8080\nb: true\nc: 3.10\n");
    eq(doc.get("a"), "8080", "untyped a");
    eq(doc.get("b"), "true", "untyped b");
    eq(doc.get("c"), "3.10", "untyped c");
  }

  static void throwsXun(String src, String msg) {
    try {
      Xun.parse(src);
      fail(msg + ": expected error");
    } catch (Xun.Error ignored) {
    }
  }

  static void testDuplicate() {
    throwsXun("a: 1\na: 2\n", "duplicate");
  }

  static void testVersion() {
    eq(Xun.parse("py: !ver 3.10\n").get("py"), new Xun.Tagged("ver", "3.10"), "ver");
  }

  static void testFloatTag() {
    throwsXun("x: !f 8080\n", "float without dot");
  }

  static void testMultilineCloser() {
    throwsXun("a: |\n  hi\n", "unclosed multiline");
  }

  static void testKeyNoSpace() {
    throwsXun("key:value\n", "no space after colon");
  }

  static void testRootList() {
    throwsXun("- a\n- b\n", "root list");
  }

  static void testLiteralS() {
    eq(Xun.parse("a: !s !important\n").get("a"), "!important", "literal s");
  }

  static void testCompactChars() {
    eq(
        Xun.parse("v: !c[a, e, i]\n").get("v"),
        List.of(new Xun.Tagged("c", "a"), new Xun.Tagged("c", "e"), new Xun.Tagged("c", "i")),
        "compact c");
  }

  static void testStringCompact() {
    throwsXun("v: !s[a, b]\n", "string compact");
  }

  static void testEncodeAndRoundTrip() {
    Map<String, Object> data = new LinkedHashMap<>();
    Map<String, Object> server = new LinkedHashMap<>();
    server.put("host", "localhost");
    server.put("port", 8080L);
    data.put("server", server);
    data.put("empty_dict", new LinkedHashMap<>());
    data.put("empty_list", List.of());
    data.put("features", List.of("auth", "cache"));
    data.put("banner", "Welcome\nto XUN");
    data.put("flag", true);
    data.put("color", new byte[] {(byte) 0xde, (byte) 0xad, (byte) 0xbe, (byte) 0xef});
    data.put("py", new Xun.Tagged("ver", "3.10"));

    String encoded = Xun.encode(data);
    Map<String, Object> parsed = Xun.parse(encoded);
    eq(parsed.get("banner"), "Welcome\nto XUN", "roundtrip banner");
    eq(parsed.get("flag"), true, "roundtrip flag");
    eq(parsed.get("color"), new byte[] {(byte) 0xde, (byte) 0xad, (byte) 0xbe, (byte) 0xef}, "roundtrip color");
    eq(parsed.get("py"), new Xun.Tagged("ver", "3.10"), "roundtrip py");
  }
}
