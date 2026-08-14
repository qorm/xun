use std::collections::HashMap;

#[derive(Debug, Clone, PartialEq)]
pub struct Tagged {
    pub tag: String,
    pub value: String,
}

#[derive(Debug, Clone, PartialEq)]
pub enum Value {
    String(String),
    Int(i64),
    Float(f64),
    Bool(bool),
    Bytes(Vec<u8>),
    Tagged(Tagged),
    List(Vec<Value>),
    Dict(Vec<(String, Value)>),
}

#[derive(Debug)]
pub struct Error {
    pub line: usize,
    pub message: String,
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        if self.line == 0 {
            write!(f, "{}", self.message)
        } else {
            write!(f, "line {}: {}", self.line, self.message)
        }
    }
}

impl std::error::Error for Error {}

fn err(line: usize, msg: impl Into<String>) -> Error {
    Error {
        line,
        message: msg.into(),
    }
}

struct Line {
    raw: String,
    indent: usize,
    text: String,
    n: usize,
    blank: bool,
}

pub fn parse(source: &str) -> Result<Value, Error> {
    if source.contains('\0') {
        return Err(err(0, "NUL is not allowed"));
    }
    if source.len() > 1024 * 1024 {
        return Err(err(0, "document exceeds 1MB"));
    }
    let source = source.strip_prefix('\u{feff}').unwrap_or(source);
    Parser {
        lines: split_lines(source)?,
        i: 0,
        env: HashMap::new(),
        resolving: HashMap::new(),
    }
    .parse_document()
}

fn split_lines(source: &str) -> Result<Vec<Line>, Error> {
    if source.is_empty() {
        return Ok(vec![]);
    }
    let mut out = Vec::new();
    let mut n = 1usize;
    let mut start = 0usize;
    let bytes = source.as_bytes();
    let mut i = 0usize;
    while i <= bytes.len() {
        let at_end = i == bytes.len();
        let c = if at_end { 0 } else { bytes[i] };
        if !at_end && c != b'\n' && c != b'\r' {
            i += 1;
            continue;
        }
        let raw = &source[start..i];
        if c == b'\r' && i + 1 < bytes.len() && bytes[i + 1] == b'\n' {
            i += 1;
        }
        out.push(make_line(raw, n)?);
        n += 1;
        i += 1;
        start = i;
    }
    Ok(out)
}

fn make_line(raw: &str, n: usize) -> Result<Line, Error> {
    let mut i = 0usize;
    let b = raw.as_bytes();
    while i < b.len() && b[i] == b' ' {
        i += 1;
    }
    if i < b.len() && b[i] == b'\t' {
        return Err(err(n, "tab is not allowed"));
    }
    if i % 2 != 0 {
        return Err(err(n, "indent must be a multiple of 2"));
    }
    let text = raw[i..].trim_end_matches([' ', '\t']).to_string();
    Ok(Line {
        raw: raw.to_string(),
        indent: i,
        blank: text.is_empty(),
        text,
        n,
    })
}

struct Parser {
    lines: Vec<Line>,
    i: usize,
    env: HashMap<String, Value>,
    resolving: HashMap<String, bool>,
}

impl Parser {
    fn peek(&self) -> Option<&Line> {
        self.lines.get(self.i)
    }

    fn skip_noise(&mut self) {
        while let Some(l) = self.peek() {
            if l.blank || l.text.starts_with('#') {
                self.i += 1;
            } else {
                break;
            }
        }
    }

    fn parse_document(&mut self) -> Result<Value, Error> {
        self.parse_vars()?;
        self.skip_noise();
        if self.peek().is_none() {
            return Ok(Value::Dict(vec![]));
        }
        let first = self.peek().unwrap();
        if first.indent != 0 {
            return Err(err(first.n, "document must start at indent 0"));
        }
        if self.is_list_item(first) {
            return Err(err(first.n, "root must be a dictionary"));
        }
        self.parse_dict(0, 0)
    }

