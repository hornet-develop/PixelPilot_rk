#include "osd_publish.h"
#include "osd_service.hpp"

#include <string>
#include <utility>
#include <vector>

#include <osd/fact.hpp>

namespace {

FactTags makeTags(const osd_tag *tags, int n_tags) {
    FactTags fact_tags;
    for (int i = 0; i < n_tags; ++i) {
        fact_tags.emplace(tags[i].key, tags[i].val);
    }
    return fact_tags;
}

std::vector<Fact> *getBatch(void *batch) {
    return static_cast<std::vector<Fact> *>(batch);
}

}

extern "C" {

// -----------------------------------------------------------------------------
// Batch API
// -----------------------------------------------------------------------------

void *osd_batch_init(unsigned int n) {
    auto *batch = new std::vector<Fact>;
    batch->reserve(n);
    return batch;
}

void osd_publish_batch(void *batch) {
    auto *facts = getBatch(batch);
    OsdService::publishFacts(std::move(*facts));
    delete facts;
}

void osd_add_bool_fact(void *batch, const char *name, const osd_tag *tags, int n_tags, bool value) {
    getBatch(batch)->emplace_back(FactMeta(name, makeTags(tags, n_tags)), value);
}

void osd_add_int_fact(void *batch, const char *name, const osd_tag *tags, int n_tags, long value) {
    getBatch(batch)->emplace_back(FactMeta(name, makeTags(tags, n_tags)), value);
}

void osd_add_uint_fact(void *batch, const char *name, const osd_tag *tags, int n_tags, unsigned long value) {
    getBatch(batch)->emplace_back(FactMeta(name, makeTags(tags, n_tags)), value);
}

void osd_add_double_fact(void *batch, const char *name, const osd_tag *tags, int n_tags, double value) {
    getBatch(batch)->emplace_back(FactMeta(name, makeTags(tags, n_tags)), value);
}

void osd_add_str_fact(void *batch, const char *name, const osd_tag *tags, int n_tags, const char *value) {
    getBatch(batch)->emplace_back(FactMeta(name, makeTags(tags, n_tags)), std::string(value));
}

void osd_add_clear_fact(void *batch, const char *name, const osd_tag *tags, int n_tags) {
    getBatch(batch)->emplace_back(
        FactMeta(name, makeTags(tags, n_tags))
    );
}

// -----------------------------------------------------------------------------
// Individual API
// -----------------------------------------------------------------------------

void osd_publish_bool_fact(const char *name, const osd_tag *tags, int n_tags, bool value) {
    OsdService::publishFact(Fact(FactMeta(name, makeTags(tags, n_tags)), value));
}

void osd_publish_int_fact(const char *name, const osd_tag *tags, int n_tags, long value) {
    OsdService::publishFact(Fact(FactMeta(name, makeTags(tags, n_tags)), value));
}

void osd_publish_uint_fact(const char *name, const osd_tag *tags, int n_tags, unsigned long value) {
    OsdService::publishFact(Fact(FactMeta(name, makeTags(tags, n_tags)), value));
}

void osd_publish_double_fact(const char *name, const osd_tag *tags, int n_tags, double value) {
    OsdService::publishFact(Fact(FactMeta(name, makeTags(tags, n_tags)), value));
}

void osd_publish_str_fact(const char *name, const osd_tag *tags, int n_tags, const char *value) {
    OsdService::publishFact(Fact(FactMeta(name, makeTags(tags, n_tags)), std::string(value)));
}

void osd_clear_fact(const char *name, const osd_tag *tags, int n_tags) {
    OsdService::publishFact(Fact(FactMeta(name, makeTags(tags, n_tags))));
}

} // extern "C"
