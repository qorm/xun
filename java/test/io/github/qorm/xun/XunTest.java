package io.github.qorm.xun;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
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
    if (failed > 0) {
      System.err.println(failed + " failed");
      System.exit(1);
    }
    System.out.println("ok");
  }

  static void eq(Object a, Object b, String msg) {
    if (a instanceof byte[] ba && b instanceof byte[] bb) {
      if (!Arrays.equals(ba, bb)) {
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
    eq(Xun.parse("$api: https://x.test\na: !s $api\n").get("a"), "$api", "literal $");
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
}