    fn parse_vars(&mut self) -> Result<(), Error> {
        while let Some(l) = self.peek() {
            if l.blank || l.text.starts_with('#') {
                self.i += 1;
                continue;
            }
            if l.indent != 0 || !l.text.starts_with('$') {
                break;
            }
            let text = l.text.clone();
            let n = l.n;
            let Some((name, rest)) = parse_var_def(&text) else {
                return Err(err(n, "invalid variable definition"));
            };
            if self.env.contains_key(&name) {
                return Err(err(n, format!("duplicate variable ${name}")));
            }
            self.i += 1;
            let val = self.parse_value(&rest, 0, n, 1)?;
            self.env.insert(name, val);
        }
        Ok(())
    }

    fn parse_dict(&mut self, indent: usize, depth: usize) -> Result<Value, Error> {
        if depth > 64 {
            return Err(err(self.peek().map(|l| l.n).unwrap_or(0), "nesting exceeds 64"));
        }
        let mut obj = Vec::new();
        loop {
            self.skip_noise();
            let Some(l) = self.peek() else { break };
            if l.blank || l.indent < indent {
                break;
            }
            if l.indent > indent {
                return Err(err(l.n, "invalid indent jump"));
            }
            if l.text.starts_with('$') && indent == 0 {
                return Err(err(l.n, "variable definitions only allowed at file start"));
            }
            if self.is_list_item(l) {
                return Err(err(l.n, "cannot mix list items into a dictionary"));
            }
            let text = l.text.clone();
            let n = l.n;
            let (key, rest) = split_key(&text, n)?;
            if obj.iter().any(|(k, _)| k == &key) {
                return Err(err(n, format!("duplicate key '{key}'")));
            }
            self.i += 1;
            let val = self.parse_value(&rest, indent, n, depth + 1)?;
            obj.push((key, val));
        }
        Ok(Value::Dict(obj))
    }

    fn parse_list(&mut self, indent: usize, depth: usize, item_tag: Option<&str>) -> Result<Value, Error> {
        let mut arr = Vec::new();
        loop {
            self.skip_noise();
            let Some(l) = self.peek() else { break };
            if l.blank || l.indent < indent {
                break;
            }
            if l.indent > indent {
                return Err(err(l.n, "invalid indent jump"));
            }
            if !self.is_list_item(l) {
                return Err(err(l.n, "cannot mix dictionary keys into a list"));
            }
            let rest = if l.text == "-" {
                String::new()
            } else {
                l.text[2..].to_string()
            };
            let n = l.n;
            self.i += 1;
            let mut val = self.parse_value(&rest, indent, n, depth + 1)?;
            if let Some(tag) = item_tag {
                val = apply_tag(tag, &glyph_of(&val)?, n)?;
            }
            arr.push(val);
        }
        Ok(Value::List(arr))
    }

    fn is_list_item(&self, l: &Line) -> bool {
        l.text == "-" || l.text.starts_with("- ")
    }

    fn parse_value(&mut self, raw: &str, parent_indent: usize, line_no: usize, depth: usize) -> Result<Value, Error> {
        if raw == "[]" {
            return Ok(Value::List(vec![]));
        }
        if raw == "{}" {
            return Ok(Value::Dict(vec![]));
        }
        if let Some(closer) = match_multiline(raw) {
            return self.read_multiline(parent_indent, &closer, line_no);
        }
        if let Some(rest) = raw.strip_prefix('!') {
            return self.parse_tagged(rest, parent_indent, line_no, depth);
        }
        if raw.is_empty() {
            return self.parse_empty_or_nested(parent_indent, line_no, depth, None);
        }
        if is_whole_ref(raw) {
            return self.lookup(&raw[1..], line_no);
        }
        interpolate(raw, &mut |name| self.lookup(name, line_no))
    }

