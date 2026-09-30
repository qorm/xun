package xun

import (
	"encoding/base64"
	"encoding/hex"
	"fmt"
	"net"
	"reflect"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"
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

func (t Tagged) AsTime() (time.Time, error) {
	switch t.Tag {
	case "dt":
		return time.Parse(time.RFC3339Nano, t.Value)
	case "d":
		return time.Parse("2006-01-02", t.Value)
	case "t":
		for _, layout := range []string{"15:04:05.999999999", "15:04:05", "15:04"} {
			if tm, err := time.Parse(layout, t.Value); err == nil {
				return tm, nil
			}
		}
		return time.Time{}, fmt.Errorf("invalid time value %q", t.Value)
	}
	return time.Time{}, fmt.Errorf("cannot convert !%s to time.Time", t.Tag)
}

func (t Tagged) AsIP() (net.IP, error) {
	if t.Tag != "ip" {
		return nil, fmt.Errorf("cannot convert !%s to net.IP", t.Tag)
	}
	ip := net.ParseIP(t.Value)
	if ip == nil {
		return nil, fmt.Errorf("invalid IP: %s", t.Value)
	}
	return ip, nil
}

func (t Tagged) AsUUID() ([16]byte, error) {
	if t.Tag != "uuid" {
		return [16]byte{}, fmt.Errorf("cannot convert !%s to UUID", t.Tag)
	}
	compact := strings.ReplaceAll(t.Value, "-", "")
	if len(compact) != 32 {
		return [16]byte{}, fmt.Errorf("invalid UUID: %s", t.Value)
	}
	raw, err := hex.DecodeString(compact)
	if err != nil {
		return [16]byte{}, fmt.Errorf("invalid UUID: %s", t.Value)
	}
	var out [16]byte
	copy(out[:], raw)
	return out, nil
}

func (t Tagged) AsChar() (rune, error) {
	if t.Tag != "c" {
		return 0, fmt.Errorf("cannot convert !%s to char", t.Tag)
	}
	if m := cpRe.FindStringSubmatch(t.Value); m != nil {
		cp, err := strconv.ParseInt(m[1], 16, 32)
		if err != nil || cp > 0x10ffff {
			return 0, fmt.Errorf("invalid code point %q", t.Value)
		}
		return rune(cp), nil
	}
	runes := []rune(t.Value)
	if len(runes) != 1 {
		return 0, fmt.Errorf("value %q is not a single character", t.Value)
	}
	return runes[0], nil
}

func (t Tagged) AsBytes() ([]byte, error) {
	if t.Tag == "xb" {
		s := strings.ReplaceAll(t.Value, "_", "")
		return hex.DecodeString(s)
	}
	if t.Tag == "b64" {
		s := regexp.MustCompile(`\s+`).ReplaceAllString(t.Value, "")
		return base64.StdEncoding.DecodeString(s)
	}
	return nil, fmt.Errorf("cannot convert !%s to bytes", t.Tag)
}

func (t Tagged) AsSizeBytes() (int64, error) {
	if t.Tag != "sz" {
		return 0, fmt.Errorf("cannot convert !%s to size bytes", t.Tag)
	}
	return ParseSize(t.Value)
}

func (t Tagged) AsDuration() (time.Duration, error) {
	if t.Tag != "du" {
		return 0, fmt.Errorf("cannot convert !%s to duration", t.Tag)
	}
	return ParseDuration(t.Value)
}

func (t Tagged) AsVersion() ([]int, error) {
	if t.Tag != "ver" {
		return nil, fmt.Errorf("cannot convert !%s to version", t.Tag)
	}
	return ParseVersion(t.Value)
}

func ParseSize(s string) (int64, error) {
	units := map[string]int64{
		"B":   1,
		"KB":  1000,
		"MB":  1000 * 1000,
		"GB":  1000 * 1000 * 1000,
		"TB":  1000 * 1000 * 1000 * 1000,
		"KiB": 1024,
		"MiB": 1024 * 1024,
		"GiB": 1024 * 1024 * 1024,
		"TiB": 1024 * 1024 * 1024 * 1024,
	}
	re := regexp.MustCompile(`^(\d+(?:\.\d+)?)(B|KB|MB|GB|TB|KiB|MiB|GiB|TiB)$`)
	matches := re.FindStringSubmatch(s)
	if len(matches) < 3 {
		return 0, fmt.Errorf("invalid size format %q", s)
	}
	val, err := strconv.ParseFloat(matches[1], 64)
	if err != nil {
		return 0, err
	}
	unitVal := units[matches[2]]
	return int64(val * float64(unitVal)), nil
}

func ParseDuration(s string) (time.Duration, error) {
	if s == "" {
		return 0, fmt.Errorf("empty duration")
	}
	re := regexp.MustCompile(`^(?:(\d+)d)?(?:(\d+)h)?(?:(\d+)m)?(?:(\d+(?:\.\d+)?)s)?$`)
	matches := re.FindStringSubmatch(s)
	if len(matches) == 0 || (matches[1] == "" && matches[2] == "" && matches[3] == "" && matches[4] == "") {
		return 0, fmt.Errorf("invalid duration format %q", s)
	}
	var total time.Duration
	if matches[1] != "" {
		d, _ := strconv.Atoi(matches[1])
		total += time.Duration(d) * 24 * time.Hour
	}
	if matches[2] != "" {
		h, _ := strconv.Atoi(matches[2])
		total += time.Duration(h) * time.Hour
	}
	if matches[3] != "" {
		m, _ := strconv.Atoi(matches[3])
		total += time.Duration(m) * time.Minute
	}
	if matches[4] != "" {
		sec, _ := strconv.ParseFloat(matches[4], 64)
		total += time.Duration(sec * float64(time.Second))
	}
	return total, nil
}

func ParseVersion(s string) ([]int, error) {
	parts := strings.Split(s, ".")
	res := make([]int, 0, len(parts))
	for _, p := range parts {
		n, err := strconv.Atoi(p)
		if err != nil {
			return nil, fmt.Errorf("invalid version %q: %w", s, err)
		}
		res = append(res, n)
	}
	return res, nil
}

type line struct {
	raw    string
	indent int
	text   string
	code   string
	n      int
	blank  bool
}

type parser struct {
	lines      []line
	i          int
	openBlocks []blockRef
}

// blockRef tracks an open dict/list/value block so a trailing 'end' or
// 'end <key>' line can be validated against its enclosing key (RFC-0001).
type blockRef struct {
	indent int
	key    *string
}

func Decode(source string) (doc any, err error) {
	return Parse(source)
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
	p := &parser{lines: splitLines(source)}
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
	return line{raw: raw, indent: i, text: text, code: stripTrailingComment(text), n: n, blank: text == ""}
}

// stripTrailingComment removes a trailing " #" comment that sits outside
// quoted strings. "#" at the start of the text or after a space/tab opens
// a comment; otherwise it is literal ("foo#bar"). Required by RFC-0001 /
// RFC-0002 so end lines and inline object literals aren't tripped up by
// trailing punctuation.
func stripTrailingComment(text string) string {
	inQuote := false
	for i := 0; i < len(text); i++ {
		ch := text[i]
		if inQuote {
			if ch == '\\' {
				i++
			} else if ch == '"' {
				inQuote = false
			}
			continue
		}
		if ch == '"' {
			inQuote = true
			continue
		}
		if ch == '#' && (i == 0 || text[i-1] == ' ' || text[i-1] == '\t') {
			return strings.TrimRight(text[:i], " \t")
		}
	}
	return text
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
		if l.blank || l.code == "" {
			p.i++
			continue
		}
		break
	}
}

