#pragma once
// Minimal read-only JSON parser for glTF documents.
//
// Supports the JSON grammar (RFC 8259) that a glTF file can use. Values are
// stored by value and strings are owned by the Parser, so a Value stays valid
// for as long as the Parser that produced it.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ofs::client::json {

class Value;
using Array = std::vector<Value>;

enum class Type : std::uint8_t { Null, Bool, Number, String, Array, Object };

class Value {
 public:
  Value() = default;

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::Null; }
  bool isNumber() const { return type_ == Type::Number; }
  bool isString() const { return type_ == Type::String; }
  bool isArray() const { return type_ == Type::Array; }
  bool isObject() const { return type_ == Type::Object; }
  bool isBool() const { return type_ == Type::Bool; }

  // glTF treats an absent member as its default, so numeric accessors return 0
  // for null or non-numeric values and callers never branch on presence.
  double number() const { return isNumber() ? number_ : 0.0; }
  float numberf() const { return static_cast<float>(number()); }
  int integer() const { return static_cast<int>(number()); }
  std::uint32_t u32() const;
  std::size_t count() const { return elements_.size(); }
  bool boolean(bool fallback = false) const { return isBool() ? bool_ : fallback; }
  std::string_view text() const { return isString() ? string_ : std::string_view{}; }
  const char* cstr() const { return isString() ? string_.data() : ""; }

  // Array element / object member access. Out-of-range or missing lookups
  // return a shared null value rather than throwing, matching glTF defaults.
  const Value& operator[](std::size_t index) const;
  const Value& operator[](std::string_view key) const;
  bool has(std::string_view key) const;

  const Array& items() const { return elements_; }
  // Object members, in document order.
  const std::vector<std::string>& keys() const { return keys_; }

  // Copies up to `max` numbers of an array into `out`.
  void numbers(float* out, std::size_t max) const;
  std::vector<float> numbers() const;

 private:
  friend class Parser;
  static const Value& null();

  Type type_{Type::Null};
  double number_{};
  bool bool_{};
  std::string_view string_{};
  Array elements_{};
  std::vector<std::string> keys_{};
};

class Parser {
 public:
  // The caller must keep `text` alive for the lifetime of the Parser.
  explicit Parser(std::string_view text) : text_(text) {
    value_ = std::make_unique<Value>(parseValue());
    checkEnd();
  }
  const Value& root() const { return *value_; }

 private:
  void skip();
  void checkEnd();
  Value parseValue();
  Value parseObject();
  Value parseArray();
  std::string_view parseString();
  Value parseNumber();
  Value parseLiteral();
  std::uint32_t parseHex4();
  [[noreturn]] void fail(const char* what) const;

  std::string_view text_;
  std::size_t pos_{};
  // Owns decoded escape sequences; views into these stay valid.
  std::deque<std::string> owned_;
  std::unique_ptr<Value> value_;
};

}  // namespace ofs::client::json
