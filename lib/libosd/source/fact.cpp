#include <cassert>

#include <fact.hpp>
#include <spdlog/spdlog.h>

/**
 * Returns true if names are equal and all match_tags are defined and have equal value
 */
bool FactMeta::match(const FactMatcher &matcher) const {
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

bool Fact::getBoolValue() const {
    return checkType(T_BOOL) ? std::get<bool>(value_) : false;
}

long Fact::getIntValue() const {
    return checkType(T_INT) ? std::get<long>(value_) : 0;
}

ulong Fact::getUintValue() const {
    return checkType(T_UINT) ? std::get<ulong>(value_) : 0;
}

double Fact::getDoubleValue() const {
    return checkType(T_DOUBLE) ? std::get<double>(value_) : 0.0;
}

std::string Fact::getStrValue() const {
    return checkType(T_STRING) ? std::get<std::string>(value_) : std::string{};
}

bool Fact::matches(const FactMatcher &matcher) const {
    return meta_.match(matcher);
}

std::string Fact::asString() const {
    switch (getType()) {
        case T_UNDEF:
            return "(undefined)";
        case T_BOOL:
            return getBoolValue() ? "true" : "false";
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

std::string Fact::getTypeName() const {
    static constexpr const char *names[] = {"UNDEF", "BOOL", "INT", "UINT", "DOUBLE", "STRING"};
    return names[getType()];
}

bool Fact::checkType(Type expected) const {
    if (getType() == expected)
        return true;

    spdlog::error("'{}': unexpected fact type {}", meta_.getName(), getTypeName());
    assert(false && "Fact type mismatch");
    return false;
}