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

impl Value {
    pub fn as_str(&self) -> Option<&str> {
        match self {
            Value::String(s) => Some(s),
            _ => None,
        }
    }
    pub fn as_i64(&self) -> Option<i64> {
        match self {
            Value::Int(i) => Some(*i),
            _ => None,
        }
    }
    pub fn as_f64(&self) -> Option<f64> {
        match self {
            Value::Float(f) => Some(*f),
            _ => None,
        }
    }
    pub fn as_bool(&self) -> Option<bool> {
        match self {
            Value::Bool(b) => Some(*b),
            _ => None,
        }
    }
    pub fn as_bytes(&self) -> Option<&[u8]> {
        match self {
            Value::Bytes(b) => Some(b),
            _ => None,
        }
    }
    pub fn as_dict(&self) -> Option<&[(String, Value)]> {
        match self {
            Value::Dict(d) => Some(d),
            _ => None,
        }
    }
    pub fn as_list(&self) -> Option<&[Value]> {
        match self {
            Value::List(l) => Some(l),
            _ => None,
        }
    }
    pub fn as_tagged(&self) -> Option<&Tagged> {
        match self {
            Value::Tagged(t) => Some(t),
            _ => None,
        }
    }
}

impl Tagged {
    pub fn to_size_bytes(&self) -> Result<u64, Error> {
        if self.tag != "sz" {
            return Err(err(0, format!("cannot convert !{} to size bytes", self.tag)));
        }
        parse_size(&self.value)
    }

    pub fn to_duration_seconds(&self) -> Result<f64, Error> {
        if self.tag != "du" {
            return Err(err(0, format!("cannot convert !{} to duration seconds", self.tag)));
        }
        parse_duration(&self.value)
    }

    pub fn to_version_parts(&self) -> Result<Vec<u64>, Error> {
        if self.tag != "ver" {
            return Err(err(0, format!("cannot convert !{} to version parts", self.tag)));
        }
        parse_version(&self.value)
    }
}

pub fn parse_size(s: &str) -> Result<u64, Error> {
    let units: &[(&str, u64)] = &[
        ("PiB", 1024 * 1024 * 1024 * 1024 * 1024),
        ("TiB", 1024 * 1024 * 1024 * 1024),
        ("GiB", 1024 * 1024 * 1024),
        ("MiB", 1024 * 1024),
        ("KiB", 1024),
        ("PB", 1000 * 1000 * 1000 * 1000 * 1000),
        ("TB", 1000 * 1000 * 1000 * 1000),
        ("GB", 1000 * 1000 * 1000),
        ("MB", 1000 * 1000),
        ("KB", 1000),
        ("B", 1),
    ];
    for (unit, mult) in units {
        if let Some(num_str) = s.strip_suffix(unit) {
            let num: f64 = num_str.parse().map_err(|_| err(0, format!("invalid size number: {s}")))?;
            return Ok((num * *mult as f64) as u64);
        }
    }
    Err(err(0, format!("invalid size string: {s}")))
}

pub fn parse_duration(s: &str) -> Result<f64, Error> {
    if s.is_empty() {
        return Err(err(0, "empty duration"));
    }
    let mut total = 0.0;
    let mut cur_num = String::new();
    let mut chars = s.chars().peekable();
    let mut matched_any = false;
    while let Some(&c) = chars.peek() {
        if c.is_ascii_digit() || c == '.' {
            cur_num.push(c);
            chars.next();
        } else {
            chars.next();
            if cur_num.is_empty() {
                return Err(err(0, format!("invalid duration format: {s}")));
            }
            let n: f64 = cur_num.parse().map_err(|_| err(0, "invalid duration number"))?;
            cur_num.clear();
            matched_any = true;
            match c {
                'd' => total += n * 86400.0,
                'h' => total += n * 3600.0,
                'm' => {
                    if let Some(&'s') = chars.peek() {
                        chars.next();
                        total += n * 0.001;
                    } else {
                        total += n * 60.0;
                    }
                }
                's' => total += n,
                _ => return Err(err(0, format!("unknown duration unit '{c}' in {s}"))),
            }
        }
    }
    if !matched_any || !cur_num.is_empty() {
        return Err(err(0, format!("invalid duration format: {s}")));
    }
    Ok(total)
}

