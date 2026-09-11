#ifndef FACT_HPP
#define FACT_HPP

#include <map>
#include <string>
#include <sys/types.h>
#include <variant>

typedef std::map<std::string, std::string> FactTags;

class FactMatcher {
  public:
    FactMatcher(std::string name, FactTags tags) : name(name), tags(tags) {};
    FactMatcher(std::string name) : name(name), tags({}) {};

    std::string name;
    FactTags tags;
};

class FactMeta {
  public:
    FactMeta();
    FactMeta(std::string name);
    FactMeta(std::string name, FactTags tags);

    std::string getName();
    FactTags getTags();

    bool match(FactMatcher matcher);

  private:
    std::string name;
    FactTags tags;
};

class Fact {
  public:
    enum Type { T_UNDEF, T_BOOL, T_INT, T_UINT, T_DOUBLE, T_STRING };

    Fact();
    Fact(FactMeta meta, bool val);
    Fact(FactMeta meta, long val);
    Fact(FactMeta meta, ulong val);
    Fact(FactMeta meta, double val);
    Fact(FactMeta meta, std::string val);
    Fact(FactMeta meta);

    bool isDefined();

    bool getBoolValue();

    long getIntValue();

    ulong getUintValue();

    double getDoubleValue();

    std::string getStrValue();

    bool matches(FactMatcher matcher);

    std::string getTypeName();

    Type getType();

    std::string getName();

    FactTags getTags();

    std::string asString();

  private:
    Type type = T_UNDEF;

    std::string typeName(Type t);
    void assertType(Type t);

    FactMeta meta;
    std::variant<bool, long, ulong, double, std::string> value;
};

#endif
