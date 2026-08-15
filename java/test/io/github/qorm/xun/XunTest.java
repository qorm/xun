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
    testFileWriteAndRead();
    testSymmetricAndUnpack();
    if (failed > 0) {
      System.err.println(failed + " failed");
      System.exit(1);
    }
    System.out.println("ok");
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

  static void testFileWriteAndRead() throws Exception {
    Map<String, Object> data = new LinkedHashMap<>();
    data.put("app", "java-xun");
    data.put("version", new Xun.Tagged("ver", "0.1.2"));
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
      eq(parsed.get("version"), new Xun.Tagged("ver", "0.1.2"), "file ver");
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
