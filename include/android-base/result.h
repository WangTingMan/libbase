/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Result<T, E> is the type that is used to pass a success value of type T or an error code of type
// E, optionally together with an error message. T and E can be any type. If E is omitted it
// defaults to int, which is useful when errno(3) is used as the error code.
//
// Passing a success value or an error value:
//
// Result<std::string> readFile() {
//   std::string content;
//   if (base::ReadFileToString("path", &content)) {
//     return content; // ok case
//   } else {
//     return ErrnoError() << "failed to read"; // error case
//   }
// }
//
// Checking the result and then unwrapping the value or propagating the error:
//
// Result<bool> hasAWord() {
//   auto content = readFile();
//   if (!content.ok()) {
//     return Error() << "failed to process: " << content.error();
//   }
//   return (*content.find("happy") != std::string::npos);
// }
//
// Using custom error code type:
//
// enum class MyError { A, B }; // assume that this is the error code you already have
//
// // To use the error code with Result, define a wrapper class that provides the following
// operations and use the wrapper class as the second type parameter (E) when instantiating
// Result<T, E>
//
// 1. default constructor
// 2. copy constructor / and move constructor if copying is expensive
// 3. conversion operator to the error code type
// 4. value() function that return the error code value
// 5. print() function that gives a string representation of the error ode value
//
// struct MyErrorWrapper {
//   MyError val_;
//   MyErrorWrapper() : val_(/* reasonable default value */) {}
//   MyErrorWrapper(MyError&& e) : val_(std:forward<MyError>(e)) {}
//   operator const MyError&() const { return val_; }
//   MyError value() const { return val_; }
//   std::string print() const {
//     switch(val_) {
//       MyError::A: return "A";
//       MyError::B: return "B";
//     }
//   }
// };
//
// #define NewMyError(e) Error<MyErrorWrapper>(MyError::e)
//
// Result<T, MyError> val = NewMyError(A) << "some message";
//
// Formatting the error message using fmtlib:
//
// Errorf("{} errors", num); // equivalent to Error() << num << " errors";
// ErrnoErrorf("{} errors", num); // equivalent to ErrnoError() << num << " errors";
//
// Returning success or failure, but not the value:
//
// Result<void> doSomething() {
//   if (success) return {};
//   else return Error() << "error occurred";
// }
//
// Extracting error code:
//
// Result<T> val = Error(3) << "some error occurred";
// assert(3 == val.error().code());
//

#pragma once

#include <assert.h>
#include <errno.h>

#include <sstream>
#include <string>

#include "android-base/errors.h"
#include "android-base/expected.h"
#include "android-base/format.h"

namespace android {
namespace base {

// Errno is a wrapper class for errno(3). Use this type instead of `int` when instantiating
// `Result<T, E>` and `Error<E>` template classes. This is required to distinguish errno from other
// integer-based error code types like `status_t`.
struct Errno {
  Errno() : val_(0) {}
  Errno(int e) : val_(e) {}
  int value() const { return val_; }
  operator int() const { return value(); }
  std::string print() const { return strerror(value()); }

  int val_;

  // TODO(b/209929099): remove this conversion operator. This currently is needed to not break
  // existing places where error().code() is used to construct enum values.
  template <typename E, typename = std::enable_if_t<std::is_enum_v<E>>>
  operator E() const {
    return E(val_);
  }
};

template <typename E = Errno>
struct ResultError {
  template <typename T, typename P, typename = std::enable_if_t<std::is_convertible_v<P, E>>>
  ResultError(T&& message, P&& code)
      : message_(std::forward<T>(message)), code_(E(std::forward<P>(code))) {}

  template <typename T>
  // NOLINTNEXTLINE(google-explicit-constructor)
  operator android::base::expected<T, ResultError<E>>() const {
    return android::base::unexpected(ResultError<E>(message_, code_));
  }

  std::string message() const { return message_; }
  const E& code() const { return code_; }

 private:
  std::string message_;
  E code_;
};

template <typename E>
inline bool operator==(const ResultError<E>& lhs, const ResultError<E>& rhs) {
  return lhs.message() == rhs.message() && lhs.code() == rhs.code();
}

template <typename E>
inline bool operator!=(const ResultError<E>& lhs, const ResultError<E>& rhs) {
  return !(lhs == rhs);
}

template <typename E>
inline std::ostream& operator<<(std::ostream& os, const ResultError<E>& t) {
  os << t.message();
  return os;
}

template <typename E = Errno, typename = std::enable_if_t<!std::is_same_v<E, int>>>
class Error {
 public:
  Error() : code_(0), has_code_(false) {}
  template <typename P, typename = std::enable_if_t<std::is_convertible_v<P, E>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  Error(P&& code) : code_(std::forward<P>(code)), has_code_(true) {}

