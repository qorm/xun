package io.github.qorm.xun;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Base64;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * XUN (X Unquoted Notation) parser.
 * Root value is always a dictionary.
 */
public final class Xun {
  private static final int MAX_BYTES = 1024 * 1024;
  private static final int MAX_DEPTH = 64;

  private Xun() {}

  public static final class Tagged {
    public final String tag;
    public final String value;

    public Tagged(String tag, String value) {
      this.tag = tag;
      this.value = value;
    }

    @Override
    public boolean equals(Object o) {
      if (!(o instanceof Tagged t)) return false;
      return tag.equals(t.tag) && value.equals(t.value);
    }

    @Override
    public int hashCode() {
      return tag.hashCode() * 31 + value.hashCode();
    }

    @Override
    public String toString() {
      return "!" + tag + " " + value;
    }
  }

  public static final class Error extends RuntimeException {
    public final int line;

    public Error(String message) {
      this(message, 0);
    }

    public Error(String message, int line) {
      super(line == 0 ? message : "line " + line + ": " + message);
      this.line = line;
    }
  }

  public static Map<String, Object> parse(String source) {
    if (source == null) throw new Error("source must be a string");
    if (source.getBytes(StandardCharsets.UTF_8).length > MAX_BYTES) {
      throw new Error("document exceeds 1MB");
    }
    if (source.indexOf('\0') >= 0) throw new Error("NUL is not allowed");
    if (!source.isEmpty() && source.charAt(0) == '\uFEFF') source = source.substring(1);
    return new Parser(splitLines(source)).parseDocument();
  }

  private static final class Line {
    final String raw;
    final int indent;
    final String text;
    final int n;
    final boolean blank;

    Line(String raw, int indent, String text, int n, boolean blank) {
      this.raw = raw;
      this.indent = indent;
      this.text = text;
      this.n = n;
      this.blank = blank;
    }
  }

  private static List<Line> splitLines(String source) {
    List<Line> out = new ArrayList<>();
    if (source.isEmpty()) return out;
    int n = 1;
    int start = 0;
    int i = 0;
    int len = source.length();
    while (i <= len) {
      boolean atEnd = i == len;
      char c = atEnd ? 0 : source.charAt(i);
      if (!atEnd && c != '\n' && c != '\r') {
        i++;
        continue;
      }
      String raw = source.substring(start, i);
      if (c == '\r' && i + 1 < len && source.charAt(i + 1) == '\n') i++;
      out.add(makeLine(raw, n));
      n++;
      i++;
      start = i;
    }
    return out;
  }

  private static Line makeLine(String raw, int n) {
    int i = 0;
    while (i < raw.length() && raw.charAt(i) == ' ') i++;
    if (i < raw.length() && raw.charAt(i) == '\t') throw new Error("tab is not allowed", n);
    if (i % 2 != 0) throw new Error("indent must be a multiple of 2", n);
    String text = rstripSpaceTab(raw.substring(i));
    return new Line(raw, i, text, n, text.isEmpty());
  }

  private static String rstripSpaceTab(String s) {
    int i = s.length();
    while (i > 0) {
      char c = s.charAt(i - 1);
      if (c != ' ' && c != '\t') break;
      i--;
    }
    return s.substring(0, i);
  }

  private static final class Parser {
    final List<Line> lines;
    int i;
    final Map<String, Object> env = new LinkedHashMap<>();
    final java.util.HashSet<String> resolving = new java.util.HashSet<>();

    Parser(List<Line> lines) {
      this.lines = lines;
    }

    Line peek() {
      return i < lines.size() ? lines.get(i) : null;
    }

    void skipNoise() {
      while (peek() != null) {
        Line l = peek();
        if (l.blank || l.text.startsWith("#")) i++;
        else break;
      }
    }

    Map<String, Object> parseDocument() {
      parseVars();
      skipNoise();
      if (peek() == null) return new LinkedHashMap<>();
      Line first = peek();
      if (first.indent != 0) throw new Error("document must start at indent 0", first.n);
      if (isListItem(first)) throw new Error("root must be a dictionary", first.n);
      return parseDict(0, 0);
    }

