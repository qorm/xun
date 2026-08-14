package xun

import (
	"encoding/base64"
	"encoding/hex"
	"fmt"
	"net"
	"regexp"
	"strconv"
	"strings"
)

const (
	maxBytes = 1024 * 1024
	maxDepth = 64
)

type Error struct {
	Line int
	Msg  string
}

func (e *Error) Error() string {
	if e.Line == 0 {
		return e.Msg
	}
	return fmt.Sprintf("line %d: %s", e.Line, e.Msg)
}

func fail(line int, format string, args ...any) {
	panic(&Error{Line: line, Msg: fmt.Sprintf(format, args...)})
}

type Tagged struct {
	Tag   string
	Value string
}

type line struct {
	raw    string
	indent int
	text   string
	n      int
	blank  bool
}

type parser struct {
	lines     []line
	i         int
	env       map[string]any
	resolving map[string]bool
}

func Parse(source string) (doc any, err error) {
	defer func() {
		if r := recover(); r != nil {
			if e, ok := r.(*Error); ok {
				err = e
				doc = nil
			} else {
				panic(r)
			}
		}
	}()
	if strings.ContainsRune(source, 0) {
		fail(0, "NUL is not allowed")
	}
	if len(source) > maxBytes {
		fail(0, "document exceeds 1MB")
	}
	source = strings.TrimPrefix(source, "\ufeff")
	p := &parser{lines: splitLines(source), env: map[string]any{}, resolving: map[string]bool{}}
	return p.parseDocument(), nil
}

func splitLines(source string) []line {
	if source == "" {
		return nil
	}
	var out []line
	n, start := 1, 0
	for i := 0; i <= len(source); i++ {
		atEnd := i == len(source)
		var c byte
		if !atEnd {
			c = source[i]
		}
		if !atEnd && c != '\n' && c != '\r' {
			continue
		}
		raw := source[start:i]
		if c == '\r' && i+1 < len(source) && source[i+1] == '\n' {
			i++
		}
		out = append(out, makeLine(raw, n))
		n++
		start = i + 1
	}
	return out
}

func makeLine(raw string, n int) line {
	i := 0
	for i < len(raw) && raw[i] == ' ' {
		i++
	}
	if i < len(raw) && raw[i] == '\t' {
		fail(n, "tab is not allowed")
	}
	if i%2 != 0 {
		fail(n, "indent must be a multiple of 2")
	}
	text := strings.TrimRight(raw[i:], " \t")
	return line{raw: raw, indent: i, text: text, n: n, blank: text == ""}
}

func (p *parser) peek() *line {
	if p.i >= len(p.lines) {
		return nil
	}
	return &p.lines[p.i]
}

func (p *parser) skipNoise() {
	for p.peek() != nil {
		l := p.peek()
		if l.blank || strings.HasPrefix(l.text, "#") {
			p.i++
			continue
		}
		break
	}
}

func (p *parser) parseDocument() any {
	p.parseVars()
	p.skipNoise()
	if p.peek() == nil {
		return map[string]any{}
	}
	first := p.peek()
	if first.indent != 0 {
		fail(first.n, "document must start at indent 0")
	}
	if p.isListItem(first) {
		fail(first.n, "root must be a dictionary")
	}
	return p.parseDict(0, 0)
}

var varDef = regexp.MustCompile(`^\$([A-Za-z_][A-Za-z0-9_]*):(.*)$`)

func (p *parser) parseVars() {
	for p.peek() != nil {
		l := p.peek()
		if l.blank || strings.HasPrefix(l.text, "#") {
			p.i++
			continue
		}
		if l.indent != 0 || !strings.HasPrefix(l.text, "$") {
			break
		}
		m := varDef.FindStringSubmatch(l.text)
		if m == nil {
			fail(l.n, "invalid variable definition")
		}
		if m[2] != "" && !strings.HasPrefix(m[2], " ") {
			fail(l.n, "expected ': ' in variable definition")
		}
		name := m[1]
		if _, ok := p.env[name]; ok {
			fail(l.n, "duplicate variable $%s", name)
		}
		p.i++
		raw := ""
		if strings.HasPrefix(m[2], " ") {
			raw = m[2][1:]
		}
		p.env[name] = p.parseValue(raw, 0, l.n, 1)
	}
}

func (p *parser) parseDict(indent, depth int) map[string]any {
	if depth > maxDepth {
		n := 0
		if p.peek() != nil {
			n = p.peek().n
		}
		fail(n, "nesting exceeds 64")
	}
	obj := map[string]any{}
	for p.peek() != nil {
		p.skipNoise()
		l := p.peek()
		if l == nil || l.blank {
			break
		}
		if l.indent < indent {
			break
		}
		if l.indent > indent {
			fail(l.n, "invalid indent jump")
		}
		if strings.HasPrefix(l.text, "$") && indent == 0 {
			fail(l.n, "variable definitions only allowed at file start")
		}
		if p.isListItem(l) {
			fail(l.n, "cannot mix list items into a dictionary")
		}
		key, rest := splitKey(l.text, l.n)
		if _, ok := obj[key]; ok {
			fail(l.n, "duplicate key '%s'", key)
		}
		p.i++
		obj[key] = p.parseValue(rest, indent, l.n, depth+1)
	}
	return obj
}

