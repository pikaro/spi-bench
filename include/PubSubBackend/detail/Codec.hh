#pragma once

#include "Macros/Facade.hh"
#include "PubSubBackend/detail/Concepts.hh"
#include "Types/Error.hh"
#include <cstddef>
#include <cstring>
#include <expected>
#include <span>

namespace Totem::PubSubBackend::detail {

template <class T>
    requires IsWireMessage<T>
class Codec {
  public:
    static ReturnCode encode(const T &value, std::span<std::byte> out) {
        if (out.size() < sizeof(T)) {
            return ERR(Overflow);
        }

        std::memcpy(out.data(), &value, sizeof(T));
        return OK();
    }

    static std::expected<T, ReturnCode>
    decode(std::span<const std::byte> bytes) {
        if (bytes.size() != sizeof(T)) {
            return std::unexpected(ERR(InvalidArgument));
        }

        T value{};
        std::memcpy(&value, bytes.data(), sizeof(T));
        return value;
    }

  private:
    using DefaultError = CoreError;
};

} // namespace Totem::PubSubBackend::detail