    fn parse_tagged(&mut self, rest0: &str, parent_indent: usize, line_no: usize, depth: usize) -> Result<Value, Error> {
        let (tag, rest) = split_ident(rest0).ok_or_else(|| err(line_no, "invalid type tag"))?;
        if let Some(inner_all) = rest.strip_prefix('[') {
            if tag == "s" && rest != "[]" {
                return Err(err(line_no, "string arrays cannot use compact form"));
            }
            let inner = inner_all.strip_suffix(']').ok_or_else(|| err(line_no, "unclosed compact array"))?;
            if inner.is_empty() {
                return self.parse_empty_or_nested(parent_indent, line_no, depth, Some(tag));
            }
            let mut out = Vec::new();
            for g in inner.split(',') {
                out.push(apply_tag(tag, g.trim(), line_no)?);
            }
            return Ok(Value::List(out));
        }
        if rest.is_empty() {
            return Err(err(line_no, format!("missing value for !{tag}")));
        }
        let body = rest.strip_prefix(' ').ok_or_else(|| err(line_no, "expected space after type tag"))?;
        if let Some(closer) = match_multiline(body) {
            let text = match self.read_multiline(parent_indent, &closer, line_no)? {
                Value::String(s) => s,
                other => return Ok(other),
            };
            if tag == "s" {
                return Ok(Value::String(text));
            }
            return apply_tag(tag, &text, line_no);
        }
        if tag == "s" {
            return Ok(Value::String(body.to_string()));
        }
        if is_whole_ref(body) {
            return self.lookup(&body[1..], line_no);
        }
        apply_tag(tag, body, line_no)
    }

    fn parse_empty_or_nested(&mut self, parent_indent: usize, _line_no: usize, depth: usize, item_tag: Option<&str>) -> Result<Value, Error> {
        self.skip_noise();
        let child = parent_indent + 2;
        match self.peek() {
            None => {
                if item_tag.is_some() {
                    Ok(Value::List(vec![]))
                } else {
                    Ok(Value::String(String::new()))
                }
            }
            Some(n) if n.blank || n.indent <= parent_indent => {
                if item_tag.is_some() {
                    Ok(Value::List(vec![]))
                } else {
                    Ok(Value::String(String::new()))
                }
            }
            Some(n) if n.indent != child => Err(err(n.n, "child indent must be parent + 2")),
            Some(n) if self.is_list_item(n) => self.parse_list(child, depth, item_tag),
            Some(n) if item_tag.is_some() => Err(err(n.n, "typed array expected list items")),
            Some(_) => self.parse_dict(child, depth),
        }
    }

    fn read_multiline(&mut self, parent_indent: usize, closer: &str, line_no: usize) -> Result<Value, Error> {
        let base = parent_indent + 2;
        let mut parts = Vec::new();
        while let Some(l) = self.peek() {
            let stripped = l.raw.trim_end_matches([' ', '\t']);
            let content = stripped.trim_start_matches(' ');
            let ind = l.raw.len() - l.raw.trim_start_matches(' ').len();
            if !l.blank && ind == parent_indent && content == closer {
                self.i += 1;
                return Ok(Value::String(parts.join("\n")));
            }
            if l.blank {
                parts.push(String::new());
                self.i += 1;
                continue;
            }
            if ind < base {
                return Err(err(l.n, "multiline body must indent +2, or close at opener indent"));
            }
            if l.raw.contains('\t') {
                return Err(err(l.n, "tab is not allowed"));
            }
            parts.push(if l.raw.len() >= base {
                l.raw[base..].to_string()
            } else {
                String::new()
            });
            self.i += 1;
        }
        Err(err(line_no, "unclosed multiline block"))
    }

    fn lookup(&mut self, name: &str, line_no: usize) -> Result<Value, Error> {
        if !self.env.contains_key(name) {
            return Err(err(line_no, format!("undefined variable ${name}")));
        }
        if self.resolving.get(name).copied().unwrap_or(false) {
            return Err(err(line_no, format!("cyclic variable ${name}")));
        }
        let v = self.env.get(name).unwrap().clone();
        if let Value::String(s) = &v {
            if is_whole_ref(s) {
                self.resolving.insert(name.to_string(), true);
                let r = self.lookup(&s[1..], line_no);
                self.resolving.remove(name);
                let r = r?;
                self.env.insert(name.to_string(), r.clone());
                return Ok(r);
            }
        }
        Ok(v)
    }
}