func (p *parser) parseDocument() any {
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
	// RFC-0001: track root block so trailing 'end' can close it.
	p.openBlocks = append(p.openBlocks, blockRef{indent: 0, key: nil})
	defer func() {
		p.openBlocks = p.openBlocks[:len(p.openBlocks)-1]
	}()
	obj := p.parseDict(0, 0, nil)
	p.skipNoise()
	p.tryConsumeEnd(0, nil)
	return obj
}

func (p *parser) parseDict(indent, depth int, dictKey *string) map[string]any {
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
		// RFC-0001: 'end' or 'end <key>' at body indent stops the dict; the
		// caller is responsible for validating and consuming it.
		if l.code == "end" || strings.HasPrefix(l.code, "end ") {
			break
		}
		if p.isListItem(l) {
			fail(l.n, "cannot mix list items into a dictionary")
		}
		key, rest := splitKey(l.code, l.n)
		if _, ok := obj[key]; ok {
			fail(l.n, "duplicate key '%s'", key)
		}
		p.i++
		obj[key] = p.parseValue(rest, indent, l.n, depth+1, &key)
	}
	return obj
}

func (p *parser) parseList(indent, depth int, itemTag string, parentKey *string) []any {
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
		// RFC-0001: 'end' or 'end <key>' at list indent stops parsing the list.
		if l.code == "end" || strings.HasPrefix(l.code, "end ") {
			break
		}
		if !p.isListItem(l) {
			fail(l.n, "cannot mix dictionary keys into a list")
		}
		rest := ""
		if l.code != "-" {
			rest = l.code[2:]
		}
		p.i++
		trimmed := strings.TrimSpace(rest)
		if strings.HasPrefix(trimmed, "{") {
			// RFC-0002: inline object literal {key: value, ...} as a list item.
			if itemTag != "" {
				fail(l.n, "!%s[] cannot contain dictionary entries", itemTag)
			}
			arr = append(arr, p.parseInlineDict(rest, l.n))
			continue
		}
		if looksLikeDictEntry(rest) {
			// "- key: value" / "- key:" — multi-key list-item form.
			if itemTag != "" {
				fail(l.n, "!%s[] cannot contain dictionary entries", itemTag)
			}
			arr = append(arr, p.parseInlineDictEntry(rest, indent+2, l.n, depth+1))
			continue
		}
		val := p.parseValue(rest, indent, l.n, depth+1, nil)
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
	return l.code == "-" || strings.HasPrefix(l.code, "- ")
}

// looksLikeDictEntry reports whether a list-item remainder should be
// parsed as an inline dictionary entry (multi-key form) rather than a
// scalar. The RFC-0002 inline object literal {key: val, ...} is also
// flagged here so callers can dispatch to parseInlineDict.
func looksLikeDictEntry(s string) bool {
	s = strings.TrimSpace(s)
	if s == "" || strings.HasPrefix(s, "!") || strings.HasPrefix(s, `"`) || strings.HasPrefix(s, "|") {
		return false
	}
	if strings.HasPrefix(s, "{") {
		return true
	}
	return strings.Contains(s, ": ") || (strings.HasSuffix(s, ":") && len(s) > 1)
}

