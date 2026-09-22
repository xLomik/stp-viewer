// Minimal ISO 10303-21 (STEP physical file, "part 21") reader.
// Parses the DATA section into entity instances without interpreting them.
#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace stp {

struct Value {
    enum class Kind { Null, Star, Number, String, Enum, Ref, List };

    Kind kind = Kind::Null;
    double num = 0.0;
    std::string text;  // string contents or enum name (without dots)
    int ref = 0;       // entity id for Kind::Ref
    std::vector<Value> items;

    bool isRef() const { return kind == Kind::Ref; }
    bool isNum() const { return kind == Kind::Number; }
    bool isList() const { return kind == Kind::List; }
    bool boolValue() const { return text == "T"; }
};

// One instance line. Complex instances (#1=(A(..)B(..))) keep every part.
struct Entity {
    int id = 0;
    std::string type;  // first part's type, upper case
    std::vector<std::pair<std::string, std::vector<Value>>> parts;

    const std::vector<Value>* part(const char* name) const;
    const std::vector<Value>& params() const;  // first part's parameters
    bool is(const char* name) const;
};

class StepFile {
public:
    bool parse(const char* data, std::size_t len, std::string* error);
    bool parseFile(const std::string& path, std::string* error);

    const Entity* get(int id) const;
    const Entity* get(const Value& v) const;  // null unless v is a resolvable ref
    std::vector<const Entity*> byType(const char* type) const;

    const std::unordered_map<int, Entity>& entities() const { return m_entities; }
    const std::string& schema() const { return m_schema; }
    const std::string& name() const { return m_name; }

private:
    std::unordered_map<int, Entity> m_entities;
    std::string m_schema;
    std::string m_name;
};

}  // namespace stp
