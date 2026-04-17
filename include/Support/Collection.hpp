#pragma once

#include <functional>
#include <optional>
#include <unordered_map>

template <typename K, typename V, typename H = std::hash<K>>
std::optional<std::reference_wrapper<V>>
find_in_map(std::unordered_map<K, V, H> &map, const K &key) {
    auto it = map.find(key);
    if (it != map.end()) {
        return std::ref(it->second);
    }
    return std::nullopt;
}

template <typename K, typename V, typename H = std::hash<K>>
std::optional<std::reference_wrapper<const V>>
find_in_map(const std::unordered_map<K, V, H> &map, const K &key) {
    auto it = map.find(key);
    if (it != map.end()) {
        return std::cref(it->second);
    }
    return std::nullopt;
}
