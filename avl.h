#ifndef REDIS_AVL_H_
#define REDIS_AVL_H_

#include <cstddef>
#include <cstdint>

struct AVLNode {
  uint32_t depth = 0;
  uint32_t cnt = 0;
  AVLNode *left = nullptr;
  AVLNode *right = nullptr;
  AVLNode *parent = nullptr;
};

void AvlInit(AVLNode *node);
uint32_t AvlDepth(AVLNode *node);
uint32_t AvlCnt(AVLNode *node);
void AvlUpdate(AVLNode *node);
AVLNode *AvlFix(AVLNode *node);
AVLNode *AvlDel(AVLNode *node);
AVLNode *AvlOffset(AVLNode *node, int64_t offset);

#endif  // REDIS_AVL_H_