func (p *parser) parseValue(raw string, parentIndent, lineNo, depth int, valueKey *string) any {
	raw = stripTrailingComment(raw)
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
		return p.parseTagged(raw, parentIndent, lineNo, depth, valueKey)
	}
	// RFC-0002: untagged compact array of inline objects: [{...}, {...}]
	if strings.HasPrefix(raw, "[") && strings.HasSuffix(raw, "]") && raw != "[]" {
		inner := raw[1 : len(raw)-1]
		pieces := splitTopLevelCommas(inner, lineNo)
		hasObjects := false
		for _, piece := range pieces {
			if strings.HasPrefix(strings.TrimSpace(piece), "{") {
				hasObjects = true
				break
			}
		}
		if hasObjects {
			out := make([]any, len(pieces))
			for i, piece := range pieces {
				t := strings.TrimSpace(piece)
				if strings.HasPrefix(t, "{") {
					out[i] = p.parseInlineDict(t, lineNo)
				} else {
					out[i] = t
				}
			}
			return out
		}
	}
	if raw == "" {
		// RFC-0001: nested block — header at parentIndent, named valueKey.
		p.openBlocks = append(p.openBlocks, blockRef{indent: parentIndent, key: valueKey})
		defer func() {
			p.openBlocks = p.openBlocks[:len(p.openBlocks)-1]
		}()
		result := p.parseEmptyOrNested(parentIndent, lineNo, depth, "", valueKey)
		p.skipNoise()
		p.tryConsumeEnd(parentIndent, valueKey)
		return result
	}
	if strings.HasPrefix(raw, `"`) {
		s, err := parseQuotedString(raw, lineNo)
		if err != nil {
			panic(err)
		}
		return s
	}
	return raw
}

var tagRe = regexp.MustCompile(`^!([A-Za-z_][A-Za-z0-9_]*)(.*)$`)

func (p *parser) parseTagged(raw string, parentIndent, lineNo, depth int, valueKey *string) any {
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
			// RFC-0001: track tagged block for 'end' validation.
			p.openBlocks = append(p.openBlocks, blockRef{indent: parentIndent, key: valueKey})
			defer func() {
				p.openBlocks = p.openBlocks[:len(p.openBlocks)-1]
			}()
			result := p.parseEmptyOrNested(parentIndent, lineNo, depth, tag, valueKey)
			p.skipNoise()
			p.tryConsumeEnd(parentIndent, valueKey)
			return result
		}
		// RFC-0002: detect inline object elements {key: val, ...} within compact arrays.
		pieces := splitTopLevelCommas(inner, lineNo)
		hasObjects := false
		for _, piece := range pieces {
			if strings.HasPrefix(strings.TrimSpace(piece), "{") {
				hasObjects = true
				break
			}
		}
		if hasObjects {
			out := make([]any, len(pieces))
			for i, piece := range pieces {
				t := strings.TrimSpace(piece)
				if strings.HasPrefix(t, "{") {
					out[i] = p.parseInlineDict(t, lineNo)
				} else {
					out[i] = applyTag(tag, t, lineNo)
				}
			}
			return out
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
		return parseStringBody(body, lineNo)
	}
	return applyTag(tag, body, lineNo)
}

func (p *parser) parseEmptyOrNested(parentIndent, lineNo, depth int, itemTag string, valueKey *string) any {
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
		return p.parseList(child, depth, itemTag, valueKey)
	}
	if itemTag != "" {
		fail(n.n, "!%s[] expected list items", itemTag)
	}
	return p.parseDict(child, depth, valueKey)
}

// tryConsumeEnd attempts to consume an 'end' or 'end <key>' line at the
// given indent (RFC-0001). Returns true if it consumed a line. A bare
// 'end' is always accepted when the indent matches; 'end <key>' must
// match expectedKey when expectedKey is non-nil, otherwise it errors.
func (p *parser) tryConsumeEnd(indent int, expectedKey *string) bool {
	l := p.peek()
	if l == nil || l.blank || l.indent != indent {
		return false
	}
	code := l.text
	if code == "end" {
		p.i++
		return true
	}
	if strings.HasPrefix(code, "end ") {
		endKey := strings.TrimSpace(code[4:])
		if endKey == "" {
			fail(l.n, "expected identifier after 'end'")
		}
		if expectedKey != nil && *expectedKey != endKey {
			fail(l.n, "end-key mismatch: expected '%s', got '%s'", *expectedKey, endKey)
		}
		p.i++
		return true
	}
	return false
}

// splitTopLevelCommas splits inner by ',' at depth zero, tracking { } [ ]
// nesting and quoted strings so commas inside objects/arrays/quotes are
// preserved (RFC-0002).
func splitTopLevelCommas(inner string, lineNo int) []string {
	var out []string
	brace, bracket := 0, 0
	inQuote, escape := false, false
	start := 0
	for i := 0; i < len(inner); i++ {
		c := inner[i]
		if escape {
			escape = false
			continue
		}
		if inQuote {
			if c == '\\' {
				escape = true
			} else if c == '"' {
				inQuote = false
			}
			continue
		}
		switch c {
		case '"':
			inQuote = true
		case '{':
			brace++
		case '}':
			if brace > 0 {
				brace--
			}
		case '[':
			bracket++
		case ']':
			if bracket > 0 {
				bracket--
			}
		case ',':
			if brace == 0 && bracket == 0 {
				out = append(out, inner[start:i])
				start = i + 1
			}
		}
	}
	last := inner[start:]
	if strings.TrimSpace(last) != "" {
		out = append(out, last)
	}
	return out
}

