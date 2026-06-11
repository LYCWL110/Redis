#include "avl.h"

#include <cstdint>

static uint32_t max(uint32_t lhs, uint32_t rhs) {
  return lhs < rhs ? rhs : lhs;
}

void AvlInit(AVLNode *node) {
  node->depth = 1;
  node->cnt = 1;
  node->left = node->right = node->parent = nullptr;
}

uint32_t AvlDepth(AVLNode *node) { return node ? node->depth : 0; }

uint32_t AvlCnt(AVLNode *node) { return node ? node->cnt : 0; }

void AvlUpdate(AVLNode *node) {
  node->depth = 1 + max(AvlDepth(node->left), AvlDepth(node->right));
  node->cnt = 1 + AvlCnt(node->left) + AvlCnt(node->right);
}

static AVLNode *rot_left(AVLNode *node) {
  AVLNode *new_node = node->right;
  if (new_node->left) {
    new_node->left->parent = node;
  }
  node->right = new_node->left;
  new_node->left = node;
  new_node->parent = node->parent;
  node->parent = new_node;
  AvlUpdate(node);
  AvlUpdate(new_node);
  return new_node;
}

static AVLNode *rot_right(AVLNode *node) {
  AVLNode *new_node = node->left;
  if (new_node->right) {
    new_node->right->parent = node;
  }
  node->left = new_node->right;
  new_node->right = node;
  new_node->parent = node->parent;
  node->parent = new_node;
  AvlUpdate(node);
  AvlUpdate(new_node);
  return new_node;
}

static AVLNode *avl_fix_left(AVLNode *root) {
  if (AvlDepth(root->left->left) < AvlDepth(root->left->right)) {
    root->left = rot_left(root->left);
  }
  return rot_right(root);
}

static AVLNode *avl_fix_right(AVLNode *root) {
  if (AvlDepth(root->right->right) < AvlDepth(root->right->left)) {
    root->right = rot_right(root->right);
  }
  return rot_left(root);
}

AVLNode *AvlFix(AVLNode *node) {
  while (true) {
    AvlUpdate(node);
    uint32_t l = AvlDepth(node->left);
    uint32_t r = AvlDepth(node->right);
    AVLNode **from = nullptr;
    if (node->parent) {
      from = (node->parent->left == node) ? &node->parent->left
                                          : &node->parent->right;
    }
    if (l == r + 2) {
      node = avl_fix_left(node);
    } else if (l + 2 == r) {
      node = avl_fix_right(node);
    }
    if (!from) {
      return node;
    }
    *from = node;
    node = node->parent;
  }
}

AVLNode *AvlDel(AVLNode *node) {
  if (node->right == nullptr) {
    AVLNode *parent = node->parent;
    if (node->left) {
      node->left->parent = parent;
    }
    if (parent) {
      (parent->left == node ? parent->left : parent->right) =
          node->left;
      return AvlFix(parent);
    } else {
      return node->left;
    }
  } else {
    AVLNode *victim = node->right;
    while (victim->left) {
      victim = victim->left;
    }
    AVLNode *root = AvlDel(victim);

    *victim = *node;
    if (victim->left) {
      victim->left->parent = victim;
    }
    if (victim->right) {
      victim->right->parent = victim;
    }

    AVLNode *parent = node->parent;
    if (parent) {
      (parent->left == node ? parent->left : parent->right) = victim;
      return root;
    } else {
      return victim;
    }
  }
}

AVLNode *AvlOffset(AVLNode *node, int64_t offset) {
  int64_t pos = 0;
  while (offset != pos) {
    if (pos < offset && pos + AvlCnt(node->right) >= offset) {
      node = node->right;
      pos += AvlCnt(node->left) + 1;
    } else if (pos > offset &&
               pos - AvlCnt(node->left) <= offset) {
      node = node->left;
      pos -= AvlCnt(node->right) + 1;
    } else {
      AVLNode *parent = node->parent;
      if (!parent) {
        return nullptr;
      }
      if (parent->right == node) {
        pos -= AvlCnt(node->left) + 1;
      } else {
        pos += AvlCnt(node->right) + 1;
      }
      node = parent;
    }
  }
  return node;
}
