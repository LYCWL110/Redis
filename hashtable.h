#ifndef REDIS_HASHTABLE_H_
#define REDIS_HASHTABLE_H_

#include <cstddef>
#include <cstdint>
#include <stdlib.h>

#define container_of(ptr, type, member)                          \
  ({                                                             \
    const decltype(((type *)0)->member) *mptr = (ptr);           \
    (type *)((char *)mptr - offsetof(type, member));             \
  })

struct HNode {
  HNode *next = nullptr;
  uint64_t hcode = 0;
};

struct HTab {
  HNode **tab = nullptr;
  size_t mask = 0;
  size_t size = 0;
};

struct HMap {
  HTab ht1;
  HTab ht2;
  size_t resizing_pos = 0;
};

uint64_t StrHash(const uint8_t *data, size_t len);
HNode *HmLookup(HMap *hmap, HNode *key, bool (*cmp)(HNode *, HNode *));
void HmInsert(HMap *hmap, HNode *node);
HNode *HmPop(HMap *hmap, HNode *key, bool (*cmp)(HNode *, HNode *));
size_t HmSize(HMap *hmap);
void HScan(HTab *tab, void (*f)(HNode *, void *), void *arg);

#endif  // REDIS_HASHTABLE_H_
