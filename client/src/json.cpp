#include "json.hpp"

#include <cmath>
#include <cstdlib>

namespace ofs::client::json {
namespace {

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Appends the UTF-8 encoding of a code point. glTF JSON is UTF-8, so an escaped
// surrogate pair is recombined before encoding.
void appendUtf8(std::string& out, std::uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else {
    out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  }
}

}  // namespace

const Value& Value::null() {
  static const Value value;
  return value;
}

std::uint32_t Value::u32() const {
  const double n = number();
  if (!(n >= 0.0) || n > 4294967295.0 || n != std::floor(n)) return 0;
  return static_cast<std::uint32_t>(n);
}

const Value& Value::operator[](std::size_t index) const {
  if (!isArray() || index >= elements_.size()) return null();
  return elements_[index];
}

const Value& Value::operator[](std::string_view key) const {
  if (!isObject()) return null();
  // glTF objects are short, so a linear scan keeps lookups allocation-free.
  for (std::size_t i = 0; i < keys_.size(); ++i)
    if (keys_[i] == key) return elements_[i];
  return null();
}

bool Value::has(std::string_view key) const {
  if (!isObject()) return false;
  for (const auto& k : keys_)
    if (k == key) return true;
  return false;
}

void Value::numbers(float* out, std::size_t max) const {
  if (!isArray()) return;
  const std::size_t n = elements_.size() < max ? elements_.size() : max;
  for (std::size_t i = 0; i < n; ++i) out[i] = elements_[i].numberf();
}

std::vector<float> Value::numbers() const {
  if (!isArray()) return {};
  std::vector<float> result(elements_.size());
  for (std::size_t i = 0; i < elements_.size(); ++i) result[i] = elements_[i].numberf();
  return result;
}

void Parser::skip() {
  while (pos_ < text_.size()) {
    const char c = text_[pos_];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      ++pos_;
    } else if (static_cast<unsigned char>(c) == 0xef && pos_ + 2 < text_.size() &&
               static_cast<unsigned char>(text_[pos_ + 1]) == 0xbb &&
               static_cast<unsigned char>(text_[pos_ + 2]) == 0xbf) {
      pos_ += 3;  // Tolerate a UTF-8 BOM, which some exporters emit.
    } else {
      break;
    }
  }
}

void Parser::checkEnd() {
  skip();
  if (pos_ != text_.size()) fail("trailing content");
}

void Parser::fail(const char* what) const {
  throw std::runtime_error(std::string("JSON: ") + what + " at offset " +
                           std::to_string(pos_));
}

std::uint32_t Parser::parseHex4() {
  if (pos_ + 4 > text_.size()) fail("short \\u escape");
  std::uint32_t cp = 0;
  for (int i = 0; i < 4; ++i) {
    const int d = hexDigit(text_[pos_ + i]);
    if (d < 0) fail("bad \\u escape");
    cp = (cp << 4) | static_cast<std::uint32_t>(d);
  }
  pos_ += 4;
  return cp;
}

std::string_view Parser::parseString() {
  if (pos_ >= text_.size() || text_[pos_] != '"') fail("expected string");
  ++pos_;
  const std::size_t start = pos_;
  // Fast path: no escapes, so the result is a direct view of the source.
  while (pos_ < text_.size()) {
    const char c = text_[pos_];
    if (c == '"') {
      const std::string_view result = text_.substr(start, pos_ - start);
      ++pos_;
      return result;
    }
    if (c == '\\') break;
    if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
    ++pos_;
  }
  if (pos_ >= text_.size()) fail("unterminated string");
  // Slow path: at least one escape, so decode into parser-owned storage.
  owned_.emplace_back(text_.substr(start, pos_ - start));
  std::string& out = owned_.back();
  while (pos_ < text_.size()) {
    const char c = text_[pos_];
    if (c == '"') {
      ++pos_;
      break;
    }
    if (c != '\\') {
      out.push_back(c);
      ++pos_;
      continue;
    }
    if (++pos_ >= text_.size()) fail("unterminated escape");
    switch (text_[pos_]) {
      case '"': out.push_back('"'); ++pos_; break;
      case '\\': out.push_back('\\'); ++pos_; break;
      case '/': out.push_back('/'); ++pos_; break;
      case 'b': out.push_back('\b'); ++pos_; break;
      case 'f': out.push_back('\f'); ++pos_; break;
      case 'n': out.push_back('\n'); ++pos_; break;
      case 'r': out.push_back('\r'); ++pos_; break;
      case 't': out.push_back('\t'); ++pos_; break;
      case 'u': {
        ++pos_;
        std::uint32_t cp = parseHex4();
        if (cp >= 0xd800 && cp <= 0xdbff && pos_ + 1 < text_.size() &&
            text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
          const std::size_t save = pos_;
          pos_ += 2;
          const std::uint32_t low = parseHex4();
          if (low >= 0xdc00 && low <= 0xdfff)
            cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
          else
            pos_ = save;
        }
        appendUtf8(out, cp);
        break;
      }
      default: fail("unknown escape");
    }
  }
  return out;
}

