#ifndef FILTER_BLOOM_STRING_HASH
#define FILTER_BLOOM_STRING_HASH

#include <stdlib.h>

typedef size_t (*hash_func)(const char *);

size_t murmur_hash(const char *);
size_t siphash24(const char *);
size_t xxhash(const char *);

static const hash_func hashes[] = {
  murmur_hash,
  siphash24,
  xxhash,
  0
};

#define HASH_ITERATE(str, hvar, block) do {                \
  int _ind;                                                \
  hash_func _func;                                         \
  for (_ind = 0; (_func = hashes[_ind]) != 0; ++_ind) {    \
    hvar = _func(str);                                     \
    block                                                  \
  }                                                        \
} while (0)

#endif
