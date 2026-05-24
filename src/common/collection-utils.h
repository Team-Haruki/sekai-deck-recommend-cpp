#ifndef COLLECTION_UTILS_H
#define COLLECTION_UTILS_H

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <optional>
#include <functional>
#include <iostream>
#include <type_traits>

#include <yyjson.h>

#include "common/enum-maps.h"
#include "common/common-enums.h"

using TS = long long;

class json_view;

class json_array_iterator {
public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = json_view;
    using difference_type = std::ptrdiff_t;

    json_array_iterator();
    json_array_iterator(yyjson_val* node, size_t index, size_t size);

    json_view operator*() const;
    json_array_iterator& operator++();
    bool operator==(const json_array_iterator& other) const;
    bool operator!=(const json_array_iterator& other) const;

private:
    yyjson_val* node = nullptr;
    size_t index = 0;
    size_t size = 0;
};

class json_view {
public:
    json_view() = default;
    explicit json_view(yyjson_val* node) : node(node) {}

    static json_view array() { return json_view(); }

    bool contains(const char* key) const { return node != nullptr && yyjson_obj_get(node, key) != nullptr; }
    bool contains(const std::string& key) const { return contains(key.c_str()); }
    bool is_null() const { return node == nullptr || yyjson_is_null(node); }
    bool is_array() const { return yyjson_is_arr(node); }
    bool is_object() const { return yyjson_is_obj(node); }
    bool is_string() const { return yyjson_is_str(node); }
    bool is_number_integer() const { return yyjson_is_int(node); }

    size_t size() const {
        if (yyjson_is_arr(node)) return yyjson_arr_size(node);
        if (yyjson_is_obj(node)) return yyjson_obj_size(node);
        return 0;
    }

    json_view operator[](const char* key) const { return node ? json_view(yyjson_obj_get(node, key)) : json_view(); }
    json_view operator[](const std::string& key) const { return (*this)[key.c_str()]; }
    json_view operator[](size_t index) const { return json_view(yyjson_arr_get(node, index)); }
    json_view operator[](int index) const { return (*this)[static_cast<size_t>(index)]; }
    json_view at(const char* key) const {
        yyjson_val* child = node ? yyjson_obj_get(node, key) : nullptr;
        if (!child) throw std::out_of_range(std::string("json key not found: ") + key);
        return json_view(child);
    }
    json_view at(const std::string& key) const { return at(key.c_str()); }

    json_array_iterator begin() const;
    json_array_iterator end() const;

    template <typename T>
    T get() const {
        if constexpr (std::is_same_v<T, std::string>) {
            const char* s = yyjson_get_str(node);
            return s ? std::string(s) : std::string();
        } else if constexpr (std::is_same_v<T, bool>) {
            return yyjson_get_bool(node);
        } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
            return static_cast<T>(yyjson_get_sint(node));
        } else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T>) {
            return static_cast<T>(yyjson_get_uint(node));
        } else if constexpr (std::is_floating_point_v<T>) {
            return static_cast<T>(yyjson_get_num(node));
        } else if constexpr (is_vector<T>::value) {
            using Item = typename T::value_type;
            T result;
            if (is_array()) {
                result.reserve(size());
                for (const auto& item : *this) {
                    result.push_back(item.template get<Item>());
                }
            }
            return result;
        } else {
            static_assert(always_false<T>::value, "Unsupported json_view::get<T>() type");
        }
    }

    template <typename T>
    T value(const char* key, const T& defaultValue) const {
        yyjson_val* child = node ? yyjson_obj_get(node, key) : nullptr;
        if (!child || yyjson_is_null(child)) return defaultValue;
        if constexpr (std::is_same_v<T, json_view>) {
            return json_view(child);
        } else {
            return json_view(child).get<T>();
        }
    }

    std::string value(const char* key, const char* defaultValue) const {
        yyjson_val* child = node ? yyjson_obj_get(node, key) : nullptr;
        if (!child || yyjson_is_null(child)) return defaultValue ? std::string(defaultValue) : std::string();
        return json_view(child).get<std::string>();
    }

    template <typename T>
    T value(const std::string& key, const T& defaultValue) const {
        return value(key.c_str(), defaultValue);
    }

    operator int() const { return get<int>(); }
    operator long long() const { return get<long long>(); }
    operator double() const { return get<double>(); }
    operator std::string() const { return get<std::string>(); }

    const yyjson_val* raw() const { return node; }
    yyjson_val* raw() { return node; }