    void parseVars() {
      Pattern def = Pattern.compile("^\\$([A-Za-z_][A-Za-z0-9_]*):(.*)$");
      while (peek() != null) {
        Line l = peek();
        if (l.blank || l.text.startsWith("#")) {
          i++;
          continue;
        }
        if (l.indent != 0 || !l.text.startsWith("$")) break;
        Matcher m = def.matcher(l.text);
        if (!m.matches()) throw new Error("invalid variable definition", l.n);
        String after = m.group(2);
        if (!after.isEmpty() && !after.startsWith(" ")) {
          throw new Error("expected ': ' in variable definition", l.n);
        }
        String name = m.group(1);
        if (env.containsKey(name)) throw new Error("duplicate variable $" + name, l.n);
        i++;
        String raw = after.startsWith(" ") ? after.substring(1) : "";
        env.put(name, parseValue(raw, 0, l.n, 1));
      }
    }

    Map<String, Object> parseDict(int indent, int depth) {
      if (depth > MAX_DEPTH) throw new Error("nesting exceeds 64", peek() == null ? 0 : peek().n);
      Map<String, Object> obj = new LinkedHashMap<>();
      while (peek() != null) {
        skipNoise();
        Line l = peek();
        if (l == null || l.blank) break;
        if (l.indent < indent) break;
        if (l.indent > indent) throw new Error("invalid indent jump", l.n);
        if (l.text.startsWith("$") && indent == 0) {
          throw new Error("variable definitions only allowed at file start", l.n);
        }
        if (isListItem(l)) throw new Error("cannot mix list items into a dictionary", l.n);
        String[] kr = splitKey(l.text, l.n);
        if (obj.containsKey(kr[0])) throw new Error("duplicate key '" + kr[0] + "'", l.n);
        i++;
        obj.put(kr[0], parseValue(kr[1], indent, l.n, depth + 1));
      }
      return obj;
    }

    List<Object> parseList(int indent, int depth, String itemTag) {
      if (depth > MAX_DEPTH) throw new Error("nesting exceeds 64", peek() == null ? 0 : peek().n);
      List<Object> arr = new ArrayList<>();
      while (peek() != null) {
        skipNoise();
        Line l = peek();
        if (l == null || l.blank) break;
        if (l.indent < indent) break;
        if (l.indent > indent) throw new Error("invalid indent jump", l.n);
        if (!isListItem(l)) throw new Error("cannot mix dictionary keys into a list", l.n);
        String rest = l.text.equals("-") ? "" : l.text.substring(2);
        i++;
        Object val = parseValue(rest, indent, l.n, depth + 1);
        if (itemTag != null) val = applyTag(itemTag, glyphOf(val), l.n);
        arr.add(val);
      }
      return arr;
    }

    boolean isListItem(Line l) {
      return l.text.equals("-") || l.text.startsWith("- ");
    }

    Object parseValue(String raw, int parentIndent, int lineNo, int depth) {
      if (raw.equals("[]")) return new ArrayList<>();
      if (raw.equals("{}")) return new LinkedHashMap<>();
      String ml = matchMultiline(raw);
      if (ml != null) return readMultiline(parentIndent, ml, lineNo);
      if (raw.startsWith("!")) return parseTagged(raw, parentIndent, lineNo, depth);
      if (raw.isEmpty()) return parseEmptyOrNested(parentIndent, depth, null);
      if (isWholeRef(raw)) return lookup(raw.substring(1), lineNo);
      return interpolate(raw, lineNo);
    }