// parseInlineDict parses {key: value, key2: value2, ...} literals (RFC-0002).
// Keys may be bare identifiers or quoted strings. Values may be quoted
// strings, tagged values (!tag ... or !tag[...]) — or absent (trailing
// colon), which yields the empty string.
func (p *parser) parseInlineDict(text string, lineNo int) map[string]any {
	trimmed := strings.TrimSpace(text)
	if !strings.HasPrefix(trimmed, "{") || !strings.HasSuffix(trimmed, "}") {
		fail(lineNo, "inline object must be wrapped in '{...}'")
	}
	inner := trimmed[1 : len(trimmed)-1]
	if strings.TrimSpace(inner) == "" {
		return map[string]any{}
	}
	pieces := splitTopLevelCommas(inner, lineNo)
	obj := map[string]any{}
	for _, piece := range pieces {
		t := strings.TrimSpace(piece)
		if t == "" {
			continue
		}
		// Find the top-level key separator ": " or trailing ":".
		inQuote, escape := false, false
		foundIdx := -1
		for i := 0; i < len(t); i++ {
			c := t[i]
			if escape {
				escape = false
				continue
			}
			if inQuote {
				if c == '\\' {
					escape = true
				} else if c == '"' {
					inQuote = false
				}
				continue
			}
			if c == '"' {
				inQuote = true
				continue
			}
			if c == ':' {
				if i == len(t)-1 {
					foundIdx = i
					break
				}
				if i+1 < len(t) && t[i+1] == ' ' {
					foundIdx = i
					break
				}
			}
		}
		if foundIdx == -1 {
			fail(lineNo, "inline object entry missing ': ' separator: '%s'", t)
		}
		keyRaw := strings.TrimSpace(t[:foundIdx])
		rest := strings.TrimSpace(t[foundIdx+1:])
		if strings.HasPrefix(keyRaw, `"`) {
			key, _, err := parseQuotedPrefix(keyRaw, lineNo)
			if err != nil {
				panic(err)
			}
			keyRaw = key
		}
		if keyRaw == "" {
			fail(lineNo, "empty key in inline object")
		}
		if _, ok := obj[keyRaw]; ok {
			fail(lineNo, "duplicate key '%s'", keyRaw)
		}
		var value any
		switch {
		case rest == "":
			value = ""
		case strings.HasPrefix(rest, `"`):
			s, err := parseQuotedString(rest, lineNo)
			if err != nil {
				panic(err)
			}
			value = s
		case strings.HasPrefix(rest, "!"):
			value = p.parseInlineTagged(rest, lineNo)
		default:
			value = rest
		}
		obj[keyRaw] = value
	}
	return obj
}

// parseInlineTagged handles a tagged value within an inline object literal:
// "!tag <value>" or "!tag[items]" (RFC-0002). Empty compact arrays collapse
// to []any{}.
func (p *parser) parseInlineTagged(rest string, lineNo int) any {
	m := tagRe.FindStringSubmatch(rest)
	if m == nil {
		fail(lineNo, "invalid type tag in inline object: '%s'", rest)
	}
	tag, tail := m[1], m[2]
	if tail == "" {
		fail(lineNo, "missing value for !%s", tag)
	}
	if strings.HasPrefix(tail, " ") {
		return applyTag(tag, strings.TrimLeft(tail, " "), lineNo)
	}
	if strings.HasPrefix(tail, "[") {
		if !strings.HasSuffix(tail, "]") {
			fail(lineNo, "unclosed compact array in inline object")
		}
		inner := tail[1 : len(tail)-1]
		if inner == "" {
			return []any{}
		}
		parts := splitCompact(inner)
		out := make([]any, len(parts))
		for i, g := range parts {
			out[i] = applyTag(tag, g, lineNo)
		}
		return out
	}
	fail(lineNo, "expected space or '[' after type tag in inline object")
	return nil
}

// parseInlineDictEntry parses a dict written across a list item
// (e.g. "- host: a\n    port: 80"): the first line seeds the dict, and
// continuation keys at the first key's indent are appended. Used by
// parseList when the remainder looks like "key: value" rather than a
// RFC-0002 inline object literal.
func (p *parser) parseInlineDictEntry(first string, keyIndent, lineNo, depth int) map[string]any {
	obj := map[string]any{}
	key, rest := splitKey(first, lineNo)
	if _, ok := obj[key]; ok {
		fail(lineNo, "duplicate key '%s'", key)
	}
	obj[key] = p.parseValue(rest, keyIndent, lineNo, depth+1, &key)
	for p.peek() != nil {
		p.skipNoise()
		l := p.peek()
		if l == nil || l.blank {
			break
		}
		if l.indent < keyIndent {
			break
		}
		if l.indent > keyIndent {
			fail(l.n, "invalid indent jump")
		}
		if p.isListItem(l) {
			break
		}
		key, rest := splitKey(l.text, l.n)
		if _, ok := obj[key]; ok {
			fail(l.n, "duplicate key '%s'", key)
		}
		p.i++
		obj[key] = p.parseValue(rest, keyIndent, l.n, depth+1, &key)
	}
	return obj
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
		if ind < base && !l.blank {
			fail(l.n, "multiline body must indent +2, or close at opener indent")
		}
		if strings.Contains(l.raw, "\t") {
			fail(l.n, "tab is not allowed")
		}
		parts = append(parts, l.raw[base:])
		p.i++
	}
	fail(lineNo, "unclosed multiline block")
	return nil
}

