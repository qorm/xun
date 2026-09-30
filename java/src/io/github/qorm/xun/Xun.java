package io.github.qorm.xun;

import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Deque;
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

    public java.time.Instant toInstant() {
      if (!tag.equals("dt")) throw new Error("cannot convert !" + tag + " to Instant");
      return java.time.Instant.parse(value);
    }

    public java.time.LocalDate toLocalDate() {
      if (!tag.equals("d")) throw new Error("cannot convert !" + tag + " to LocalDate");
      return java.time.LocalDate.parse(value);
    }

    public java.time.LocalTime toLocalTime() {
      if (!tag.equals("t")) throw new Error("cannot convert !" + tag + " to LocalTime");
      return java.time.LocalTime.parse(value);
    }

    public java.net.InetAddress toInetAddress() throws Exception {
      if (!tag.equals("ip")) throw new Error("cannot convert !" + tag + " to InetAddress");
      return java.net.InetAddress.getByName(value);
    }

    public java.util.UUID toUUID() {
      if (!tag.equals("uuid")) throw new Error("cannot convert !" + tag + " to UUID");
      return java.util.UUID.fromString(value);
    }

    public byte[] toBytes() {
      if (tag.equals("xb")) {
        String s = value.replace("_", "");
        byte[] arr = new byte[s.length() / 2];
        for (int i = 0; i < s.length(); i += 2) {
          arr[i / 2] = (byte) Integer.parseInt(s.substring(i, i + 2), 16);
        }
        return arr;
      }
      if (tag.equals("b64")) {
        return Base64.getDecoder().decode(value.replaceAll("\\s+", ""));
      }
      throw new Error("cannot convert !" + tag + " to raw bytes");
    }

    public long toBytesSize() {
      if (!tag.equals("sz")) throw new Error("cannot convert !" + tag + " to size bytes");
      return parseSize(value);
    }

    public java.time.Duration toDuration() {
      if (!tag.equals("du")) throw new Error("cannot convert !" + tag + " to Duration");
      return java.time.Duration.ofMillis(parseDurationMs(value));
    }

    public double toDurationSeconds() {
      if (!tag.equals("du")) throw new Error("cannot convert !" + tag + " to duration seconds");
      return parseDuration(value);
    }

    public String toIP() {
      if (!tag.equals("ip")) throw new Error("cannot convert !" + tag + " to IP address");
      if (!isIp(value)) throw new Error("invalid IP address: " + value);
      return value;
    }

    public Number toNumber() {
      if (tag.equals("o")) {
        if (!value.matches("^[0-7]+$")) throw new Error("invalid octal: " + value);
        return parseBigInteger(value, 8);
      }
      if (tag.equals("x")) {
        String s = value.replace("_", "");
        if (!s.matches("^[0-9A-Fa-f]+$")) throw new Error("invalid hex: " + value);
        return parseBigInteger(s, 16);
      }
      if (tag.equals("unix")) {
        return (Number) parseUnix(value, 0);
      }
      if (tag.equals("n") || tag.equals("i") || tag.equals("f")) {
        String s = value.replace("_", "");
        if (s.matches("^-?\\d+$")) {
          try {
            return Long.parseLong(s);
          } catch (NumberFormatException e) {
            return Double.parseDouble(s);
          }
        }
        return Double.parseDouble(s);
      }
      throw new Error("cannot convert !" + tag + " to number");
    }

    public int[] toVersionParts() {
      if (!tag.equals("ver")) throw new Error("cannot convert !" + tag + " to version parts");
      return parseVersion(value);
    }

    public java.time.ZoneId toZoneId() {
      if (!tag.equals("tz")) throw new Error("cannot convert !" + tag + " to ZoneId");
      if (value.equals("Z")) return java.time.ZoneOffset.UTC;
      try {
        return java.time.ZoneId.of(value);
      } catch (RuntimeException e) {
        throw new Error("unknown timezone: " + value);
      }
    }

    public char toChar() {
      if (!tag.equals("c")) throw new Error("cannot convert !" + tag + " to char");
      if (value.startsWith("U+")) {
        int cp = Integer.parseInt(value.substring(2), 16);
        if (cp < 0 || cp > 0x10FFFF) throw new Error("invalid code point: " + value);
        return Character.toChars(cp)[0];
      }
      if (value.codePointCount(0, value.length()) != 1) {
        throw new Error("value '" + value + "' is not a single character");
      }
      return value.charAt(0);
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

  public static long parseSize(String s) {
    Pattern p = Pattern.compile("^(\\d+(?:\\.\\d+)?)(B|KB|MB|GB|TB|PB|KiB|MiB|GiB|TiB|PiB)$");
    Matcher m = p.matcher(s);
    if (!m.matches()) throw new Error("invalid size format: " + s);
    double num = Double.parseDouble(m.group(1));
    String unit = m.group(2);
    long mult = switch (unit) {
      case "B" -> 1L;
      case "KB" -> 1_000L;
      case "MB" -> 1_000_000L;
      case "GB" -> 1_000_000_000L;
      case "TB" -> 1_000_000_000_000L;
      case "PB" -> 1_000_000_000_000_000L;
      case "KiB" -> 1024L;
      case "MiB" -> 1024L * 1024L;
      case "GiB" -> 1024L * 1024L * 1024L;
      case "TiB" -> 1024L * 1024L * 1024L * 1024L;
      case "PiB" -> 1024L * 1024L * 1024L * 1024L * 1024L;
      default -> throw new Error("unknown unit: " + unit);
    };
    return (long) (num * mult);
  }

  private static Number parseBigInteger(String s, int radix) {
    java.math.BigInteger bi = new java.math.BigInteger(s, radix);
    if (bi.bitLength() < 64) return bi.longValue();
    return bi.doubleValue();
  }

  private static final Pattern DURATION_RE =
      Pattern.compile("^(?:(\\d+)d)?(?:(\\d+)h)?(?:(\\d+)m)?(?:(\\d+(?:\\.\\d+)?)s)?(?:(\\d+(?:\\.\\d+)?)ms)?$");

  /** Parse a duration glyph (order d,h,m,s,ms) into seconds as a double. */
  public static double parseDuration(String s) {
    if (s == null || s.isEmpty()) throw new Error("empty duration string");
    Matcher m = DURATION_RE.matcher(s);
    if (!m.matches()
        || (m.group(1) == null && m.group(2) == null && m.group(3) == null && m.group(4) == null && m.group(5) == null)) {
      throw new Error("invalid duration format: \"" + s + "\"");
    }
    long days = m.group(1) != null ? Long.parseLong(m.group(1)) : 0;
    long hours = m.group(2) != null ? Long.parseLong(m.group(2)) : 0;
    long minutes = m.group(3) != null ? Long.parseLong(m.group(3)) : 0;
    double seconds = m.group(4) != null ? Double.parseDouble(m.group(4)) : 0.0;
    double millis = m.group(5) != null ? Double.parseDouble(m.group(5)) : 0.0;
    return days * 86400 + hours * 3600 + minutes * 60 + seconds + millis / 1000.0;
  }

  public static long parseDurationMs(String s) {
    return Math.round(parseDuration(s) * 1000.0);
  }

  public static int[] parseVersion(String s) {
    String[] parts = s.split("\\.");
    int[] res = new int[parts.length];
    for (int i = 0; i < parts.length; i++) {
      res[i] = Integer.parseInt(parts[i]);
    }
    return res;
  }

  public static Map<String, Object> decode(String source) {
    return parse(source);
  }

  /** Recursively convert Tagged values to native Java values. */
  public static Object unpack(Object v) {
    if (v instanceof Tagged) {
      Tagged t = (Tagged) v;
      switch (t.tag) {
        case "dt":
          return t.toInstant();
        case "d":
          return t.toLocalDate();
        case "ver":
          return t.toVersionParts();
        case "sz":
          return t.toBytesSize();
        case "du":
          return t.toDurationSeconds();
        case "xb":
        case "b64":
          return t.toBytes();
        case "ip":
          return t.toIP();
        case "uuid":
          return t.toUUID();
        case "c":
          return t.toChar();
        case "o":
        case "x":
        case "unix":
          return t.toNumber();
        default:
          return t.value;
      }
    }
    if (v instanceof Map) {
      Map<?, ?> in = (Map<?, ?>) v;
      Map<String, Object> out = new LinkedHashMap<>();
      for (Map.Entry<?, ?> e : in.entrySet()) {
        out.put(String.valueOf(e.getKey()), unpack(e.getValue()));
      }
      return out;
    }
    if (v instanceof List) {
      List<?> in = (List<?>) v;
      List<Object> out = new ArrayList<>();
      for (Object item : in) out.add(unpack(item));
      return out;
    }
    return v;
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
    final String code;
    final int n;
    final boolean blank;

    Line(String raw, int indent, String text, String code, int n, boolean blank) {
      this.raw = raw;
      this.indent = indent;
      this.text = text;
      this.code = code;
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
    String code = stripTrailingComment(text);
    return new Line(raw, i, text, code, n, text.isEmpty());
  }

  /** Strip a trailing ` # ...` comment outside of quoted strings. */
  private static String stripTrailingComment(String text) {
    boolean inQuote = false;
    for (int i = 0; i < text.length(); i++) {
      char ch = text.charAt(i);
      if (inQuote) {
        if (ch == '\\') i++;
        else if (ch == '"') inQuote = false;
        continue;
      }
      if (ch == '"') {
        inQuote = true;
        continue;
      }
      if (ch == '#' && (i == 0 || text.charAt(i - 1) == ' ' || text.charAt(i - 1) == '\t')) {
        return rstripSpaceTab(text.substring(0, i));
      }
    }
    return text;
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
    // RFC-0001: stack of [indent, key] for 'end' validation. key may be null.
    final Deque<String[]> openBlocks = new ArrayDeque<>();

    Parser(List<Line> lines) {
      this.lines = lines;
    }

    Line peek() {
      return i < lines.size() ? lines.get(i) : null;
    }

    void skipNoise() {
      while (peek() != null) {
        Line l = peek();
        if (l.blank || l.code.isEmpty()) i++;
        else break;
      }
    }

    // RFC-0001: try to consume an 'end' or 'end <key>' statement at given indent.
    // Returns true if consumed, false if not an end statement.
    boolean tryConsumeEnd(int indent, String expectedKey) {
      Line l = peek();
      if (l == null || l.blank || l.indent != indent) return false;
      String code = l.code;
      if (code.equals("end")) {
        i++;
        return true;
      }
      if (code.startsWith("end ")) {
        String endKey = code.substring(4).trim();
        if (endKey.isEmpty()) return false;
        if (expectedKey != null && !expectedKey.equals(endKey)) {
          throw new Error(
              "end-key mismatch: expected '" + expectedKey + "', got '" + endKey + "'", l.n);
        }
        i++;
        return true;
      }
      return false;
    }

    Map<String, Object> parseDocument() {
      skipNoise();
      if (peek() == null) return new LinkedHashMap<>();
      Line first = peek();
      if (first.indent != 0) throw new Error("document must start at indent 0", first.n);
      if (isListItem(first)) throw new Error("root must be a dictionary", first.n);
      // RFC-0001: track root block so trailing 'end' can close it.
      openBlocks.push(new String[] {"0", null});
      try {
        Map<String, Object> obj = parseDict(0, 0, null);
        skipNoise();
        tryConsumeEnd(0, null);
        return obj;
      } finally {
        openBlocks.pop();
      }
    }

    Map<String, Object> parseDict(int indent, int depth, String dictKey) {
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
        // RFC-0001: 'end' at body indent stops the dict (caller validates via tryConsumeEnd).
        if (l.code.equals("end") || l.code.startsWith("end ")) break;
        if (isListItem(l)) throw new Error("cannot mix list items into a dictionary", l.n);
        String[] parts = splitKey(l.code, l.n);
        String key = parts[0];
        String rest = parts[1];
        if (obj.containsKey(key)) throw new Error("duplicate key '" + key + "'", l.n);
        i++;
        obj.put(key, parseValue(rest, indent, l.n, depth + 1, key));
      }
      return obj;
    }

    List<Object> parseList(int indent, int depth, String itemTag, String parentKey) {
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
        // RFC-0001: 'end' at list level stops the list.
        if (l.code.equals("end") || l.code.startsWith("end ")) break;
        if (!isListItem(l)) throw new Error("cannot mix dictionary keys into a list", l.n);
        String rest = l.code.equals("-") ? "" : l.code.substring(2);
        i++;
        if (looksLikeDictEntry(rest)) {
          if (itemTag != null) {
            throw new Error("!" + itemTag + "[] cannot contain dictionary entries", l.n);
          }
          // RFC-0002: inline object literal {key: value, ...} as a list item
          if (rest.trim().startsWith("{")) {
            arr.add(parseInlineDict(rest, l.n));
            continue;
          }
          arr.add(parseInlineDictEntry(rest, indent + 2, l.n, depth + 1));
          continue;
        }
        Object val = parseValue(rest, indent, l.n, depth + 1, parentKey);
        if (itemTag != null) {
          val = applyTag(itemTag, glyphOf(val), l.n);
        }
        arr.add(val);
      }
      return arr;
    }

    Map<String, Object> parseInlineDictEntry(String first, int keyIndent, int lineNo, int depth) {
      Map<String, Object> obj = new LinkedHashMap<>();
      String[] firstParsed = splitKey(first, lineNo);
      obj.put(firstParsed[0], parseValue(firstParsed[1], keyIndent, lineNo, depth + 1, firstParsed[0]));
      while (peek() != null) {
        skipNoise();
        Line l = peek();
        if (l == null || l.blank) break;
        if (l.indent < keyIndent) break;
        if (l.indent > keyIndent) throw new Error("invalid indent jump", l.n);
        if (isListItem(l)) break;
        String[] parts = splitKey(l.text, l.n);
        String key = parts[0];
        String rest = parts[1];
        if (obj.containsKey(key)) throw new Error("duplicate key '" + key + "'", l.n);
        i++;
        obj.put(key, parseValue(rest, keyIndent, l.n, depth + 1, key));
      }
      return obj;
    }

    // RFC-0002: parse inline object literal {key: value, key2: value2, ...}
    Map<String, Object> parseInlineDict(String text, int lineNo) {
      String trimmed = text.trim();
      if (trimmed.length() < 2 || !trimmed.startsWith("{") || !trimmed.endsWith("}")) {
        throw new Error("inline object must be wrapped in '{...}'", lineNo);
      }
      String inner = trimmed.substring(1, trimmed.length() - 1);
      if (inner.trim().isEmpty()) return new LinkedHashMap<>();
      List<String> pieces = splitTopLevelCommas(inner, lineNo);
      Map<String, Object> obj = new LinkedHashMap<>();
      for (String piece : pieces) {
        String t = piece.trim();
        if (t.isEmpty()) continue;
        // Find top-level key separator ": " (or trailing ":" at end).
        boolean inQuote = false;
        boolean escape = false;
        int foundIdx = -1;
        for (int i = 0; i < t.length(); i++) {
          char ch = t.charAt(i);
          if (escape) { escape = false; continue; }
          if (inQuote) {
            if (ch == '\\') { escape = true; continue; }
            if (ch == '"') inQuote = false;
            continue;
          }
          if (ch == '"') { inQuote = true; continue; }
          if (ch == ':') {
            // Trailing ":" separator (key with no value)
            if (i == t.length() - 1) { foundIdx = i; break; }
            // ": " separator (key: value) — require the next char to be a space
            if (i + 1 < t.length() && t.charAt(i + 1) == ' ') { foundIdx = i; break; }
          }
        }
        if (foundIdx == -1) {
          throw new Error("inline object entry missing ': ' separator: '" + t + "'", lineNo);
        }
        String keyRaw = t.substring(0, foundIdx).trim();
        String rest = t.substring(foundIdx + 1).trim();
        if (keyRaw.startsWith("\"")) {
          String[] qp = parseQuotedPrefix(keyRaw, lineNo);
          keyRaw = qp[0];
        }
        if (keyRaw.isEmpty()) throw new Error("empty key in inline object", lineNo);
        if (obj.containsKey(keyRaw)) throw new Error("duplicate key '" + keyRaw + "'", lineNo);
        Object value;
        if (rest.isEmpty()) {
          value = "";
        } else if (rest.startsWith("\"")) {
          value = parseQuotedString(rest, lineNo);
        } else if (rest.startsWith("!")) {
          Matcher tm = Pattern.compile("^!([A-Za-z_][A-Za-z0-9_]*)(.*)$").matcher(rest);
          if (!tm.matches()) throw new Error("invalid tag in inline object: '" + rest + "'", lineNo);
          String itag = tm.group(1);
          String tail = tm.group(2);
          if (tail.isEmpty()) throw new Error("missing value for !" + itag, lineNo);
          if (tail.startsWith(" ")) {
            value = applyTag(itag, tail.substring(1), lineNo);
          } else if (tail.startsWith("[")) {
            // Compact tag array inside inline object: e.g. ports: !n[80, 443]
            if (!tail.endsWith("]")) throw new Error("unclosed compact array in inline object", lineNo);
            String compactInner = tail.substring(1, tail.length() - 1);
            if (compactInner.isEmpty()) {
              value = new ArrayList<>();
            } else {
              List<Object> ca = new ArrayList<>();
              for (String g : splitCompact(compactInner)) {
                ca.add(applyTag(itag, g, lineNo));
              }
              value = ca;
            }
          } else {
            throw new Error("expected space or '[' after type tag in inline object", lineNo);
          }
        } else {
          value = rest;
        }
        obj.put(keyRaw, value);
      }
      return obj;
    }

    boolean isListItem(Line l) {
      return l.code.equals("-") || l.code.startsWith("- ");
    }

    Object parseValue(String raw, int parentIndent, int lineNo, int depth, String valueKey) {
      raw = stripTrailingComment(raw);
      if (raw.equals("[]")) return new ArrayList<>();
      if (raw.equals("{}")) return new LinkedHashMap<String, Object>();
      Ml ml = matchMultiline(raw);
      if (ml != null) {
        return readMultiline(parentIndent, null, ml.closer, ml.chomp, lineNo);
      }
      if (raw.startsWith("!")) {
        return parseTagged(raw, parentIndent, lineNo, depth, valueKey);
      }
      // RFC-0002: untagged compact array of inline objects: [{...}, {...}]
      if (raw.startsWith("[") && raw.endsWith("]") && !raw.equals("[]")) {
        String inner = raw.substring(1, raw.length() - 1);
        List<String> pieces = splitTopLevelCommas(inner, lineNo);
        boolean hasObjects = false;
        for (String p : pieces) {
          if (p.trim().startsWith("{")) { hasObjects = true; break; }
        }
        if (hasObjects) {
          List<Object> out = new ArrayList<>();
          for (String piece : pieces) {
            String t = piece.trim();
            if (t.startsWith("{")) out.add(parseInlineDict(t, lineNo));
            else out.add(t);
          }
          return out;
        }
      }
      if (raw.isEmpty()) {
        // RFC-0001: nested block — header at parentIndent, named valueKey.
        openBlocks.push(new String[] {String.valueOf(parentIndent), valueKey});
        try {
          Object result = parseEmptyOrNested(parentIndent, lineNo, depth, null, valueKey);
          skipNoise();
          tryConsumeEnd(parentIndent, valueKey);
          return result;
        } finally {
          openBlocks.pop();
        }
      }
      if (raw.startsWith("\"")) {
        return parseQuotedString(raw, lineNo);
      }
      return raw;
    }

    Object parseTagged(String raw, int parentIndent, int lineNo, int depth, String valueKey) {
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
          // RFC-0001: track tagged block for 'end' validation
          openBlocks.push(new String[] {String.valueOf(parentIndent), valueKey});
          try {
            Object result = parseEmptyOrNested(parentIndent, lineNo, depth, tag, valueKey);
            skipNoise();
            tryConsumeEnd(parentIndent, valueKey);
            return result;
          } finally {
            openBlocks.pop();
          }
        }
        // RFC-0002: detect inline object elements {key: val, ...} within compact arrays.
        List<String> pieces = splitTopLevelCommas(inner, lineNo);
        boolean hasObjects = false;
        for (String p : pieces) {
          if (p.trim().startsWith("{")) { hasObjects = true; break; }
        }
        if (hasObjects) {
          List<Object> out = new ArrayList<>();
          for (String piece : pieces) {
            String t = piece.trim();
            if (t.startsWith("{")) out.add(parseInlineDict(t, lineNo));
            else out.add(applyTag(tag, t, lineNo));
          }
          return out;
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
      Ml ml = matchMultiline(body);
      if (ml != null) {
        String text = (String) readMultiline(parentIndent, null, ml.closer, ml.chomp, lineNo);
        if (tag.equals("s")) return text;
        return applyTag(tag, text, lineNo);
      }
      if (tag.equals("s")) return parseStringBody(body, lineNo);
      return applyTag(tag, body, lineNo);
    }

    Object parseEmptyOrNested(int parentIndent, int lineNo, int depth, String itemTag, String valueKey) {
      skipNoise();
      Line n = peek();
      int child = parentIndent + 2;
      if (n == null || n.blank || n.indent <= parentIndent) {
        return itemTag != null ? new ArrayList<>() : "";
      }
      if (n.indent != child) throw new Error("child indent must be parent + 2", n.n);
      if (isListItem(n)) return parseList(child, depth, itemTag, valueKey);
      if (itemTag != null) throw new Error("!" + itemTag + "[] expected list items", n.n);
      return parseDict(child, depth, valueKey);
    }

    Object readMultiline(int parentIndent, String tag, String closer, String chomp, int lineNo) {
      int base = parentIndent + 2;
      List<String> parts = new ArrayList<>();
      while (peek() != null) {
        Line l = peek();
        String stripped = rstripSpaceTab(l.raw);
        String content = stripLeadingSpaces(stripped);
        int ind = countLeadingSpaces(l.raw);
        String closerText = stripTrailingComment(content);
        if (!l.blank && ind == parentIndent && closerText.equals(closer)) {
          i++;
          String s = String.join("\n", parts);
          if (chomp.equals("strip")) {
            while (s.endsWith("\n")) s = s.substring(0, s.length() - 1);
          } else if (chomp.equals("clip") && s.length() > 0 && !s.endsWith("\n")) {
            s += "\n";
          }
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
        parts.add(l.raw.length() > base ? l.raw.substring(base) : "");
        i++;
      }
      throw new Error("unclosed multiline block", lineNo);
    }
  }

  private static int countLeadingSpaces(String s) {
    int i = 0;
    while (i < s.length() && s.charAt(i) == ' ') i++;
    return i;
  }

  private static String stripLeadingSpaces(String s) {
    int i = countLeadingSpaces(s);
    return s.substring(i);
  }

  private static String[] parseQuotedPrefix(String raw, int lineNo) {
    if (!raw.startsWith("\"")) throw new Error("quoted string must start with '\"'", lineNo);
    StringBuilder out = new StringBuilder();
    for (int i = 1; i < raw.length(); i++) {
      char ch = raw.charAt(i);
      if (ch == '\\') {
        if (i + 1 >= raw.length()) throw new Error("unclosed escape in quoted string", lineNo);
        char next = raw.charAt(++i);
        if (next == '\\' || next == '"') {
          out.append(next);
          continue;
        }
        throw new Error("invalid escape \\" + next + " in quoted string", lineNo);
      }
      if (ch == '"') {
        return new String[] {out.toString(), String.valueOf(i + 1)};
      }
      out.append(ch);
    }
    throw new Error("unclosed quoted string", lineNo);
  }

  private static String[] splitKey(String text, int n) {
    if (text.startsWith("\"")) {
      String[] kv = parseQuotedPrefix(text, n);
      String key = kv[0];
      int end = Integer.parseInt(kv[1]);
      String after = text.substring(end);
      if (after.equals(":")) return new String[] {key, ""};
      if (after.startsWith(": ")) return new String[] {key, after.substring(2)};
      throw new Error("expected ': ' or trailing ':' after quoted key", n);
    }
    int idx = text.indexOf(": ");
    if (idx > 0) {
      String key = text.substring(0, idx);
      if (key.endsWith(":")) throw new Error("key must not end with ':': '" + key + "'", n);
      return new String[] {key, text.substring(idx + 2)};
    }
    if (text.endsWith(":") && text.length() > 1) {
      String key = text.substring(0, text.length() - 1);
      if (key.endsWith(":")) throw new Error("key must not end with ':': '" + key + "'", n);
      return new String[] {key, ""};
    }
    throw new Error("expected ': ' or trailing ':'", n);
  }

  private static final class Ml {
    final String closer;
    final String chomp;

    Ml(String closer, String chomp) {
      this.closer = closer;
      this.chomp = chomp;
    }
  }

  private static Ml matchMultiline(String raw) {
    if (raw.equals("|")) return new Ml("|", "exact");
    if (raw.equals("|-")) return new Ml("|", "strip");
    if (raw.equals("|+")) return new Ml("|", "clip");
    Matcher m = Pattern.compile("^\\|([A-Za-z_][A-Za-z0-9_]*)([-+]?)$").matcher(raw);
    if (m.matches()) {
      String chomp = m.group(2).equals("-") ? "strip" : m.group(2).equals("+") ? "clip" : "exact";
      return new Ml(m.group(1), chomp);
    }
    return null;
  }

  private static boolean looksLikeDictEntry(String s) {
    if (s.isEmpty() || s.startsWith("!") || s.startsWith("\"") || s.startsWith("|")) return false;
    // RFC-0002: inline object literal {key: val, ...} counts as a dict entry
    if (s.startsWith("{")) return true;
    return s.contains(": ") || (s.endsWith(":") && s.length() > 1);
  }

  // RFC-0002: split a string by top-level commas, respecting brace, bracket,
  // and quote nesting.
  private static List<String> splitTopLevelCommas(String inner, int lineNo) {
    List<String> out = new ArrayList<>();
    int start = 0;
    int brace = 0;
    int bracket = 0;
    boolean inQuote = false;
    boolean escape = false;
    for (int i = 0; i < inner.length(); i++) {
      char c = inner.charAt(i);
      if (escape) { escape = false; continue; }
      if (inQuote) {
        if (c == '\\') { escape = true; continue; }
        if (c == '"') inQuote = false;
        continue;
      }
      if (c == '"') { inQuote = true; continue; }
      if (c == '{') { brace++; continue; }
      if (c == '}') { brace--; continue; }
      if (c == '[') { bracket++; continue; }
      if (c == ']') { bracket--; continue; }
      if (c == ',' && brace == 0 && bracket == 0) {
        out.add(inner.substring(start, i));
        start = i + 1;
      }
    }
    String last = inner.substring(start);
    if (!last.trim().isEmpty()) out.add(last);
    return out;
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

  // Glyphs that JavaScript Number() would coerce, plus syntactic specials.
  private static final Pattern LOOKS_LIKE_JS_NUMBER =
      Pattern.compile(
          "^[ \\t\\n\\r\\f\\u000b]*[+-]?(?:Infinity|0[xX][0-9a-fA-F]+|0[bB][01]+|0[oO][0-7]+|(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][+-]?\\d+)?)[ \\t\\n\\r\\f\\u000b]*$");

  private static boolean needsStringTag(String s) {
    return s.startsWith("!")
        || s.equals("[]")
        || s.equals("{}")
        || s.startsWith("|")
        || LOOKS_LIKE_JS_NUMBER.matcher(s).matches();
  }

  private static String parseQuotedString(String raw, int lineNo) {
    if (!raw.startsWith("\"")) throw new Error("quoted string must start with '\"'", lineNo);
    StringBuilder out = new StringBuilder();
    for (int i = 1; i < raw.length(); i++) {
      char ch = raw.charAt(i);
      if (ch == '\\') {
        if (i + 1 >= raw.length()) throw new Error("unclosed escape in quoted string", lineNo);
        char next = raw.charAt(++i);
        if (next == '\\' || next == '"') {
          out.append(next);
          continue;
        }
        throw new Error("invalid escape \\" + next + " in quoted string", lineNo);
      }
      if (ch == '"') {
        if (i != raw.length() - 1) throw new Error("unexpected trailing content after quoted string", lineNo);
        return out.toString();
      }
      out.append(ch);
    }
    throw new Error("unclosed quoted string", lineNo);
  }

  private static boolean needsQuotedGlyph(String s) {
    return !s.equals(s.trim())
        || s.indexOf('"') >= 0
        || s.indexOf('\\') >= 0
        || s.contains(" #")
        || s.startsWith("#")
        || s.contains(": ")
        || (s.endsWith(":") && s.length() > 1)
        || s.equals("|")
        || s.startsWith("|");
  }

  private static String quoteGlyph(String s) {
    StringBuilder out = new StringBuilder("\"");
    for (int i = 0; i < s.length(); i++) {
      char ch = s.charAt(i);
      if (ch == '\\') out.append("\\\\");
      else if (ch == '"') out.append("\\\"");
      else out.append(ch);
    }
    return out.append('"').toString();
  }

  private static String encodeStringGlyph(String s) {
    String body = needsQuotedGlyph(s) ? quoteGlyph(s) : s;
    return needsStringTag(s) ? "!s " + body : body;
  }

  private static String parseStringBody(String body, int lineNo) {
    if (body.startsWith("\"")) return parseQuotedString(body, lineNo);
    return body;
  }

  /** Convert a java.time.Duration into a XUN !du glyph (e.g. 1h30m15s). */
  private static String durationToGlyph(java.time.Duration d) {
    long totalMs = d.toMillis();
    StringBuilder sb = new StringBuilder();
    long days = totalMs / 86400000L;
    totalMs %= 86400000L;
    long hours = totalMs / 3600000L;
    totalMs %= 3600000L;
    long minutes = totalMs / 60000L;
    totalMs %= 60000L;
    double seconds = totalMs / 1000.0;
    if (days > 0) sb.append(days).append('d');
    if (hours > 0) sb.append(hours).append('h');
    if (minutes > 0) sb.append(minutes).append('m');
    if (seconds > 0 || sb.length() == 0) {
      if (seconds == Math.floor(seconds) && !Double.isInfinite(seconds)) {
        sb.append((long) seconds).append('s');
      } else {
        sb.append(seconds).append('s');
      }
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
        return new Tagged("x", glyph);
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
        return new Tagged("o", glyph);
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
        if (glyph.isEmpty() || !glyph.matches("^(\\d+d)?(\\d+h)?(\\d+m)?(\\d+(\\.\\d+)?s)?(\\d+(\\.\\d+)?ms)?$")) {
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
        parseUnix(glyph, n);
        return new Tagged("unix", glyph);
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

  /** Encode a double/float as !i when integral (JS Number.isInteger), else !f. */
  private static String encodeNumberTag(Number v) {
    double d = v.doubleValue();
    if (!Double.isNaN(d) && !Double.isInfinite(d) && d == Math.rint(d) && d >= Long.MIN_VALUE && d <= Long.MAX_VALUE) {
      return "!i " + (long) d;
    }
    String s = String.valueOf(d);
    if (Float.class.isInstance(v)) s = String.valueOf(v.floatValue());
    if (!s.contains(".") && !s.contains("e") && !s.contains("E")) {
      s += ".0";
    }
    return "!f " + s;
  }

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
          for (String line : s.split("\\R", -1)) {
            lines.add(indent + "  " + line);
          }
          lines.add(indent + "|");
        } else if (s.isEmpty()) {
          lines.add(indent + key + ":");
        } else {
          lines.add(indent + key + ": " + encodeStringGlyph(s));
        }
      } else if (v instanceof Boolean) {
        lines.add(indent + key + ": !b " + (((Boolean) v) ? "true" : "false"));
      } else if (v instanceof Long || v instanceof Integer || v instanceof Short || v instanceof Byte) {
        lines.add(indent + key + ": !i " + v);
      } else if (v instanceof Double || v instanceof Float) {
        lines.add(indent + key + ": " + encodeNumberTag((Number) v));
      } else if (v instanceof byte[]) {
        lines.add(indent + key + ": !xb " + hexEncode((byte[]) v).toUpperCase());
      } else if (v instanceof java.time.Instant) {
        lines.add(indent + key + ": !dt " + v);
      } else if (v instanceof java.time.LocalDate) {
        lines.add(indent + key + ": !d " + v);
      } else if (v instanceof java.time.LocalTime) {
        lines.add(indent + key + ": !t " + v);
      } else if (v instanceof java.time.ZoneId) {
        lines.add(indent + key + ": !tz " + v);
      } else if (v instanceof java.time.Duration) {
        lines.add(indent + key + ": !du " + durationToGlyph((java.time.Duration) v));
      } else if (v instanceof Character) {
        lines.add(indent + key + ": !c " + v);
      } else if (v instanceof int[]) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < ((int[]) v).length; i++) {
          if (i > 0) sb.append('.');
          sb.append(((int[]) v)[i]);
        }
        lines.add(indent + key + ": !ver " + sb);
      } else if (v instanceof java.util.UUID) {
        lines.add(indent + key + ": !uuid " + v);
      } else if (v instanceof java.net.InetAddress) {
        lines.add(indent + key + ": !ip " + ((java.net.InetAddress) v).getHostAddress());
      } else if (v instanceof java.util.Date) {
        lines.add(indent + key + ": !dt " + ((java.util.Date) v).toInstant().toString());
      } else if (v instanceof Tagged) {
        Tagged t = (Tagged) v;
        if (t.value.contains("\n") || t.value.contains("\r")) {
          lines.add(indent + key + ": !" + t.tag + " |");
          for (String line : t.value.split("\\R", -1)) {
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
          for (String line : s.split("\\R", -1)) {
            lines.add(indent + "  " + line);
          }
          lines.add(indent + "|");
        } else if (s.isEmpty()) {
          lines.add(indent + "-");
        } else {
          lines.add(indent + "- " + encodeStringGlyph(s));
        }
      } else if (v instanceof Boolean) {
        lines.add(indent + "- !b " + (((Boolean) v) ? "true" : "false"));
      } else if (v instanceof Long || v instanceof Integer || v instanceof Short || v instanceof Byte) {
        lines.add(indent + "- !i " + v);
      } else if (v instanceof Double || v instanceof Float) {
        lines.add(indent + "- " + encodeNumberTag((Number) v));
      } else if (v instanceof byte[]) {
        lines.add(indent + "- !xb " + hexEncode((byte[]) v).toUpperCase());
      } else if (v instanceof java.time.Instant) {
        lines.add(indent + "- !dt " + v);
      } else if (v instanceof java.time.LocalDate) {
        lines.add(indent + "- !d " + v);
      } else if (v instanceof java.time.LocalTime) {
        lines.add(indent + "- !t " + v);
      } else if (v instanceof java.time.ZoneId) {
        lines.add(indent + "- !tz " + v);
      } else if (v instanceof java.time.Duration) {
        lines.add(indent + "- !du " + durationToGlyph((java.time.Duration) v));
      } else if (v instanceof Character) {
        lines.add(indent + "- !c " + v);
      } else if (v instanceof int[]) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < ((int[]) v).length; i++) {
          if (i > 0) sb.append('.');
          sb.append(((int[]) v)[i]);
        }
        lines.add(indent + "- !ver " + sb);
      } else if (v instanceof java.util.UUID) {
        lines.add(indent + "- !uuid " + v);
      } else if (v instanceof java.net.InetAddress) {
        lines.add(indent + "- !ip " + ((java.net.InetAddress) v).getHostAddress());
      } else if (v instanceof java.util.Date) {
        lines.add(indent + "- !dt " + ((java.util.Date) v).toInstant().toString());
      } else if (v instanceof Tagged) {
        Tagged t = (Tagged) v;
        if (t.value.contains("\n") || t.value.contains("\r")) {
          lines.add(indent + "- !" + t.tag + " |");
          for (String line : t.value.split("\\R", -1)) {
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
