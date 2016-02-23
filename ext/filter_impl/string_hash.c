#include "string_hash.h"
#include "ruby/st.h"
#include "xxhash.h"

/* hash 1: Use the hash function from the ruby hash table library (MurmurHash2) */

#define MURMUR_INIT (st_index_t)0x811c9dc5
size_t
murmur_hash(const char *str)
{
  return (size_t)st_hash(str, strlen(str), MURMUR_INIT);
}

/* hash 2: Use siphash-2-4 with a hardcoded key (see https://github.com/veorq/SipHash) */

int siphash(uint8_t *out, const uint8_t *in, uint64_t inlen, const uint8_t *k);
static uint8_t key[] = {
  0x23, 0xbc, 0x00, 0x5f, 0x1a, 0xc2, 0xac, 0x11,
  0x81, 0xff, 0xd9, 0x20, 0xda, 0x77, 0x8d, 0x3b
};

size_t
siphash24(const char *str)
{
  size_t hash = 0, shift;
  uint8_t out[8];
  int i;

  siphash(out, (uint8_t *)str, strlen(str), key);
  for (i = 0, shift = 0; i < sizeof(size_t); i++, shift += 8) {
    hash |= (size_t)out[i] << shift;
  }

  return hash;
}

/* hash 3: xxhash */

size_t
xxhash(const char *str)
{
  if (sizeof(size_t) == 4) {
    XXH32_hash_t hash;
    unsigned int seed = 0x811c9dc5;

    return (size_t)XXH32(str, strlen(str), seed);
  }
  else {
    XXH64_hash_t hash;
    unsigned long long seed = 0x811c9dc5;

    return (size_t)XXH64(str, strlen(str), seed);
  }
}
