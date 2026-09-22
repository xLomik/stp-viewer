#include "step_file.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace stp {
namespace {

inline char upper(char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }

void appendUtf8(std::string& out, unsigned int cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Decodes the \X2\....\X0\ and \X\hh escapes used for non-ASCII text in part 21.
std::string decodeText(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size();) {
        if (raw[i] == '\\' && i + 2 < raw.size() && upper(raw[i + 1]) == 'X' && raw[i + 2] == '2' &&
            i + 3 < raw.size() && raw[i + 3] == '\\') {
            std::size_t j = i + 4;
            while (j + 3 < raw.size()) {
                if (raw[j] == '\\') break;
                unsigned int cp = 0;
                if (std::sscanf(raw.substr(j, 4).c_str(), "%4x", &cp) != 1) break;
                appendUtf8(out, cp);
                j += 4;
            }
            while (j < raw.size() && raw[j] != '\\') ++j;
            if (j + 3 < raw.size() && upper(raw[j + 1]) == 'X' && raw[j + 2] == '0') j += 4;
            i = j;
        } else if (raw[i] == '\\' && i + 3 < raw.size() && upper(raw[i + 1]) == 'X' &&
                   raw[i + 2] == '\\') {
            unsigned int cp = 0;
            if (std::sscanf(raw.substr(i + 3, 2).c_str(), "%2x", &cp) == 1) {
                appendUtf8(out, cp);
                i += 5;
            } else {
                out.push_back(raw[i++]);
            }
        } else {
            out.push_back(raw[i++]);
        }
    }
    return out;
}

class Cursor {
public:
    Cursor(const char* data, std::size_t len) : m_p(data), m_n(len) {}

    bool eof() const { return m_i >= m_n; }
    char peek() const { return m_i < m_n ? m_p[m_i] : '\0'; }
    char next() { return m_i < m_n ? m_p[m_i++] : '\0'; }
    std::size_t pos() const { return m_i; }

    void skipSpace() {
        while (m_i < m_n) {
            const char c = m_p[m_i];
            if (c == '/' && m_i + 1 < m_n && m_p[m_i + 1] == '*') {
                m_i += 2;
                while (m_i + 1 < m_n && !(m_p[m_i] == '*' && m_p[m_i + 1] == '/')) ++m_i;
                m_i = m_i + 2 <= m_n ? m_i + 2 : m_n;
            } else if (static_cast<unsigned char>(c) <= ' ') {
                ++m_i;
            } else {
                break;
            }
        }
    }

    // Moves past the next top-level ';', ignoring ones inside strings.
    void skipStatement() {
        bool inString = false;
        while (m_i < m_n) {
            const char c = m_p[m_i++];
            if (inString) {
                if (c == '\'') {
                    if (m_i < m_n && m_p[m_i] == '\'') ++m_i;
                    else inString = false;
                }
            } else if (c == '\'') {
                inString = true;
            } else if (c == ';') {
                return;
            }
        }
    }

    std::string readKeyword() {
        skipSpace();
        std::string s;
        while (m_i < m_n) {
            const char c = m_p[m_i];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') {
                s.push_back(upper(c));
                ++m_i;
            } else {
                break;
            }
        }
        return s;
    }

    bool readValue(Value* out, int depth) {
        skipSpace();
        if (eof()) return false;
        const char c = peek();
        if (c == '#') {
            ++m_i;
            out->kind = Value::Kind::Ref;
            out->ref = static_cast<int>(std::strtol(m_p + m_i, nullptr, 10));
            while (m_i < m_n && std::isdigit(static_cast<unsigned char>(m_p[m_i]))) ++m_i;
            return true;
        }
        if (c == '$') { ++m_i; out->kind = Value::Kind::Null; return true; }
        if (c == '*') { ++m_i; out->kind = Value::Kind::Star; return true; }
        if (c == '\'') {
            ++m_i;
            std::string raw;
            while (m_i < m_n) {
                const char s = m_p[m_i++];
                if (s == '\'') {
                    if (m_i < m_n && m_p[m_i] == '\'') { raw.push_back('\''); ++m_i; }
                    else break;
                } else {
                    raw.push_back(s);
                }
            }
            out->kind = Value::Kind::String;
            out->text = decodeText(raw);
            return true;
        }
        if (c == '.') {
            ++m_i;
            std::string e;
            while (m_i < m_n && m_p[m_i] != '.') e.push_back(upper(m_p[m_i++]));
            if (m_i < m_n) ++m_i;
            out->kind = Value::Kind::Enum;
            out->text = e;
            return true;
        }
        if (c == '(') {
            ++m_i;
            out->kind = Value::Kind::List;
            if (depth > 24) { skipBalanced(); return true; }
            for (;;) {
                skipSpace();
                if (eof()) return false;
                if (peek() == ')') { ++m_i; return true; }
                if (peek() == ',') { ++m_i; continue; }
                Value item;
                if (!readValue(&item, depth + 1)) return false;
                out->items.push_back(std::move(item));
            }
        }
        if (c == '-' || c == '+' || c == '.' || std::isdigit(static_cast<unsigned char>(c))) {
            char* end = nullptr;
            out->kind = Value::Kind::Number;
            out->num = std::strtod(m_p + m_i, &end);
            m_i = end ? static_cast<std::size_t>(end - m_p) : m_n;
            return true;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            // Inline typed value such as LENGTH_MEASURE(1.0); keep the payload only.
            const std::string name = readKeyword();
            skipSpace();
            if (peek() == '(') {
                Value inner;
                if (!readValue(&inner, depth + 1)) return false;
                if (inner.items.size() == 1) {
                    *out = inner.items[0];
                } else {
                    *out = inner;
                }
                out->text = name;
                return true;
            }
            out->kind = Value::Kind::Enum;
            out->text = name;
            return true;
        }
        ++m_i;  // unknown character: ignore
        return true;
    }