Value Parser::parseNumber() {
  const std::size_t start = pos_;
  if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) ++pos_;
  bool digits = false;
  while (pos_ < text_.size()) {
    const char c = text_[pos_];
    if (c >= '0' && c <= '9') {
      digits = true;
      ++pos_;
    } else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
      ++pos_;
    } else {
      break;
    }
  }
  if (!digits) fail("expected number");
  const std::string_view token = text_.substr(start, pos_ - start);
  char* end = nullptr;
  const std::string copy(token);
  const double parsed = std::strtod(copy.c_str(), &end);
  if (end != copy.c_str() + copy.size()) fail("malformed number");
  if (!std::isfinite(parsed)) fail("non-finite number");
  Value value;
  value.type_ = Type::Number;
  value.number_ = parsed;
  return value;
}

Value Parser::parseLiteral() {
  Value value;
  if (text_.compare(pos_, 4, "true") == 0) {
    pos_ += 4;
    value.type_ = Type::Bool;
    value.bool_ = true;
  } else if (text_.compare(pos_, 5, "false") == 0) {
    pos_ += 5;
    value.type_ = Type::Bool;
    value.bool_ = false;
  } else if (text_.compare(pos_, 4, "null") == 0) {
    pos_ += 4;
    value.type_ = Type::Null;
  } else {
    fail("expected value");
  }
  return value;
}

Value Parser::parseObject() {
  ++pos_;  // '{'
  Value value;
  value.type_ = Type::Object;
  skip();
  if (pos_ < text_.size() && text_[pos_] == '}') {
    ++pos_;
    return value;
  }
  while (true) {
    skip();
    const std::string_view key = parseString();
    skip();
    if (pos_ >= text_.size() || text_[pos_] != ':') fail("expected ':'");
    ++pos_;
    value.keys_.emplace_back(key);
    value.elements_.push_back(parseValue());
    skip();
    if (pos_ >= text_.size()) fail("unterminated object");
    if (text_[pos_] == ',') { ++pos_; continue; }
    if (text_[pos_] == '}') { ++pos_; break; }
    fail("expected ',' or '}'");
  }
  return value;
}

Value Parser::parseArray() {
  ++pos_;  // '['
  Value value;
  value.type_ = Type::Array;
  skip();
  if (pos_ < text_.size() && text_[pos_] == ']') {
    ++pos_;
    return value;
  }
  while (true) {
    value.elements_.push_back(parseValue());
    skip();
    if (pos_ >= text_.size()) fail("unterminated array");
    if (text_[pos_] == ',') { ++pos_; continue; }
    if (text_[pos_] == ']') { ++pos_; break; }
    fail("expected ',' or ']'");
  }
  return value;
}

Value Parser::parseValue() {
  skip();
  if (pos_ >= text_.size()) fail("unexpected end of input");
  switch (text_[pos_]) {
    case '{': return parseObject();
    case '[': return parseArray();
    case '"': {
      Value value;
      value.type_ = Type::String;
      value.string_ = parseString();
      return value;
    }
    case 't':
    case 'f':
    case 'n': return parseLiteral();
    default: return parseNumber();
  }
}

}  // namespace ofs::client::json
