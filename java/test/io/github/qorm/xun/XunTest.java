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
    testEncodeKeepsLiteralQuotes();
    testEncodeNumericLookingStrings();
    testExtreme();
    testUnpackNewHelpers();
    testFileWriteAndRead();
    testSymmetricAndUnpack();
    testUnicodeAndChinese();
    testFullCoreTags();
    testExtremeIndentErrors();
    testDurationMs();
    testOxUnixTagged();
    testTrailingComments();
    testQuotedKeysAndValidation();
    testInlineDictEntries();
    testMultilineChomp();
    testStructureLikeStringsQuoted();
    // RFC-0001
    testRfc0001BareEndClosesTopLevelDict();
    testRfc0001BareEndClosesNestedDict();
    testRfc0001EndWithKeyClosesNamedNestedDict();
    testRfc0001BareEndClosesListBlock();
    testRfc0001BareEndEquivalentToDedent();
    testRfc0001EndKeyMismatchThrows();
    testRfc0001BareEndOnRootAllowed();
    testRfc0001EndAsDictKeyAllowed();
    testRfc0001EndInsideMultilineBlockIsLiteral();
    testRfc0001DeeplyNestedEndChains();
    testRfc0001EndAllowsSiblingContentAfter();
    testRfc0001RootEndWithComplexDict();
    // RFC-0002
    testRfc0002InlineObjectAsListItem();
    testRfc0002InlineObjectWithQuotedValue();
    testRfc0002InlineObjectWithTaggedValues();
    testRfc0002InlineObjectWithEmptyValue();
    testRfc0002CompactArrayOfInlineObjectsNoTag();
    testRfc0002CompactTaggedArrayOfInlineObjects();
    testRfc0002MixBlockAndInlineListItems();
    testRfc0002EmptyInlineObject();
    testRfc0002InlineObjectInNestedBlock();
    testRfc0002DuplicateKeysInInlineObjectThrows();
    testRfc0002MalformedInlineObjectThrows();
    testRfc0002InlineObjectPreservesKeyOrder();
    testRfc0002NestedCompactArrayInInlineObject();
    testRfc0002TraditionalBlockListRegression();
    testRfc0002TraditionalCompactArrayRegression();
    testRfc0002EndAsInlineObjectValue();
    testRfc0002InlineObjectWithComplexTaggedValues();
    if (failed > 0) {
      System.err.println(failed + " failed");
      System.exit(1);
    }
    System.out.println("ok");
  }

  static void testDurationMs() {
    eq(Xun.parseDuration("500ms"), 0.5, "500ms");
    eq(Xun.parseDuration("15s500ms"), 15.5, "15s500ms");
    eq(Xun.parseDuration("1d2h30m15s"), 95415.0, "1d2h30m15s");
    eq(Xun.parseDuration("1m"), 60.0, "1m");
    try {
      Xun.parseDuration("90 minutes");
      fail("90 minutes: expected error");
    } catch (Xun.Error ignored) {
    }
    eq(Xun.decode("x: !du 500ms").get("x"), new Xun.Tagged("du", "500ms"), "du 500ms tag");
    @SuppressWarnings("unchecked")
    Map<String, Object> u = (Map<String, Object>) Xun.unpack(Xun.decode("x: !du 500ms"));
    eq(u.get("x"), 0.5, "du unpack seconds");
  }

  static void testOxUnixTagged() {
    eq(Xun.decode("mode: !o 755").get("mode"), new Xun.Tagged("o", "755"), "o tagged");
    eq(Xun.decode("h: !x DEAD_BEEF").get("h"), new Xun.Tagged("x", "DEAD_BEEF"), "x tagged");
    eq(Xun.decode("u: !unix 1692000000").get("u"), new Xun.Tagged("unix", "1692000000"), "unix tagged");
    eq(Xun.encode(Xun.decode("mode: !o 755")), "mode: !o 755\n", "o encode");
    eq(Xun.encode(Xun.decode("h: !x DEAD_BEEF")), "h: !x DEAD_BEEF\n", "x encode");
    eq(Xun.encode(Xun.decode("u: !unix 1692000000")), "u: !unix 1692000000\n", "unix encode");
    @SuppressWarnings("unchecked")
    Map<String, Object> nativeVals =
        (Map<String, Object>)
            Xun.unpack(Xun.decode("mode: !o 755\nh: !x DEAD_BEEF\nu: !unix 1692000000\n"));
    eq(nativeVals.get("mode"), 493L, "unpack o");
    eq(nativeVals.get("h"), 0xDEADBEEFL, "unpack x");
    eq(nativeVals.get("u"), 1692000000L, "unpack unix");
    eq(new Xun.Tagged("o", "755").toNumber(), 493L, "toNumber o");
    eq(new Xun.Tagged("x", "DEAD_BEEF").toNumber(), 0xDEADBEEFL, "toNumber x");
    eq(new Xun.Tagged("unix", "1692000000").toNumber(), 1692000000L, "toNumber unix");
  }

  static void testTrailingComments() {
    eq(Xun.decode("port: 8080 # listen").get("port"), "8080", "comment after number");
    eq(Xun.decode("a: foo # bar").get("a"), "foo", "comment after text");
    eq(Xun.decode("a: foo#bar").get("a"), "foo#bar", "hash inside value");
    eq(Xun.decode("a: \"foo # bar\"").get("a"), "foo # bar", "hash inside quotes");
    eq(Xun.decode("a: # only comment").get("a"), "", "value is comment only");
    eq(Xun.decode("list:\n  - x # c\n  - y").get("list"), List.of("x", "y"), "list item comments");
    eq(Xun.decode("t: |\n  keep # me\n|").get("t"), "keep # me", "multiline keeps hash");
    Map<String, Object> doc = Xun.decode("a: 1 # c\n# full line\nb: 2");
    eq(doc.get("a"), "1", "a with trailing comment");
    eq(doc.get("b"), "2", "b after comment line");
  }

  static void testQuotedKeysAndValidation() {
    eq(Xun.decode("\"my key\": 1"), Map.of("my key", "1"), "quoted key");
    eq(Xun.decode("\"a: b\": 1\nc: 2"), Map.of("a: b", "1", "c", "2"), "quoted key with colon");
    eq(Xun.decode("\"my key\":"), Map.of("my key", ""), "quoted key empty value");
    throwsXun("a:: 1", "key ends with colon");
    eq(Xun.decode("a:b: 1"), Map.of("a:b", "1"), "colon without space in key");
  }

  static void testInlineDictEntries() {
    eq(
        Xun.decode("a:\n  - x: 1\n  - y: 2"),
        Map.of("a", List.of(Map.of("x", "1"), Map.of("y", "2"))),
        "inline dicts");
    eq(
        Xun.decode("a:\n  - x: 1\n    y: 2\n  - z: 3"),
        Map.of("a", List.of(Map.of("x", "1", "y", "2"), Map.of("z", "3"))),
        "inline dict continuations");
    eq(
        Xun.decode("a:\n  - x: 1\n  - simple"),
        Map.of("a", List.of(Map.of("x", "1"), "simple")),
        "mixed list");
    throwsXun("a: !n[]\n  - x: 1\n", "typed array rejects map");
  }

  static void testMultilineChomp() {
    eq(Xun.decode("a: |\n  x\n|").get("a"), "x", "exact");
    eq(Xun.decode("a: |-\n  x\n\n|").get("a"), "x", "strip blank");
    eq(Xun.decode("a: |-\n  x\n  y\n|").get("a"), "x\ny", "strip");
    eq(Xun.decode("a: |+\n  x\n|").get("a"), "x\n", "clip");
    eq(Xun.decode("a: |\n  x\n\n|").get("a"), "x\n", "exact keeps blank");
    eq(Xun.decode("a: |MD-\n  x\n\nMD").get("a"), "x", "custom closer strip");
    eq(Xun.decode("a: |MD\n  x\nMD").get("a"), "x", "custom closer exact");
    eq(Xun.decode("t: |\n  a # not comment\n| # comment").get("t"), "a # not comment", "closer comment");
  }

  static void testStructureLikeStringsQuoted() {
    eq(Xun.encode(Map.of("a", "hello: world")), "a: \"hello: world\"\n", "quote colon-space");
    eq(Xun.decode(Xun.encode(Map.of("a", "hello: world"))).get("a"), "hello: world", "rt colon-space");
    Map<String, Object> items = new LinkedHashMap<>();
    items.put("items", List.of("a: b"));
    eq(Xun.encode(items), "items:\n  - \"a: b\"\n", "list item quoted");
    eq(
        Xun.decode(Xun.encode(items)),
        Map.of("items", List.of("a: b")),
        "list item rt");
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
    eq(doc.get("num_hex"), new Xun.Tagged("x", "DEAD_BEEF"), "core hex");
    eq(doc.get("num_oct"), new Xun.Tagged("o", "755"), "core oct");
    eq(doc.get("unix_v"), new Xun.Tagged("unix", "1700000000"), "core unix");
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

  static void testEncodeKeepsLiteralQuotes() {
    String s = "\"hello\"";
    eq(Xun.encode(Map.of("a", s)), "a: \"\\\"hello\\\"\"\n", "encode literal quotes");
    eq(Xun.decode(Xun.encode(Map.of("a", s))).get("a"), s, "round-trip literal quotes");
    eq(Xun.decode(Xun.encode(Map.of("a", "\"\""))).get("a"), "\"\"", "round-trip empty quotes");
    eq(Xun.decode(Xun.encode(Map.of("a", "\"!x\""))).get("a"), "\"!x\"", "round-trip quoted tag");
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
    eq(Xun.encode(Map.of("a", "\"8080\"")), "a: \"\\\"8080\\\"\"\n", "quoted 8080 kept");
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
    eq(Xun.encode(Map.of("a", "|foo")), "a: !s \"|foo\"\n", "special |");
    eq(Xun.encode(Map.of("a", "\"8080\"")), "a: \"\\\"8080\\\"\"\n", "quoted 8080 kept");
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
    eq(tls.get("mode"), new Xun.Tagged("o", "755"), "mode");
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

  // === RFC-0001: optional 'end' block delimiter ===

  static void testRfc0001BareEndClosesTopLevelDict() {
    String src = "server:\n  host: localhost\n  port: 8080\nend\n";
    Map<String, Object> doc = Xun.parse(src);
    @SuppressWarnings("unchecked")
    Map<String, Object> server = (Map<String, Object>) doc.get("server");
    eq(server.get("host"), "localhost", "rfc0001 bare end top host");
    eq(server.get("port"), "8080", "rfc0001 bare end top port");
  }

  static void testRfc0001BareEndClosesNestedDict() {
    String src = "server:\n  host: localhost\n  tls:\n    cert: /etc/ssl/cert.pem\n    mode: 755\n  end\n  port: 8080\nend\n";
    Map<String, Object> doc = Xun.parse(src);
    @SuppressWarnings("unchecked")
    Map<String, Object> server = (Map<String, Object>) doc.get("server");
    @SuppressWarnings("unchecked")
    Map<String, Object> tls = (Map<String, Object>) server.get("tls");
    eq(tls.get("cert"), "/etc/ssl/cert.pem", "rfc0001 nested tls cert");
    eq(tls.get("mode"), "755", "rfc0001 nested tls mode");
    eq(server.get("port"), "8080", "rfc0001 nested server port");
  }

  static void testRfc0001EndWithKeyClosesNamedNestedDict() {
    String src = "server:\n  host: localhost\n  tls:\n    cert: /etc/ssl/cert.pem\n  end tls\n  port: 8080\nend server\n";
    Map<String, Object> doc = Xun.parse(src);
    @SuppressWarnings("unchecked")
    Map<String, Object> server = (Map<String, Object>) doc.get("server");
    eq(server.get("host"), "localhost", "rfc0001 end-key host");
    @SuppressWarnings("unchecked")
    Map<String, Object> tls = (Map<String, Object>) server.get("tls");
    eq(tls.get("cert"), "/etc/ssl/cert.pem", "rfc0001 end-key tls cert");
    eq(server.get("port"), "8080", "rfc0001 end-key server port");
  }

  static void testRfc0001BareEndClosesListBlock() {
    String src = "servers:\n  - host: a\n    port: 80\n  - host: b\n    port: 81\nend\n";
    Map<String, Object> doc = Xun.parse(src);
    @SuppressWarnings("unchecked")
    List<Object> servers = (List<Object>) doc.get("servers");
    eq(servers.size(), 2, "rfc0001 list end length");
    @SuppressWarnings("unchecked")
    Map<String, Object> s0 = (Map<String, Object>) servers.get(0);
    eq(s0.get("host"), "a", "rfc0001 list end s0 host");
    @SuppressWarnings("unchecked")
    Map<String, Object> s1 = (Map<String, Object>) servers.get(1);
    eq(s1.get("host"), "b", "rfc0001 list end s1 host");
  }

  static void testRfc0001BareEndEquivalentToDedent() {
    String withEnd = "a: 1\nb: 2\nend\n";
    String withoutEnd = "a: 1\nb: 2\n";
    Map<String, Object> d1 = Xun.parse(withEnd);
    Map<String, Object> d2 = Xun.parse(withoutEnd);
    eq(d1, d2, "rfc0001 bare end equivalent");
  }

  static void testRfc0001EndKeyMismatchThrows() {
    String src = "server:\n  host: localhost\n  port: 8080\nend tls\n";
    throwsXun(src, "end-key mismatch");
  }

  static void testRfc0001BareEndOnRootAllowed() {
    String src = "a: 1\nend\n";
    Map<String, Object> doc = Xun.parse(src);
    eq(doc.get("a"), "1", "rfc0001 bare end root");
  }

  static void testRfc0001EndAsDictKeyAllowed() {
    // 'end' as a dict key is just a regular identifier, not a delimiter.
    String src = "end: 1\nend2: 2\n";
    Map<String, Object> doc = Xun.parse(src);
    eq(doc.get("end"), "1", "rfc0001 end as key");
    eq(doc.get("end2"), "2", "rfc0001 end2 as key");
  }

  static void testRfc0001EndInsideMultilineBlockIsLiteral() {
    String src = "script: |\n  echo \"end of script\"\n|\n";
    Map<String, Object> doc = Xun.parse(src);
    eq(doc.get("script"), "echo \"end of script\"", "rfc0001 end literal in multiline");
  }

  static void testRfc0001DeeplyNestedEndChains() {
    String src = "a:\n  b:\n    c:\n      d: 1\n    end c\n  end b\nend a\ne: 2\n";
    Map<String, Object> doc = Xun.parse(src);
    @SuppressWarnings("unchecked")
    Map<String, Object> a = (Map<String, Object>) doc.get("a");
    @SuppressWarnings("unchecked")
    Map<String, Object> b = (Map<String, Object>) a.get("b");
    @SuppressWarnings("unchecked")
    Map<String, Object> c = (Map<String, Object>) b.get("c");
    eq(c.get("d"), "1", "rfc0001 deep nested d");
    eq(doc.get("e"), "2", "rfc0001 deep nested e");
  }

  static void testRfc0001EndAllowsSiblingContentAfter() {
    String src = "server:\n  host: a\nend server\nproxy:\n  host: b\nend proxy\n";
    Map<String, Object> doc = Xun.parse(src);
    @SuppressWarnings("unchecked")
    Map<String, Object> server = (Map<String, Object>) doc.get("server");
    @SuppressWarnings("unchecked")
    Map<String, Object> proxy = (Map<String, Object>) doc.get("proxy");
    eq(server.get("host"), "a", "rfc0001 sibling server");
    eq(proxy.get("host"), "b", "rfc0001 sibling proxy");
  }

  static void testRfc0001RootEndWithComplexDict() {
    String src = "name: cfg\nitems:\n  - one\n  - two\nflags:\n  debug: true\n  verbose: false\nend\n";
    Map<String, Object> doc = Xun.parse(src);
    eq(doc.get("name"), "cfg", "rfc0001 complex name");
    @SuppressWarnings("unchecked")
    List<Object> items = (List<Object>) doc.get("items");
    eq(items.get(0), "one", "rfc0001 complex items[0]");
    eq(items.get(1), "two", "rfc0001 complex items[1]");
    @SuppressWarnings("unchecked")
    Map<String, Object> flags = (Map<String, Object>) doc.get("flags");
    eq(flags.get("debug"), "true", "rfc0001 complex flags.debug");
    eq(flags.get("verbose"), "false", "rfc0001 complex flags.verbose");
  }

  // === RFC-0002: inline object / array literals ===

  static void testRfc0002InlineObjectAsListItem() {
    String src = "servers:\n  - {host: a, port: 80}\n  - {host: b, port: 81}\n";
    Map<String, Object> doc = Xun.parse(src);
    @SuppressWarnings("unchecked")
    List<Object> servers = (List<Object>) doc.get("servers");
    eq(servers.size(), 2, "rfc0002 inline len");
    @SuppressWarnings("unchecked")
    Map<String, Object> s0 = (Map<String, Object>) servers.get(0);
    eq(s0.get("host"), "a", "rfc0002 inline s0 host");
    eq(s0.get("port"), "80", "rfc0002 inline s0 port");
    @SuppressWarnings("unchecked")
    Map<String, Object> s1 = (Map<String, Object>) servers.get(1);
    eq(s1.get("host"), "b", "rfc0002 inline s1 host");
    eq(s1.get("port"), "81", "rfc0002 inline s1 port");
  }

  static void testRfc0002InlineObjectWithQuotedValue() {
    String src = "servers:\n  - {host: \"my host\", port: 80}\n";
    Map<String, Object> doc = Xun.parse(src);
    @SuppressWarnings("unchecked")
    List<Object> servers = (List<Object>) doc.get("servers");
    @SuppressWarnings("unchecked")
    Map<String, Object> s0 = (Map<String, Object>) servers.get(0);
    eq(s0.get("host"), "my host", "rfc0002 quoted host");
    eq(s0.get("port"), "80", "rfc0002 quoted port");
  }

  static void testRfc0002InlineObjectWithTaggedValues() {
    String src = "cfg:\n  - {port: !n 8080, mode: !o 755, color: !xb FF00AA}\n";
    Map<String, Object> doc = Xun.parse(src);
    @SuppressWarnings("unchecked")
    List<Object> cfg = (List<Object>) doc.get("cfg");
    @SuppressWarnings("unchecked")
    Map<String, Object> item = (Map<String, Object>) cfg.get(0);
    eq(item.get("port"), 8080L, "rfc0002 tagged !n port");
    eq(item.get("mode"), new Xun.Tagged("o", "755"), "rfc0002 tagged !o mode");
    eq(item.get("color"), new byte[] {(byte) 0xff, 0x00, (byte) 0xaa}, "rfc0002 tagged !xb color");
  }

  static void testRfc0002InlineObjectWithEmptyValue() {
    String src = "cfg:\n  - {name: \"\"}\n  - {empty:}\n";
    Map<String, Object> doc = Xun.parse(src);
    @SuppressWarnings("unchecked")
    List<Object> cfg = (List<Object>) doc.get("cfg");
    @SuppressWarnings("unchecked")
    Map<String, Object> i0 = (Map<String, Object>) cfg.get(0);
    @SuppressWarnings("unchecked")
    Map<String, Object> i1 = (Map<String, Object>) cfg.get(1);
    eq(i0.get("name"), "", "rfc0002 empty quoted name");
    eq(i1.get("empty"), "", "rfc0002 trailing colon empty");
  }

  static void testRfc0002CompactArrayOfInlineObjectsNoTag() {
    String src = "peers: [{host: a, port: 80}, {host: b, port: 81}]\n";
    Map<String, Object> doc = Xun.parse(src);
    @SuppressWarnings("unchecked")
    List<Object> peers = (List<Object>) doc.get("peers");
    eq(peers.size(), 2, "rfc0002 compact peers len");
    @SuppressWarnings("unchecked")
    Map<String, Object> p0 = (Map<String, Object>) peers.get(0);
    eq(p0.get("host"), "a", "rfc0002 compact peers[0]");
    @SuppressWarnings("unchecked")
    Map<String, Object> p1 = (Map<String, Object>) peers.get(1);
    eq(p1.get("host"), "b", "rfc0002 compact peers[1]");
  }

  static void testRfc0002CompactTaggedArrayOfInlineObjects() {
    // Mixed: one inline object + one scalar in a numeric array.
    // Inline objects in compact arrays must have a tag prefix? Let's check.
    String src = "items: !o[{a: 1, b: 2}, {c: 3}]\n";
    Map<String, Object> doc = Xun.parse(src);
    @SuppressWarnings("unchecked")
    List<Object> items = (List<Object>) doc.get("items");
    eq(items.size(), 2, "rfc0002 tagged compact len");
    @SuppressWarnings("unchecked")
    Map<String, Object> i0 = (Map<String, Object>) items.get(0);
    eq(i0.get("a"), "1", "rfc0002 tagged compact i0.a");
    eq(i0.get("b"), "2", "rfc0002 tagged compact i0.b");
    @SuppressWarnings("unchecked")
    Map<String, Object> i1 = (Map<String, Object>) items.get(1);
    eq(i1.get("c"), "3", "rfc0002 tagged compact i1.c");
  }

  static void testRfc0002MixBlockAndInlineListItems() {
    String src = "servers:\n  - {host: a, port: 80}\n  - host: b\n    port: 81\n    tls: enabled\n";
    Map<String, Object> doc = Xun.parse(src);
    @SuppressWarnings("unchecked")
    List<Object> servers = (List<Object>) doc.get("servers");
    eq(servers.size(), 2, "rfc0002 mix len");
    @SuppressWarnings("unchecked")
    Map<String, Object> s0 = (Map<String, Object>) servers.get(0);
    eq(s0.get("host"), "a", "rfc0002 mix s0 host");
    @SuppressWarnings("unchecked")
    Map<String, Object> s1 = (Map<String, Object>) servers.get(1);
    eq(s1.get("host"), "b", "rfc0002 mix s1 host");
    eq(s1.get("tls"), "enabled", "rfc0002 mix s1 tls");
  }

  static void testRfc0002EmptyInlineObject() {
    String src = "cfg:\n  - {}\n";
    Map<String, Object> doc = Xun.parse(src);
    @SuppressWarnings("unchecked")
    List<Object> cfg = (List<Object>) doc.get("cfg");
    @SuppressWarnings("unchecked")
    Map<String, Object> empty = (Map<String, Object>) cfg.get(0);
    eq(empty.size(), 0, "rfc0002 empty inline obj size");
  }

  static void testRfc0002InlineObjectInNestedBlock() {
    String src = "data:\n  items:\n    - {id: 1, name: alice}\n    - {id: 2, name: bob}\n";
    Map<String, Object> doc = Xun.parse(src);
    @SuppressWarnings("unchecked")
    Map<String, Object> data = (Map<String, Object>) doc.get("data");
    @SuppressWarnings("unchecked")
    List<Object> items = (List<Object>) data.get("items");
    eq(items.size(), 2, "rfc0002 nested inline len");
    @SuppressWarnings("unchecked")
    Map<String, Object> i0 = (Map<String, Object>) items.get(0);
    eq(i0.get("id"), "1", "rfc0002 nested inline i0.id");
    eq(i0.get("name"), "alice", "rfc0002 nested inline i0.name");
    @SuppressWarnings("unchecked")
    Map<String, Object> i1 = (Map<String, Object>) items.get(1);
    eq(i1.get("id"), "2", "rfc0002 nested inline i1.id");
    eq(i1.get("name"), "bob", "rfc0002 nested inline i1.name");
  }

  static void testRfc0002DuplicateKeysInInlineObjectThrows() {
    String src = "cfg:\n  - {a: 1, a: 2}\n";
    throwsXun(src, "duplicate key 'a'");
  }

  static void testRfc0002MalformedInlineObjectThrows() {
    String src = "cfg:\n  - {a, b: 2}\n";
    throwsXun(src, "malformed inline object");
  }

  static void testRfc0002InlineObjectPreservesKeyOrder() {
    String src = "cfg:\n  - {z: 1, a: 2, m: 3}\n";
    Map<String, Object> doc = Xun.parse(src);
    @SuppressWarnings("unchecked")
    List<Object> cfg = (List<Object>) doc.get("cfg");
    @SuppressWarnings("unchecked")
    Map<String, Object> item = (Map<String, Object>) cfg.get(0);
    // LinkedHashMap preserves insertion order — first key must be 'z'.
    java.util.Set<String> keys = item.keySet();
    String first = keys.iterator().next();
    eq(first, "z", "rfc0002 inline key order first");
    eq(item.size(), 3, "rfc0002 inline key count");
    eq(item.get("a"), "2", "rfc0002 inline key a");
    eq(item.get("m"), "3", "rfc0002 inline key m");
  }

  static void testRfc0002NestedCompactArrayInInlineObject() {
    String src = "cfg:\n  - {tags: !s[a, b, c], port: !n 80}\n";
    Map<String, Object> doc = Xun.parse(src);
    @SuppressWarnings("unchecked")
    List<Object> cfg = (List<Object>) doc.get("cfg");
    @SuppressWarnings("unchecked")
    Map<String, Object> item = (Map<String, Object>) cfg.get(0);
    @SuppressWarnings("unchecked")
    List<Object> tags = (List<Object>) item.get("tags");
    eq(tags.size(), 3, "rfc0002 nested compact array len");
    eq(tags.get(0), "a", "rfc0002 nested compact array tags[0]");
    eq(tags.get(1), "b", "rfc0002 nested compact array tags[1]");
    eq(tags.get(2), "c", "rfc0002 nested compact array tags[2]");
    eq(item.get("port"), 80L, "rfc0002 nested compact array port");
  }

  static void testRfc0002TraditionalBlockListRegression() {
    String src = "items:\n  - one\n  - two\n";
    Map<String, Object> doc = Xun.parse(src);
    @SuppressWarnings("unchecked")
    List<Object> items = (List<Object>) doc.get("items");
    eq(items, List.of("one", "two"), "rfc0002 regression block list");
  }

  static void testRfc0002TraditionalCompactArrayRegression() {
    String src = "ports: !n[80, 443, 8080]\n";
    Map<String, Object> doc = Xun.parse(src);
    @SuppressWarnings("unchecked")
    List<Object> ports = (List<Object>) doc.get("ports");
    eq(ports.size(), 3, "rfc0002 regression compact len");
    eq(ports.get(0), 80L, "rfc0002 regression compact[0]");
    eq(ports.get(1), 443L, "rfc0002 regression compact[1]");
    eq(ports.get(2), 8080L, "rfc0002 regression compact[2]");
  }

  static void testRfc0002EndAsInlineObjectValue() {
    // 'end' as a key inside { ... } must not be confused with block-close.
    String src = "cfg:\n  - {end: foo, value: 1}\n";
    Map<String, Object> doc = Xun.parse(src);
    @SuppressWarnings("unchecked")
    List<Object> cfg = (List<Object>) doc.get("cfg");
    @SuppressWarnings("unchecked")
    Map<String, Object> item = (Map<String, Object>) cfg.get(0);
    eq(item.get("end"), "foo", "rfc0002 end as key");
    eq(item.get("value"), "1", "rfc0002 end as key value");
  }

  static void testRfc0002InlineObjectWithComplexTaggedValues() {
    String src = "cfg:\n  - {hex: !x FF, bytes: !xb AABB, when: !dt 2024-01-15T10:00:00Z}\n";
    Map<String, Object> doc = Xun.parse(src);
    @SuppressWarnings("unchecked")
    List<Object> cfg = (List<Object>) doc.get("cfg");
    @SuppressWarnings("unchecked")
    Map<String, Object> item = (Map<String, Object>) cfg.get(0);
    // For !x, applyTag returns Tagged("x", "FF"), not the parsed number
    eq(item.get("hex"), new Xun.Tagged("x", "FF"), "rfc0002 complex !x");
    eq(item.get("bytes"), new byte[] {(byte) 0xaa, (byte) 0xbb}, "rfc0002 complex !xb");
    eq(item.get("when"), new Xun.Tagged("dt", "2024-01-15T10:00:00Z"), "rfc0002 complex !dt");
  }
}
