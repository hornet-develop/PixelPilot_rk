#include <osd/fact.hpp>

#include <spdlog/spdlog.h>

FactMeta::FactMeta() : name(""), tags({}) {}

FactMeta::FactMeta(std::string name) : name(name), tags({}) {}

FactMeta::FactMeta(std::string name, FactTags tags) : name(name), tags(tags) {}

std::string FactMeta::getName() {
    return name;
}

FactTags FactMeta::getTags() {
    return tags;
}

/**
 * Returns true if names are equal and all match_tags are defined and have equal value
 */
bool FactMeta::match(FactMatcher matcher) {
    if (matcher.name != name) {
        return false;
    }
    for (const auto &[key, match_value] : matcher.tags) {
        if (auto value = tags.find(key); value != tags.end()) {
            if (value->second != match_value)
                return false;
        } else {
            return false;
        }
    }
    return true;
}

Fact::Fact() : meta(FactMeta("", {})), type(T_UNDEF) {};

Fact::Fact(FactMeta meta, bool val) : meta(meta), value(val), type(T_BOOL) {};

Fact::Fact(FactMeta meta, long val) : meta(meta), value(val), type(T_INT) {};

Fact::Fact(FactMeta meta, ulong val) : meta(meta), value(val), type(T_UINT) {};

Fact::Fact(FactMeta meta, double val) : meta(meta), value(val), type(T_DOUBLE) {};

Fact::Fact(FactMeta meta, std::string val) : meta(meta), value(val), type(T_STRING) {};

Fact::Fact(FactMeta meta) : meta(meta), type(T_UNDEF) {};

bool Fact::isDefined() {
    return type != T_UNDEF;
}

// TODO: try to cast instead of crash
bool Fact::getBoolValue() {
    assertType(T_BOOL);
    return std::get<bool>(value);
}

long Fact::getIntValue() {
    assertType(T_INT);
    return std::get<long>(value);
}

ulong Fact::getUintValue() {
    assertType(T_UINT);
    return std::get<ulong>(value);
}

double Fact::getDoubleValue() {
    assertType(T_DOUBLE);
    return std::get<double>(value);
}

std::string Fact::getStrValue() {
    assertType(T_STRING);
    return std::get<std::string>(value);
}

bool Fact::matches(FactMatcher matcher) {
    return meta.match(matcher);
}

std::string Fact::getTypeName() {
    return typeName(type);
}

Fact::Type Fact::getType() {
    return type;
}

std::string Fact::getName() {
    return meta.getName();
}

FactTags Fact::getTags() {
    return meta.getTags();
}

std::string Fact::asString() {
    switch (type) {
        case T_UNDEF:
            return "(undefined)";
        case T_BOOL:
            if (getBoolValue()) {
                return "true";
            } else {
                return "false";
            };
        case T_INT:
            return std::to_string(getIntValue());
        case T_UINT:
            return std::to_string(getUintValue());
        case T_DOUBLE:
            return std::to_string(getDoubleValue());
        case T_STRING:
            return getStrValue();
    }
    return "(unknown)";
}

std::string Fact::typeName(Type t) {
    switch (t) {
        case T_UNDEF:
            return "UNDEF";
        case T_BOOL:
            return "BOOL";
        case T_INT:
            return "INT";
        case T_UINT:
            return "UINT";
        case T_DOUBLE:
            return "DOUBLE";
        case T_STRING:
            return "STRING";
    }
    return "UNKNOWN";
}

void Fact::assertType(Type t) {
    if (t != type) {
        spdlog::error("'{}': requested type of {}, but the actual type is {}", meta.getName(), typeName(t),
                      typeName(type));
        assert(type == t);
    }
}
