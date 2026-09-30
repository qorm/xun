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

    pub fn to_date(&self) -> Result<Date, Error> {
        if self.tag != "d" {
            return Err(err(0, format!("cannot convert !{} to date", self.tag)));
        }
        parse_date(&self.value)
    }

    pub fn to_time(&self) -> Result<Time, Error> {
        if self.tag != "t" {
            return Err(err(0, format!("cannot convert !{} to time", self.tag)));
        }
        parse_time(&self.value)
    }

    pub fn to_datetime(&self) -> Result<DateTime, Error> {
        if self.tag != "dt" {
            return Err(err(0, format!("cannot convert !{} to datetime", self.tag)));
        }
        parse_datetime(&self.value)
    }

    pub fn to_ip(&self) -> Result<std::net::IpAddr, Error> {
        if self.tag != "ip" {
            return Err(err(0, format!("cannot convert !{} to IP address", self.tag)));
        }
        self.value
            .parse::<std::net::IpAddr>()
            .map_err(|_| err(0, format!("invalid IP address: {}", self.value)))
    }

    pub fn to_uuid(&self) -> Result<[u8; 16], Error> {
        if self.tag != "uuid" {
            return Err(err(0, format!("cannot convert !{} to UUID", self.tag)));
        }
        parse_uuid(&self.value)
    }

    pub fn to_char(&self) -> Result<char, Error> {
        if self.tag != "c" {
            return Err(err(0, format!("cannot convert !{} to char", self.tag)));
        }
        parse_char(&self.value)
    }
}

/// Calendar date (YYYY-MM-DD).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Date {
    pub year: i32,
    pub month: u32,
    pub day: u32,
}

/// Clock time (HH:MM[:SS[.fraction]]).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Time {
    pub hour: u32,
    pub minute: u32,
    pub second: u32,
    pub nanos: u32,
}

/// ISO 8601 datetime with a timezone offset, as in RFC 3339.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DateTime {
    pub year: i32,
    pub month: u32,
    pub day: u32,
    pub hour: u32,
    pub minute: u32,
    pub second: u32,
    pub nanos: u32,
    pub offset_seconds: i32,
}

fn parse_fixed_digits(s: &str) -> Option<u32> {
    if !s.bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    s.parse::<u32>().ok()
}

pub fn parse_date(s: &str) -> Result<Date, Error> {
    let mut parts = s.splitn(3, '-');
    let y = parts.next().unwrap_or("");
    let m = parts.next().unwrap_or("");
    let d = parts.next().unwrap_or("");
    if parts.next().is_some() || y.len() != 4 || m.len() != 2 || d.len() != 2 {
        return Err(err(0, format!("invalid date: {s}")));
    }
    let year = parse_fixed_digits(y)
        .ok_or_else(|| err(0, format!("invalid date: {s}")))?
        .try_into()
        .map_err(|_| err(0, format!("invalid date: {s}")))?;
    let month = parse_fixed_digits(m).ok_or_else(|| err(0, format!("invalid date: {s}")))?;
    let day = parse_fixed_digits(d).ok_or_else(|| err(0, format!("invalid date: {s}")))?;
    if !(1..=12).contains(&month) || !(1..=31).contains(&day) {
        return Err(err(0, format!("invalid date: {s}")));
    }
    Ok(Date { year, month, day })
}

pub fn parse_time(s: &str) -> Result<Time, Error> {
    let (base, frac) = match s.split_once('.') {
        Some((b, f)) => (b, f),
        None => (s, ""),
    };
    let parts: Vec<&str> = base.split(':').collect();
    if parts.len() != 2 && parts.len() != 3 {
        return Err(err(0, format!("invalid time: {s}")));
    }
    let hour = parse_fixed_digits(parts[0]).ok_or_else(|| err(0, format!("invalid time: {s}")))?;
    let minute = parse_fixed_digits(parts[1]).ok_or_else(|| err(0, format!("invalid time: {s}")))?;
    let second = if parts.len() == 3 {
        parse_fixed_digits(parts[2]).ok_or_else(|| err(0, format!("invalid time: {s}")))?
    } else {
        0
    };
    if hour > 23 || minute > 59 || second > 59 {
        return Err(err(0, format!("invalid time: {s}")));
    }
    let nanos = if frac.is_empty() {
        0
    } else {
        let mut frac = frac.to_string();
        if frac.len() > 9 {
            frac.truncate(9);
        }
        while frac.len() < 9 {
            frac.push('0');
        }
        frac.parse::<u32>().map_err(|_| err(0, format!("invalid time: {s}")))?
    };
    Ok(Time { hour, minute, second, nanos })
}

pub fn parse_datetime(s: &str) -> Result<DateTime, Error> {
    let (date_part, rest) = s.split_once('T').ok_or_else(|| err(0, format!("invalid datetime: {s}")))?;
    let date = parse_date(date_part)?;
    let (time_part, offset_part) = if let Some(stripped) = rest.strip_suffix('Z') {
        (stripped, 0i32)
    } else if let Some(idx) = rest.rfind(|c| c == '+' || c == '-') {
        if idx == 0 {
            return Err(err(0, format!("invalid datetime: {s}")));
        }
        let (t, off) = (&rest[..idx], &rest[idx..]);
        let sign = if off.starts_with('-') { -1 } else { 1 };
        let mut op = off[1..].split(':');
        let oh = parse_fixed_digits(op.next().unwrap_or(""))
            .ok_or_else(|| err(0, format!("invalid datetime: {s}")))?;
        let om = parse_fixed_digits(op.next().unwrap_or(""))
            .ok_or_else(|| err(0, format!("invalid datetime: {s}")))?;
        if op.next().is_some() || oh > 23 || om > 59 {
            return Err(err(0, format!("invalid datetime: {s}")));
        }
        (t, sign * (oh as i32 * 3600 + om as i32 * 60))
    } else {
        return Err(err(0, format!("invalid datetime: {s}")));
    };
    let time = parse_time(time_part)?;
    Ok(DateTime {
        year: date.year,
        month: date.month,
        day: date.day,
        hour: time.hour,
        minute: time.minute,
        second: time.second,
        nanos: time.nanos,
        offset_seconds: offset_part,
    })
}

pub fn parse_uuid(s: &str) -> Result<[u8; 16], Error> {
    let compact: String = s.chars().filter(|c| *c != '-').collect();
    if compact.len() != 32 {
        return Err(err(0, format!("invalid UUID: {s}")));
    }
    let mut out = [0u8; 16];
    for i in 0..16 {
        let byte_str = &compact[i * 2..i * 2 + 2];
        out[i] = u8::from_str_radix(byte_str, 16).map_err(|_| err(0, format!("invalid UUID: {s}")))?;
    }
    Ok(out)
}