    Object parseTagged(String raw, int parentIndent, int lineNo, int depth) {
      Matcher m = Pattern.compile("^!([A-Za-z_][A-Za-z0-9_]*)(.*)$").matcher(raw);
      if (!m.matches()) throw new Error("invalid type tag", lineNo);
      String tag = m.group(1);
      String rest = m.group(2);
      if (rest.startsWith("[")) {
        if (tag.equals("s") && !rest.equals("[]")) {
          throw new Error("string arrays cannot use compact form", lineNo);
        }
        if (!rest.endsWith("]")) throw new Error("unclosed compact array", lineNo);
        String inner = rest.substring(1, rest.length() - 1);
        if (inner.isEmpty()) return parseEmptyOrNested(parentIndent, depth, tag);
        List<Object> out = new ArrayList<>();
        for (String g : inner.split(",", -1)) out.add(applyTag(tag, g.trim(), lineNo));
        return out;
      }
      if (rest.isEmpty()) throw new Error("missing value for !" + tag, lineNo);
      if (!rest.startsWith(" ")) throw new Error("expected space after type tag", lineNo);
      String body = rest.substring(1);
      String ml = matchMultiline(body);
      if (ml != null) {
        Object text = readMultiline(parentIndent, ml, lineNo);
        if (tag.equals("s")) return text;
        return applyTag(tag, (String) text, lineNo);
      }
      if (tag.equals("s")) return body;
      if (isWholeRef(body)) return lookup(body.substring(1), lineNo);
      return applyTag(tag, body, lineNo);
    }

    Object parseEmptyOrNested(int parentIndent, int depth, String itemTag) {
      skipNoise();
      Line n = peek();
      int child = parentIndent + 2;
      if (n == null || n.blank || n.indent <= parentIndent) {
        return itemTag != null ? new ArrayList<>() : "";
      }
      if (n.indent != child) throw new Error("child indent must be parent + 2", n.n);
      if (isListItem(n)) return parseList(child, depth, itemTag);
      if (itemTag != null) throw new Error("!" + itemTag + "[] expected list items", n.n);
      return parseDict(child, depth);
    }

    Object readMultiline(int parentIndent, String closer, int lineNo) {
      int base = parentIndent + 2;
      List<String> parts = new ArrayList<>();
      while (peek() != null) {
        Line l = peek();
        String stripped = rstripSpaceTab(l.raw);
        String content = stripped.replaceFirst("^ +", "");
        int ind = leadingSpaces(l.raw);
        if (!l.blank && ind == parentIndent && content.equals(closer)) {
          i++;
          return String.join("\n", parts);
        }
        if (l.blank) {
          parts.add("");
          i++;
          continue;
        }
        if (ind < base) {
          throw new Error("multiline body must indent +2, or close at opener indent", l.n);
        }
        if (l.raw.indexOf('\t') >= 0) throw new Error("tab is not allowed", l.n);
        parts.add(l.raw.length() >= base ? l.raw.substring(base) : "");
        i++;
      }
      throw new Error("unclosed multiline block", lineNo);
    }

    Object lookup(String name, int lineNo) {
      if (!env.containsKey(name)) throw new Error("undefined variable $" + name, lineNo);
      if (resolving.contains(name)) throw new Error("cyclic variable $" + name, lineNo);
      Object v = env.get(name);
      if (v instanceof String s && isWholeRef(s)) {
        resolving.add(name);
        try {
          Object r = lookup(s.substring(1), lineNo);
          env.put(name, r);
          return r;
        } finally {
          resolving.remove(name);
        }
      }
      return v;
    }

    String interpolate(String s, int lineNo) {
      Matcher m = Pattern.compile("\\$\\{([A-Za-z_][A-Za-z0-9_]*)\\}").matcher(s);
      StringBuffer sb = new StringBuffer();
      while (m.find()) {
        m.appendReplacement(sb, Matcher.quoteReplacement(glyphOf(lookup(m.group(1), lineNo))));
      }
      m.appendTail(sb);
      return sb.toString();
    }
  }

  private static int leadingSpaces(String s) {
    int i = 0;
    while (i < s.length() && s.charAt(i) == ' ') i++;
    return i;
  }

