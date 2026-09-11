#ifndef OSD_PUBLISH_H
#define OSD_PUBLISH_H

#include <stdbool.h>

#define TAG_MAX_LEN 64

typedef struct {
    char key[TAG_MAX_LEN];
    char val[TAG_MAX_LEN];
} osd_tag;

#ifdef __cplusplus
extern "C" {
#endif
// Batch functions are when you publish several facts from the same place
// It has optimized publishing algorithm - takes the lock only once per-batch
void *osd_batch_init(unsigned int n);
void osd_publish_batch(void *batch);
void osd_add_bool_fact(void *batch, const char *name, const osd_tag *tags, int n_tags, bool value);
void osd_add_int_fact(void *batch, const char *name, const osd_tag *tags, int n_tags, long value);
void osd_add_uint_fact(void *batch, const char *name, const osd_tag *tags, int n_tags, unsigned long value);
void osd_add_double_fact(void *batch, const char *name, const osd_tag *tags, int n_tags, double value);
void osd_add_str_fact(void *batch, const char *name, const osd_tag *tags, int n_tags, const char *value);
void osd_add_clear_fact(void *batch, const char *name, const osd_tag *tags, int n_tags);

// Publish individual facts
void osd_publish_bool_fact(const char *name, const osd_tag *tags, int n_tags, bool value);
void osd_publish_int_fact(const char *name, const osd_tag *tags, int n_tags, long value);
void osd_publish_uint_fact(const char *name, const osd_tag *tags, int n_tags, unsigned long value);
void osd_publish_double_fact(const char *name, const osd_tag *tags, int n_tags, double value);
void osd_publish_str_fact(const char *name, const osd_tag *tags, int n_tags, const char *value);
void osd_clear_fact(const char *name, const osd_tag *tags, int n_tags);
#ifdef __cplusplus
}
#endif

#endif
