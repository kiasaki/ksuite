#ifndef SHL_HTABLE_H
#define SHL_HTABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Simple hash table implementation compatible with libtsm symbol table usage */

struct shl_htable_entry {
  const void *key;
  size_t hash;
  struct shl_htable_entry *next;
};

struct shl_htable {
  size_t bucket_count;
  struct shl_htable_entry **buckets;
  bool (*compare)(const void *, const void *);
  size_t (*hash_fn)(const void *, void *);
  void *priv;
};

static inline void shl_htable_init(struct shl_htable *ht,
                                   bool (*compare)(const void *, const void *),
                                   size_t (*hash_fn)(const void *, void *),
                                   void *priv) {
  ht->bucket_count = 64;
  ht->buckets = calloc(ht->bucket_count, sizeof(*ht->buckets));
  ht->compare = compare;
  ht->hash_fn = hash_fn;
  ht->priv = priv;
}

static inline void shl_htable_clear(struct shl_htable *ht,
                                    void (*free_cb)(void *, void *),
                                    void *priv) {
  if (!ht->buckets) return;
  for (size_t i = 0; i < ht->bucket_count; i++) {
    struct shl_htable_entry *e = ht->buckets[i];
    while (e) {
      struct shl_htable_entry *next = e->next;
      if (free_cb) free_cb((void*)e->key, priv);
      free(e);
      e = next;
    }
  }
  free(ht->buckets);
  ht->buckets = NULL;
}

static inline int shl_htable_insert(struct shl_htable *ht, const void *key, size_t hash) {
  if (!ht->buckets) return -1;
  size_t idx = hash % ht->bucket_count;
  struct shl_htable_entry *e = malloc(sizeof(*e));
  if (!e) return -1;
  e->key = key;
  e->hash = hash;
  e->next = ht->buckets[idx];
  ht->buckets[idx] = e;
  return 0;
}

static inline bool shl_htable_lookup(struct shl_htable *ht, const void *key, size_t hash, void **out) {
  if (!ht->buckets) return false;
  size_t idx = hash % ht->bucket_count;
  for (struct shl_htable_entry *e = ht->buckets[idx]; e; e = e->next) {
    if (e->hash == hash && ht->compare(e->key, key)) {
      if (out) *out = (void*)e->key;
      return true;
    }
  }
  return false;
}

static inline bool shl_htable_remove(struct shl_htable *ht, const void *key, size_t hash, void **out) {
  if (!ht->buckets) return false;
  size_t idx = hash % ht->bucket_count;
  struct shl_htable_entry **pp = &ht->buckets[idx];
  while (*pp) {
    struct shl_htable_entry *e = *pp;
    if (e->hash == hash && ht->compare(e->key, key)) {
      *pp = e->next;
      if (out) *out = (void*)e->key;
      free(e);
      return true;
    }
    pp = &e->next;
  }
  return false;
}

/* These are kept for API compatibility but not used */
static inline int shl_htable_new(struct shl_htable **out,
                                 bool (*compare)(const void *, const void *),
                                 size_t (*hash_fn)(const void *, void *),
                                 void *priv,
                                 size_t initial_size) {
  (void)initial_size;
  struct shl_htable *ht = calloc(1, sizeof(*ht));
  if (!ht) return -1;
  shl_htable_init(ht, compare, hash_fn, priv);
  *out = ht;
  return 0;
}

static inline void shl_htable_free(struct shl_htable *ht) {
  if (!ht) return;
  shl_htable_clear(ht, NULL, NULL);
  free(ht);
}

#endif /* SHL_HTABLE_H */
