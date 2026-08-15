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
 * XUN (X Unquoted Notation) parser and encoder.
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
      if (this == o) return true;
      if (!(o instanceof Tagged)) return false;
      Tagged t = (Tagged) o;
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

  public static String encode(Object value) {
    if (!(value instanceof Map)) {
      throw new Error("root must be a dictionary");
    }
    Map<?, ?> map = (Map<?, ?>) value;
    if (map.isEmpty()) {
      return "";
    }
    List<String> lines = new ArrayList<>();
    encodeMap(map, 0, lines);
    return String.join("\n", lines) + "\n";
  }

  public static String dump(Object value) {
    return encode(value);
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
      skipNoise();
      if (peek() == null) return new LinkedHashMap<>();
      Line first = peek();
      if (first.indent != 0) throw new Error("document must start at indent 0", first.n);
      if (isListItem(first)) throw new Error("root must be a dictionary", first.n);
      return parseDict(0, 0);
    }

    Map<String, Object> parseDict(int indent, int depth) {
      if (depth > MAX_DEPTH) {
        int n = peek() == null ? 0 : peek().n;
        throw new Error("nesting exceeds 64", n);
      }
      Map<String, Object> obj = new LinkedHashMap<>();
      while (peek() != null) {
        skipNoise();
        Line l = peek();
        if (l == null || l.blank) break;
        if (l.indent < indent) break;
        if (l.indent > indent) throw new Error("invalid indent jump", l.n);
        if (isListItem(l)) throw new Error("cannot mix list items into a dictionary", l.n);
        String[] parts = splitKey(l.text, l.n);
        String key = parts[0];
        String rest = parts[1];
        if (obj.containsKey(key)) throw new Error("duplicate key '" + key + "'", l.n);
        i++;
        obj.put(key, parseValue(rest, indent, l.n, depth + 1));
      }
      return obj;
    }

    List<Object> parseList(int indent, int depth, String itemTag) {
      if (depth > MAX_DEPTH) {
        int n = peek() == null ? 0 : peek().n;
        throw new Error("nesting exceeds 64", n);
      }
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
        if (itemTag != null) {
          val = applyTag(itemTag, glyphOf(val), l.n);
        }
        arr.add(val);
      }
      return arr;
    }

    boolean isListItem(Line l) {
      return l.text.equals("-") || l.text.startsWith("- ");
    }

    Object parseValue(String raw, int parentIndent, int lineNo, int depth) {
      if (raw.equals("[]")) return new ArrayList<>();
      if (raw.equals("{}")) return new LinkedHashMap<String, Object>();
      String closer = matchMultiline(raw);
      if (closer != null) {
        return readMultiline(parentIndent, null, closer, lineNo);
      }
      if (raw.startsWith("!")) {
        return parseTagged(raw, parentIndent, lineNo, depth);
      }
      if (raw.isEmpty()) {
        return parseEmptyOrNested(parentIndent, lineNo, depth, null);
      }
      return raw;
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
        if (inner.isEmpty()) {
          return parseEmptyOrNested(parentIndent, lineNo, depth, tag);
        }
        String[] parts = splitCompact(inner);
        List<Object> out = new ArrayList<>();
        for (String g : parts) {
          out.add(applyTag(tag, g, lineNo));
        }
        return out;
      }
      if (rest.isEmpty()) throw new Error("missing value for !" + tag, lineNo);
      if (!rest.startsWith(" ")) throw new Error("expected space after type tag", lineNo);
      String body = rest.substring(1);
      String closer = matchMultiline(body);
      if (closer != null) {
        String text = (String) readMultiline(parentIndent, null, closer, lineNo);
        if (tag.equals("s")) return text;
        return applyTag(tag, text, lineNo);
      }
      if (tag.equals("s")) return body;
      return applyTag(tag, body, lineNo);
    }

    Object parseEmptyOrNested(int parentIndent, int lineNo, int depth, String itemTag) {
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

    Object readMultiline(int parentIndent, String tag, String closer, int lineNo) {
      int base = parentIndent + 2;
      List<String> parts = new ArrayList<>();
      while (peek() != null) {
        Line l = peek();
        String stripped = rstripSpaceTab(l.raw);
        String content = stripped.stripLeading();
        int ind = l.raw.length() - l.raw.stripLeading().length();
        if (!l.blank && ind == parentIndent && content.equals(closer)) {
          i++;
          String s = String.join("\n", parts);
          if (tag != null && !tag.equals("s")) return applyTag(tag, s, lineNo);
          return s;
        }
        if (l.blank) {
          parts.add("");
          i++;
          continue;
        }
        if (ind < base && !l.blank) {
          throw new Error("multiline body must indent +2, or close at opener indent", l.n);
        }
        if (l.raw.contains("\t")) throw new Error("tab is not allowed", l.n);
        parts.add(l.raw.substring(base));
        i++;
      }
      throw new Error("unclosed multiline block", lineNo);
    }
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
    Matcher m = Pattern.compile("^\\|([A-Za-z_][A-Za-z0-9_]*)$").matcher(raw);
    if (m.matches()) return m.group(1);
    return null;
  }

  private static String glyphOf(Object v) {
    if (v instanceof Tagged) return ((Tagged) v).value;
    if (v instanceof byte[]) return hexEncode((byte[]) v);
    if (v instanceof Boolean) return ((Boolean) v) ? "true" : "false";
    if (v instanceof String || v instanceof Number) return String.valueOf(v);
    throw new Error("cannot stringify a collection as scalar glyph");
  }

  private static String[] splitCompact(String inner) {
    String[] parts = inner.split(",");
    for (int i = 0; i < parts.length; i++) parts[i] = parts[i].trim();
    return parts;
  }

  private static String stripUnderscores(String s, int n) {
    if (s.contains("__") || s.startsWith("_") || s.endsWith("_")) {
      throw new Error("invalid numeric underscores", n);
    }
    return s.replace("_", "");
  }

  private static String hexEncode(byte[] b) {
    StringBuilder sb = new StringBuilder(b.length * 2);
    for (byte x : b) {
      sb.append(String.format("%02x", x));
    }
    return sb.toString();
  }

  private static Object applyTag(String tag, String glyph, int n) {
    switch (tag) {
      case "s":
        return glyph;
      case "n":
        return parseN(glyph, n);
      case "i":
        return parseI(glyph, n);
      case "f":
        return parseF(glyph, n);
      case "x": {
        String s = stripUnderscores(glyph, n);
        if (!s.matches("^[0-9A-Fa-f]+$")) throw new Error("invalid hex", n);
        return Long.parseLong(s, 16);
      }
      case "xb": {
        String s = glyph.replace("_", "");
        if (!s.matches("^[0-9A-Fa-f]*$") || s.length() % 2 != 0 || s.isEmpty()) {
          throw new Error("hex bytes must be an even number of digits", n);
        }
        byte[] b = new byte[s.length() / 2];
        for (int i = 0; i < s.length(); i += 2) {
          b[i / 2] = (byte) Integer.parseInt(s.substring(i, i + 2), 16);
        }
        return b;
      }
      case "o": {
        if (!glyph.matches("^[0-7]+$")) throw new Error("invalid octal", n);
        return Long.parseLong(glyph, 8);
      }
      case "b": {
        if (glyph.equals("true")) return Boolean.TRUE;
        if (glyph.equals("false")) return Boolean.FALSE;
        throw new Error("boolean must be true or false", n);
      }
      case "d": {
        if (!glyph.matches("^\\d{4}-\\d{2}-\\d{2}$")) throw new Error("invalid date", n);
        return new Tagged("d", glyph);
      }
      case "t": {
        if (!glyph.matches("^\\d{2}:\\d{2}(:\\d{2}(\\.\\d+)?)?$")) throw new Error("invalid time", n);
        return new Tagged("t", glyph);
      }
      case "dt": {
        if (!glyph.matches("^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}(\\.\\d+)?(Z|[+-]\\d{2}:\\d{2})$")) {
          throw new Error("datetime must include a timezone offset", n);
        }
        return new Tagged("dt", glyph);
      }
      case "tz": {
        if (!glyph.equals("Z") && !glyph.equals("UTC") && !glyph.matches("^[+-]\\d{2}:\\d{2}$") && !glyph.matches("^[A-Za-z_]+(/[A-Za-z0-9_+-]+)+$")) {
          throw new Error("invalid time zone", n);
        }
        return new Tagged("tz", glyph);
      }
      case "du": {
        if (glyph.isEmpty() || !glyph.matches("^(\\d+d)?(\\d+h)?(\\d+m)?(\\d+(\\.\\d+)?s)?$")) {
          throw new Error("invalid duration", n);
        }
        return new Tagged("du", glyph);
      }
      case "sz": {
        if (!glyph.matches("^\\d+(\\.\\d+)?(B|KB|MB|GB|TB|PB|KiB|MiB|GiB|TiB|PiB)$")) {
          throw new Error("invalid data size", n);
        }
        return new Tagged("sz", glyph);
      }
      case "unix":
        return parseUnix(glyph, n);
      case "ver": {
        if (!glyph.matches("^\\d+(\\.\\d+)*$")) throw new Error("invalid version", n);
        return new Tagged("ver", glyph);
      }
      case "uuid": {
        if (!glyph.matches("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$")) {
          throw new Error("invalid uuid", n);
        }
        return new Tagged("uuid", glyph);
      }
      case "ip": {
        if (!isIp(glyph)) throw new Error("invalid ip", n);
        return new Tagged("ip", glyph);
      }
      case "b64": {
        String s = glyph.replaceAll("\\s+", "");
        try {
          return Base64.getDecoder().decode(s);
        } catch (Exception e) {
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

  private static Object parseN(String g, int n) {
    String s = stripUnderscores(g, n);
    if (s.matches("^-?0\\d.*")) throw new Error("leading zeros are not allowed", n);
    if (s.matches("^-?\\d+$")) {
      try {
        return Long.parseLong(s);
      } catch (NumberFormatException e) {
        throw new Error("integer overflow", n);
      }
    }
    if (s.matches("^-?\\d+\\.\\d+([eE][+-]?\\d+)?$") || s.matches("^-?\\d+[eE][+-]?\\d+$")) {
      return Double.parseDouble(s);
    }
    throw new Error("invalid number", n);
  }

  private static long parseI(String g, int n) {
    String s = stripUnderscores(g, n);
    if (!s.matches("^-?\\d+$")) throw new Error("invalid integer", n);
    if (s.matches("^-?0\\d.*")) throw new Error("leading zeros are not allowed", n);
    try {
      return Long.parseLong(s);
    } catch (NumberFormatException e) {
      throw new Error("integer overflow", n);
    }
  }

  private static double parseF(String g, int n) {
    String s = stripUnderscores(g, n);
    if (!s.contains(".") && !s.contains("e") && !s.contains("E")) {
      throw new Error("float must contain '.' or 'e'", n);
    }
    return Double.parseDouble(s);
  }

  private static Object parseUnix(String g, int n) {
    String s = stripUnderscores(g, n);
    if (s.matches("^-?0\\d.*")) throw new Error("leading zeros are not allowed", n);
    if (s.matches("^-?\\d+$")) return Long.parseLong(s);
    if (s.matches("^-?\\d+\\.\\d+$")) return Double.parseDouble(s);
    throw new Error("invalid unix timestamp", n);
  }

  private static boolean isIp(String s) {
    String[] parts = s.split("\\.");
    if (parts.length == 4) {
      for (String p : parts) {
        if (!p.matches("^\\d+$")) return false;
        int num = Integer.parseInt(p);
        if (num < 0 || num > 255 || !String.valueOf(num).equals(p)) return false;
      }
      return true;
    }
    return s.contains(":") && !s.contains(":::") && s.matches("^[0-9a-fA-F:]+$");
  }

  // --- Encoder ---

  private static String validateKey(String key) {
    if (key == null || key.isEmpty()) {
      throw new Error("key must be a non-empty string");
    }
    if (key.contains("\n") || key.contains("\r") || key.contains(": ") || key.endsWith(":")) {
      throw new Error("invalid key format: " + key);
    }
    return key;
  }

  private static void encodeMap(Map<?, ?> map, int depth, List<String> lines) {
    if (depth > MAX_DEPTH) throw new Error("nesting depth exceeds limit");
    String indent = "  ".repeat(depth);
    for (Map.Entry<?, ?> entry : map.entrySet()) {
      String key = validateKey(String.valueOf(entry.getKey()));
      Object v = entry.getValue();
      if (v instanceof Map) {
        Map<?, ?> sub = (Map<?, ?>) v;
        if (sub.isEmpty()) {
          lines.add(indent + key + ": {}");
        } else {
          lines.add(indent + key + ":");
          encodeMap(sub, depth + 1, lines);
        }
      } else if (v instanceof List) {
        List<?> sub = (List<?>) v;
        if (sub.isEmpty()) {
          lines.add(indent + key + ": []");
        } else {
          lines.add(indent + key + ":");
          encodeList(sub, depth + 1, lines);
        }
      } else if (v instanceof String) {
        String s = (String) v;
        if (s.contains("\n") || s.contains("\r")) {
          lines.add(indent + key + ": |");
          for (String line : s.split("\\R")) {
            lines.add(indent + "  " + line);
          }
          lines.add(indent + "|");
        } else if (s.isEmpty()) {
          lines.add(indent + key + ":");
        } else if (s.startsWith("!") || s.equals("[]") || s.equals("{}") || s.startsWith("|")) {
          lines.add(indent + key + ": !s " + s);
        } else {
          lines.add(indent + key + ": " + s);
        }
      } else if (v instanceof Boolean) {
        lines.add(indent + key + ": !b " + (((Boolean) v) ? "true" : "false"));
      } else if (v instanceof Long || v instanceof Integer || v instanceof Short || v instanceof Byte) {
        lines.add(indent + key + ": !i " + v);
      } else if (v instanceof Double || v instanceof Float) {
        String s = String.valueOf(v);
        if (!s.contains(".") && !s.contains("e") && !s.contains("E")) {
          s += ".0";
        }
        lines.add(indent + key + ": !f " + s);
      } else if (v instanceof byte[]) {
        lines.add(indent + key + ": !xb " + hexEncode((byte[]) v).toUpperCase());
      } else if (v instanceof Tagged) {
        Tagged t = (Tagged) v;
        if (t.value.contains("\n") || t.value.contains("\r")) {
          lines.add(indent + key + ": !" + t.tag + " |");
          for (String line : t.value.split("\\R")) {
            lines.add(indent + "  " + line);
          }
          lines.add(indent + "|");
        } else {
          lines.add(indent + key + ": !" + t.tag + " " + t.value);
        }
      } else if (v == null) {
        lines.add(indent + key + ":");
      } else {
        throw new Error("unsupported value type: " + v.getClass().getName() + " for key '" + key + "'");
      }
    }
  }

  private static void encodeList(List<?> items, int depth, List<String> lines) {
    if (depth > MAX_DEPTH) throw new Error("nesting depth exceeds limit");
    String indent = "  ".repeat(depth);
    for (Object v : items) {
      if (v instanceof Map) {
        Map<?, ?> sub = (Map<?, ?>) v;
        if (sub.isEmpty()) {
          lines.add(indent + "- {}");
        } else {
          lines.add(indent + "-");
          encodeMap(sub, depth + 1, lines);
        }
      } else if (v instanceof List) {
        List<?> sub = (List<?>) v;
        if (sub.isEmpty()) {
          lines.add(indent + "- []");
        } else {
          lines.add(indent + "-");
          encodeList(sub, depth + 1, lines);
        }
      } else if (v instanceof String) {
        String s = (String) v;
        if (s.contains("\n") || s.contains("\r")) {
          lines.add(indent + "- |");
          for (String line : s.split("\\R")) {
            lines.add(indent + "  " + line);
          }
          lines.add(indent + "|");
        } else if (s.isEmpty()) {
          lines.add(indent + "-");
        } else if (s.startsWith("!") || s.equals("[]") || s.equals("{}") || s.startsWith("|")) {
          lines.add(indent + "- !s " + s);
        } else {
          lines.add(indent + "- " + s);
        }
      } else if (v instanceof Boolean) {
        lines.add(indent + "- !b " + (((Boolean) v) ? "true" : "false"));
      } else if (v instanceof Long || v instanceof Integer || v instanceof Short || v instanceof Byte) {
        lines.add(indent + "- !i " + v);
      } else if (v instanceof Double || v instanceof Float) {
        String s = String.valueOf(v);
        if (!s.contains(".") && !s.contains("e") && !s.contains("E")) {
          s += ".0";
        }
        lines.add(indent + "- !f " + s);
      } else if (v instanceof byte[]) {
        lines.add(indent + "- !xb " + hexEncode((byte[]) v).toUpperCase());
      } else if (v instanceof Tagged) {
        Tagged t = (Tagged) v;
        if (t.value.contains("\n") || t.value.contains("\r")) {
          lines.add(indent + "- !" + t.tag + " |");
          for (String line : t.value.split("\\R")) {
            lines.add(indent + "  " + line);
          }
          lines.add(indent + "|");
        } else {
          lines.add(indent + "- !" + t.tag + " " + t.value);
        }
      } else if (v == null) {
        lines.add(indent + "-");
      } else {
        throw new Error("unsupported list item type: " + v.getClass().getName());
      }
    }
  }
}