private:
    template <typename T>
    struct always_false : std::false_type {};

    template <typename T>
    struct is_vector : std::false_type {};

    template <typename T, typename Alloc>
    struct is_vector<std::vector<T, Alloc>> : std::true_type {};

    yyjson_val* node = nullptr;
};

inline json_array_iterator::json_array_iterator() = default;

inline json_array_iterator::json_array_iterator(yyjson_val* node, size_t index, size_t size)
    : node(node), index(index), size(size) {}

inline json_view json_array_iterator::operator*() const {
    return json_view(node);
}

inline json_array_iterator& json_array_iterator::operator++() {
    if (index < size) {
        ++index;
        node = index < size ? unsafe_yyjson_get_next(node) : nullptr;
    }
    return *this;
}

inline bool json_array_iterator::operator==(const json_array_iterator& other) const {
    return node == other.node;
}

inline bool json_array_iterator::operator!=(const json_array_iterator& other) const {
    return !(*this == other);
}

inline json_array_iterator json_view::begin() const {
    if (!is_array() || size() == 0) return {};
    return json_array_iterator(yyjson_arr_get_first(node), 0, size());
}

inline json_array_iterator json_view::end() const {
    return {};
}

class json_doc {
public:
    json_doc() = default;
    explicit json_doc(yyjson_doc* doc) : doc(doc) {}
    json_doc(const json_doc&) = delete;
    json_doc& operator=(const json_doc&) = delete;
    json_doc(json_doc&& other) noexcept : doc(other.doc) { other.doc = nullptr; }
    json_doc& operator=(json_doc&& other) noexcept {
        if (this != &other) {
            reset();
            doc = other.doc;
            other.doc = nullptr;
        }
        return *this;
    }
    ~json_doc() { reset(); }

    static json_doc parse(const std::string& text, const std::string& source = "json") {
        yyjson_read_err err{};
        std::string padded = text;
        padded.resize(text.size() + YYJSON_PADDING_SIZE);
        yyjson_doc* parsed = yyjson_read_opts(
            padded.data(),
            text.size(),
            YYJSON_READ_NOFLAG,
            nullptr,
            &err
        );
        if (!parsed) {
            throw std::runtime_error(
                "Failed to parse " + source + " at position " + std::to_string(err.pos) +
                ": " + (err.msg ? std::string(err.msg) : std::string("unknown error"))
            );
        }
        return json_doc(parsed);
    }

    json_view root() const { return json_view(yyjson_doc_get_root(doc)); }
    explicit operator bool() const { return doc != nullptr; }

private:
    void reset() {
        if (doc) {
            yyjson_doc_free(doc);
            doc = nullptr;
        }
    }

    yyjson_doc* doc = nullptr;
};

class ElementNoFoundError : public std::runtime_error {
public:
    ElementNoFoundError(const std::string& message) : std::runtime_error(message) {}
};


template <typename T, typename U>
const T& findOrThrow(const std::vector<T>& vec, const U& predicate) {
    auto it = std::find_if(vec.begin(), vec.end(), predicate);
    if (it == vec.end()) {
        throw ElementNoFoundError("Element not found");
    }
    return *it;
}

template <typename T, typename U>
T& findOrThrow(std::vector<T>& vec, const U& predicate) {
    auto it = std::find_if(vec.begin(), vec.end(), predicate);
    if (it == vec.end()) {
        throw ElementNoFoundError("Element not found");
    }
    return *it;
}

template <typename T, typename U>
T& findOrThrow(std::vector<T>& vec, const U& predicate, const std::string& error_msg) {
    auto it = std::find_if(vec.begin(), vec.end(), predicate);
    if (it == vec.end()) {
        throw ElementNoFoundError(error_msg);
    }
    return *it;
}

template <typename T, typename U, typename V>
T& findOrThrow(std::vector<T>& vec, const U& predicate, const V& error_msg_func) {
    auto it = std::find_if(vec.begin(), vec.end(), predicate);
    if (it == vec.end()) {
        throw ElementNoFoundError(error_msg_func());
    }
    return *it;
}


#endif // COLLECTION_UTILS_H
