#pragma once

namespace Totem::Generated::Wire {

template <auto MemberPtr> struct Field {
    static constexpr auto member = MemberPtr;
    const char *name;
};

template <typename T> struct FieldList;

} // namespace Totem::Generated::Wire
