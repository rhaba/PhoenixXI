/**
 * Farplane UI - minimal reader for data-only Lua files (`return { ... }`), e.g. the questtracker
 * addon's data/quests.lua and overrides. Supports tables ([n] = / name = / positional entries),
 * strings ('..', "..", [[..]]) with `..` concatenation, numbers, booleans and comments. Anything
 * else (function calls, identifiers) reads as nil.
 */
#ifndef FARPLANEUI_LUATABLE_HPP_INCLUDED
#define FARPLANEUI_LUATABLE_HPP_INCLUDED

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace farplaneui::lua
{
    struct Value;
    using Table = std::shared_ptr<struct TableData>;
    struct Value
    {
        enum class Type : unsigned char { Nil, Number, String, Bool, Table } type{Type::Nil};
        double num{0};
        std::string str;
        bool b{false};
        Table tab;

        auto IsTable(void) const -> bool { return type == Type::Table && tab != nullptr; }
        auto Num(double def = 0) const -> double { return type == Type::Number ? num : def; }
        auto Int(int def = 0) const -> int { return type == Type::Number ? static_cast<int>(num) : def; }
        auto Str(const std::string& def = {}) const -> std::string { return type == Type::String ? str : def; }
        auto Bool(bool def = false) const -> bool { return type == Type::Bool ? b : def; }
        // Field / element access (nil value when missing or not a table).
        auto operator[](const std::string& key) const -> const Value&;
        auto operator[](long long index) const -> const Value&;
        auto Size(void) const -> size_t; // array part length (1..n)
    };
    struct TableData
    {
        std::map<std::string, Value> named;
        std::map<long long, Value> indexed;
    };

    // Parses `return <value>` (or a bare value). Empty / nil value on error; `error` says why.
    auto Parse(const std::string& text, std::string* error = nullptr) -> Value;
    auto ParseFile(const std::string& path, std::string* error = nullptr) -> Value;
} // namespace farplaneui::lua

#endif