func splitKey(text string, n int) (string, string) {
	idx := strings.Index(text, ": ")
	if idx > 0 {
		return text[:idx], text[idx+2:]
	}
	if strings.HasSuffix(text, ":") && len(text) > 1 {
		return text[:len(text)-1], ""
	}
	fail(n, "expected ': ' or trailing ':'")
	return "", ""
}

var mlRe = regexp.MustCompile(`^\|([A-Za-z_][A-Za-z0-9_]*)$`)

func matchMultiline(raw string) (string, bool) {
	if raw == "|" {
		return "|", true
	}
	m := mlRe.FindStringSubmatch(raw)
	if m != nil {
		return m[1], true
	}
	return "", false
}

func glyphOf(v any) string {
	switch val := v.(type) {
	case Tagged:
		return val.Value
	case []byte:
		return hex.EncodeToString(val)
	case bool:
		if val {
			return "true"
		}
		return "false"
	case string:
		return val
	case int, int8, int16, int32, int64, uint, uint8, uint16, uint32, uint64, float32, float64:
		return fmt.Sprintf("%v", val)
	default:
		fail(0, "cannot stringify a collection as scalar glyph")
		return ""
	}
}

func splitCompact(inner string) []string {
	parts := strings.Split(inner, ",")
	for i := range parts {
		parts[i] = strings.TrimSpace(parts[i])
	}
	return parts
}

func stripUnderscores(s string, n int) string {
	if strings.Contains(s, "__") || strings.HasPrefix(s, "_") || strings.HasSuffix(s, "_") {
		fail(n, "invalid numeric underscores")
	}
	return strings.ReplaceAll(s, "_", "")
}

var (
	dateRe     = regexp.MustCompile(`^\d{4}-\d{2}-\d{2}$`)
	timeRe     = regexp.MustCompile(`^\d{2}:\d{2}(:\d{2}(\.\d+)?)?$`)
	dtRe       = regexp.MustCompile(`^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(\.\d+)?(Z|[+-]\d{2}:\d{2})$`)
	tzOffsetRe = regexp.MustCompile(`^[+-]\d{2}:\d{2}$`)
	tzNameRe   = regexp.MustCompile(`^[A-Za-z_]+(/[A-Za-z0-9_+-]+)+$`)
	duRe       = regexp.MustCompile(`^(\d+d)?(\d+h)?(\d+m)?(\d+(\.\d+)?s)?$`)
	szRe       = regexp.MustCompile(`^\d+(\.\d+)?(B|KB|MB|GB|TB|PB|KiB|MiB|GiB|TiB|PiB)$`)
	verRe      = regexp.MustCompile(`^\d+(\.\d+)*$`)
	uuidRe      = regexp.MustCompile(`^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$`)
	cpRe        = regexp.MustCompile(`^U\+([0-9A-Fa-f]{4,6})$`)
	jsNumberRe  = regexp.MustCompile(`^[ \t\n\r\f\v]*[+-]?(?:Infinity|0[xX][0-9a-fA-F]+|0[bB][01]+|0[oO][0-7]+|(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)[ \t\n\r\f\v]*$`)
)

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
		s := stripUnderscores(glyph, n)
		val, err := strconv.ParseInt(s, 16, 64)
		if err != nil {
			fail(n, "invalid hex")
		}
		return val
	case "xb":
		s := strings.ReplaceAll(glyph, "_", "")
		b, err := hex.DecodeString(s)
		if err != nil || len(s)%2 != 0 || len(s) == 0 {
			fail(n, "hex bytes must be an even number of digits")
		}
		return b
	case "o":
		val, err := strconv.ParseInt(glyph, 8, 64)
		if err != nil {
			fail(n, "invalid octal")
		}
		return val
	case "b":
		if glyph == "true" {
			return true
		}
		if glyph == "false" {
			return false
		}
		fail(n, "boolean must be true or false")
	case "d":
		if !dateRe.MatchString(glyph) {
			fail(n, "invalid date")
		}
		return Tagged{Tag: "d", Value: glyph}
	case "t":
		if !timeRe.MatchString(glyph) {
			fail(n, "invalid time")
		}
		return Tagged{Tag: "t", Value: glyph}
	case "dt":
		if !dtRe.MatchString(glyph) {
			fail(n, "datetime must include a timezone offset")
		}
		return Tagged{Tag: "dt", Value: glyph}
	case "tz":
		if glyph != "Z" && glyph != "UTC" && !tzOffsetRe.MatchString(glyph) && !tzNameRe.MatchString(glyph) {
			fail(n, "invalid time zone")
		}
		return Tagged{Tag: "tz", Value: glyph}
	case "du":
		if glyph == "" || !duRe.MatchString(glyph) {
			fail(n, "invalid duration")
		}
		return Tagged{Tag: "du", Value: glyph}
	case "sz":
		if !szRe.MatchString(glyph) {
			fail(n, "invalid data size")
		}
		return Tagged{Tag: "sz", Value: glyph}
	case "unix":
		return parseUnix(glyph, n)
	case "ver":
		if !verRe.MatchString(glyph) {
			fail(n, "invalid version")
		}
		return Tagged{Tag: "ver", Value: glyph}
	case "uuid":
		if !uuidRe.MatchString(glyph) {
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
		m := cpRe.FindStringSubmatch(glyph)
		if m != nil {
			cp, err := strconv.ParseInt(m[1], 16, 32)
			if err != nil || cp > 0x10ffff {
				fail(n, "invalid code point")
			}
			return Tagged{Tag: "c", Value: string(rune(cp))}
		}
		runes := []rune(glyph)
		if len(runes) != 1 {
			fail(n, "character must be a single scalar")
		}
		return Tagged{Tag: "c", Value: glyph}
	}
	return Tagged{Tag: tag, Value: glyph}
}

