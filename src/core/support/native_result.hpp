#ifndef GHOSTLOCK_NATIVE_RESULT_HPP
#define GHOSTLOCK_NATIVE_RESULT_HPP

#include <cerrno>
#include <system_error>
#include <utility>
#include <variant>

namespace ghostlock {

struct SysError final {
  std::error_code code;

  [[nodiscard]] static SysError from_errno(int value = errno) noexcept {
    return {{value, std::generic_category()}};
  }
};

template <typename T, typename E = SysError>
class Result final {
 public:
  [[nodiscard]] static Result success(T value) {
    return Result(std::in_place_index<0>, std::move(value));
  }

  [[nodiscard]] static Result failure(E error) {
    return Result(std::in_place_index<1>, std::move(error));
  }

  [[nodiscard]] bool has_value() const noexcept { return value_.index() == 0; }
  explicit operator bool() const noexcept { return has_value(); }
  [[nodiscard]] T &value() & { return std::get<0>(value_); }
  [[nodiscard]] const T &value() const & { return std::get<0>(value_); }
  [[nodiscard]] T &&value() && { return std::get<0>(std::move(value_)); }
  [[nodiscard]] E &error() & { return std::get<1>(value_); }
  [[nodiscard]] const E &error() const & { return std::get<1>(value_); }

 private:
  template <std::size_t Index, typename Value>
  explicit Result(std::in_place_index_t<Index> tag, Value &&value)
      : value_(tag, std::forward<Value>(value)) {}

  std::variant<T, E> value_;
};

}  // namespace ghostlock

#endif