func (p *parser) parseList(indent, depth int, itemTag string) []any {
	if depth > maxDepth {
		fail(p.peek().n, "nesting exceeds 64")
	}
	var arr []any
	for p.peek() != nil {
		p.skipNoise()
		l := p.peek()
		if l == nil || l.blank {
			break
		}
		if l.indent < indent {
			break
		}
		if l.indent > indent {
			fail(l.n, "invalid indent jump")
		}
		if !p.isListItem(l) {
			fail(l.n, "cannot mix dictionary keys into a list")
		}
		rest := ""
		if l.text != "-" {
			rest = l.text[2:]
		}
		p.i++
		val := p.parseValue(rest, indent, l.n, depth+1)
		if itemTag != "" {
			val = applyTag(itemTag, glyphOf(val), l.n)
		}
		arr = append(arr, val)
	}
	if arr == nil {
		arr = []any{}
	}
	return arr
}

func (p *parser) isListItem(l *line) bool {
	return l.text == "-" || strings.HasPrefix(l.text, "- ")
}

func (p *parser) parseValue(raw string, parentIndent, lineNo, depth int) any {
	if raw == "[]" {
		return []any{}
	}
	if raw == "{}" {
		return map[string]any{}
	}
	if closer, ok := matchMultiline(raw); ok {
		return p.readMultiline(parentIndent, "", closer, lineNo)
	}
	if strings.HasPrefix(raw, "!") {
		return p.parseTagged(raw, parentIndent, lineNo, depth)
	}
	if raw == "" {
		return p.parseEmptyOrNested(parentIndent, lineNo, depth, "")
	}
	if isWholeRef(raw) {
		return p.lookup(raw[1:], lineNo)
	}
	return interpolate(raw, func(name string) any { return p.lookup(name, lineNo) })
}

var tagRe = regexp.MustCompile(`^!([A-Za-z_][A-Za-z0-9_]*)(.*)$`)

func (p *parser) parseTagged(raw string, parentIndent, lineNo, depth int) any {
	m := tagRe.FindStringSubmatch(raw)
	if m == nil {
		fail(lineNo, "invalid type tag")
	}
	tag, rest := m[1], m[2]
	if strings.HasPrefix(rest, "[") {
		if tag == "s" && rest != "[]" {
			fail(lineNo, "string arrays cannot use compact form")
		}
		if !strings.HasSuffix(rest, "]") {
			fail(lineNo, "unclosed compact array")
		}
		inner := rest[1 : len(rest)-1]
		if inner == "" {
			return p.parseEmptyOrNested(parentIndent, lineNo, depth, tag)
		}
		parts := splitCompact(inner)
		out := make([]any, len(parts))
		for i, g := range parts {
			out[i] = applyTag(tag, g, lineNo)
		}
		return out
	}
	if rest == "" {
		fail(lineNo, "missing value for !%s", tag)
	}
	if !strings.HasPrefix(rest, " ") {
		fail(lineNo, "expected space after type tag")
	}
	body := rest[1:]
	if closer, ok := matchMultiline(body); ok {
		text := p.readMultiline(parentIndent, "", closer, lineNo).(string)
		if tag == "s" {
			return text
		}
		return applyTag(tag, text, lineNo)
	}
	if tag == "s" {
		return body
	}
	if isWholeRef(body) {
		return p.lookup(body[1:], lineNo)
	}
	return applyTag(tag, body, lineNo)
}

func (p *parser) parseEmptyOrNested(parentIndent, lineNo, depth int, itemTag string) any {
	p.skipNoise()
	n := p.peek()
	child := parentIndent + 2
	if n == nil || n.blank || n.indent <= parentIndent {
		if itemTag != "" {
			return []any{}
		}
		return ""
	}
	if n.indent != child {
		fail(n.n, "child indent must be parent + 2")
	}
	if p.isListItem(n) {
		return p.parseList(child, depth, itemTag)
	}
	if itemTag != "" {
		fail(n.n, "!%s[] expected list items", itemTag)
	}
	return p.parseDict(child, depth)
}