fn parse_var_def(text: &str) -> Option<(String, String)> {
    let rest = text.strip_prefix('$')?;
    let (name, after) = split_ident(rest)?;
    let after = after.strip_prefix(':')?;
    if !after.is_empty() && !after.starts_with(' ') {
        return None;
    }
    let raw = after.strip_prefix(' ').unwrap_or("").to_string();
    Some((name.to_string(), raw))
}

fn split_ident(s: &str) -> Option<(&str, &str)> {
    let mut i = 0;
    let b = s.as_bytes();
    if i >= b.len() || !(b[i].is_ascii_alphabetic() || b[i] == b'_') {
        return None;
    }
    i += 1;
    while i < b.len() && (b[i].is_ascii_alphanumeric() || b[i] == b'_') {
        i += 1;
    }
    Some((&s[..i], &s[i..]))
}

fn split_key(text: &str, n: usize) -> Result<(String, String), Error> {
    if let Some(i) = text.find(": ") {
        if i > 0 {
            return Ok((text[..i].to_string(), text[i + 2..].to_string()));
        }
    }
    if let Some(key) = text.strip_suffix(':') {
        if !key.is_empty() {
            return Ok((key.to_string(), String::new()));
        }
    }
    Err(err(n, "expected ': ' or trailing ':'"))
}

fn match_multiline(raw: &str) -> Option<String> {
    if raw == "|" {
        return Some("|".into());
    }
    raw.strip_prefix('|').and_then(|t| {
        if t.chars().all(|c| c.is_ascii_alphanumeric() || c == '_') && t.starts_with(|c: char| c.is_ascii_alphabetic() || c == '_') {
            Some(t.to_string())
        } else {
            None
        }
    })
}

fn is_whole_ref(raw: &str) -> bool {
    let Some(n) = raw.strip_prefix('$') else {
        return false;
    };
    let mut chars = n.chars();
    let Some(first) = chars.next() else {
        return false;
    };
    (first.is_ascii_alphabetic() || first == '_')
        && n.chars().all(|c| c.is_ascii_alphanumeric() || c == '_')
}

fn interpolate(s: &str, get: &mut dyn FnMut(&str) -> Result<Value, Error>) -> Result<Value, Error> {
    let mut out = String::new();
    let mut rest = s;
    while let Some(i) = rest.find("${") {
        out.push_str(&rest[..i]);
        rest = &rest[i + 2..];
        let Some(end) = rest.find('}') else {
            out.push_str("${");
            continue;
        };
        let name = &rest[..end];
        out.push_str(&glyph_of(&get(name)?)?);
        rest = &rest[end + 1..];
    }
    out.push_str(rest);
    Ok(Value::String(out))
}

fn glyph_of(v: &Value) -> Result<String, Error> {
    match v {
        Value::Tagged(t) => Ok(t.value.clone()),
        Value::Bytes(b) => Ok(b.iter().map(|x| format!("{x:02x}")).collect()),
        Value::String(s) => Ok(s.clone()),
        Value::Bool(true) => Ok("true".into()),
        Value::Bool(false) => Ok("false".into()),
        Value::Int(i) => Ok(i.to_string()),
        Value::Float(f) => Ok(f.to_string()),
        _ => Err(err(0, "cannot interpolate a collection")),
    }
}

