#ifndef REDIS_LIST_H_
#define REDIS_LIST_H_

#include <cstddef>

struct DList {
  DList *prev = nullptr;
  DList *next = nullptr;
};

inline void DlistInit(DList *node) { node->prev = node->next = node; }

inline bool DlistEmpty(DList *node) { return node->next == node; }

inline void DlistDetach(DList *node) {
  DList *prev = node->prev;
  DList *next = node->next;
  prev->next = next;
  next->prev = prev;
}

inline void DlistInsertBefore(DList *target, DList *rookie) {
  DList *prev = target->prev;
  prev->next = rookie;
  rookie->prev = prev;
  rookie->next = target;
  target->prev = rookie;
}

#endif  // REDIS_LIST_H_