  template <typename T, typename P, typename = std::enable_if_t<std::is_convertible_v<E, P>>>
  // NOLINTNEXTLINE(google-explicit-constructor)
  operator android::base::expected<T, ResultError<P>>() const {
    return android::base::unexpected(ResultError<P>(str(), static_cast<P>(code_)));
  }

  template <typename T>
  Error& operator<<(T&& t) {
    // NOLINTNEXTLINE(bugprone-suspicious-semicolon)
    if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, ResultError<E>>) {
      if (!has_code_) {
        code_ = t.code();
      }
      return (*this) << t.message();
    }
    int saved = errno;
    ss_ << t;
    errno = saved;
    return *this;
  }

  const std::string str() const {
    std::string str = ss_.str();
    if (has_code_) {
      if (str.empty()) {
        return code_.print();
      }
      return std::move(str) + ": " + code_.print();
    }
    return str;
  }

  Error(const Error&) = delete;
  Error(Error&&) = delete;
  Error& operator=(const Error&) = delete;
  Error& operator=(Error&&) = delete;

  template <typename T, typename... Args>
  friend Error ErrorfImpl(const T&& fmt, const Args&... args);

  template <typename T, typename... Args>
  friend Error ErrnoErrorfImpl(const T&& fmt, const Args&... args);

 private:
  Error(bool has_code, E code, const std::string& message) : code_(code), has_code_(has_code) {
    (*this) << message;
  }

  std::stringstream ss_;
  E code_;
  const bool has_code_;
};

inline Error<Errno> ErrnoError() {
  return Error<Errno>(Errno{errno});
}

template <typename E>
inline E ErrorCode(E code) {
  return code;
}

// Return the error code of the last ResultError object, if any.
// Otherwise, return `code` as it is.
template <typename T, typename E, typename... Args>
inline E ErrorCode(E code, T&& t, const Args&... args) {
  if constexpr (std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, ResultError<E>>) {
    return ErrorCode(t.code(), args...);
  }
  return ErrorCode(code, args...);
}

template <typename T, typename... Args>
inline Error<Errno> ErrorfImpl(const T&& fmt, const Args&... args) {
  return Error(false, ErrorCode(Errno{}, args...), fmt::format(fmt, args...));
}

template <typename T, typename... Args>
inline Error<Errno> ErrnoErrorfImpl(const T&& fmt, const Args&... args) {
  return Error<Errno>(true, Errno{errno}, fmt::format(fmt, args...));
}

#define Errorf(fmt, ...) android::base::ErrorfImpl(FMT_STRING(fmt), ##__VA_ARGS__)
#define ErrnoErrorf(fmt, ...) android::base::ErrnoErrorfImpl(FMT_STRING(fmt), ##__VA_ARGS__)

template <typename T, typename E = Errno>
using Result = android::base::expected<T, ResultError<E>>;

// Specialization of android::base::OkOrFail<V> for V = Result<T, E>. See android-base/errors.h
// for the contract.
template <typename T, typename E>
struct OkOrFail<Result<T, E>> {
  typedef Result<T, E> V;
  // Checks if V is ok or fail
  static bool IsOk(const V& val) { return val.ok(); }

  // Turns V into a success value
  static T Unwrap(V&& val) { return std::move(val.value()); }

  // Consumes V when it's a fail value
  static OkOrFail<V> Fail(V&& v) {
    assert(!IsOk(v));
    return OkOrFail<V>{std::move(v)};
  }
  V val_;

  // Turns V into S (convertible from E) or Result<U, E>
  template <typename S, typename = std::enable_if_t<std::is_convertible_v<E, S>>>
  operator S() && {
    return val_.error().code();
  }
  template <typename U>
  operator Result<U, E>() && {
    return val_.error();
  }

  static std::string ErrorMessage(const V& val) { return val.error().message(); }
};

// Macros for testing the results of functions that return android::base::Result.
// These also work with base::android::expected.
// For advanced matchers and customized error messages, see result-gtest.h.

#define CHECK_RESULT_OK(stmt)       \
  do {                              \
    const auto& tmp = (stmt);       \
    CHECK(tmp.ok()) << tmp.error(); \
  } while (0)

#define ASSERT_RESULT_OK(stmt)            \
  do {                                    \
    const auto& tmp = (stmt);             \
    ASSERT_TRUE(tmp.ok()) << tmp.error(); \
  } while (0)

#define EXPECT_RESULT_OK(stmt)            \
  do {                                    \
    auto tmp = (stmt);                    \
    EXPECT_TRUE(tmp.ok()) << tmp.error(); \
  } while (0)

}  // namespace base
}  // namespace android