fn apply_tag(tag: &str, glyph: &str, n: usize) -> Result<Value, Error> {
    match tag {
        "s" => Ok(Value::String(glyph.to_string())),
        "n" => parse_n(glyph, n),
        "i" => parse_i(glyph, n).map(Value::Int),
        "f" => parse_f(glyph, n).map(Value::Float),
        "x" => {
            let s = strip_underscores(glyph, n)?;
            i64::from_str_radix(&s, 16)
                .map(Value::Int)
                .map_err(|_| err(n, "invalid hex integer"))
        }
        "xb" => {
            let s = glyph.replace('_', "");
            if s.len() % 2 != 0 || s.is_empty() {
                return Err(err(n, "hex bytes must be an even number of digits"));
            }
            let mut b = Vec::new();
            let chars: Vec<char> = s.chars().collect();
            for i in (0..chars.len()).step_by(2) {
                let byte = u8::from_str_radix(&format!("{}{}", chars[i], chars[i + 1]), 16)
                    .map_err(|_| err(n, "hex bytes must be an even number of digits"))?;
                b.push(byte);
            }
            Ok(Value::Bytes(b))
        }
        "o" => i64::from_str_radix(glyph, 8)
            .map(Value::Int)
            .map_err(|_| err(n, "invalid octal")),
        "b" if glyph == "true" => Ok(Value::Bool(true)),
        "b" if glyph == "false" => Ok(Value::Bool(false)),
        "b" => Err(err(n, "boolean must be true or false")),
        "d" if regex_date(glyph) => tagged("d", glyph),
        "d" => Err(err(n, "invalid date")),
        "t" if regex_time(glyph) => tagged("t", glyph),
        "t" => Err(err(n, "invalid time")),
        "dt" if regex_dt(glyph) => tagged("dt", glyph),
        "dt" => Err(err(n, "datetime must include a timezone offset")),
        "tz" => tagged("tz", glyph),
        "du" => tagged("du", glyph),
        "sz" => tagged("sz", glyph),
        "unix" => parse_unix(glyph, n),
        "ver" if glyph.chars().all(|c| c.is_ascii_digit() || c == '.') && glyph.chars().any(|c| c.is_ascii_digit()) => {
            tagged("ver", glyph)
        }
        "ver" => Err(err(n, "invalid version")),
        "uuid" if glyph.len() == 36 => tagged("uuid", glyph),
        "uuid" => Err(err(n, "invalid uuid")),
        "ip" => tagged("ip", glyph),
        "b64" => {
            let s: String = glyph.chars().filter(|c| !c.is_whitespace()).collect();
            match b64_decode(&s) {
                Some(b) => Ok(Value::Bytes(b)),
                None => Err(err(n, "invalid base64")),
            }
        }
        "c" => {
            if let Some(hex) = glyph.strip_prefix("U+") {
                let cp = u32::from_str_radix(hex, 16).map_err(|_| err(n, "invalid code point"))?;
                let ch = char::from_u32(cp).ok_or_else(|| err(n, "invalid code point"))?;
                tagged("c", &ch.to_string())
            } else if glyph.chars().count() == 1 {
                tagged("c", glyph)
            } else {
                Err(err(n, "character must be a single scalar"))
            }
        }
        _ => tagged(tag, glyph),
    }
}

fn tagged(tag: &str, value: &str) -> Result<Value, Error> {
    Ok(Value::Tagged(Tagged {
        tag: tag.into(),
        value: value.into(),
    }))
}

fn regex_date(g: &str) -> bool {
    g.len() == 10 && g.as_bytes()[4] == b'-' && g.as_bytes()[7] == b'-'
}

fn regex_time(g: &str) -> bool {
    g.len() >= 5 && g.as_bytes()[2] == b':'
}

fn regex_dt(g: &str) -> bool {
    g.contains('T') && (g.ends_with('Z') || g.contains('+') || g.rfind('-').map(|i| i > 10).unwrap_or(false))
}

fn strip_underscores(s: &str, n: usize) -> Result<String, Error> {
    if s.contains("__") || s.starts_with('_') || s.ends_with('_') {
        return Err(err(n, "invalid numeric underscores"));
    }
    Ok(s.replace('_', ""))
}

fn parse_n(g: &str, n: usize) -> Result<Value, Error> {
    let s = strip_underscores(g, n)?;
    if s.contains('.') || s.contains('e') || s.contains('E') {
        return s.parse::<f64>().map(Value::Float).map_err(|_| err(n, "invalid number"));
    }
    s.parse::<i64>().map(Value::Int).map_err(|_| err(n, "invalid number"))
}