pub fn parse_version(s: &str) -> Result<Vec<u64>, Error> {
    let mut res = Vec::new();
    for p in s.split('.') {
        let n: u64 = p.parse().map_err(|_| err(0, format!("invalid version segment in {s}")))?;
        res.push(n);
    }
    Ok(res)
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

pub fn decode(source: &str) -> Result<Value, Error> {
    parse(source)
}

pub fn from_str(source: &str) -> Result<Value, Error> {
    parse(source)
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

    fn parse_dict(&mut self, indent: usize, depth: usize) -> Result<Value, Error> {
        if depth > 64 {
            let n = self.peek().map(|l| l.n).unwrap_or(0);
            return Err(err(n, "nesting exceeds 64"));
        }
        let mut obj = Vec::new();
        while let Some(l) = self.peek() {
            if l.blank {
                self.skip_noise();
                continue;
            }
            if l.indent < indent {
                break;
            }
            if l.indent > indent {
                return Err(err(l.n, "invalid indent jump"));
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
        if depth > 64 {
            let n = self.peek().map(|l| l.n).unwrap_or(0);
            return Err(err(n, "nesting exceeds 64"));
        }
        let mut arr = Vec::new();
        while let Some(l) = self.peek() {
            if l.blank {
                self.skip_noise();
                continue;
            }
            if l.indent < indent {
                break;
            }
            if l.indent > indent {
                return Err(err(l.n, "invalid indent jump"));
            }
            if !self.is_list_item(l) {
                return Err(err(l.n, "cannot mix dictionary keys into a list"));
            }
            let text = l.text.clone();
            let n = l.n;
            let rest = if text == "-" { "" } else { &text[2..] };
            self.i += 1;
            let mut val = self.parse_value(rest, indent, n, depth + 1)?;
            if let Some(t) = item_tag {
                val = apply_tag(t, &glyph_of(&val)?, n)?;
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
            return self.read_multiline(parent_indent, None, &closer, line_no);
        }
        if raw.starts_with('!') {
            return self.parse_tagged(raw, parent_indent, line_no, depth);
        }
        if raw.is_empty() {
            return self.parse_empty_or_nested(parent_indent, line_no, depth, None);
        }
        Ok(Value::String(raw.to_string()))
    }

    fn parse_tagged(&mut self, raw: &str, parent_indent: usize, line_no: usize, depth: usize) -> Result<Value, Error> {
        let (tag, rest) = parse_tag_head(raw).ok_or_else(|| err(line_no, "invalid type tag"))?;
        if let Some(inner) = rest.strip_prefix('[') {
            if tag == "s" && rest != "[]" {
                return Err(err(line_no, "string arrays cannot use compact form"));
            }
            let Some(inner) = inner.strip_suffix(']') else {
                return Err(err(line_no, "unclosed compact array"));
            };
            if inner.is_empty() {
                return self.parse_empty_or_nested(parent_indent, line_no, depth, Some(&tag));
            }
            let mut out = Vec::new();
            for g in split_compact(inner) {
                out.push(apply_tag(&tag, g, line_no)?);
            }
            return Ok(Value::List(out));
        }
        if rest.is_empty() {
            return Err(err(line_no, format!("missing value for !{tag}")));
        }
        let Some(body) = rest.strip_prefix(' ') else {
            return Err(err(line_no, "expected space after type tag"));
        };
        if let Some(closer) = match_multiline(body) {
            let text_val = self.read_multiline(parent_indent, None, &closer, line_no)?;
            let text = match text_val {
                Value::String(s) => s,
                _ => unreachable!(),
            };
            if tag == "s" {
                return Ok(Value::String(text));
            }
            return apply_tag(&tag, &text, line_no);
        }
        if tag == "s" {
            return Ok(Value::String(body.to_string()));
        }
        apply_tag(&tag, body, line_no)
    }

    fn parse_empty_or_nested(&mut self, parent_indent: usize, _line_no: usize, depth: usize, item_tag: Option<&str>) -> Result<Value, Error> {
        self.skip_noise();
        let child = parent_indent + 2;
        let Some(n) = self.peek() else {
            return Ok(if item_tag.is_some() {
                Value::List(vec![])
            } else {
                Value::String(String::new())
            });
        };
        if n.blank || n.indent <= parent_indent {
            return Ok(if item_tag.is_some() {
                Value::List(vec![])
            } else {
                Value::String(String::new())
            });
        }
        if n.indent != child {
            return Err(err(n.n, "child indent must be parent + 2"));
        }
        if self.is_list_item(n) {
            return self.parse_list(child, depth, item_tag);
        }
        if let Some(t) = item_tag {
            return Err(err(n.n, format!("!{t}[] expected list items")));
        }
        self.parse_dict(child, depth)
    }

    fn read_multiline(&mut self, parent_indent: usize, tag: Option<&str>, closer: &str, line_no: usize) -> Result<Value, Error> {
        let base = parent_indent + 2;
        let mut parts = Vec::new();
        while let Some(l) = self.peek() {
            let stripped = l.raw.trim_end_matches([' ', '\t']);
            let content = stripped.trim_start_matches(' ');
            let ind = l.raw.len() - l.raw.trim_start_matches(' ').len();
            if !l.blank && ind == parent_indent && content == closer {
                self.i += 1;
                let s = parts.join("\n");
                if let Some(t) = tag {
                    if t != "s" {
                        return apply_tag(t, &s, line_no);
                    }
                }
                return Ok(Value::String(s));
            }
            if l.blank {
                parts.push(String::new());
                self.i += 1;
                continue;
            }
            if ind < base && !l.blank {
                return Err(err(l.n, "multiline body must indent +2, or close at opener indent"));
            }
            if l.raw.contains('\t') {
                return Err(err(l.n, "tab is not allowed"));
            }
            parts.push(l.raw[base..].to_string());
            self.i += 1;
        }
        Err(err(line_no, "unclosed multiline block"))
    }
}

fn split_key(text: &str, n: usize) -> Result<(String, String), Error> {
    if let Some(idx) = text.find(": ") {
        return Ok((text[..idx].to_string(), text[idx + 2..].to_string()));
    }
    if text.ends_with(':') && text.len() > 1 {
        return Ok((text[..text.len() - 1].to_string(), String::new()));
    }
    Err(err(n, "expected ': ' or trailing ':'"))
}

fn match_multiline(raw: &str) -> Option<String> {
    if raw == "|" {
        return Some("|".to_string());
    }
    if let Some(rest) = raw.strip_prefix('|') {
        if is_ident(rest) {
            return Some(rest.to_string());
        }
    }
    None
}

fn is_ident(s: &str) -> bool {
    let mut chars = s.chars();
    match chars.next() {
        Some(c) if c.is_ascii_alphabetic() || c == '_' => {}
        _ => return false,
    }
    chars.all(|c| c.is_ascii_alphanumeric() || c == '_')
}

fn parse_tag_head(raw: &str) -> Option<(String, String)> {
    let s = raw.strip_prefix('!')?;
    let end = s.find(|c: char| !(c.is_ascii_alphanumeric() || c == '_')).unwrap_or(s.len());
    if end == 0 {
        return None;
    }
    let tag = &s[..end];
    if !is_ident(tag) {
        return None;
    }
    Some((tag.to_string(), s[end..].to_string()))
}

fn split_compact(inner: &str) -> Vec<&str> {
    inner.split(',').map(|s| s.trim()).collect()
}

fn glyph_of(v: &Value) -> Result<String, Error> {
    match v {
        Value::String(s) => Ok(s.clone()),
        Value::Int(i) => Ok(i.to_string()),
        Value::Float(f) => Ok(f.to_string()),
        Value::Bool(b) => Ok(if *b { "true".into() } else { "false".into() }),
        Value::Bytes(b) => Ok(hex_encode(b)),
        Value::Tagged(t) => Ok(t.value.clone()),
        _ => Err(err(0, "cannot stringify a collection as scalar glyph")),
    }
}

fn strip_underscores(s: &str, n: usize) -> Result<String, Error> {
    if s.contains("__") || s.starts_with('_') || s.ends_with('_') {
        return Err(err(n, "invalid numeric underscores"));
    }
    Ok(s.replace('_', ""))
}

fn hex_encode(bytes: &[u8]) -> String {
    let mut s = String::with_capacity(bytes.len() * 2);
    for b in bytes {
        use std::fmt::Write;
        write!(&mut s, "{:02x}", b).unwrap();
    }
    s
}

fn apply_tag(tag: &str, glyph: &str, n: usize) -> Result<Value, Error> {
    match tag {
        "s" => Ok(Value::String(glyph.to_string())),
        "n" => parse_n(glyph, n),
        "i" => parse_i(glyph, n),
        "f" => parse_f(glyph, n),
        "x" => {
            let s = strip_underscores(glyph, n)?;
            if s.is_empty() || !s.chars().all(|c| c.is_ascii_hexdigit()) {
                return Err(err(n, "invalid hex"));
            }
            let val = i64::from_str_radix(&s, 16).map_err(|_| err(n, "invalid hex"))?;
            Ok(Value::Int(val))
        }
        "xb" => {
            let s = glyph.replace('_', "");
            if s.is_empty() || s.len() % 2 != 0 || !s.chars().all(|c| c.is_ascii_hexdigit()) {
                return Err(err(n, "hex bytes must be an even number of digits"));
            }
            let mut bytes = Vec::with_capacity(s.len() / 2);
            for i in (0..s.len()).step_by(2) {
                let byte = u8::from_str_radix(&s[i..i + 2], 16).map_err(|_| err(n, "invalid hex byte"))?;
                bytes.push(byte);
            }
            Ok(Value::Bytes(bytes))
        }
        "o" => {
            if glyph.is_empty() || !glyph.chars().all(|c| matches!(c, '0'..='7')) {
                return Err(err(n, "invalid octal"));
            }
            let val = i64::from_str_radix(glyph, 8).map_err(|_| err(n, "invalid octal"))?;
            Ok(Value::Int(val))
        }
        "b" => match glyph {
            "true" => Ok(Value::Bool(true)),
            "false" => Ok(Value::Bool(false)),
            _ => Err(err(n, "boolean must be true or false")),
        },
        "d" => {
            if !is_date(glyph) {
                return Err(err(n, "invalid date"));
            }
            Ok(Value::Tagged(Tagged {
                tag: "d".into(),
                value: glyph.into(),
            }))
        }
        "t" => {
            if !is_time(glyph) {
                return Err(err(n, "invalid time"));
            }
            Ok(Value::Tagged(Tagged {
                tag: "t".into(),
                value: glyph.into(),
            }))
        }
        "dt" => {
            if !is_datetime(glyph) {
                return Err(err(n, "datetime must include a timezone offset"));
            }
            Ok(Value::Tagged(Tagged {
                tag: "dt".into(),
                value: glyph.into(),
            }))
        }
        "tz" => {
            if glyph != "Z" && glyph != "UTC" && !is_tz_offset(glyph) && !is_tz_name(glyph) {
                return Err(err(n, "invalid time zone"));
            }
            Ok(Value::Tagged(Tagged {
                tag: "tz".into(),
                value: glyph.into(),
            }))
        }
        "du" => {
            if glyph.is_empty() || !is_duration(glyph) {
                return Err(err(n, "invalid duration"));
            }
            Ok(Value::Tagged(Tagged {
                tag: "du".into(),
                value: glyph.into(),
            }))
        }
        "sz" => {
            if !is_data_size(glyph) {
                return Err(err(n, "invalid data size"));
            }
            Ok(Value::Tagged(Tagged {
                tag: "sz".into(),
                value: glyph.into(),
            }))
        }
        "unix" => parse_unix(glyph, n),
        "ver" => {
            if !is_version(glyph) {
                return Err(err(n, "invalid version"));
            }
            Ok(Value::Tagged(Tagged {
                tag: "ver".into(),
                value: glyph.into(),
            }))
        }
        "uuid" => {
            if !is_uuid(glyph) {
                return Err(err(n, "invalid uuid"));
            }
            Ok(Value::Tagged(Tagged {
                tag: "uuid".into(),
                value: glyph.into(),
            }))
        }
        "ip" => {
            if glyph.parse::<std::net::IpAddr>().is_err() {
                return Err(err(n, "invalid ip"));
            }
            Ok(Value::Tagged(Tagged {
                tag: "ip".into(),
                value: glyph.into(),
            }))
        }
        "b64" => {
            let s: String = glyph.chars().filter(|c| !c.is_whitespace()).collect();
            let bytes = base64_decode(&s).map_err(|_| err(n, "invalid base64"))?;
            Ok(Value::Bytes(bytes))
        }
        "c" => {
            if let Some(rest) = glyph.strip_prefix("U+") {
                if (4..=6).contains(&rest.len()) && rest.chars().all(|c| c.is_ascii_hexdigit()) {
                    let cp = u32::from_str_radix(rest, 16).map_err(|_| err(n, "invalid code point"))?;
                    if cp > 0x10ffff {
                        return Err(err(n, "invalid code point"));
                    }
                    if let Some(ch) = char::from_u32(cp) {
                        return Ok(Value::Tagged(Tagged {
                            tag: "c".into(),
                            value: ch.to_string(),
                        }));
                    }
                }
                return Err(err(n, "invalid code point"));
            }
            if glyph.chars().count() != 1 {
                return Err(err(n, "character must be a single scalar"));
            }
            Ok(Value::Tagged(Tagged {
                tag: "c".into(),
                value: glyph.into(),
            }))
        }
        _ => Ok(Value::Tagged(Tagged {
            tag: tag.into(),
            value: glyph.into(),
        })),
    }
}

fn parse_n(g: &str, n: usize) -> Result<Value, Error> {
    let s = strip_underscores(g, n)?;
    if has_leading_zero(&s) {
        return Err(err(n, "leading zeros are not allowed"));
    }
    if let Ok(i) = s.parse::<i64>() {
        return Ok(Value::Int(i));
    }
    if let Ok(f) = s.parse::<f64>() {
        return Ok(Value::Float(f));
    }
    Err(err(n, "invalid number"))
}

fn parse_i(g: &str, n: usize) -> Result<Value, Error> {
    let s = strip_underscores(g, n)?;
    if has_leading_zero(&s) {
        return Err(err(n, "leading zeros are not allowed"));
    }
    s.parse::<i64>().map(Value::Int).map_err(|_| err(n, "invalid integer"))
}

fn parse_f(g: &str, n: usize) -> Result<Value, Error> {
    let s = strip_underscores(g, n)?;
    if !s.contains('.') && !s.contains(['e', 'E']) {
        return Err(err(n, "float must contain '.' or 'e'"));
    }
    s.parse::<f64>().map(Value::Float).map_err(|_| err(n, "invalid float"))
}

fn parse_unix(g: &str, n: usize) -> Result<Value, Error> {
    let s = strip_underscores(g, n)?;
    if has_leading_zero(&s) {
        return Err(err(n, "leading zeros are not allowed"));
    }
    if let Ok(i) = s.parse::<i64>() {
        return Ok(Value::Int(i));
    }
    if let Ok(f) = s.parse::<f64>() {
        return Ok(Value::Float(f));
    }
    Err(err(n, "invalid unix timestamp"))
}

fn has_leading_zero(s: &str) -> bool {
    let rest = s.strip_prefix('-').unwrap_or(s);
    rest.len() > 1 && rest.starts_with('0') && rest.chars().nth(1).unwrap().is_ascii_digit()
}

fn is_date(s: &str) -> bool {
    let parts: Vec<&str> = s.split('-').collect();
    parts.len() == 3
        && parts[0].len() == 4
        && parts[0].chars().all(|c| c.is_ascii_digit())
        && parts[1].len() == 2
        && parts[1].chars().all(|c| c.is_ascii_digit())
        && parts[2].len() == 2
        && parts[2].chars().all(|c| c.is_ascii_digit())
}

fn is_time(s: &str) -> bool {
    let parts: Vec<&str> = s.split(':').collect();
    if parts.len() < 2 || parts.len() > 3 {
        return false;
    }
    if parts[0].len() != 2 || !parts[0].chars().all(|c| c.is_ascii_digit()) {
        return false;
    }
    if parts[1].len() != 2 || !parts[1].chars().all(|c| c.is_ascii_digit()) {
        return false;
    }
    if parts.len() == 3 {
        let sec_parts: Vec<&str> = parts[2].split('.').collect();
        if sec_parts[0].len() != 2 || !sec_parts[0].chars().all(|c| c.is_ascii_digit()) {
            return false;
        }
        if sec_parts.len() == 2 && !sec_parts[1].chars().all(|c| c.is_ascii_digit()) {
            return false;
        }
    }
    true
}

fn is_datetime(s: &str) -> bool {
    let Some((d, rest)) = s.split_once('T') else {
        return false;
    };
    if !is_date(d) {
        return false;
    }
    if let Some(t) = rest.strip_suffix('Z') {
        return is_time(t);
    }
    if let Some(idx) = rest.rfind(|c| c == '+' || c == '-') {
        let t = &rest[..idx];
        let offset = &rest[idx..];
        return is_time(t) && is_tz_offset(offset);
    }
    false
}

fn is_tz_offset(s: &str) -> bool {
    let rest = s.strip_prefix('+').or_else(|| s.strip_prefix('-'));
    let Some(r) = rest else { return false };
    let parts: Vec<&str> = r.split(':').collect();
    parts.len() == 2
        && parts[0].len() == 2
        && parts[0].chars().all(|c| c.is_ascii_digit())
        && parts[1].len() == 2
        && parts[1].chars().all(|c| c.is_ascii_digit())
}

fn is_tz_name(s: &str) -> bool {
    let parts: Vec<&str> = s.split('/').collect();
    parts.len() >= 2
        && parts.iter().all(|p| {
            !p.is_empty()
                && p.chars()
                    .all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '+' || c == '-')
        })
}

fn is_duration(s: &str) -> bool {
    let mut rest = s;
    for unit in ['d', 'h', 'm'] {
        if let Some(idx) = rest.find(unit) {
            let num = &rest[..idx];
            if num.is_empty() || !num.chars().all(|c| c.is_ascii_digit()) {
                return false;
            }
            rest = &rest[idx + 1..];
        }
    }
    if !rest.is_empty() {
        let Some(num) = rest.strip_suffix('s') else {
            return false;
        };
        if num.is_empty() {
            return false;
        }
        let parts: Vec<&str> = num.split('.').collect();
        if parts.len() > 2 {
            return false;
        }
        if !parts[0].chars().all(|c| c.is_ascii_digit()) {
            return false;
        }
        if parts.len() == 2 && !parts[1].chars().all(|c| c.is_ascii_digit()) {
            return false;
        }
    }
    true
}

fn is_data_size(s: &str) -> bool {
    let units = ["PiB", "TiB", "GiB", "MiB", "KiB", "PB", "TB", "GB", "MB", "KB", "B"];
    for u in units {
        if let Some(num) = s.strip_suffix(u) {
            if num.is_empty() {
                return false;
            }
            let parts: Vec<&str> = num.split('.').collect();
            if parts.len() > 2 {
                return false;
            }
            if !parts[0].chars().all(|c| c.is_ascii_digit()) {
                return false;
            }
            if parts.len() == 2 && !parts[1].chars().all(|c| c.is_ascii_digit()) {
                return false;
            }
            return true;
        }
    }
    false
}

fn is_version(s: &str) -> bool {
    let parts: Vec<&str> = s.split('.').collect();
    !parts.is_empty() && parts.iter().all(|p| !p.is_empty() && p.chars().all(|c| c.is_ascii_digit()))
}

fn is_uuid(s: &str) -> bool {
    let parts: Vec<&str> = s.split('-').collect();
    parts.len() == 5
        && parts[0].len() == 8
        && parts[1].len() == 4
        && parts[2].len() == 4
        && parts[3].len() == 4
        && parts[4].len() == 12
        && parts.iter().all(|p| p.chars().all(|c| c.is_ascii_hexdigit()))
}

fn base64_decode(input: &str) -> Result<Vec<u8>, ()> {
    const T: [i8; 256] = {
        let mut t = [-1i8; 256];
        let mut i = 0usize;
        while i < 26 {
            t[b'A' as usize + i] = i as i8;
            t[b'a' as usize + i] = (i + 26) as i8;
            i += 1;
        }
        let mut i = 0usize;
        while i < 10 {
            t[b'0' as usize + i] = (i + 52) as i8;
            i += 1;
        }
        t[b'+' as usize] = 62;
        t[b'/' as usize] = 63;
        t
    };
    let mut buf = 0u32;
    let mut bits = 0u32;
    let mut out = Vec::new();
    for &b in input.as_bytes() {
        if b == b'=' {
            break;
        }
        let val = T[b as usize];
        if val < 0 {
            return Err(());
        }
        buf = (buf << 6) | (val as u32);
        bits += 6;
        if bits >= 8 {
            bits -= 8;
            out.push((buf >> bits) as u8);
            buf &= (1 << bits) - 1;
        }
    }
    Ok(out)
}

// --- Encoder ---

pub fn encode(value: &Value) -> Result<String, Error> {
    match value {
        Value::Dict(pairs) => {
            if pairs.is_empty() {
                return Ok(String::new());
            }
            let mut lines = Vec::new();
            encode_dict(pairs, 0, &mut lines)?;
            Ok(lines.join("\n") + "\n")
        }
        _ => Err(err(0, "root must be a dictionary")),
    }
}

pub fn to_string(value: &Value) -> Result<String, Error> {
    encode(value)
}

fn validate_key(k: &str) -> Result<(), Error> {
    if k.is_empty() {
        return Err(err(0, "key cannot be empty"));
    }
    if k.contains(['\n', '\r']) || k.contains(": ") || k.ends_with(':') {
        return Err(err(0, format!("invalid key format: {k}")));
    }
    Ok(())
}

fn encode_dict(pairs: &[(String, Value)], depth: usize, lines: &mut Vec<String>) -> Result<(), Error> {
    if depth > 64 {
        return Err(err(0, "nesting depth exceeds limit"));
    }
    let indent = "  ".repeat(depth);
    for (k, v) in pairs {
        validate_key(k)?;
        match v {
            Value::Dict(sub) => {
                if sub.is_empty() {
                    lines.push(format!("{indent}{k}: {{}}"));
                } else {
                    lines.push(format!("{indent}{k}:"));
                    encode_dict(sub, depth + 1, lines)?;
                }
            }
            Value::List(sub) => {
                if sub.is_empty() {
                    lines.push(format!("{indent}{k}: []"));
                } else {
                    lines.push(format!("{indent}{k}:"));
                    encode_list(sub, depth + 1, lines)?;
                }
            }
            Value::String(s) => {
                if s.contains(['\n', '\r']) {
                    lines.push(format!("{indent}{k}: |"));
                    for line in s.lines() {
                        lines.push(format!("{indent}  {line}"));
                    }
                    lines.push(format!("{indent}|"));
                } else if s.is_empty() {
                    lines.push(format!("{indent}{k}:"));
                } else if s.starts_with('!') || s == "[]" || s == "{}" || s.starts_with('|') {
                    lines.push(format!("{indent}{k}: !s {s}"));
                } else {
                    lines.push(format!("{indent}{k}: {s}"));
                }
            }
            Value::Int(i) => {
                lines.push(format!("{indent}{k}: !i {i}"));
            }
            Value::Float(f) => {
                let mut s = f.to_string();
                if !s.contains('.') && !s.contains(['e', 'E']) {
                    s.push_str(".0");
                }
                lines.push(format!("{indent}{k}: !f {s}"));
            }
            Value::Bool(b) => {
                lines.push(format!("{indent}{k}: !b {}", if *b { "true" } else { "false" }));
            }
            Value::Bytes(b) => {
                lines.push(format!("{indent}{k}: !xb {}", hex_encode(b).to_uppercase()));
            }
            Value::Tagged(t) => {
                if t.value.contains(['\n', '\r']) {
                    lines.push(format!("{indent}{k}: !{} |", t.tag));
                    for line in t.value.lines() {
                        lines.push(format!("{indent}  {line}"));
                    }
                    lines.push(format!("{indent}|"));
                } else {
                    lines.push(format!("{indent}{k}: !{} {}", t.tag, t.value));
                }
            }
        }
    }
    Ok(())
}

fn encode_list(items: &[Value], depth: usize, lines: &mut Vec<String>) -> Result<(), Error> {
    if depth > 64 {
        return Err(err(0, "nesting depth exceeds limit"));
    }
    let indent = "  ".repeat(depth);
    for v in items {
        match v {
            Value::Dict(sub) => {
                if sub.is_empty() {
                    lines.push(format!("{indent}- {{}}"));
                } else {
                    lines.push(format!("{indent}-"));
                    encode_dict(sub, depth + 1, lines)?;
                }
            }
            Value::List(sub) => {
                if sub.is_empty() {
                    lines.push(format!("{indent}- []"));
                } else {
                    lines.push(format!("{indent}-"));
                    encode_list(sub, depth + 1, lines)?;
                }
            }
            Value::String(s) => {
                if s.contains(['\n', '\r']) {
                    lines.push(format!("{indent}- |"));
                    for line in s.lines() {
                        lines.push(format!("{indent}  {line}"));
                    }
                    lines.push(format!("{indent}|"));
                } else if s.is_empty() {
                    lines.push(format!("{indent}-"));
                } else if s.starts_with('!') || s == "[]" || s == "{}" || s.starts_with('|') {
                    lines.push(format!("{indent}- !s {s}"));
                } else {
                    lines.push(format!("{indent}- {s}"));
                }
            }
            Value::Int(i) => {
                lines.push(format!("{indent}- !i {i}"));
            }
            Value::Float(f) => {
                let mut s = f.to_string();
                if !s.contains('.') && !s.contains(['e', 'E']) {
                    s.push_str(".0");
                }
                lines.push(format!("{indent}- !f {s}"));
            }
            Value::Bool(b) => {
                lines.push(format!("{indent}- !b {}", if *b { "true" } else { "false" }));
            }
            Value::Bytes(b) => {
                lines.push(format!("{indent}- !xb {}", hex_encode(b).to_uppercase()));
            }
            Value::Tagged(t) => {
                if t.value.contains(['\n', '\r']) {
                    lines.push(format!("{indent}- !{} |", t.tag));
                    for line in t.value.lines() {
                        lines.push(format!("{indent}  {line}"));
                    }
                    lines.push(format!("{indent}|"));
                } else {
                    lines.push(format!("{indent}- !{} {}", t.tag, t.value));
                }
            }
        }
    }
    Ok(())
}

pub fn dict_get<'a>(v: &'a Value, key: &str) -> Option<&'a Value> {
    match v {
        Value::Dict(pairs) => pairs.iter().find(|(k, _)| k == key).map(|(_, v)| v),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn example() -> &'static str {
        include_str!("../../testdata/example.xun")
    }

    #[test]
    fn readme_example() {
        let doc = parse(example()).expect("parse");
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

    #[test]
    fn encode_and_roundtrip() {
        let doc = Value::Dict(vec![
            ("server".into(), Value::Dict(vec![
                ("host".into(), Value::String("localhost".into())),
                ("port".into(), Value::Int(8080)),
            ])),
            ("empty_dict".into(), Value::Dict(vec![])),
            ("empty_list".into(), Value::List(vec![])),
            ("features".into(), Value::List(vec![
                Value::String("auth".into()),
                Value::String("cache".into()),
            ])),
            ("banner".into(), Value::String("Welcome\nto XUN".into())),
            ("flag".into(), Value::Bool(true)),
            ("color".into(), Value::Bytes(vec![0xde, 0xad, 0xbe, 0xef])),
            ("py".into(), Value::Tagged(Tagged {
                tag: "ver".into(),
                value: "3.10".into(),
            })),
        ]);

        let text = encode(&doc).unwrap();
        let parsed = parse(&text).unwrap();
        assert_eq!(doc, parsed);
    }

    #[test]
    fn file_write_and_read() {
        let doc = Value::Dict(vec![
            ("app".into(), Value::String("rust-xun".into())),
            ("version".into(), Value::Tagged(Tagged { tag: "ver".into(), value: "0.1.3".into() })),
            ("count".into(), Value::Int(100)),
            ("rate".into(), Value::Float(99.5)),
            ("enabled".into(), Value::Bool(true)),
            ("raw".into(), Value::Bytes(vec![0x01, 0x02, 0x03])),
            ("text".into(), Value::String("Line 1\nLine 2".into())),
        ]);

        let tmp_path = std::env::temp_dir().join("test_rust.xun");
        let encoded = encode(&doc).unwrap();
        std::fs::write(&tmp_path, encoded).unwrap();

        let read_str = std::fs::read_to_string(&tmp_path).unwrap();
        let parsed = parse(&read_str).unwrap();
        assert_eq!(doc, parsed);

        let _ = std::fs::remove_file(tmp_path);
    }

    #[test]
    fn test_symmetric_and_unpack() {
        let doc = Value::Dict(vec![
            ("size".into(), Value::Tagged(Tagged { tag: "sz".into(), value: "10MiB".into() })),
            ("duration".into(), Value::Tagged(Tagged { tag: "du".into(), value: "1h30m".into() })),
            ("version".into(), Value::Tagged(Tagged { tag: "ver".into(), value: "3.10.1".into() })),
        ]);

        let encoded = encode(&doc).unwrap();
        let decoded = decode(&encoded).unwrap();
        assert_eq!(doc, decoded);

        let sz = dict_get(&decoded, "size").unwrap().as_tagged().unwrap();
        assert_eq!(sz.to_size_bytes().unwrap(), 10485760);

        let du = dict_get(&decoded, "duration").unwrap().as_tagged().unwrap();
        assert_eq!(du.to_duration_seconds().unwrap(), 5400.0);

        let ver = dict_get(&decoded, "version").unwrap().as_tagged().unwrap();
        assert_eq!(ver.to_version_parts().unwrap(), vec![3, 10, 1]);
    }

    #[test]
    fn test_unicode_and_chinese() {
        let doc = Value::Dict(vec![
            ("服务名称".into(), Value::String("订单处理系统".into())),
            ("版本号".into(), Value::Tagged(Tagged { tag: "ver".into(), value: "2.1.0".into() })),
            ("端口".into(), Value::Int(8080)),
        ]);
        let encoded = encode(&doc).unwrap();
        let decoded = decode(&encoded).unwrap();
        assert_eq!(doc, decoded);
        assert_eq!(dict_get(&decoded, "服务名称"), Some(&Value::String("订单处理系统".into())));
        assert_eq!(dict_get(&decoded, "端口"), Some(&Value::Int(8080)));
    }

    #[test]
    fn test_full_20_core_tags() {
        let raw = "
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
";
        let doc = decode(raw).unwrap();
        assert_eq!(dict_get(&doc, "str_plain"), Some(&Value::String("hello world".into())));
        assert_eq!(dict_get(&doc, "str_special"), Some(&Value::String("!not_a_tag".into())));
        assert_eq!(dict_get(&doc, "num_int"), Some(&Value::Int(42)));
        assert_eq!(dict_get(&doc, "num_hex"), Some(&Value::Int(0xdeadbeef)));
        assert_eq!(dict_get(&doc, "num_oct"), Some(&Value::Int(0o755)));
        assert_eq!(dict_get(&doc, "flag_t"), Some(&Value::Bool(true)));
        assert_eq!(dict_get(&doc, "bytes_v"), Some(&Value::Bytes(vec![0xff, 0x00, 0xaa])));
        assert_eq!(dict_get(&doc, "b64_v"), Some(&Value::Bytes(vec![72, 101, 108, 108, 111])));
        assert_eq!(dict_get(&doc, "char_cp"), Some(&Value::Tagged(Tagged { tag: "c".into(), value: "中".into() })));
    }

    #[test]
    fn test_compact_arrays() {
        let src = "
numbers: !n[1, 2, 3, 4]
floats: !f[1.1, 2.2, 3.3]
chars: !c[a, b, c]
";
        let doc = decode(src).unwrap();
        let nums = dict_get(&doc, "numbers").unwrap().as_list().unwrap();
        assert_eq!(nums.len(), 4);
        assert_eq!(nums[0], Value::Int(1));
    }

    #[test]
    fn test_extreme_indent_errors() {
        assert!(decode("a:\n   b: 1\n").is_err());
        assert!(decode("a:\n\tb: 1\n").is_err());
        assert!(decode("a:\n    b: 1\n").is_err());
        assert!(decode("server:\n  host: 1\n  - item1\n").is_err());
    }
}