pub fn parse_char(s: &str) -> Result<char, Error> {
    if let Some(hex) = s.strip_prefix("U+") {
        let cp = u32::from_str_radix(hex, 16).map_err(|_| err(0, format!("invalid code point: {s}")))?;
        return char::from_u32(cp).ok_or_else(|| err(0, format!("invalid code point: {s}")));
    }
    let mut chars = s.chars();
    match (chars.next(), chars.next()) {
        (Some(c), None) => Ok(c),
        _ => Err(err(0, format!("value '{s}' is not a single character"))),
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
        open_blocks: Vec::new(),
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
    // RFC-0001: stack of (indent, expected_key) used to validate 'end' / 'end <key>'.
    // expected_key is None for the root block or when the value has no name.
    open_blocks: Vec<(usize, Option<String>)>,
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

    // RFC-0001: try to consume an 'end' or 'end <key>' statement at the given indent.
    // Returns Ok(true) if consumed, Ok(false) if the peek line is not an end statement.
    // Returns Err on key mismatch.
    // expected_key: the key of the immediately enclosing dict (None for root / unnamed).
    fn try_consume_end(&mut self, indent: usize, expected_key: Option<&str>) -> Result<bool, Error> {
        let l = match self.peek() {
            Some(l) => l,
            None => return Ok(false),
        };
        if l.blank || l.indent != indent {
            return Ok(false);
        }
        if l.code == "end" {
            self.i += 1;
            return Ok(true);
        }
        if let Some(rest) = l.code.strip_prefix("end ") {
            let end_key = rest.split_whitespace().next().unwrap_or("");
            if end_key.is_empty() {
                return Ok(false);
            }
            if let Some(ek) = expected_key {
                if ek != end_key {
                    return Err(err(l.n, format!("end-key mismatch: expected '{ek}', got '{end_key}'")));
                }
            }
            self.i += 1;
            return Ok(true);
        }
        Ok(false)
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
        // RFC-0001: track root block so trailing 'end' can close it.
        self.open_blocks.push((0, None));
        let result = self.parse_dict(0, 0, None);
        self.open_blocks.pop();
        if result.is_ok() {
            self.skip_noise();
            let consumed = self.try_consume_end(0, None)?;
            let _ = consumed;
        }
        result
    }

    fn parse_dict(&mut self, indent: usize, depth: usize, dict_key: Option<&str>) -> Result<Value, Error> {
        if depth > 64 {
            let n = self.peek().map(|l| l.n).unwrap_or(0);
            return Err(err(n, "nesting exceeds 64"));
        }
        let mut obj = Vec::new();
        while self.peek().is_some() {
            self.skip_noise();
            let Some(l) = self.peek() else {
                break;
            };
            if l.blank {
                break;
            }
            if l.indent < indent {
                break;
            }
            if l.indent > indent {
                return Err(err(l.n, "invalid indent jump"));
            }
            // RFC-0001: 'end' at body indent stops the dict (caller validates via try_consume_end).
            if l.code == "end" || l.code.starts_with("end ") {
                break;
            }
            if self.is_list_item(l) {
                return Err(err(l.n, "cannot mix list items into a dictionary"));
            }
            let code = l.code.clone();
            let n = l.n;
            let (key, rest) = split_key(&code, n)?;
            if obj.iter().any(|(k, _): &(String, Value)| k == &key) {
                return Err(err(n, format!("duplicate key '{key}'")));
            }
            self.i += 1;
            let val = self.parse_value(&rest, indent, n, depth + 1, Some(&key))?;
            obj.push((key, val));
        }
        let _ = dict_key; // accepted for API symmetry; end validation is the caller's job
        Ok(Value::Dict(obj))
    }

    fn parse_list(&mut self, indent: usize, depth: usize, item_tag: Option<&str>, parent_key: Option<&str>) -> Result<Value, Error> {
        if depth > 64 {
            let n = self.peek().map(|l| l.n).unwrap_or(0);
            return Err(err(n, "nesting exceeds 64"));
        }
        let mut arr = Vec::new();
        while self.peek().is_some() {
            self.skip_noise();
            let Some(l) = self.peek() else {
                break;
            };
            if l.blank {
                break;
            }
            if l.indent < indent {
                break;
            }
            if l.indent > indent {
                return Err(err(l.n, "invalid indent jump"));
            }
            // RFC-0001: 'end' at list level stops the list.
            if l.code == "end" || l.code.starts_with("end ") {
                break;
            }
            if !self.is_list_item(l) {
                return Err(err(l.n, "cannot mix dictionary keys into a list"));
            }
            let code = l.code.clone();
            let n = l.n;
            let rest: &str = if code == "-" { "" } else { &code[2..] };
            self.i += 1;
            if looks_like_dict_entry(rest) {
                if let Some(t) = item_tag {
                    return Err(err(n, format!("!{t}[] cannot contain dictionary entries")));
                }
                // RFC-0002: inline object literal {key: value, ...} as a list item.
                if rest.trim_start().starts_with('{') {
                    arr.push(parse_inline_dict(rest, n)?);
                    continue;
                }
                arr.push(self.parse_inline_dict_entry(rest, indent + 2, n, depth + 1)?);
                continue;
            }
            let mut val = self.parse_value(rest, indent, n, depth + 1, None)?;
            if let Some(t) = item_tag {
                val = apply_tag(t, &glyph_of(&val)?, n)?;
            }
            arr.push(val);
        }
        let _ = parent_key; // accepted for API symmetry; end validation is the caller's job
        Ok(Value::List(arr))
    }

    fn parse_inline_dict_entry(
        &mut self,
        first: &str,
        key_indent: usize,
        line_no: usize,
        depth: usize,
    ) -> Result<Value, Error> {
        if depth > 64 {
            return Err(err(line_no, "nesting exceeds 64"));
        }
        let mut obj: Vec<(String, Value)> = Vec::new();
        let (key, rest) = split_key(first, line_no)?;
        obj.push((key.clone(), self.parse_value(&rest, key_indent, line_no, depth + 1, Some(&key))?));
        while self.peek().is_some() {
            self.skip_noise();
            let Some(l) = self.peek() else {
                break;
            };
            if l.blank {
                break;
            }
            if l.indent < key_indent {
                break;
            }
            if l.indent > key_indent {
                return Err(err(l.n, "invalid indent jump"));
            }
            if self.is_list_item(l) {
                break;
            }
            let text = l.text.clone();
            let n = l.n;
            let (key, rest) = split_key(&text, n)?;
            if obj.iter().any(|(k, _): &(String, Value)| k == &key) {
                return Err(err(n, format!("duplicate key '{key}'")));
            }
            self.i += 1;
            let val = self.parse_value(&rest, key_indent, n, depth + 1, Some(&key))?;
            obj.push((key, val));
        }
        Ok(Value::Dict(obj))
    }

    fn is_list_item(&self, l: &Line) -> bool {
        l.code == "-" || l.code.starts_with("- ")
    }

    fn parse_value(&mut self, raw: &str, parent_indent: usize, line_no: usize, depth: usize, value_key: Option<&str>) -> Result<Value, Error> {
        let raw = strip_trailing_comment(raw);
        let raw = raw.as_str();
        if raw == "[]" {
            return Ok(Value::List(vec![]));
        }
        if raw == "{}" {
            return Ok(Value::Dict(vec![]));
        }
        if let Some((closer, chomp)) = match_multiline(raw) {
            return self.read_multiline(parent_indent, None, &closer, chomp, line_no);
        }
        if raw.starts_with('!') {
            return self.parse_tagged(raw, parent_indent, line_no, depth, value_key);
        }
        // RFC-0002: untagged compact array of inline objects: [{...}, {...}]
        if raw.starts_with('[') && raw.ends_with(']') && raw != "[]" {
            let inner = &raw[1..raw.len() - 1];
            let pieces = split_top_level_commas(inner);
            if pieces.iter().any(|p| p.trim_start().starts_with('{')) {
                let mut out = Vec::new();
                for piece in &pieces {
                    let t = piece.trim();
                    if t.starts_with('{') {
                        out.push(parse_inline_dict(t, line_no)?);
                    } else {
                        out.push(Value::String(t.to_string()));
                    }
                }
                return Ok(Value::List(out));
            }
        }
        if raw.is_empty() {
            // RFC-0001: nested block — header at parent_indent, named value_key.
            let ek_owned = value_key.map(|s| s.to_string());
            self.open_blocks.push((parent_indent, ek_owned.clone()));
            let result = self.parse_empty_or_nested(parent_indent, line_no, depth, None, value_key);
            self.open_blocks.pop();
            if result.is_ok() {
                self.skip_noise();
                let consumed = self.try_consume_end(parent_indent, value_key)?;
                let _ = consumed;
            }
            return result;
        }
        if raw.starts_with('"') {
            return Ok(Value::String(parse_quoted_string(raw, line_no)?));
        }
        Ok(Value::String(raw.to_string()))
    }

    fn parse_tagged(&mut self, raw: &str, parent_indent: usize, line_no: usize, depth: usize, value_key: Option<&str>) -> Result<Value, Error> {
        let (tag, rest) = parse_tag_head(raw).ok_or_else(|| err(line_no, "invalid type tag"))?;
        if let Some(inner) = rest.strip_prefix('[') {
            if tag == "s" && rest != "[]" {
                return Err(err(line_no, "string arrays cannot use compact form"));
            }
            let Some(inner) = inner.strip_suffix(']') else {
                return Err(err(line_no, "unclosed compact array"));
            };
            if inner.is_empty() {
                // RFC-0001: track tagged block for 'end' validation
                let ek_owned = value_key.map(|s| s.to_string());
                self.open_blocks.push((parent_indent, ek_owned.clone()));
                let result = self.parse_empty_or_nested(parent_indent, line_no, depth, Some(&tag), value_key);
                self.open_blocks.pop();
                if result.is_ok() {
                    self.skip_noise();
                    let consumed = self.try_consume_end(parent_indent, value_key)?;
                    let _ = consumed;
                }
                return result;
            }
            // RFC-0002: detect inline object elements {key: val, ...} within compact arrays.
            let pieces = split_top_level_commas(inner);
            if pieces.iter().any(|p| p.trim_start().starts_with('{')) {
                let mut out = Vec::new();
                for piece in &pieces {
                    let t = piece.trim();
                    if t.starts_with('{') {
                        out.push(parse_inline_dict(t, line_no)?);
                    } else {
                        out.push(apply_tag(&tag, t, line_no)?);
                    }
                }
                return Ok(Value::List(out));
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
            return Ok(Value::String(parse_string_body(body, line_no)?));
        }
        apply_tag(&tag, body, line_no)
    }

    fn parse_empty_or_nested(&mut self, parent_indent: usize, _line_no: usize, depth: usize, item_tag: Option<&str>, value_key: Option<&str>) -> Result<Value, Error> {
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
            return self.parse_list(child, depth, item_tag, value_key);
        }
        if let Some(t) = item_tag {
            return Err(err(n.n, format!("!{t}[] expected list items")));
        }
        self.parse_dict(child, depth, value_key)
    }

    fn read_multiline(
        &mut self,
        parent_indent: usize,
        tag: Option<&str>,
        closer: &str,
        chomp: Chomp,
        line_no: usize,
    ) -> Result<Value, Error> {
        let base = parent_indent + 2;
        let mut parts: Vec<String> = Vec::new();
        while let Some(l) = self.peek() {
            let stripped = l.raw.trim_end_matches([' ', '\t']);
            let content = stripped.trim_start_matches(' ');
            let ind = l.raw.len() - l.raw.trim_start_matches(' ').len();
            let closer_text = strip_trailing_comment(content);
            if !l.blank && ind == parent_indent && closer_text == closer {
                self.i += 1;
                let mut s = parts.join("\n");
                match chomp {
                    Chomp::Strip => {
                        while s.ends_with('\n') {
                            s.pop();
                        }
                    }
                    Chomp::Clip => {
                        if !s.is_empty() && !s.ends_with('\n') {
                            s.push('\n');
                        }
                    }
                    Chomp::Exact => {}
                }
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

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Chomp {
    Exact,
    Strip,
    Clip,
}

fn parse_quoted_prefix(raw: &str, line_no: usize) -> Result<(String, usize), Error> {
    if !raw.starts_with('"') {
        return Err(err(line_no, "quoted string must start with '\"'"));
    }
    let mut out = String::new();
    let mut iter = raw.char_indices();
    iter.next(); // opening quote
    while let Some((idx, ch)) = iter.next() {
        match ch {
            '\\' => match iter.next() {
                Some((_, '\\')) => out.push('\\'),
                Some((_, '"')) => out.push('"'),
                Some((_, c)) => {
                    return Err(err(line_no, format!("invalid escape \\{c} in quoted string")))
                }
                None => return Err(err(line_no, "unclosed escape in quoted string")),
            },
            '"' => return Ok((out, idx + 1)),
            _ => out.push(ch),
        }
    }
    Err(err(line_no, "unclosed quoted string"))
}

fn split_key(text: &str, n: usize) -> Result<(String, String), Error> {
    if text.starts_with('"') {
        let (key, end) = parse_quoted_prefix(text, n)?;
        let after = &text[end..];
        if after == ":" {
            return Ok((key, String::new()));
        }
        if let Some(rest) = after.strip_prefix(": ") {
            return Ok((key, rest.to_string()));
        }
        return Err(err(n, "expected ': ' or trailing ':' after quoted key"));
    }
    if let Some(idx) = text.find(": ") {
        if idx > 0 {
            let key = &text[..idx];
            if key.ends_with(':') {
                return Err(err(n, format!("key must not end with ':': '{key}'")));
            }
            return Ok((key.to_string(), text[idx + 2..].to_string()));
        }
    }
    if text.ends_with(':') && text.chars().count() > 1 {
        let key = &text[..text.len() - 1];
        if key.ends_with(':') {
            return Err(err(n, format!("key must not end with ':': '{key}'")));
        }
        return Ok((key.to_string(), String::new()));
    }
    Err(err(n, "expected ': ' or trailing ':'"))
}

fn match_multiline(raw: &str) -> Option<(String, Chomp)> {
    if raw == "|" {
        return Some(("|".to_string(), Chomp::Exact));
    }
    if raw == "|-" {
        return Some(("|".to_string(), Chomp::Strip));
    }
    if raw == "|+" {
        return Some(("|".to_string(), Chomp::Clip));
    }
    let rest = raw.strip_prefix('|')?;
    if let Some(stripped) = rest.strip_suffix('-') {
        if is_ident(stripped) {
            return Some((stripped.to_string(), Chomp::Strip));
        }
    }
    if let Some(stripped) = rest.strip_suffix('+') {
        if is_ident(stripped) {
            return Some((stripped.to_string(), Chomp::Clip));
        }
    }
    if is_ident(rest) {
        return Some((rest.to_string(), Chomp::Exact));
    }
    None
}

fn looks_like_dict_entry(s: &str) -> bool {
    if s.is_empty() || s.starts_with('!') || s.starts_with('"') || s.starts_with('|') {
        return false;
    }
    // RFC-0002: inline object literal {key: val, ...} counts as a dict entry
    if s.starts_with('{') {
        return true;
    }
    s.contains(": ") || (s.ends_with(':') && s.chars().count() > 1)
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

// RFC-0002: split a string by top-level commas, respecting brace, bracket and quote nesting.
fn split_top_level_commas(inner: &str) -> Vec<String> {
    let mut out: Vec<String> = Vec::new();
    let mut start = 0usize;
    let mut brace = 0i32;
    let mut bracket = 0i32;
    let mut in_quote = false;
    let mut escape = false;
    let bytes = inner.as_bytes();
    let mut i = 0;
    while i < bytes.len() {
        let c = bytes[i];
        if escape {
            escape = false;
            i += 1;
            continue;
        }
        if in_quote {
            if c == b'\\' {
                escape = true;
                i += 1;
                continue;
            }
            if c == b'"' {
                in_quote = false;
            }
            i += 1;
            continue;
        }
        match c {
            b'"' => in_quote = true,
            b'{' => brace += 1,
            b'}' => {
                if brace > 0 {
                    brace -= 1;
                }
            }
            b'[' => bracket += 1,
            b']' => {
                if bracket > 0 {
                    bracket -= 1;
                }
            }
            b',' if brace == 0 && bracket == 0 => {
                out.push(inner[start..i].to_string());
                start = i + 1;
            }
            _ => {}
        }
        i += 1;
    }
    let last = &inner[start..];
    if !last.trim().is_empty() {
        out.push(last.to_string());
    }
    out
}

// RFC-0002: parse inline object literal {key: value, key2: value2}.
fn parse_inline_dict(text: &str, line_no: usize) -> Result<Value, Error> {
    let trimmed = text.trim();
    if !trimmed.starts_with('{') || !trimmed.ends_with('}') {
        return Err(err(line_no, "inline object must be wrapped in '{...}'"));
    }
    let inner = &trimmed[1..trimmed.len() - 1];
    if inner.trim().is_empty() {
        return Ok(Value::Dict(Vec::new()));
    }
    let pieces = split_top_level_commas(inner);
    let mut obj: Vec<(String, Value)> = Vec::new();
    for piece in &pieces {
        let t = piece.trim();
        if t.is_empty() {
            continue;
        }
        // Find top-level key separator ':' (with optional trailing ':').
        let bytes = t.as_bytes();
        let mut found: Option<usize> = None;
        let mut in_quote = false;
        let mut escape = false;
        let mut i = 0;
        while i < bytes.len() {
            let c = bytes[i];
            if escape {
                escape = false;
                i += 1;
                continue;
            }
            if in_quote {
                if c == b'\\' {
                    escape = true;
                    i += 1;
                    continue;
                }
                if c == b'"' {
                    in_quote = false;
                }
                i += 1;
                continue;
            }
            match c {
                b'"' => in_quote = true,
                b':' => {
                    if i + 1 == bytes.len() {
                        found = Some(i);
                        break;
                    }
                    if i + 1 < bytes.len() && bytes[i + 1] == b' ' {
                        found = Some(i);
                        break;
                    }
                }
                _ => {}
            }
            i += 1;
        }
        let Some(idx) = found else {
            return Err(err(line_no, format!("inline object entry missing ': ' separator: '{t}'")));
        };
        let key_raw = t[..idx].trim();
        let rest = t[idx + 1..].trim();
        let key_owned;
        let key: &str = if key_raw.starts_with('"') {
            let (k, end) = parse_quoted_prefix(key_raw, line_no)?;
            // Anything after the quoted prefix becomes part of rest (already trimmed above).
            let _ = end;
            key_owned = k;
            key_owned.as_str()
        } else {
            key_raw
        };
        if key.is_empty() {
            return Err(err(line_no, "empty key in inline object"));
        }
        if obj.iter().any(|(k, _)| k == key) {
            return Err(err(line_no, format!("duplicate key '{key}'")));
        }
        let value: Value = if rest.is_empty() {
            Value::String(String::new())
        } else if rest.starts_with('"') {
            Value::String(parse_quoted_string(rest, line_no)?)
        } else if rest.starts_with('!') {
            let (tag, tail) = parse_tag_head(rest).ok_or_else(|| err(line_no, format!("invalid tag in inline object: '{rest}'")))?;
            if tail.is_empty() {
                return Err(err(line_no, format!("missing value for !{tag}")));
            }
            if let Some(stripped) = tail.strip_prefix(' ') {
                apply_tag(&tag, stripped, line_no)?
            } else if let Some(arr) = tail.strip_prefix('[') {
                let Some(arr) = arr.strip_suffix(']') else {
                    return Err(err(line_no, "unclosed compact array in inline object"));
                };
                if arr.is_empty() {
                    Value::List(Vec::new())
                } else {
                    let mut out = Vec::new();
                    for g in split_compact(arr) {
                        out.push(apply_tag(&tag, g, line_no)?);
                    }
                    Value::List(out)
                }
            } else {
                return Err(err(line_no, "expected space or '[' after type tag in inline object"));
            }
        } else {
            Value::String(rest.to_string())
        };
        obj.push((key.to_string(), value));
    }
    Ok(Value::Dict(obj))
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

/// Strip paired double quotes from both ends, repeatedly.
fn strip_surrounding_quotes(s: &str) -> &str {
    let mut out = s;
    while out.len() >= 2 && out.starts_with('"') && out.ends_with('"') {
        out = &out[1..out.len() - 1];
    }
    out
}

fn is_ascii_ws(c: u8) -> bool {
    matches!(c, b' ' | b'\t' | b'\n' | b'\r' | 0x0b | 0x0c)
}

/// Glyphs that JavaScript Number() would coerce.
fn looks_like_js_number(s: &str) -> bool {
    let b = s.as_bytes();
    let mut start = 0;
    let mut end = b.len();
    while start < end && is_ascii_ws(b[start]) {
        start += 1;
    }
    while end > start && is_ascii_ws(b[end - 1]) {
        end -= 1;
    }
    if start >= end {
        return false;
    }
    let t = &b[start..end];
    let mut i = 0;
    if t[0] == b'+' || t[0] == b'-' {
        i = 1;
        if i >= t.len() {
            return false;
        }
    }
    let rest = &t[i..];
    if rest == b"Infinity" {
        return true;
    }
    if rest.len() >= 3 && rest[0] == b'0' && (rest[1] == b'x' || rest[1] == b'X') {
        return rest[2..].iter().all(|c| c.is_ascii_hexdigit());
    }
    if rest.len() >= 3 && rest[0] == b'0' && (rest[1] == b'b' || rest[1] == b'B') {
        return rest[2..].iter().all(|c| *c == b'0' || *c == b'1');
    }
    if rest.len() >= 3 && rest[0] == b'0' && (rest[1] == b'o' || rest[1] == b'O') {
        return rest[2..].iter().all(|c| (b'0'..=b'7').contains(c));
    }
    looks_like_js_decimal(rest)
}

fn looks_like_js_decimal(t: &[u8]) -> bool {
    let n = t.len();
    let mut i = 0;
    let mut saw_digit = false;
    if i < n && t[i] == b'.' {
        i += 1;
        while i < n && t[i].is_ascii_digit() {
            saw_digit = true;
            i += 1;
        }
    } else {
        while i < n && t[i].is_ascii_digit() {
            saw_digit = true;
            i += 1;
        }
        if i < n && t[i] == b'.' {
            i += 1;
            while i < n && t[i].is_ascii_digit() {
                i += 1;
            }
        }
    }
    if !saw_digit {
        return false;
    }
    if i < n && (t[i] == b'e' || t[i] == b'E') {
        i += 1;
        if i < n && (t[i] == b'+' || t[i] == b'-') {
            i += 1;
        }
        let exp_start = i;
        while i < n && t[i].is_ascii_digit() {
            i += 1;
        }
        if i == exp_start {
            return false;
        }
    }
    i == n
}

fn needs_string_tag(s: &str) -> bool {
    s.starts_with('!') || s == "[]" || s == "{}" || s.starts_with('|') || looks_like_js_number(s)
}

fn parse_quoted_string(raw: &str, line_no: usize) -> Result<String, Error> {
    if !raw.starts_with('"') {
        return Err(err(line_no, "quoted string must start with '\"'"));
    }
    let mut out = String::new();
    let bytes = raw.as_bytes();
    let mut i = 1;
    while i < bytes.len() {
        let ch = bytes[i];
        if ch == b'\\' {
            if i + 1 >= bytes.len() {
                return Err(err(line_no, "unclosed escape in quoted string"));
            }
            match bytes[i + 1] {
                b'\\' | b'"' => {
                    out.push(bytes[i + 1] as char);
                    i += 2;
                }
                c => return Err(err(line_no, format!("invalid escape \\{} in quoted string", c as char))),
            }
            continue;
        }
        if ch == b'"' {
            if i != bytes.len() - 1 {
                return Err(err(line_no, "unexpected trailing content after quoted string"));
            }
            return Ok(out);
        }
        out.push(ch as char);
        i += 1;
    }
    Err(err(line_no, "unclosed quoted string"))
}

fn needs_quoted_glyph(s: &str) -> bool {
    s.trim() != s || s.contains('"') || s.contains('\\')
}

fn quote_glyph(s: &str) -> String {
    let mut out = String::from('"');
    for ch in s.chars() {
        match ch {
            '\\' => out.push_str("\\\\"),
            '"' => out.push_str("\\\""),
            _ => out.push(ch),
        }
    }
    out.push('"');
    out
}

fn encode_string_glyph(s: &str) -> String {
    let body = if needs_quoted_glyph(s) { quote_glyph(s) } else { s.to_string() };
    if needs_string_tag(s) {
        format!("!s {body}")
    } else {
        body
    }
}

fn parse_string_body(body: &str, line_no: usize) -> Result<String, Error> {
    if body.starts_with('"') {
        parse_quoted_string(body, line_no)
    } else {
        Ok(body.to_string())
    }
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
                let s = strip_surrounding_quotes(s);
                if s.contains(['\n', '\r']) {
                    lines.push(format!("{indent}{k}: |"));
                    for line in s.lines() {
                        lines.push(format!("{indent}  {line}"));
                    }
                    lines.push(format!("{indent}|"));
                } else if s.is_empty() {
                    lines.push(format!("{indent}{k}:"));
                } else {
                    lines.push(format!("{indent}{k}: {}", encode_string_glyph(s)));
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
                let s = strip_surrounding_quotes(s);
                if s.contains(['\n', '\r']) {
                    lines.push(format!("{indent}- |"));
                    for line in s.lines() {
                        lines.push(format!("{indent}  {line}"));
                    }
                    lines.push(format!("{indent}|"));
                } else if s.is_empty() {
                    lines.push(format!("{indent}-"));
                } else {
                    lines.push(format!("{indent}- {}", encode_string_glyph(s)));
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
    fn encode_strips_surrounding_quotes() {
        let doc = Value::Dict(vec![
            ("a".into(), Value::String("\"hello\"".into())),
            ("b".into(), Value::String("\"\"".into())),
            ("c".into(), Value::String("\"!x\"".into())),
            ("items".into(), Value::List(vec![
                Value::String("\"p\"".into()),
                Value::String("\"q\"".into()),
            ])),
            ("keep".into(), Value::Tagged(Tagged { tag: "s".into(), value: "\"keep\"".into() })),
        ]);
        let text = encode(&doc).unwrap();
        assert_eq!(text, "a: hello\nb:\nc: !s !x\nitems:\n  - p\n  - q\nkeep: !s \"keep\"\n");
    }

    #[test]
    fn encode_numeric_looking_strings_with_s_tag() {
        let cases: Vec<(Value, &str)> = vec![
            (Value::Dict(vec![("a".into(), Value::String("123".into()))]), "a: !s 123\n"),
            (Value::Dict(vec![("a".into(), Value::String("3.10".into()))]), "a: !s 3.10\n"),
            (Value::Dict(vec![("a".into(), Value::String("-1.5".into()))]), "a: !s -1.5\n"),
            (Value::Dict(vec![("a".into(), Value::String("1e-3".into()))]), "a: !s 1e-3\n"),
            (Value::Dict(vec![("a".into(), Value::String("0xFF".into()))]), "a: !s 0xFF\n"),
            (Value::Dict(vec![("a".into(), Value::String("0b10".into()))]), "a: !s 0b10\n"),
            (Value::Dict(vec![("a".into(), Value::String("0o755".into()))]), "a: !s 0o755\n"),
            (Value::Dict(vec![("a".into(), Value::String("Infinity".into()))]), "a: !s Infinity\n"),
            (Value::Dict(vec![("a".into(), Value::String("\"8080\"".into()))]), "a: !s 8080\n"),
            (Value::Dict(vec![("items".into(), Value::List(vec![
                Value::String("80".into()),
                Value::String("443".into()),
            ]))]), "items:\n  - !s 80\n  - !s 443\n"),
            (Value::Dict(vec![("a".into(), Value::Int(123))]), "a: !i 123\n"),
            (Value::Dict(vec![("a".into(), Value::String("hello".into()))]), "a: hello\n"),
            (Value::Dict(vec![("a".into(), Value::String("123abc".into()))]), "a: 123abc\n"),
            (Value::Dict(vec![("a".into(), Value::String("1.2.3".into()))]), "a: 1.2.3\n"),
        ];
        for (doc, want) in cases {
            assert_eq!(encode(&doc).unwrap(), want);
        }
        let parsed = parse("a: 123\n").unwrap();
        assert_eq!(parsed, Value::Dict(vec![("a".into(), Value::String("123".into()))]));
        assert_eq!(encode(&parsed).unwrap(), "a: !s 123\n");
    }

    #[test]
    fn file_write_and_read() {
        let doc = Value::Dict(vec![
            ("app".into(), Value::String("rust-xun".into())),
            ("version".into(), Value::Tagged(Tagged { tag: "ver".into(), value: "0.1.5".into() })),
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

    #[test]
    fn unpack_all_formats() {
        let raw = "
date_v: !d 2026-08-14
time_v: !t 16:54:00.123
dt_v: !dt 2026-08-14T16:54:00+08:00
tz_v: !tz Asia/Shanghai
dur_v: !du 1d2h30m15s
sz_v: !sz 10MiB
unix_v: !unix 1700000000
ver_v: !ver 3.10.1
uuid_v: !uuid 12345678-1234-5678-1234-567812345678
ip_v: !ip ::1
char_v: !c A
char_cp: !c U+4E2D
";
        let doc = decode(raw).unwrap();
        let t = |k: &str| dict_get(&doc, k).unwrap().as_tagged().unwrap().clone();

        assert_eq!(t("date_v").to_date().unwrap(), Date { year: 2026, month: 8, day: 14 });
        assert_eq!(
            t("time_v").to_time().unwrap(),
            Time { hour: 16, minute: 54, second: 0, nanos: 123_000_000 }
        );
        assert_eq!(
            t("dt_v").to_datetime().unwrap(),
            DateTime {
                year: 2026,
                month: 8,
                day: 14,
                hour: 16,
                minute: 54,
                second: 0,
                nanos: 0,
                offset_seconds: 8 * 3600,
            }
        );
        assert_eq!(t("dt_v").to_datetime().unwrap().offset_seconds, 8 * 3600);
        assert_eq!(t("ip_v").to_ip().unwrap(), std::net::IpAddr::V6("::1".parse().unwrap()));
        assert_eq!(
            t("uuid_v").to_uuid().unwrap(),
            [0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x56, 0x78, 0x12, 0x34, 0x56, 0x78]
        );
        assert_eq!(t("char_v").to_char().unwrap(), 'A');
        assert_eq!(t("char_cp").to_char().unwrap(), '中');
        assert_eq!(t("sz_v").to_size_bytes().unwrap(), 10 * 1024 * 1024);
        assert_eq!(
            t("dur_v").to_duration_seconds().unwrap(),
            1.0 * 86400.0 + 2.0 * 3600.0 + 30.0 * 60.0 + 15.0
        );
        assert_eq!(t("ver_v").to_version_parts().unwrap(), vec![3, 10, 1]);

        assert!(t("uuid_v").to_char().is_err());
        assert!(Tagged { tag: "uuid".into(), value: "12345678-1234-5678-1234-5678123456".into() }
            .to_uuid()
            .is_err());
        assert!(Tagged { tag: "c".into(), value: "ab".into() }.to_char().is_err());
        assert!(Tagged { tag: "ip".into(), value: "127.0.0.1:80".into() }.to_ip().is_err());
        assert!(Tagged { tag: "dt".into(), value: "2026-08-14T16:54:00".into() }
            .to_datetime()
            .is_err());
    }

    #[test]
    fn invalid_glyphs_all_tags() {
        let cases = [
            "a: !i 1.5\n",
            "a: !f 8080\n",
            "a: !x XYZ\n",
            "a: !xb F0A\n",
            "a: !o 89\n",
            "a: !b yes\n",
            "a: !d 2026/08/14\n",
            "a: !t 4pm\n",
            "a: !dt 2026-08-14T16:54:00\n",
            "a: !tz CST\n",
            "a: !du 90 minutes\n",
            "a: !sz 10m\n",
            "a: !unix 01692000000\n",
            "a: !ver 3.10.beta\n",
            "a: !uuid 12345678-1234-5678-1234-5678123456\n",
            "a: !ip 127.0.0.1:80\n",
            "a: !b64 not_base64!!\n",
            "a: !c ab\n",
        ];
        for src in cases {
            assert!(decode(src).is_err(), "should reject: {src:?}");
        }
    }

    const MUST_TAG: &[&str] = &[
        "0", "00", "012", "08", "8080", "+123", "-123", "-0", "+0",
        "3.10", "3.", ".5", ".0", "0.", "0.0", "00.1", "-.5", "+.5", "+0.0", "-0.0",
        "1e3", "1E-3", "1e+10", "0e0", "1E+0", "1e-0", "+1.5e-10", "5.e2", "+.5e2", "-.5E-1",
        "0xFF", "0Xff", "0x0", "0xabcdef", "0XABCDEF",
        "0b10", "0B10", "0b0", "0b01",
        "0o755", "0O7", "0o0", "0o07",
        "Infinity", "+Infinity", "-Infinity",
        "9007199254740991", "9007199254740993",
        "-0x10", "+0x10", "-0b1", "+0b10", "-0o10",
        " 123",
    ];

    const MUST_NOT_TAG: &[&str] = &[
        "hello", "123abc", "abc123", "1.2.3", "3.1.0",
        "1e", "1e+", "e3", "e10", "5.e", ".", "+", "-",
        "0x", "0b", "0o", "0xg", "0xG", "0b2", "0o8", "0x10n", "123n",
        "infinity", "INFINITY", "Inf", "NaN", "true", "false", "null",
        "1_000", "1_2", "0xFF_AA", "127.0.0.1", "2026-08-14", "::1",
    ];

    fn nest_encode(levels: usize) -> Value {
        let mut o = Value::Dict(vec![("v".into(), Value::String("leaf".into()))]);
        for _ in 0..levels {
            o = Value::Dict(vec![("c".into(), o)]);
        }
        o
    }

    fn nest_source(levels: usize) -> String {
        let mut s = String::new();
        for i in 0..levels {
            s.push_str(&"  ".repeat(i));
            s.push_str(&format!("k{i}:\n"));
        }
        s.push_str(&"  ".repeat(levels));
        s.push_str("v: leaf\n");
        s
    }

    fn dict_a(v: Value) -> Value {
        Value::Dict(vec![("a".into(), v)])
    }

    #[test]
    fn extreme_numeric_looking_strings() {
        for s in MUST_TAG {
            let quoted = s.trim() != *s || s.contains('"') || s.contains('\\');
            let body = if quoted { quote_glyph(s) } else { s.to_string() };
            let text = encode(&dict_a(Value::String((*s).into()))).unwrap();
            assert_eq!(text, format!("a: !s {body}\n"), "encode {s:?}");
            let parsed = parse(&text).unwrap();
            assert_eq!(parsed, dict_a(Value::String((*s).into())));
        }
    }

    #[test]
    fn extreme_quoted_strings_preserve_spaces() {
        assert_eq!(encode(&dict_a(Value::String("123 ".into()))).unwrap(), "a: !s \"123 \"\n");
        assert_eq!(parse("a: \"123 \"\n").unwrap(), dict_a(Value::String("123 ".into())));
        assert_eq!(parse("a: \"\"\n").unwrap(), dict_a(Value::String("".into())));
        let round = parse("a: \"123 \"\n").unwrap();
        assert_eq!(encode(&round).unwrap(), "a: !s \"123 \"\n");
    }

    #[test]
    fn extreme_non_numeric_stay_untagged() {
        for s in MUST_NOT_TAG {
            let text = encode(&dict_a(Value::String((*s).into()))).unwrap();
            assert_eq!(text, format!("a: {s}\n"), "encode {s:?}");
        }
    }

    #[test]
    fn extreme_specials_quotes_lists() {
        assert_eq!(encode(&dict_a(Value::String("!x".into()))).unwrap(), "a: !s !x\n");
        assert_eq!(encode(&dict_a(Value::String("[]".into()))).unwrap(), "a: !s []\n");
        assert_eq!(encode(&dict_a(Value::String("{}".into()))).unwrap(), "a: !s {}\n");
        assert_eq!(encode(&dict_a(Value::String("|foo".into()))).unwrap(), "a: !s |foo\n");
        assert_eq!(encode(&dict_a(Value::Int(123))).unwrap(), "a: !i 123\n");
        assert_eq!(encode(&dict_a(Value::Float(3.14))).unwrap(), "a: !f 3.14\n");
        assert_eq!(encode(&dict_a(Value::Bool(true))).unwrap(), "a: !b true\n");
        let data = Value::Dict(vec![
            ("ports".into(), Value::List(vec![
                Value::String("80".into()),
                Value::String("443".into()),
                Value::String("8080".into()),
            ])),
            ("mixed".into(), Value::List(vec![
                Value::String("1".into()),
                Value::Int(1),
                Value::String("x".into()),
            ])),
        ]);
        let text = encode(&data).unwrap();
        let parsed = parse(&text).unwrap();
        assert_eq!(parsed, data);
    }

    #[test]
    fn extreme_untagged_reencode() {
        let parsed = parse("a: 123\nb: 3.10\nc: 0xFF\nd: Infinity\ne: true\n").unwrap();
        assert_eq!(
            encode(&parsed).unwrap(),
            "a: !s 123\nb: !s 3.10\nc: !s 0xFF\nd: !s Infinity\ne: true\n"
        );
    }

    #[test]
    fn extreme_parser_limits() {
        assert_eq!(parse("").unwrap(), Value::Dict(vec![]));
        assert_eq!(
            dict_get(&parse("\u{feff}a: hello\n").unwrap(), "a"),
            Some(&Value::String("hello".into()))
        );
        assert!(parse("a: ok\0no\n").is_err());
        assert!(parse(&"x".repeat(1024 * 1024 + 1)).is_err());
        assert!(parse(&nest_source(64)).is_ok());
        assert!(parse(&nest_source(65)).is_err());
        assert_eq!(
            dict_get(&parse("a: 123\r\nb: x\r\n").unwrap(), "a"),
            Some(&Value::String("123".into()))
        );
        assert_eq!(
            dict_get(&parse("a: !s 3.10\n").unwrap(), "a"),
            Some(&Value::String("3.10".into()))
        );
    }

    #[test]
    fn extreme_encoder_limits() {
        let text = encode(&nest_encode(64)).unwrap();
        let mut cur = parse(&text).unwrap();
        for _ in 0..64 {
            cur = dict_get(&cur, "c").unwrap().clone();
        }
        assert_eq!(dict_get(&cur, "v"), Some(&Value::String("leaf".into())));
        assert!(encode(&nest_encode(65)).is_err());
        assert!(encode(&Value::Dict(vec![("".into(), Value::String("x".into()))])).is_err());
        assert!(encode(&Value::Dict(vec![("a: b".into(), Value::String("x".into()))])).is_err());
        let text = encode(&Value::Dict(vec![
            ("8080".into(), Value::String("8080".into())),
            ("3.10".into(), Value::String("3.10".into())),
        ]))
        .unwrap();
        assert!(text.contains("8080: !s 8080"));
        assert!(text.contains("3.10: !s 3.10"));
    }

    #[test]
    fn duration_supports_ms() {
        assert_eq!(parse_duration("500ms").unwrap(), 0.5);
        assert_eq!(parse_duration("15s500ms").unwrap(), 15.5);
        assert_eq!(parse_duration("1d2h30m15s").unwrap(), 95415.0);
        assert_eq!(parse_duration("1m").unwrap(), 60.0);
        assert!(parse_duration("90 minutes").is_err());
        let doc = decode("x: !du 500ms").unwrap();
        assert_eq!(
            dict_get(&doc, "x"),
            Some(&Value::Tagged(Tagged { tag: "du".into(), value: "500ms".into() }))
        );
        let native = unpack(&doc).unwrap();
        assert_eq!(dict_get(&native, "x"), Some(&Value::Float(0.5)));
    }

    #[test]
    fn o_x_unix_tags_round_trip_via_tagged() {
        assert_eq!(
            dict_get(&decode("mode: !o 755").unwrap(), "mode"),
            Some(&Value::Tagged(Tagged { tag: "o".into(), value: "755".into() }))
        );
        assert_eq!(
            dict_get(&decode("h: !x DEAD_BEEF").unwrap(), "h"),
            Some(&Value::Tagged(Tagged { tag: "x".into(), value: "DEAD_BEEF".into() }))
        );
        assert_eq!(
            dict_get(&decode("u: !unix 1692000000").unwrap(), "u"),
            Some(&Value::Tagged(Tagged { tag: "unix".into(), value: "1692000000".into() }))
        );
        assert_eq!(encode(&decode("mode: !o 755").unwrap()).unwrap(), "mode: !o 755\n");
        assert_eq!(encode(&decode("h: !x DEAD_BEEF").unwrap()).unwrap(), "h: !x DEAD_BEEF\n");
        assert_eq!(encode(&decode("u: !unix 1692000000").unwrap()).unwrap(), "u: !unix 1692000000\n");
        let doc = decode("mode: !o 755\nh: !x DEAD_BEEF\nu: !unix 1692000000\n").unwrap();
        let native = unpack(&doc).unwrap();
        assert_eq!(dict_get(&native, "mode"), Some(&Value::Int(0o755)));
        assert_eq!(dict_get(&native, "h"), Some(&Value::Int(0xdeadbeef)));
        assert_eq!(dict_get(&native, "u"), Some(&Value::Int(1692000000)));
        // to_number() on Tagged
        let mode = dict_get(&doc, "mode").unwrap().as_tagged().unwrap();
        assert_eq!(mode.to_number().unwrap(), 0o755 as f64);
        let h = dict_get(&doc, "h").unwrap().as_tagged().unwrap();
        assert_eq!(h.to_number().unwrap(), 3735928559.0);
        let u = dict_get(&doc, "u").unwrap().as_tagged().unwrap();
        assert_eq!(u.to_number().unwrap(), 1692000000.0);
    }

    #[test]
    fn trailing_comments_are_stripped_outside_quotes_and_multiline() {
        let get = |src: &str, k: &str| dict_get(&decode(src).unwrap(), k).cloned();
        assert_eq!(get("port: 8080 # listen\n", "port"), Some(Value::String("8080".into())));
        assert_eq!(get("a: foo # bar\n", "a"), Some(Value::String("foo".into())));
        assert_eq!(get("a: foo#bar\n", "a"), Some(Value::String("foo#bar".into())));
        assert_eq!(get("a: \"foo # bar\"\n", "a"), Some(Value::String("foo # bar".into())));
        assert_eq!(get("a: # only comment\n", "a"), Some(Value::String(String::new())));
        assert_eq!(
            get("list:\n  - x # c\n  - y\n", "list"),
            Some(Value::List(vec![
                Value::String("x".into()),
                Value::String("y".into())
            ]))
        );
        assert_eq!(get("t: |\n  keep # me\n|\n", "t"), Some(Value::String("keep # me".into())));
        assert_eq!(get("a: 1 # c\n# full line\nb: 2\n", "a"), Some(Value::String("1".into())));
        assert_eq!(get("a: 1 # c\n# full line\nb: 2\n", "b"), Some(Value::String("2".into())));
    }

    #[test]
    fn quoted_keys_and_key_validation() {
        assert_eq!(
            decode("\"my key\": 1").unwrap(),
            dict_of(&[("my key", Value::String("1".into()))])
        );
        assert_eq!(
            decode("\"a: b\": 1\nc: 2").unwrap(),
            dict_of(&[
                ("a: b", Value::String("1".into())),
                ("c", Value::String("2".into()))
            ])
        );
        assert_eq!(
            decode("\"my key\":").unwrap(),
            dict_of(&[("my key", Value::String(String::new()))])
        );
        assert!(decode("a:: 1").is_err());
        // unquoted key may contain colon without following space
        assert_eq!(
            decode("a:b: 1").unwrap(),
            dict_of(&[("a:b", Value::String("1".into()))])
        );
    }

    fn dict_of(pairs: &[(&str, Value)]) -> Value {
        Value::Dict(pairs.iter().map(|(k, v)| (k.to_string(), v.clone())).collect())
    }

    // =====================================================================
    // RFC-0001: optional 'end' block delimiter
    // =====================================================================

    #[test]
    fn test_rfc0001_bare_end_closes_top_level_dict() {
        let src = "server:\n  host: localhost\n  port: 8080\nend\n";
        let doc = decode(src).unwrap();
        let server = dict_get(&doc, "server").unwrap();
        assert_eq!(dict_get(server, "host"), Some(&Value::String("localhost".into())));
        assert_eq!(dict_get(server, "port"), Some(&Value::String("8080".into())));
    }

    #[test]
    fn test_rfc0001_bare_end_closes_nested_dict() {
        let src = "server:\n  host: localhost\n  tls:\n    cert: /etc/ssl/cert.pem\n    mode: 755\n  end\n  port: 8080\nend\n";
        let doc = decode(src).unwrap();
        let server = dict_get(&doc, "server").unwrap();
        let tls = dict_get(server, "tls").unwrap();
        assert_eq!(dict_get(tls, "cert"), Some(&Value::String("/etc/ssl/cert.pem".into())));
        assert_eq!(dict_get(tls, "mode"), Some(&Value::String("755".into())));
        assert_eq!(dict_get(server, "port"), Some(&Value::String("8080".into())));
    }

    #[test]
    fn test_rfc0001_end_with_key_closes_named_nested_dict() {
        let src = "server:\n  host: localhost\n  tls:\n    cert: /etc/ssl/cert.pem\n  end tls\n  port: 8080\nend server\n";
        let doc = decode(src).unwrap();
        let server = dict_get(&doc, "server").unwrap();
        assert_eq!(dict_get(server, "host"), Some(&Value::String("localhost".into())));
        let tls = dict_get(server, "tls").unwrap();
        assert_eq!(dict_get(tls, "cert"), Some(&Value::String("/etc/ssl/cert.pem".into())));
        assert_eq!(dict_get(server, "port"), Some(&Value::String("8080".into())));
    }

    #[test]
    fn test_rfc0001_bare_end_closes_list_block() {
        let src = "servers:\n  - host: a\n    port: 80\n  - host: b\n    port: 81\nend\n";
        let doc = decode(src).unwrap();
        let servers = dict_get(&doc, "servers").unwrap().as_list().unwrap();
        assert_eq!(servers.len(), 2);
        assert_eq!(dict_get(&servers[0], "host"), Some(&Value::String("a".into())));
        assert_eq!(dict_get(&servers[1], "host"), Some(&Value::String("b".into())));
    }

    #[test]
    fn test_rfc0001_bare_end_equivalent_to_dedent() {
        let with_end = "a: 1\nb: 2\nend\n";
        let without_end = "a: 1\nb: 2\n";
        let doc_with = decode(with_end).unwrap();
        let doc_without = decode(without_end).unwrap();
        assert_eq!(doc_with, doc_without);
    }

    #[test]
    fn test_rfc0001_end_key_mismatch_throws() {
        let src = "server:\n  host: localhost\n  port: 8080\nend tls\n";
        let err = decode(src).unwrap_err();
        assert!(err.message.contains("end-key mismatch"), "expected end-key mismatch, got: {}", err.message);
    }

    #[test]
    fn test_rfc0001_bare_end_on_root_allowed() {
        let src = "a: 1\nend\n";
        let doc = decode(src).unwrap();
        assert_eq!(dict_get(&doc, "a"), Some(&Value::String("1".into())));
    }

    #[test]
    fn test_rfc0001_end_as_dict_key_allowed() {
        // 'end' as a dict key is just a regular identifier, not a delimiter.
        let src = "end: 1\nend2: 2\n";
        let doc = decode(src).unwrap();
        assert_eq!(dict_get(&doc, "end"), Some(&Value::String("1".into())));
        assert_eq!(dict_get(&doc, "end2"), Some(&Value::String("2".into())));
    }

    #[test]
    fn test_rfc0001_end_inside_multiline_block_is_literal() {
        let src = "script: |\n  echo \"end of script\"\n|\n";
        let doc = decode(src).unwrap();
        assert_eq!(dict_get(&doc, "script"), Some(&Value::String("echo \"end of script\"".into())));
    }

    #[test]
    fn test_rfc0001_deeply_nested_end_chains() {
        let src = "a:\n  b:\n    c:\n      d: 1\n    end c\n  end b\nend a\ne: 2\n";
        let doc = decode(src).unwrap();
        let a = dict_get(&doc, "a").unwrap();
        let b = dict_get(a, "b").unwrap();
        let c = dict_get(b, "c").unwrap();
        assert_eq!(dict_get(c, "d"), Some(&Value::String("1".into())));
        assert_eq!(dict_get(&doc, "e"), Some(&Value::String("2".into())));
    }

    #[test]
    fn test_rfc0001_end_allows_sibling_content_after() {
        let src = "server:\n  host: a\nend server\nproxy:\n  host: b\nend proxy\n";
        let doc = decode(src).unwrap();
        let server = dict_get(&doc, "server").unwrap();
        let proxy = dict_get(&doc, "proxy").unwrap();
        assert_eq!(dict_get(server, "host"), Some(&Value::String("a".into())));
        assert_eq!(dict_get(proxy, "host"), Some(&Value::String("b".into())));
    }

    #[test]
    fn test_rfc0001_root_end_with_complex_dict() {
        let src = "server:\n  host: localhost\n  tls:\n    cert: /etc/ssl/cert.pem\n  end tls\n  port: 8080\nend server\nname: production\n";
        let doc = decode(src).unwrap();
        let server = dict_get(&doc, "server").unwrap();
        assert_eq!(dict_get(server, "host"), Some(&Value::String("localhost".into())));
        let tls = dict_get(server, "tls").unwrap();
        assert_eq!(dict_get(tls, "cert"), Some(&Value::String("/etc/ssl/cert.pem".into())));
        assert_eq!(dict_get(server, "port"), Some(&Value::String("8080".into())));
        assert_eq!(dict_get(&doc, "name"), Some(&Value::String("production".into())));
    }

    // =====================================================================
    // RFC-0002: inline object / array literals
    // =====================================================================

    #[test]
    fn test_rfc0002_inline_object_as_list_item() {
        let src = "servers:\n  - {host: a, port: 80}\n  - {host: b, port: 81}\n";
        let doc = decode(src).unwrap();
        let servers = dict_get(&doc, "servers").unwrap().as_list().unwrap();
        assert_eq!(servers.len(), 2);
        assert_eq!(dict_get(&servers[0], "host"), Some(&Value::String("a".into())));
        assert_eq!(dict_get(&servers[0], "port"), Some(&Value::String("80".into())));
        assert_eq!(dict_get(&servers[1], "host"), Some(&Value::String("b".into())));
        assert_eq!(dict_get(&servers[1], "port"), Some(&Value::String("81".into())));
    }

    #[test]
    fn test_rfc0002_inline_object_with_quoted_value() {
        let src = "servers:\n  - {host: \"my host\", port: 80}\n";
        let doc = decode(src).unwrap();
        let servers = dict_get(&doc, "servers").unwrap().as_list().unwrap();
        assert_eq!(dict_get(&servers[0], "host"), Some(&Value::String("my host".into())));
        assert_eq!(dict_get(&servers[0], "port"), Some(&Value::String("80".into())));
    }

    #[test]
    fn test_rfc0002_inline_object_with_tagged_values() {
        let src = "cfg:\n  - {port: !n 8080, mode: !o 755, color: !xb FF00AA}\n";
        let doc = decode(src).unwrap();
        let cfg = dict_get(&doc, "cfg").unwrap().as_list().unwrap();
        assert_eq!(dict_get(&cfg[0], "port"), Some(&Value::Int(8080)));
        let mode = dict_get(&cfg[0], "mode").unwrap();
        assert_eq!(mode, &Value::Tagged(crate::Tagged { tag: "o".into(), value: "755".into() }));
        let color = dict_get(&cfg[0], "color").unwrap();
        assert_eq!(color, &Value::Bytes(vec![0xff, 0x00, 0xaa]));
    }

    #[test]
    fn test_rfc0002_inline_object_with_empty_value() {
        let src = "cfg:\n  - {name: \"\"}\n  - {empty:}\n";
        let doc = decode(src).unwrap();
        let cfg = dict_get(&doc, "cfg").unwrap().as_list().unwrap();
        assert_eq!(dict_get(&cfg[0], "name"), Some(&Value::String(String::new())));
        assert_eq!(dict_get(&cfg[1], "empty"), Some(&Value::String(String::new())));
    }

    #[test]
    fn test_rfc0002_compact_array_of_inline_objects_no_tag() {
        let src = "peers: [{host: a, port: 80}, {host: b, port: 81}]\n";
        let doc = decode(src).unwrap();
        let peers = dict_get(&doc, "peers").unwrap().as_list().unwrap();
        assert_eq!(peers.len(), 2);
        assert_eq!(dict_get(&peers[0], "host"), Some(&Value::String("a".into())));
        assert_eq!(dict_get(&peers[1], "host"), Some(&Value::String("b".into())));
    }

    #[test]
    fn test_rfc0002_compact_tagged_array_of_inline_objects() {
        let src = "items: !o[{a: 1, b: 2}, {c: 3}]\n";
        let doc = decode(src).unwrap();
        let items = dict_get(&doc, "items").unwrap().as_list().unwrap();
        assert_eq!(items.len(), 2);
        assert_eq!(dict_get(&items[0], "a"), Some(&Value::String("1".into())));
        assert_eq!(dict_get(&items[0], "b"), Some(&Value::String("2".into())));
        assert_eq!(dict_get(&items[1], "c"), Some(&Value::String("3".into())));
    }

    #[test]
    fn test_rfc0002_mix_block_and_inline_list_items() {
        let src = "servers:\n  - {host: a, port: 80}\n  - host: b\n    port: 81\n    tls: enabled\n";
        let doc = decode(src).unwrap();
        let servers = dict_get(&doc, "servers").unwrap().as_list().unwrap();
        assert_eq!(servers.len(), 2);
        assert_eq!(dict_get(&servers[0], "host"), Some(&Value::String("a".into())));
        assert_eq!(dict_get(&servers[1], "host"), Some(&Value::String("b".into())));
        assert_eq!(dict_get(&servers[1], "tls"), Some(&Value::String("enabled".into())));
    }

    #[test]
    fn test_rfc0002_empty_inline_object() {
        let src = "cfg:\n  - {}\n";
        let doc = decode(src).unwrap();
        let cfg = dict_get(&doc, "cfg").unwrap().as_list().unwrap();
        assert_eq!(cfg[0], Value::Dict(vec![]));
    }

    #[test]
    fn test_rfc0002_inline_object_in_nested_block() {
        let src = "data:\n  items:\n    - {id: 1, name: alice}\n    - {id: 2, name: bob}\n";
        let doc = decode(src).unwrap();
        let data = dict_get(&doc, "data").unwrap();
        let items = dict_get(data, "items").unwrap().as_list().unwrap();
        assert_eq!(items.len(), 2);
        assert_eq!(dict_get(&items[0], "id"), Some(&Value::String("1".into())));
        assert_eq!(dict_get(&items[0], "name"), Some(&Value::String("alice".into())));
        assert_eq!(dict_get(&items[1], "id"), Some(&Value::String("2".into())));
        assert_eq!(dict_get(&items[1], "name"), Some(&Value::String("bob".into())));
    }

    #[test]
    fn test_rfc0002_duplicate_keys_in_inline_object_throws() {
        let src = "cfg:\n  - {a: 1, a: 2}\n";
        let err = decode(src).unwrap_err();
        assert!(err.message.contains("duplicate key 'a'"), "expected duplicate-key error, got: {}", err.message);
    }

    #[test]
    fn test_rfc0002_malformed_inline_object_throws() {
        let src = "cfg:\n  - {a, b: 2}\n";
        assert!(decode(src).is_err());
    }

    #[test]
    fn test_rfc0002_inline_object_preserves_key_order() {
        let src = "cfg:\n  - {z: 1, a: 2, m: 3}\n";
        let doc = decode(src).unwrap();
        let cfg = dict_get(&doc, "cfg").unwrap().as_list().unwrap();
        let keys: Vec<&str> = cfg[0].as_dict().unwrap().iter().map(|(k, _)| k.as_str()).collect();
        assert_eq!(keys, vec!["z", "a", "m"]);
    }

    #[test]
    fn test_rfc0002_nested_compact_array_in_inline_object() {
        let src = "cfg:\n  - {tags: !s[a, b, c], port: !n 80}\n";
        let doc = decode(src).unwrap();
        let cfg = dict_get(&doc, "cfg").unwrap().as_list().unwrap();
        let tags = dict_get(&cfg[0], "tags").unwrap().as_list().unwrap();
        assert_eq!(tags.len(), 3);
        assert_eq!(tags[0], Value::String("a".into()));
        assert_eq!(dict_get(&cfg[0], "port"), Some(&Value::Int(80)));
    }

    #[test]
    fn test_rfc0002_traditional_block_list_regression() {
        let src = "items:\n  - one\n  - two\n";
        let doc = decode(src).unwrap();
        let items = dict_get(&doc, "items").unwrap().as_list().unwrap();
        assert_eq!(items, &vec![Value::String("one".into()), Value::String("two".into())]);
    }

    #[test]
    fn test_rfc0002_traditional_compact_array_regression() {
        let src = "ports: !n[80, 443, 8080]\n";
        let doc = decode(src).unwrap();
        let ports = dict_get(&doc, "ports").unwrap().as_list().unwrap();
        assert_eq!(ports.len(), 3);
        assert_eq!(ports[0], Value::Int(80));
        assert_eq!(ports[1], Value::Int(443));
        assert_eq!(ports[2], Value::Int(8080));
    }

    #[test]
    fn test_rfc0002_end_as_inline_object_value() {
        // 'end' as a key inside { ... } must not be confused with the block-close keyword
        let src = "cfg:\n  - {end: foo, value: 1}\n";
        let doc = decode(src).unwrap();
        let cfg = dict_get(&doc, "cfg").unwrap().as_list().unwrap();
        assert_eq!(dict_get(&cfg[0], "end"), Some(&Value::String("foo".into())));
        assert_eq!(dict_get(&cfg[0], "value"), Some(&Value::String("1".into())));
    }

    #[test]
    fn test_rfc0002_inline_object_with_complex_tagged_values() {
        let src = "cfg:\n  - {name: !s hello, uid: !i 42, ratio: !f 3.14, kind: !o 0644, addr: !ip 10.0.0.1}\n";
        let doc = decode(src).unwrap();
        let cfg = dict_get(&doc, "cfg").unwrap().as_list().unwrap();
        assert_eq!(dict_get(&cfg[0], "name"), Some(&Value::String("hello".into())));
        assert_eq!(dict_get(&cfg[0], "uid"), Some(&Value::Int(42)));
        assert_eq!(dict_get(&cfg[0], "ratio"), Some(&Value::Float(3.14)));
        assert_eq!(
            dict_get(&cfg[0], "kind"),
            Some(&Value::Tagged(crate::Tagged { tag: "o".into(), value: "0644".into() }))
        );
        // !ip is parsed as a string body in the current pipeline
        let addr = dict_get(&cfg[0], "addr").unwrap();
        assert!(matches!(addr, Value::Tagged(_)));
    }

    #[test]
    fn inline_dict_entries_in_lists() {
        assert_eq!(
            decode("a:\n  - x: 1\n  - y: 2").unwrap(),
            dict_of(&[(
                "a",
                Value::List(vec![
                    dict_of(&[("x", Value::String("1".into()))]),
                    dict_of(&[("y", Value::String("2".into()))])
                ])
            )])
        );
        assert_eq!(
            decode("a:\n  - x: 1\n    y: 2\n  - z: 3").unwrap(),
            dict_of(&[(
                "a",
                Value::List(vec![
                    dict_of(&[
                        ("x", Value::String("1".into())),
                        ("y", Value::String("2".into()))
                    ]),
                    dict_of(&[("z", Value::String("3".into()))])
                ])
            )])
        );
        assert_eq!(
            decode("a:\n  - x: 1\n  - simple").unwrap(),
            dict_of(&[(
                "a",
                Value::List(vec![
                    dict_of(&[("x", Value::String("1".into()))]),
                    Value::String("simple".into())
                ])
            )])
        );
        assert!(decode("a: !n[]\n  - x: 1\n").is_err());
    }

    #[test]
    fn multiline_chomp_indicators() {
        assert_eq!(decode("a: |\n  x\n|").unwrap().as_dict().unwrap()[0].1, Value::String("x".into()));
        assert_eq!(decode("a: |-\n  x\n\n|").unwrap().as_dict().unwrap()[0].1, Value::String("x".into()));
        assert_eq!(
            decode("a: |-\n  x\n  y\n|").unwrap().as_dict().unwrap()[0].1,
            Value::String("x\ny".into())
        );
        assert_eq!(
            decode("a: |+\n  x\n|").unwrap().as_dict().unwrap()[0].1,
            Value::String("x\n".into())
        );
        assert_eq!(
            decode("a: |\n  x\n\n|").unwrap().as_dict().unwrap()[0].1,
            Value::String("x\n".into())
        );
        assert_eq!(
            decode("a: |MD-\n  x\n\nMD").unwrap().as_dict().unwrap()[0].1,
            Value::String("x".into())
        );
        assert_eq!(
            decode("t: |\n  a # not comment\n| # comment").unwrap().as_dict().unwrap()[0].1,
            Value::String("a # not comment".into())
        );
    }

    #[test]
    fn strings_that_reparse_as_structure_are_quoted_or_tagged() {
        assert_eq!(
            encode(&dict_a(Value::String("hello: world".into()))).unwrap(),
            "a: \"hello: world\"\n"
        );
        let enc = encode(&dict_a(Value::String("hello: world".into()))).unwrap();
        assert_eq!(parse(&enc).unwrap(), dict_a(Value::String("hello: world".into())));
        let items = Value::Dict(vec![(
            "items".into(),
            Value::List(vec![Value::String("a: b".into())]),
        )]);
        assert_eq!(encode(&items).unwrap(), "items:\n  - \"a: b\"\n");
        assert_eq!(parse("items:\n  - \"a: b\"\n").unwrap(), items);
    }
}
