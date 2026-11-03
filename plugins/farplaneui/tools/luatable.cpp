/**
 * Farplane UI - minimal data-only Lua table reader (see luatable.hpp).
 */
#include "luatable.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace farplaneui::lua
{
    namespace
    {
        const Value kNil{};

        struct Parser
        {
            const std::string& s;
            size_t p{0};
            std::string err;

            void Skip(void)
            {
                for (;;)
                {
                    while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p])))
                        ++p;
                    if (p + 1 < s.size() && s[p] == '-' && s[p + 1] == '-')
                    {
                        p += 2;
                        if (p + 1 < s.size() && s[p] == '[' && s[p + 1] == '[')
                        {
                            const size_t e = s.find("]]", p + 2);
                            p = e == std::string::npos ? s.size() : e + 2;
                        }
                        else
                            while (p < s.size() && s[p] != '\n')
                                ++p;
                        continue;
                    }
                    break;
                }
            }
            auto Peek(char c) -> bool
            {
                Skip();
                return p < s.size() && s[p] == c;
            }
            auto Eat(const char* tok) -> bool
            {
                Skip();
                const size_t n = std::char_traits<char>::length(tok);
                if (s.compare(p, n, tok) == 0)
                {
                    p += n;
                    return true;
                }
                return false;
            }
            auto Ident(void) -> std::string
            {
                Skip();
                size_t b = p;
                while (p < s.size() && (std::isalnum(static_cast<unsigned char>(s[p])) || s[p] == '_'))
                    ++p;
                return s.substr(b, p - b);
            }
            auto String(void) -> std::string
            {
                std::string out;
                if (s.compare(p, 2, "[[") == 0)
                {
                    const size_t e = s.find("]]", p + 2);
                    out = s.substr(p + 2, (e == std::string::npos ? s.size() : e) - p - 2);
                    p   = e == std::string::npos ? s.size() : e + 2;
                    return out;
                }
                const char q = s[p++];
                while (p < s.size() && s[p] != q)
                {
                    char c = s[p++];
                    if (c == '\\' && p < s.size())
                    {
                        c = s[p++];
                        switch (c)
                        {
                        case 'n': c = '\n'; break;
                        case 't': c = '\t'; break;
                        case 'r': c = '\r'; break;
                        default: break; // \' \" \\ and the rest: the character itself
                        }
                    }
                    out += c;
                }
                ++p; // closing quote
                return out;
            }
            auto Single(void) -> Value
            {
                Skip();
                Value v;
                if (p >= s.size())
                    return v;
                const char c = s[p];
                if (c == '{')
                    return TableValue();
                if (c == '\'' || c == '"' || (c == '[' && p + 1 < s.size() && s[p + 1] == '['))
                {
                    v.type = Value::Type::String;
                    v.str  = String();
                    return v;
                }
                if (std::isdigit(static_cast<unsigned char>(c)) || ((c == '-' || c == '.') && p + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[p + 1]))))
                {
                    char* end = nullptr;
                    v.type    = Value::Type::Number;
                    v.num     = std::strtod(s.c_str() + p, &end);
                    p         = static_cast<size_t>(end - s.c_str());
                    return v;
                }
                const std::string id = Ident();
                if (id == "true" || id == "false")
                {
                    v.type = Value::Type::Bool;
                    v.b    = id == "true";
                }
                else if (id.empty())
                    ++p; // unknown character: skip it
                else if (Peek('('))
                    SkipBalanced('(', ')'); // function call: nil
                return v;
            }
            void SkipBalanced(char open, char close)
            {
                int depth = 0;
                for (; p < s.size(); ++p)
                {
                    if (s[p] == open)
                        ++depth;
                    else if (s[p] == close && --depth == 0)
                    {
                        ++p;
                        return;
                    }
                }
            }
            // value [.. value]*
            auto Expr(void) -> Value
            {
                Value v = Single();
                while (Eat(".."))
                {
                    const Value r = Single();
                    const auto str = [](const Value& x) {
                        if (x.type == Value::Type::Number)
                        {
                            char b[32]{};
                            ::snprintf(b, sizeof(b), "%g", x.num);
                            return std::string(b);
                        }
                        return x.str;
                    };
                    v.str  = str(v) + str(r);
                    v.type = Value::Type::String;
                }
                return v;
            }
            auto TableValue(void) -> Value
            {
                Value v;
                v.type = Value::Type::Table;
                v.tab  = std::make_shared<TableData>();
                ++p; // {
                long long next = 1;
                for (;;)
                {
                    Skip();
                    if (p >= s.size())
                        return v;
                    if (s[p] == '}')
                    {
                        ++p;
                        return v;
                    }
                    if (s[p] == '[' && !(p + 1 < s.size() && s[p + 1] == '['))
                    {
                        ++p;
                        const Value k = Expr();
                        Eat("]");
                        Eat("=");
                        const Value val = Expr();
                        if (k.type == Value::Type::Number)
                            v.tab->indexed[static_cast<long long>(k.num)] = val;
                        else
                            v.tab->named[k.str] = val;
                    }
                    else
                    {
                        const size_t save = p;
                        const std::string id = Ident();
                        if (!id.empty() && id != "true" && id != "false" && Peek('=') && !(p + 1 < s.size() && s[p + 1] == '='))
                        {
                            ++p; // =
                            v.tab->named[id] = Expr();
                        }
                        else
                        {
                            p = save;
                            v.tab->indexed[next++] = Expr();
                        }
                    }
                    Skip();
                    if (p < s.size() && (s[p] == ',' || s[p] == ';'))
                        ++p;
                }
            }
        };
    } // namespace

    auto Value::operator[](const std::string& key) const -> const Value&
    {
        if (!IsTable())
            return kNil;
        const auto it = tab->named.find(key);
        return it != tab->named.end() ? it->second : kNil;
    }
    auto Value::operator[](long long index) const -> const Value&
    {
        if (!IsTable())
            return kNil;
        const auto it = tab->indexed.find(index);
        return it != tab->indexed.end() ? it->second : kNil;
    }
    auto Value::Size(void) const -> size_t
    {
        if (!IsTable())
            return 0;
        size_t n = 0;
        while (tab->indexed.count(static_cast<long long>(n + 1)) != 0)
            ++n;
        return n;
    }

    auto Parse(const std::string& text, std::string* error) -> Value
    {
        Parser ps{text};
        ps.Skip();
        const size_t r = text.find("return", ps.p);
        if (r != std::string::npos)
            ps.p = r + 6;
        Value v = ps.Expr();
        if (error != nullptr)
            *error = v.IsTable() ? std::string() : "no table found";
        return v;
    }
    auto ParseFile(const std::string& path, std::string* error) -> Value
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
        {
            if (error != nullptr)
                *error = "can't open " + path;
            return {};
        }
        std::stringstream ss;
        ss << f.rdbuf();
        return Parse(ss.str(), error);
    }
} // namespace farplaneui::lua
