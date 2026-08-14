from pathlib import Path

import pytest
from xun import Tagged, XunError, parse

ROOT = Path(__file__).resolve().parents[2]


def test_readme_example():
    src = (ROOT / "testdata" / "example.xun").read_text(encoding="utf-8")
    doc = parse(src)
    assert doc["server"]["host"] == "localhost"
    assert doc["server"]["port"] == 8080
    assert doc["server"]["bind"] == Tagged("ip", "::1")
    assert doc["server"]["tls"]["mode"] == 0o755
    assert doc["features"] == ["auth", "cache"]
    assert doc["ports"] == [80, 443, 8080]
    assert doc["endpoint"] == "https://api.example.com/v2/orders"
    assert doc["tz"] == Tagged("tz", "Asia/Shanghai")
    assert doc["py"] == Tagged("ver", "3.10")
    assert doc["color"] == bytes.fromhex("ff00aa")
    assert doc["roles"] == ["admin", "ops"]
    assert doc["banner"] == "Welcome\nto XUN"


def test_empty_file():
    assert parse("") == {}
    assert parse("# only\n") == {}


def test_untyped_strings():
    doc = parse("a: 8080\nb: true\nc: 3.10\n")
    assert doc == {"a": "8080", "b": "true", "c": "3.10"}


def test_duplicate_keys():
    with pytest.raises(XunError):
        parse("a: 1\na: 2\n")


def test_version_not_float():
    assert parse("py: !ver 3.10\n")["py"] == Tagged("ver", "3.10")