  private static String[] splitKey(String text, int n) {
    int idx = text.indexOf(": ");
    if (idx > 0) return new String[] {text.substring(0, idx), text.substring(idx + 2)};
    if (text.endsWith(":") && text.length() > 1) {
      return new String[] {text.substring(0, text.length() - 1), ""};
    }
    throw new Error("expected ': ' or trailing ':'", n);
  }

  private static String matchMultiline(String raw) {
    if (raw.equals("|")) return "|";
    if (raw.matches("^\\|[A-Za-z_][A-Za-z0-9_]*$")) return raw.substring(1);
    return null;
  }

  private static boolean isWholeRef(String raw) {
    return raw.matches("^\\$[A-Za-z_][A-Za-z0-9_]*$");
  }

  static String glyphOf(Object v) {
    if (v instanceof Tagged t) return t.value;
    if (v instanceof byte[] b) {
      StringBuilder sb = new StringBuilder();
      for (byte x : b) sb.append(String.format("%02x", x & 0xff));
      return sb.toString();
    }
    if (v instanceof String s) return s;
    if (v instanceof Boolean b) return b ? "true" : "false";
    if (v instanceof Number) return v.toString();
    throw new Error("cannot interpolate a collection");
  }

  static Object applyTag(String tag, String glyph, int n) {
    switch (tag) {
      case "s":
        return glyph;
      case "n":
        return parseN(glyph, n);
      case "i":
        return parseI(glyph, n);
      case "f":
        return parseF(glyph, n);
      case "x":
        return Long.parseUnsignedLong(stripUnderscores(glyph, n), 16);
      case "xb": {
        String s = glyph.replace("_", "");
        if (!s.matches("[0-9A-Fa-f]*") || s.length() % 2 != 0 || s.isEmpty()) {
          throw new Error("hex bytes must be an even number of digits", n);
        }
        byte[] out = new byte[s.length() / 2];
        for (int i = 0; i < out.length; i++) {
          out[i] = (byte) Integer.parseInt(s.substring(i * 2, i * 2 + 2), 16);
        }
        return out;
      }
      case "o":
        if (!glyph.matches("[0-7]+")) throw new Error("invalid octal", n);
        return Long.parseLong(glyph, 8);
      case "b":
        if (glyph.equals("true")) return Boolean.TRUE;
        if (glyph.equals("false")) return Boolean.FALSE;
        throw new Error("boolean must be true or false", n);
      case "d":
        if (!glyph.matches("\\d{4}-\\d{2}-\\d{2}")) throw new Error("invalid date", n);
        return new Tagged("d", glyph);
      case "t":
        if (!glyph.matches("\\d{2}:\\d{2}(:\\d{2}(\\.\\d+)?)?")) throw new Error("invalid time", n);
        return new Tagged("t", glyph);
      case "dt":
        if (!glyph.matches("\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}(\\.\\d+)?(Z|[+-]\\d{2}:\\d{2})")) {
          throw new Error("datetime must include a timezone offset", n);
        }
        return new Tagged("dt", glyph);
      case "tz":
        if (!glyph.equals("Z")
            && !glyph.equals("UTC")
            && !glyph.matches("[+-]\\d{2}:\\d{2}")
            && !glyph.matches("[A-Za-z_]+(/[A-Za-z0-9_+-]+)+")) {
          throw new Error("invalid time zone", n);
        }
        return new Tagged("tz", glyph);
      case "du":
        if (glyph.isEmpty() || !glyph.matches("(\\d+d)?(\\d+h)?(\\d+m)?(\\d+(\\.\\d+)?s)?")) {
          throw new Error("invalid duration", n);
        }
        return new Tagged("du", glyph);
      case "sz":
        if (!glyph.matches("\\d+(\\.\\d+)?(B|KB|MB|GB|TB|PB|KiB|MiB|GiB|TiB|PiB)")) {
          throw new Error("invalid data size", n);
        }
        return new Tagged("sz", glyph);
      case "unix":
        return parseUnix(glyph, n);
      case "ver":
        if (!glyph.matches("\\d+(\\.\\d+)*")) throw new Error("invalid version", n);
        return new Tagged("ver", glyph);
      case "uuid":
        if (!glyph.matches("[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}")) {
          throw new Error("invalid uuid", n);
        }
        return new Tagged("uuid", glyph);
      case "ip":
        if (!isIp(glyph)) throw new Error("invalid ip", n);
        return new Tagged("ip", glyph);
      case "b64": {
        String s = glyph.replaceAll("\\s+", "");
        try {
          return Base64.getDecoder().decode(s);
        } catch (IllegalArgumentException e) {
          throw new Error("invalid base64", n);
        }
      }
      case "c": {
        Matcher u = Pattern.compile("^U\\+([0-9A-Fa-f]{4,6})$").matcher(glyph);
        if (u.matches()) {
          int cp = Integer.parseInt(u.group(1), 16);
          if (cp > 0x10FFFF) throw new Error("invalid code point", n);
          return new Tagged("c", new String(Character.toChars(cp)));
        }
        if (glyph.codePointCount(0, glyph.length()) != 1) {
          throw new Error("character must be a single scalar", n);
        }
        return new Tagged("c", glyph);
      }
      default:
        return new Tagged(tag, glyph);
    }
  }