    void skipBalanced() {
        int depth = 1;
        bool inString = false;
        while (m_i < m_n && depth > 0) {
            const char c = m_p[m_i++];
            if (inString) {
                if (c == '\'') {
                    if (m_i < m_n && m_p[m_i] == '\'') ++m_i;
                    else inString = false;
                }
            } else if (c == '\'') {
                inString = true;
            } else if (c == '(') {
                ++depth;
            } else if (c == ')') {
                --depth;
            }
        }
    }

private:
    const char* m_p;
    std::size_t m_n;
    std::size_t m_i = 0;
};

bool readParams(Cursor& cur, std::vector<Value>* params) {
    cur.skipSpace();
    if (cur.peek() != '(') return false;
    Value list;
    if (!cur.readValue(&list, 0)) return false;
    *params = std::move(list.items);
    return true;
}

}  // namespace

const std::vector<Value>* Entity::part(const char* name) const {
    for (const auto& p : parts) {
        if (p.first == name) return &p.second;
    }
    return nullptr;
}

const std::vector<Value>& Entity::params() const {
    static const std::vector<Value> empty;
    return parts.empty() ? empty : parts.front().second;
}

bool Entity::is(const char* name) const {
    for (const auto& p : parts) {
        if (p.first == name) return true;
    }
    return false;
}

bool StepFile::parse(const char* data, std::size_t len, std::string* error) {
    m_entities.clear();
    m_schema.clear();
    m_name.clear();

    if (!data || len == 0) {
        if (error) *error = "archivo vacio";
        return false;
    }
    if (len > 3 && static_cast<unsigned char>(data[0]) == 0xEF) {  // UTF-8 BOM
        data += 3;
        len -= 3;
    }

    Cursor cur(data, len);
    bool inData = false;

    while (!cur.eof()) {
        cur.skipSpace();
        if (cur.eof()) break;

        if (cur.peek() == '#') {
            cur.next();
            std::string digits;
            while (!cur.eof() && std::isdigit(static_cast<unsigned char>(cur.peek()))) {
                digits.push_back(cur.next());
            }
            const int entityId = std::atoi(digits.c_str());
            cur.skipSpace();
            if (cur.peek() != '=') { cur.skipStatement(); continue; }
            cur.next();
            cur.skipSpace();

            Entity ent;
            ent.id = entityId;
            if (cur.peek() == '(') {  // complex instance
                cur.next();
                for (;;) {
                    cur.skipSpace();
                    if (cur.eof()) break;
                    if (cur.peek() == ')') { cur.next(); break; }
                    const std::string type = cur.readKeyword();
                    if (type.empty()) { cur.next(); continue; }
                    std::vector<Value> params;
                    if (!readParams(cur, &params)) break;
                    ent.parts.emplace_back(type, std::move(params));
                }
            } else {
                const std::string type = cur.readKeyword();
                std::vector<Value> params;
                readParams(cur, &params);
                ent.parts.emplace_back(type, std::move(params));
            }
            if (!ent.parts.empty()) {
                ent.type = ent.parts.front().first;
                m_entities.emplace(ent.id, std::move(ent));
            }
            cur.skipStatement();
            continue;
        }

        const std::string kw = cur.readKeyword();
        if (kw.empty()) { cur.next(); continue; }
        if (kw == "DATA") {
            inData = true;
            cur.skipStatement();
        } else if (kw == "ENDSEC" || kw == "HEADER" || kw == "ISO-10303-21") {
            cur.skipStatement();
        } else if (kw == "FILE_SCHEMA" || kw == "FILE_NAME") {
            std::vector<Value> params;
            if (readParams(cur, &params)) {
                if (kw == "FILE_SCHEMA" && !params.empty()) {
                    const Value& v = params[0];
                    if (v.isList() && !v.items.empty()) m_schema = v.items[0].text;
                    else m_schema = v.text;
                } else if (kw == "FILE_NAME" && !params.empty()) {
                    m_name = params[0].text;
                }
            }
            cur.skipStatement();
        } else {
            cur.skipStatement();
        }
        (void)inData;
    }

    if (m_entities.empty()) {
        if (error) *error = "no se encontraron entidades en la seccion DATA";
        return false;
    }
    return true;
}

bool StepFile::parseFile(const std::string& path, std::string* error) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (error) *error = "no se pudo abrir el archivo";
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        std::fclose(f);
        if (error) *error = "archivo vacio";
        return false;
    }
    std::string buffer(static_cast<std::size_t>(size), '\0');
    const std::size_t got = std::fread(&buffer[0], 1, buffer.size(), f);
    std::fclose(f);
    buffer.resize(got);
    return parse(buffer.data(), buffer.size(), error);
}

const Entity* StepFile::get(int id) const {
    const auto it = m_entities.find(id);
    return it == m_entities.end() ? nullptr : &it->second;
}

const Entity* StepFile::get(const Value& v) const {
    return v.isRef() ? get(v.ref) : nullptr;
}

std::vector<const Entity*> StepFile::byType(const char* type) const {
    std::vector<const Entity*> out;
    for (const auto& kv : m_entities) {
        if (kv.second.is(type)) out.push_back(&kv.second);
    }
    return out;
}

}  // namespace stp