var (
	leadingZero = regexp.MustCompile(`^-?0\d`)
	intRe       = regexp.MustCompile(`^-?\d+$`)
	floatRe1    = regexp.MustCompile(`^-?\d+\.\d+([eE][+-]?\d+)?$`)
	floatRe2    = regexp.MustCompile(`^-?\d+[eE][+-]?\d+$`)
)

func parseN(g string, n int) any {
	s := stripUnderscores(g, n)
	if leadingZero.MatchString(s) {
		fail(n, "leading zeros are not allowed")
	}
	if intRe.MatchString(s) {
		val, err := strconv.ParseInt(s, 10, 64)
		if err != nil {
			fail(n, "integer overflow")
		}
		return val
	}
	if floatRe1.MatchString(s) || floatRe2.MatchString(s) {
		val, err := strconv.ParseFloat(s, 64)
		if err != nil {
			fail(n, "invalid number")
		}
		return val
	}
	fail(n, "invalid number")
	return nil
}

func parseI(g string, n int) int64 {
	s := stripUnderscores(g, n)
	if !intRe.MatchString(s) {
		fail(n, "invalid integer")
	}
	if leadingZero.MatchString(s) {
		fail(n, "leading zeros are not allowed")
	}
	val, err := strconv.ParseInt(s, 10, 64)
	if err != nil {
		fail(n, "integer overflow")
	}
	return val
}

func parseF(g string, n int) float64 {
	s := stripUnderscores(g, n)
	if !strings.Contains(s, ".") && !strings.ContainsAny(s, "eE") {
		fail(n, "float must contain '.' or 'e'")
	}
	val, err := strconv.ParseFloat(s, 64)
	if err != nil {
		fail(n, "invalid float")
	}
	return val
}

func parseUnix(g string, n int) any {
	s := stripUnderscores(g, n)
	if leadingZero.MatchString(s) {
		fail(n, "leading zeros are not allowed")
	}
	if intRe.MatchString(s) {
		val, err := strconv.ParseInt(s, 10, 64)
		if err == nil {
			return val
		}
	}
	if floatRe1.MatchString(s) {
		val, err := strconv.ParseFloat(s, 64)
		if err == nil {
			return val
		}
	}
	fail(n, "invalid unix timestamp")
	return nil
}

// --- Encoder ---

func Marshal(v any) ([]byte, error) {
	s, err := Encode(v)
	if err != nil {
		return nil, err
	}
	return []byte(s), nil
}

func Unmarshal(data []byte, v any) error {
	doc, err := Parse(string(data))
	if err != nil {
		return err
	}
	rv := reflect.ValueOf(v)
	if rv.Kind() != reflect.Pointer || rv.IsNil() {
		return fmt.Errorf("xun: Unmarshal requires a non-nil pointer")
	}
	elem := rv.Elem()
	docVal := reflect.ValueOf(doc)
	if docVal.Type().AssignableTo(elem.Type()) || elem.Kind() == reflect.Interface {
		elem.Set(docVal)
		return nil
	}
	return fmt.Errorf("xun: cannot unmarshal %T into %T", doc, elem.Interface())
}

func Encode(v any) (out string, err error) {
	defer func() {
		if r := recover(); r != nil {
			if e, ok := r.(*Error); ok {
				err = e
				out = ""
			} else {
				panic(r)
			}
		}
	}()

	rv := reflect.ValueOf(v)
	if rv.Kind() == reflect.Pointer {
		rv = rv.Elem()
	}
	if rv.Kind() != reflect.Map {
		fail(0, "root must be a dictionary")
	}
	if rv.Len() == 0 {
		return "", nil
	}

	var lines []string
	encodeMap(rv, 0, &lines)
	return strings.Join(lines, "\n") + "\n", nil
}

func validateKey(k string) string {
	if k == "" {
		fail(0, "key cannot be empty")
	}
	if strings.ContainsAny(k, "\n\r") || strings.Contains(k, ": ") || strings.HasSuffix(k, ":") {
		fail(0, "invalid key format: %s", k)
	}
	return k
}

func encodeMap(rv reflect.Value, depth int, lines *[]string) {
	if depth > maxDepth {
		fail(0, "nesting depth exceeds limit")
	}
	indent := strings.Repeat("  ", depth)
	keys := rv.MapKeys()
	sort.Slice(keys, func(i, j int) bool {
		return keys[i].String() < keys[j].String()
	})

	for _, k := range keys {
		keyStr := validateKey(k.String())
		val := rv.MapIndex(k).Interface()
		encodeKeyVal(indent, keyStr, val, depth, lines)
	}
}

// stripSurroundingQuotes removes paired double quotes from both ends, repeatedly.
func stripSurroundingQuotes(s string) string {
	for len(s) >= 2 && strings.HasPrefix(s, "\"") && strings.HasSuffix(s, "\"") {
		s = s[1 : len(s)-1]
	}
	return s
}

