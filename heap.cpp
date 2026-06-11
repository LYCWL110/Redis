#include "heap.h"

size_t HeapParent(size_t i) { return (i + 1) / 2 - 1; }

size_t HeapLeft(size_t i) { return i * 2 + 1; }

size_t HeapRight(size_t i) { return i * 2 + 2; }

void HeapUp(HeapItem *a, size_t pos) {
  HeapItem t = a[pos];
  while (pos > 0 && a[HeapParent(pos)].val > t.val) {
    a[pos] = a[HeapParent(pos)];
    *a[pos].ref = pos;
    pos = HeapParent(pos);
  }
  a[pos] = t;
  *a[pos].ref = pos;
}

void HeapDown(HeapItem *a, size_t pos, size_t len) {
  HeapItem t = a[pos];
  while (true) {
    size_t l = HeapLeft(pos);
    size_t r = HeapRight(pos);
    size_t min_pos = -1;
    size_t min_val = t.val;
    if (l < len && a[l].val < min_val) {
      min_pos = l;
      min_val = a[l].val;
    }
    if (r < len && a[r].val < min_val) {
      min_pos = r;
    }
    if (min_pos == (size_t)-1) {
      break;
    }
    a[pos] = a[min_pos];
    *a[pos].ref = pos;
    pos = min_pos;
  }
  a[pos] = t;
  *a[pos].ref = pos;
}

void HeapUpdate(HeapItem *a, size_t pos, size_t len) {
  if (pos > 0 && a[HeapParent(pos)].val > a[pos].val) {
    HeapUp(a, pos);
  } else {
    HeapDown(a, pos, len);
  }
}
