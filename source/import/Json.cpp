// =============================================================================
//  Abrasion Labs // OMNIS
//  import/Json.cpp
// =============================================================================
#include "Json.h"

#include <cctype>
#include <cstdlib>
#include <string_view>

namespace omnis::import {

namespace {

class Reader {
public:
    explicit Reader(const std::string& t) : t_(t) {}
    bool value(Json& out, int depth = 0) {
        if (depth > 64) return false;
        space();
        if (i_ >= t_.size()) return false;
        const char c = t_[i_];
        if (c == '{') return object(out, depth);
        if (c == '[') return array(out, depth);
        if (c == '"') {
            out.type = Json::Type::String;
            return string(out.string);
        }
        if (literal("true")) return out.type = Json::Type::Bool, out.boolean = true, true;
        if (literal("false")) return out.type = Json::Type::Bool, out.boolean = false, true;
        if (literal("null")) return out.type = Json::Type::Null, true;
        return number(out);
    }
    bool end() {
        space();
        return i_ == t_.size();
    }

private:
    void space() {
        while (i_ < t_.size() && std::isspace(static_cast<unsigned char>(t_[i_]))) ++i_;
    }
    bool literal(const char* word) {
        const std::string w(word);
        if (t_.compare(i_, w.size(), w) != 0) return false;
        i_ += w.size();
        return true;
    }
    bool number(Json& out) {
        const std::size_t start = i_;
        while (i_ < t_.size() && (std::isdigit(static_cast<unsigned char>(t_[i_])) || std::string_view("+-.eE").find(t_[i_]) != std::string_view::npos))
            ++i_;
        if (i_ == start) return false;
        out.type = Json::Type::Number;
        out.number = std::strtod(t_.substr(start, i_ - start).c_str(), nullptr);
        return true;
    }
    static void utf8(std::string& s, unsigned cp) {
        if (cp < 0x80) s += static_cast<char>(cp);
        else if (cp < 0x800) s += static_cast<char>(0xC0 | (cp >> 6)), s += static_cast<char>(0x80 | (cp & 0x3F));
        else if (cp < 0x10000)
            s += static_cast<char>(0xE0 | (cp >> 12)), s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)), s += static_cast<char>(0x80 | (cp & 0x3F));
        else
            s += static_cast<char>(0xF0 | (cp >> 18)), s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)),
                s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)), s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    bool hex4(unsigned& cp) {
        if (i_ + 4 > t_.size()) return false;
        cp = 0;
        for (int k = 0; k < 4; ++k) {
            const char h = t_[i_++];
            cp <<= 4;
            if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string& out) {
        ++i_; // "
        while (i_ < t_.size()) {
            const char c = t_[i_++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i_ >= t_.size()) return false;
            const char e = t_[i_++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                unsigned cp = 0;
                if (!hex4(cp)) return false;
                // Paire de substitution (emoji, etc.).
                if (cp >= 0xD800 && cp < 0xDC00 && t_.compare(i_, 2, "\\u") == 0) {
                    i_ += 2;
                    unsigned low = 0;
                    if (!hex4(low)) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                }
                utf8(out, cp);
                break;
            }
            default: out += e; // \" \\ \/
            }
        }
        return false;
    }
    bool array(Json& out, int depth) {
        out.type = Json::Type::Array;
        ++i_;
        space();
        if (i_ < t_.size() && t_[i_] == ']') return ++i_, true;
        while (true) {
            Json v;
            if (!value(v, depth + 1)) return false;
            out.array.push_back(std::move(v));
            space();
            if (i_ >= t_.size()) return false;
            if (t_[i_] == ',') ++i_;
            else if (t_[i_] == ']') return ++i_, true;
            else return false;
        }
    }
    bool object(Json& out, int depth) {
        out.type = Json::Type::Object;
        ++i_;
        space();
        if (i_ < t_.size() && t_[i_] == '}') return ++i_, true;
        while (true) {
            space();
            std::string key;
            if (i_ >= t_.size() || t_[i_] != '"' || !string(key)) return false;
            space();
            if (i_ >= t_.size() || t_[i_++] != ':') return false;
            Json v;
            if (!value(v, depth + 1)) return false;
            out.object[key] = std::move(v);
            space();
            if (i_ >= t_.size()) return false;
            if (t_[i_] == ',') ++i_;
            else if (t_[i_] == '}') return ++i_, true;
            else return false;
        }
    }
    const std::string& t_;
    std::size_t i_ = 0;
};

const Json& nothing() {
    static const Json n;
    return n;
}

} // namespace

const Json& Json::operator[](const std::string& key) const {
    if (type != Type::Object) return nothing();
    const auto it = object.find(key);
    return it == object.end() ? nothing() : it->second;
}

bool parseJson(const std::string& text, Json& out) {
    Reader r(text);
    out = Json{};
    return r.value(out) && r.end();
}

} // namespace omnis::import