func (p *parser) readMultiline(parentIndent int, tag, closer string, lineNo int) any {
	base := parentIndent + 2
	var parts []string
	for p.peek() != nil {
		l := p.peek()
		stripped := strings.TrimRight(l.raw, " \t")
		content := strings.TrimLeft(stripped, " ")
		ind := len(l.raw) - len(strings.TrimLeft(l.raw, " "))
		if !l.blank && ind == parentIndent && content == closer {
			p.i++
			s := strings.Join(parts, "\n")
			if tag != "" && tag != "s" {
				return applyTag(tag, s, lineNo)
			}
			return s
		}
		if l.blank {
			parts = append(parts, "")
			p.i++
			continue
		}
		if ind < base {
			fail(l.n, "multiline body must indent +2, or close at opener indent")
		}
		if strings.Contains(l.raw, "\t") {
			fail(l.n, "tab is not allowed")
		}
		if len(l.raw) < base {
			parts = append(parts, "")
		} else {
			parts = append(parts, l.raw[base:])
		}
		p.i++
	}
	fail(lineNo, "unclosed multiline block")
	return ""
}

func (p *parser) lookup(name string, lineNo int) any {
	v, ok := p.env[name]
	if !ok {
		fail(lineNo, "undefined variable $%s", name)
	}
	if p.resolving[name] {
		fail(lineNo, "cyclic variable $%s", name)
	}
	if s, ok := v.(string); ok && isWholeRef(s) {
		p.resolving[name] = true
		defer delete(p.resolving, name)
		r := p.lookup(s[1:], lineNo)
		p.env[name] = r
		return r
	}
	return v
}

func splitKey(text string, n int) (string, string) {
	if i := strings.Index(text, ": "); i > 0 {
		return text[:i], text[i+2:]
	}
	if strings.HasSuffix(text, ":") && len(text) > 1 {
		return text[:len(text)-1], ""
	}
	fail(n, "expected ': ' or trailing ':'")
	return "", ""
}

var taggedCloser = regexp.MustCompile(`^\|([A-Za-z_][A-Za-z0-9_]*)$`)

func matchMultiline(raw string) (string, bool) {
	if raw == "|" {
		return "|", true
	}
	if m := taggedCloser.FindStringSubmatch(raw); m != nil {
		return m[1], true
	}
	return "", false
}

func isWholeRef(raw string) bool {
	ok, _ := regexp.MatchString(`^\$[A-Za-z_][A-Za-z0-9_]*$`, raw)
	return ok
}

var interpRe = regexp.MustCompile(`\$\{([A-Za-z_][A-Za-z0-9_]*)\}`)

func interpolate(s string, get func(string) any) string {
	return interpRe.ReplaceAllStringFunc(s, func(m string) string {
		name := m[2 : len(m)-1]
		return glyphOf(get(name))
	})
}

func glyphOf(v any) string {
	switch t := v.(type) {
	case Tagged:
		return t.Value
	case []byte:
		return hex.EncodeToString(t)
	case string:
		return t
	case bool:
		if t {
			return "true"
		}
		return "false"
	case int64:
		return strconv.FormatInt(t, 10)
	case float64:
		return strconv.FormatFloat(t, 'g', -1, 64)
	default:
		fail(0, "cannot interpolate a collection")
		return ""
	}
}

func splitCompact(inner string) []string {
	parts := strings.Split(inner, ",")
	for i, p := range parts {
		parts[i] = strings.TrimSpace(p)
	}
	return parts
}

