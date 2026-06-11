#ifndef REDIS_HEAP_H_
#define REDIS_HEAP_H_

#include <cstddef>
#include <cstdint>

struct HeapItem {
  uint64_t val = 0;
  size_t *ref = nullptr;
};

size_t HeapParent(size_t i);
size_t HeapLeft(size_t i);
size_t HeapRight(size_t i);
void HeapUp(HeapItem *a, size_t pos);
void HeapDown(HeapItem *a, size_t pos, size_t len);
void HeapUpdate(HeapItem *a, size_t pos, size_t len);

#endif  // REDIS_HEAP_H_