// needsStringTag reports whether a string glyph would be ambiguous untagged:
// syntactic specials, or a token JavaScript Number() would coerce to a number.
func needsStringTag(s string) bool {
	return strings.HasPrefix(s, "!") || s == "[]" || s == "{}" || strings.HasPrefix(s, "|") || jsNumberRe.MatchString(s)
}

func parseQuotedString(raw string, lineNo int) (string, error) {
	if !strings.HasPrefix(raw, `"`) {
		return "", &Error{Line: lineNo, Msg: `quoted string must start with '"'`}
	}
	var out strings.Builder
	for i := 1; i < len(raw); i++ {
		ch := raw[i]
		if ch == '\\' {
			if i+1 >= len(raw) {
				return "", &Error{Line: lineNo, Msg: "unclosed escape in quoted string"}
			}
			nxt := raw[i+1]
			if nxt == '\\' || nxt == '"' {
				out.WriteByte(nxt)
				i++
				continue
			}
			return "", &Error{Line: lineNo, Msg: fmt.Sprintf("invalid escape \\%c in quoted string", nxt)}
		}
		if ch == '"' {
			if i != len(raw)-1 {
				return "", &Error{Line: lineNo, Msg: "unexpected trailing content after quoted string"}
			}
			return out.String(), nil
		}
		out.WriteByte(ch)
	}
	return "", &Error{Line: lineNo, Msg: "unclosed quoted string"}
}

// parseQuotedPrefix reads a quoted string from the start of raw and
// returns the unquoted value plus the index immediately after the closing
// quote. Used by RFC-0002 to consume the quoted key inside an inline
// object literal without requiring the rest of the line to be a complete
// quoted string.
func parseQuotedPrefix(raw string, lineNo int) (string, int, error) {
	if !strings.HasPrefix(raw, `"`) {
		return "", 0, &Error{Line: lineNo, Msg: `quoted string must start with '"'`}
	}
	var out strings.Builder
	for i := 1; i < len(raw); i++ {
		ch := raw[i]
		if ch == '\\' {
			if i+1 >= len(raw) {
				return "", 0, &Error{Line: lineNo, Msg: "unclosed escape in quoted string"}
			}
			nxt := raw[i+1]
			if nxt == '\\' || nxt == '"' {
				out.WriteByte(nxt)
				i++
				continue
			}
			return "", 0, &Error{Line: lineNo, Msg: fmt.Sprintf("invalid escape \\%c in quoted string", nxt)}
		}
		if ch == '"' {
			return out.String(), i + 1, nil
		}
		out.WriteByte(ch)
	}
	return "", 0, &Error{Line: lineNo, Msg: "unclosed quoted string"}
}

func needsQuotedGlyph(s string) bool {
	return strings.TrimSpace(s) != s || strings.ContainsAny(s, `"\\`)
}

func quoteGlyph(s string) string {
	var b strings.Builder
	b.WriteByte('"')
	for i := 0; i < len(s); i++ {
		switch s[i] {
		case '\\':
			b.WriteString(`\\`)
		case '"':
			b.WriteString(`\"`)
		default:
			b.WriteByte(s[i])
		}
	}
	b.WriteByte('"')
	return b.String()
}

func encodeStringGlyph(s string) string {
	body := s
	if needsQuotedGlyph(s) {
		body = quoteGlyph(s)
	}
	if needsStringTag(s) {
		return "!s " + body
	}
	return body
}

func parseStringBody(body string, lineNo int) any {
	if strings.HasPrefix(body, `"`) {
		s, err := parseQuotedString(body, lineNo)
		if err != nil {
			panic(err)
		}
		return s
	}
	return body
}