  private static String stripUnderscores(String s, int n) {
    if (s.contains("__") || s.startsWith("_") || s.endsWith("_")) {
      throw new Error("invalid numeric underscores", n);
    }
    return s.replace("_", "");
  }

  private static Object parseN(String g, int n) {
    String s = stripUnderscores(g, n);
    if (s.matches("-?0\\d.*")) throw new Error("leading zeros are not allowed", n);
    if (s.matches("-?\\d+")) {
      try {
        return Long.parseLong(s);
      } catch (NumberFormatException e) {
        throw new Error("integer overflow", n);
      }
    }
    if (s.matches("-?\\d+\\.\\d+([eE][+-]?\\d+)?") || s.matches("-?\\d+[eE][+-]?\\d+")) {
      return Double.parseDouble(s);
    }
    throw new Error("invalid number", n);
  }

  private static long parseI(String g, int n) {
    String s = stripUnderscores(g, n);
    if (!s.matches("-?\\d+")) throw new Error("invalid integer", n);
    if (s.matches("-?0\\d.*")) throw new Error("leading zeros are not allowed", n);
    try {
      return Long.parseLong(s);
    } catch (NumberFormatException e) {
      throw new Error("integer overflow", n);
    }
  }

  private static double parseF(String g, int n) {
    String s = stripUnderscores(g, n);
    if (!s.contains(".") && !s.matches(".*[eE].*")) {
      throw new Error("float must contain '.' or 'e'", n);
    }
    try {
      return Double.parseDouble(s);
    } catch (NumberFormatException e) {
      throw new Error("invalid float", n);
    }
  }

  private static Object parseUnix(String g, int n) {
    String s = stripUnderscores(g, n);
    if (s.matches("-?0\\d.*")) throw new Error("leading zeros are not allowed", n);
    if (s.matches("-?\\d+")) return Long.parseLong(s);
    if (s.matches("-?\\d+\\.\\d+")) return Double.parseDouble(s);
    throw new Error("invalid unix timestamp", n);
  }

  private static boolean isIp(String s) {
    if (s.matches("\\d{1,3}(\\.\\d{1,3}){3}")) {
      String[] p = s.split("\\.");
      for (String part : p) {
        int v = Integer.parseInt(part);
        if (v < 0 || v > 255 || !String.valueOf(v).equals(part)) return false;
      }
      return true;
    }
    if (s.indexOf(':') >= 0) {
      if (s.indexOf('.') >= 0) return false;
      String[] parts = s.split(":", -1);
      if (parts.length > 8) return false;
      int empty = 0;
      for (String p : parts) {
        if (p.isEmpty()) empty++;
        else if (!p.matches("[0-9A-Fa-f]{1,4}")) return false;
      }
      return empty <= 2;
    }
    return false;
  }
}