fn parse_i(g: &str, n: usize) -> Result<i64, Error> {
    let s = strip_underscores(g, n)?;
    s.parse().map_err(|_| err(n, "invalid integer"))
}

fn parse_f(g: &str, n: usize) -> Result<f64, Error> {
    let s = strip_underscores(g, n)?;
    if !s.contains('.') && !s.contains('e') && !s.contains('E') {
        return Err(err(n, "float must contain '.' or 'e'"));
    }
    s.parse().map_err(|_| err(n, "invalid float"))
}

fn parse_unix(g: &str, n: usize) -> Result<Value, Error> {
    let s = strip_underscores(g, n)?;
    if s.contains('.') {
        s.parse::<f64>().map(Value::Float).map_err(|_| err(n, "invalid unix timestamp"))
    } else {
        s.parse::<i64>().map(Value::Int).map_err(|_| err(n, "invalid unix timestamp"))
    }
}

fn b64_decode(s: &str) -> Option<Vec<u8>> {
    fn val(c: u8) -> Option<u8> {
        match c {
            b'A'..=b'Z' => Some(c - b'A'),
            b'a'..=b'z' => Some(c - b'a' + 26),
            b'0'..=b'9' => Some(c - b'0' + 52),
            b'+' => Some(62),
            b'/' => Some(63),
            _ => None,
        }
    }
    if s.len() % 4 != 0 {
        return None;
    }
    let bytes = s.as_bytes();
    let mut out = Vec::new();
    let mut i = 0;
    while i < bytes.len() {
        let pad2 = bytes[i + 2] == b'=';
        let pad3 = bytes[i + 3] == b'=';
        let a = val(bytes[i])?;
        let b = val(bytes[i + 1])?;
        out.push((a << 2) | (b >> 4));
        if !pad2 {
            let c = val(bytes[i + 2])?;
            out.push(((b & 0xf) << 4) | (c >> 2));
            if !pad3 {
                let d = val(bytes[i + 3])?;
                out.push(((c & 0x3) << 6) | d);
            }
        }
        i += 4;
    }
    Some(out)
}

#[cfg(test)]
fn dict_get<'a>(d: &'a Value, key: &str) -> Option<&'a Value> {
    match d {
        Value::Dict(pairs) => pairs.iter().find(|(k, _)| k == key).map(|(_, v)| v),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;

    fn example() -> String {
        let path = concat!(env!("CARGO_MANIFEST_DIR"), "/../testdata/example.xun");
        fs::read_to_string(path).unwrap()
    }

    #[test]
    fn readme_example() {
        let doc = parse(&example()).expect("parse");
        assert_eq!(dict_get(&doc, "endpoint"), Some(&Value::String("https://api.example.com/v2/orders".into())));
        let server = dict_get(&doc, "server").unwrap();
        assert_eq!(dict_get(server, "port"), Some(&Value::Int(8080)));
        assert_eq!(dict_get(server, "host"), Some(&Value::String("localhost".into())));
        let banner = dict_get(&doc, "banner").unwrap();
        assert_eq!(banner, &Value::String("Welcome\nto XUN".into()));
        let py = dict_get(&doc, "py").unwrap();
        assert_eq!(
            py,
            &Value::Tagged(Tagged {
                tag: "ver".into(),
                value: "3.10".into()
            })
        );
        let color = dict_get(&doc, "color").unwrap();
        assert_eq!(color, &Value::Bytes(vec![0xff, 0x00, 0xaa]));
    }

    #[test]
    fn empty_file() {
        assert_eq!(parse("").unwrap(), Value::Dict(vec![]));
    }

    #[test]
    fn untyped() {
        let doc = parse("a: 8080\n").unwrap();
        assert_eq!(dict_get(&doc, "a"), Some(&Value::String("8080".into())));
    }
}