func encodeKeyVal(indent, key string, val any, depth int, lines *[]string) {
	if val == nil {
		*lines = append(*lines, fmt.Sprintf("%s%s:", indent, key))
		return
	}

	switch v := val.(type) {
	case Tagged:
		if strings.ContainsAny(v.Value, "\n\r") {
			*lines = append(*lines, fmt.Sprintf("%s%s: !%s |", indent, key, v.Tag))
			for _, line := range strings.Split(strings.ReplaceAll(v.Value, "\r\n", "\n"), "\n") {
				*lines = append(*lines, fmt.Sprintf("%s  %s", indent, line))
			}
			*lines = append(*lines, fmt.Sprintf("%s|", indent))
		} else {
			*lines = append(*lines, fmt.Sprintf("%s%s: !%s %s", indent, key, v.Tag, v.Value))
		}
	case time.Time:
		*lines = append(*lines, fmt.Sprintf("%s%s: !dt %s", indent, key, v.Format(time.RFC3339Nano)))
	case net.IP:
		*lines = append(*lines, fmt.Sprintf("%s%s: !ip %s", indent, key, v.String()))
	case []byte:
		*lines = append(*lines, fmt.Sprintf("%s%s: !xb %s", indent, key, strings.ToUpper(hex.EncodeToString(v))))
	case string:
		v = stripSurroundingQuotes(v)
		if strings.ContainsAny(v, "\n\r") {
			*lines = append(*lines, fmt.Sprintf("%s%s: |", indent, key))
			for _, line := range strings.Split(strings.ReplaceAll(v, "\r\n", "\n"), "\n") {
				*lines = append(*lines, fmt.Sprintf("%s  %s", indent, line))
			}
			*lines = append(*lines, fmt.Sprintf("%s|", indent))
		} else if v == "" {
			*lines = append(*lines, fmt.Sprintf("%s%s:", indent, key))
		} else {
			*lines = append(*lines, fmt.Sprintf("%s%s: %s", indent, key, encodeStringGlyph(v)))
		}
	case bool:
		bStr := "false"
		if v {
			bStr = "true"
		}
		*lines = append(*lines, fmt.Sprintf("%s%s: !b %s", indent, key, bStr))
	case int, int8, int16, int32, int64, uint, uint8, uint16, uint32, uint64:
		*lines = append(*lines, fmt.Sprintf("%s%s: !i %v", indent, key, v))
	case float32, float64:
		s := fmt.Sprintf("%v", v)
		if !strings.Contains(s, ".") && !strings.ContainsAny(s, "eE") {
			s += ".0"
		}
		*lines = append(*lines, fmt.Sprintf("%s%s: !f %s", indent, key, s))
	default:
		rv := reflect.ValueOf(val)
		if rv.Kind() == reflect.Pointer {
			rv = rv.Elem()
		}
		if rv.Kind() == reflect.Map {
			if rv.Len() == 0 {
				*lines = append(*lines, fmt.Sprintf("%s%s: {}", indent, key))
			} else {
				*lines = append(*lines, fmt.Sprintf("%s%s:", indent, key))
				encodeMap(rv, depth+1, lines)
			}
		} else if rv.Kind() == reflect.Slice || rv.Kind() == reflect.Array {
			if rv.Len() == 0 {
				*lines = append(*lines, fmt.Sprintf("%s%s: []", indent, key))
			} else {
				*lines = append(*lines, fmt.Sprintf("%s%s:", indent, key))
				encodeSlice(rv, depth+1, lines)
			}
		} else {
			fail(0, "unsupported type %T for key %s", val, key)
		}
	}
}

func encodeSlice(rv reflect.Value, depth int, lines *[]string) {
	if depth > maxDepth {
		fail(0, "nesting depth exceeds limit")
	}
	indent := strings.Repeat("  ", depth)
	for i := 0; i < rv.Len(); i++ {
		val := rv.Index(i).Interface()
		encodeListItem(indent, val, depth, lines)
	}
}

func encodeListItem(indent string, val any, depth int, lines *[]string) {
	if val == nil {
		*lines = append(*lines, fmt.Sprintf("%s-", indent))
		return
	}

	switch v := val.(type) {
	case Tagged:
		if strings.ContainsAny(v.Value, "\n\r") {
			*lines = append(*lines, fmt.Sprintf("%s- !%s |", indent, v.Tag))
			for _, line := range strings.Split(strings.ReplaceAll(v.Value, "\r\n", "\n"), "\n") {
				*lines = append(*lines, fmt.Sprintf("%s  %s", indent, line))
			}
			*lines = append(*lines, fmt.Sprintf("%s|", indent))
		} else {
			*lines = append(*lines, fmt.Sprintf("%s- !%s %s", indent, v.Tag, v.Value))
		}
	case []byte:
		*lines = append(*lines, fmt.Sprintf("%s- !xb %s", indent, strings.ToUpper(hex.EncodeToString(v))))
	case string:
		v = stripSurroundingQuotes(v)
		if strings.ContainsAny(v, "\n\r") {
			*lines = append(*lines, fmt.Sprintf("%s- |", indent))
			for _, line := range strings.Split(strings.ReplaceAll(v, "\r\n", "\n"), "\n") {
				*lines = append(*lines, fmt.Sprintf("%s  %s", indent, line))
			}
			*lines = append(*lines, fmt.Sprintf("%s|", indent))
		} else if v == "" {
			*lines = append(*lines, fmt.Sprintf("%s-", indent))
		} else {
			*lines = append(*lines, fmt.Sprintf("%s- %s", indent, encodeStringGlyph(v)))
		}
	case bool:
		bStr := "false"
		if v {
			bStr = "true"
		}
		*lines = append(*lines, fmt.Sprintf("%s- !b %s", indent, bStr))
	case int, int8, int16, int32, int64, uint, uint8, uint16, uint32, uint64:
		*lines = append(*lines, fmt.Sprintf("%s- !i %v", indent, v))
	case float32, float64:
		s := fmt.Sprintf("%v", v)
		if !strings.Contains(s, ".") && !strings.ContainsAny(s, "eE") {
			s += ".0"
		}
		*lines = append(*lines, fmt.Sprintf("%s- !f %s", indent, s))
	default:
		rv := reflect.ValueOf(val)
		if rv.Kind() == reflect.Pointer {
			rv = rv.Elem()
		}
		if rv.Kind() == reflect.Map {
			if rv.Len() == 0 {
				*lines = append(*lines, fmt.Sprintf("%s- {}", indent))
			} else {
				*lines = append(*lines, fmt.Sprintf("%s-", indent))
				encodeMap(rv, depth+1, lines)
			}
		} else if rv.Kind() == reflect.Slice || rv.Kind() == reflect.Array {
			if rv.Len() == 0 {
				*lines = append(*lines, fmt.Sprintf("%s- []", indent))
			} else {
				*lines = append(*lines, fmt.Sprintf("%s-", indent))
				encodeSlice(rv, depth+1, lines)
			}
		} else {
			fail(0, "unsupported list item type %T", val)
		}
	}
}