func applyTag(tag, glyph string, n int) any {
	switch tag {
	case "s":
		return glyph
	case "n":
		return parseN(glyph, n)
	case "i":
		return parseI(glyph, n)
	case "f":
		return parseF(glyph, n)
	case "x":
		v, err := strconv.ParseInt(stripUnderscores(glyph, n), 16, 64)
		if err != nil {
			fail(n, "invalid hex integer")
		}
		return v
	case "xb":
		s := strings.ReplaceAll(glyph, "_", "")
		b, err := hex.DecodeString(s)
		if err != nil || len(s)%2 != 0 || len(s) == 0 {
			fail(n, "hex bytes must be an even number of digits")
		}
		return b
	case "o":
		v, err := strconv.ParseInt(glyph, 8, 64)
		if err != nil {
			fail(n, "invalid octal")
		}
		return v
	case "b":
		if glyph == "true" {
			return true
		}
		if glyph == "false" {
			return false
		}
		fail(n, "boolean must be true or false")
	case "d":
		if ok, _ := regexp.MatchString(`^\d{4}-\d{2}-\d{2}$`, glyph); !ok {
			fail(n, "invalid date")
		}
		return Tagged{Tag: "d", Value: glyph}
	case "t":
		if ok, _ := regexp.MatchString(`^\d{2}:\d{2}(:\d{2}(\.\d+)?)?$`, glyph); !ok {
			fail(n, "invalid time")
		}
		return Tagged{Tag: "t", Value: glyph}
	case "dt":
		if ok, _ := regexp.MatchString(`^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d+)?(Z|[+-]\d{2}:\d{2})$`, glyph); !ok {
			fail(n, "datetime must include a timezone offset")
		}
		return Tagged{Tag: "dt", Value: glyph}
	case "tz":
		ok1, _ := regexp.MatchString(`^[+-]\d{2}:\d{2}$`, glyph)
		ok2, _ := regexp.MatchString(`^[A-Za-z_]+(/[A-Za-z0-9_+-]+)+$`, glyph)
		if glyph != "Z" && glyph != "UTC" && !ok1 && !ok2 {
			fail(n, "invalid time zone")
		}
		return Tagged{Tag: "tz", Value: glyph}
	case "du":
		ok, _ := regexp.MatchString(`^(\d+d)?(\d+h)?(\d+m)?(\d+(\.\d+)?s)?$`, glyph)
		if !ok || glyph == "" {
			fail(n, "invalid duration")
		}
		return Tagged{Tag: "du", Value: glyph}
	case "sz":
		ok, _ := regexp.MatchString(`^\d+(\.\d+)?(B|KB|MB|GB|TB|PB|KiB|MiB|GiB|TiB|PiB)$`, glyph)
		if !ok {
			fail(n, "invalid data size")
		}
		return Tagged{Tag: "sz", Value: glyph}
	case "unix":
		return parseUnix(glyph, n)
	case "ver":
		if ok, _ := regexp.MatchString(`^\d+(\.\d+)*$`, glyph); !ok {
			fail(n, "invalid version")
		}
		return Tagged{Tag: "ver", Value: glyph}
	case "uuid":
		if ok, _ := regexp.MatchString(`^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$`, glyph); !ok {
			fail(n, "invalid uuid")
		}
		return Tagged{Tag: "uuid", Value: glyph}
	case "ip":
		if net.ParseIP(glyph) == nil {
			fail(n, "invalid ip")
		}
		return Tagged{Tag: "ip", Value: glyph}
	case "b64":
		s := regexp.MustCompile(`\s+`).ReplaceAllString(glyph, "")
		b, err := base64.StdEncoding.DecodeString(s)
		if err != nil {
			fail(n, "invalid base64")
		}
		return b
	case "c":
		if m := regexp.MustCompile(`^U\+([0-9A-Fa-f]{4,6})$`).FindStringSubmatch(glyph); m != nil {
			cp, _ := strconv.ParseInt(m[1], 16, 32)
			return Tagged{Tag: "c", Value: string(rune(cp))}
		}
		if len([]rune(glyph)) != 1 {
			fail(n, "character must be a single scalar")
		}
		return Tagged{Tag: "c", Value: glyph}
	default:
		return Tagged{Tag: tag, Value: glyph}
	}
	return nil
}

func stripUnderscores(s string, n int) string {
	if strings.Contains(s, "__") || strings.HasPrefix(s, "_") || strings.HasSuffix(s, "_") {
		fail(n, "invalid numeric underscores")
	}
	return strings.ReplaceAll(s, "_", "")
}

func parseN(g string, n int) any {
	s := stripUnderscores(g, n)
	if regexp.MustCompile(`^-?0\d`).MatchString(s) {
		fail(n, "leading zeros are not allowed")
	}
	if ok, _ := regexp.MatchString(`^-?\d+$`, s); ok {
		v, err := strconv.ParseInt(s, 10, 64)
		if err != nil {
			fail(n, "integer overflow")
		}
		return v
	}
	v, err := strconv.ParseFloat(s, 64)
	if err != nil {
		fail(n, "invalid number")
	}
	return v
}

func parseI(g string, n int) int64 {
	s := stripUnderscores(g, n)
	if ok, _ := regexp.MatchString(`^-?\d+$`, s); !ok {
		fail(n, "invalid integer")
	}
	if regexp.MustCompile(`^-?0\d`).MatchString(s) {
		fail(n, "leading zeros are not allowed")
	}
	v, err := strconv.ParseInt(s, 10, 64)
	if err != nil {
		fail(n, "integer overflow")
	}
	return v
}

func parseF(g string, n int) float64 {
	s := stripUnderscores(g, n)
	if !strings.Contains(s, ".") && !strings.ContainsAny(s, "eE") {
		fail(n, "float must contain '.' or 'e'")
	}
	v, err := strconv.ParseFloat(s, 64)
	if err != nil {
		fail(n, "invalid float")
	}
	return v
}

func parseUnix(g string, n int) any {
	s := stripUnderscores(g, n)
	if regexp.MustCompile(`^-?0\d`).MatchString(s) {
		fail(n, "leading zeros are not allowed")
	}
	if ok, _ := regexp.MatchString(`^-?\d+$`, s); ok {
		v, _ := strconv.ParseInt(s, 10, 64)
		return v
	}
	v, err := strconv.ParseFloat(s, 64)
	if err != nil {
		fail(n, "invalid unix timestamp")
	}
	return v
}
