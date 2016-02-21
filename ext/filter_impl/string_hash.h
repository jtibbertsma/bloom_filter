#ifndef FILTER_BLOOM_STRING_HASH
#define FILTER_BLOOM_STRING_HASH

typedef size_t (*hash_func)(const char *);

size_t hash1(const char *);
size_t hash2(const char *);
size_t hash3(const char *);

static const hash_func hashes[] = {
  hash1,
  hash2,
  hash3,
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
