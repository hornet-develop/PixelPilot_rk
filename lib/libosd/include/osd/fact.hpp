#ifndef OSD_FACT_HPP
#define OSD_FACT_HPP

#include <map>
#include <string>
#include <sys/types.h>
#include <utility>
#include <variant>

using FactTags = std::map<std::string, std::string>;

class FactMatcher {
  public:
    explicit FactMatcher(std::string name, FactTags tags = {}) : name(std::move(name)), tags(std::move(tags)) {}

    std::string name;
    FactTags tags;
};

class FactMeta {
  public:
    FactMeta() = default;
    explicit FactMeta(std::string name, FactTags tags = {}) : name(std::move(name)), tags(std::move(tags)) {}

    std::string getName() const {
        return name;
    }

    FactTags getTags() const {
        return tags;
    }

    bool match(const FactMatcher &matcher) const;

  private:
    std::string name;
    FactTags tags;
};

class Fact {
  public:
    enum Type { T_UNDEF, T_BOOL, T_INT, T_UINT, T_DOUBLE, T_STRING };

    Fact() = default;
    explicit Fact(FactMeta meta) : meta_(std::move(meta)) {}
    Fact(FactMeta meta, bool value) : meta_(std::move(meta)), value_(value) {}
    Fact(FactMeta meta, long value) : meta_(std::move(meta)), value_(value) {}
    Fact(FactMeta meta, ulong value) : meta_(std::move(meta)), value_(value) {}
    Fact(FactMeta meta, double value) : meta_(std::move(meta)), value_(value) {}
    Fact(FactMeta meta, std::string value) : meta_(std::move(meta)), value_(std::move(value)) {}

    Fact(FactMeta meta, const char *val) = delete;

    bool isDefined() const {
        return !std::holds_alternative<std::monostate>(value_);
    }

    bool getBoolValue() const;
    long getIntValue() const;
    ulong getUintValue() const;
    double getDoubleValue() const;
    std::string getStrValue() const;

    bool matches(const FactMatcher &matcher) const;

    Type getType() const {
        if (value_.valueless_by_exception())
            return T_UNDEF;
        return static_cast<Type>(value_.index());
    }

    std::string getName() const {
        return meta_.getName();
    }

    FactTags getTags() const {
        return meta_.getTags();
    }

    std::string getTypeName() const;
    std::string asString() const;

  private:
    // Keep alternatives in the same order as Type.
    using Value = std::variant<std::monostate, bool, long, ulong, double, std::string>;

    bool checkType(Type expected) const;

    FactMeta meta_;
    Value value_;
};

#endif
