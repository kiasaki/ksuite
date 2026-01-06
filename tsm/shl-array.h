#ifndef SHL_ARRAY_H
#define SHL_ARRAY_H

#include <stdlib.h>
#include <string.h>

struct shl_array {
  size_t element_size;
  size_t length;
  size_t size;
  void *data;
};

#define SHL_ARRAY_AT(_arr, _type, _pos) (&((_type*)((_arr)->data))[(_pos)])

static inline int shl_array_new(struct shl_array **out, size_t element_size, size_t initial_size) {
  struct shl_array *arr = malloc(sizeof(*arr));
  if (!arr) return -1;
  arr->element_size = element_size;
  arr->length = 0;
  arr->size = initial_size > 0 ? initial_size : 4;
  arr->data = malloc(arr->element_size * arr->size);
  if (!arr->data) {
    free(arr);
    return -1;
  }
  *out = arr;
  return 0;
}

static inline void shl_array_free(struct shl_array *arr) {
  if (!arr) return;
  free(arr->data);
  free(arr);
}

static inline int shl_array_push(struct shl_array *arr, const void *data) {
  if (arr->length >= arr->size) {
    size_t newsize = arr->size * 2;
    void *newdata = realloc(arr->data, arr->element_size * newsize);
    if (!newdata) return -1;
    arr->data = newdata;
    arr->size = newsize;
  }
  memcpy((char*)arr->data + arr->length * arr->element_size, data, arr->element_size);
  arr->length++;
  return 0;
}

static inline size_t shl_array_get_length(struct shl_array *arr) {
  return arr->length;
}

static inline void *shl_array_get_array(struct shl_array *arr) {
  return arr->data;
}

static inline void *shl_array_get_barray(struct shl_array *arr, size_t *size) {
  if (size) *size = arr->length;
  return arr->data;
}

#endif /* SHL_ARRAY_H */
