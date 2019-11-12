
/*
 * Copyright (C) Igor Sysoev
 */


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * The code is based on the algorithm described in the "Introduction
 * to Algorithms" by Cormen, Leiserson and Rivest.
 */

#define ngx_rbt_red(node)           ((node)->color = 1)
#define ngx_rbt_black(node)         ((node)->color = 0)
#define ngx_rbt_is_red(node)        ((node)->color)
#define ngx_rbt_is_black(node)      (!ngx_rbt_is_red(node))
#define ngx_rbt_copy_color(n1, n2)  (n1->color = n2->color)


ngx_inline void ngx_rbtree_left_rotate(ngx_rbtree_t **root,
                                       ngx_rbtree_t *sentinel,
                                       ngx_rbtree_t *node);
ngx_inline void ngx_rbtree_right_rotate(ngx_rbtree_t **root,
                                        ngx_rbtree_t *sentinel,
                                        ngx_rbtree_t *node);


void ngx_rbtree_insert(ngx_rbtree_t **root, ngx_rbtree_t *sentinel,
                       ngx_rbtree_t *node)
{
    ngx_rbtree_t  *temp;

    /* a binary tree insert */
    // 初始化的时候等于哨兵
    if (*root == sentinel) {
        node->parent = NULL;
        node->left = sentinel;
        node->right = sentinel;
        // 根节点必须是黑色
        ngx_rbt_black(node);
        *root = node;

        return;
    }
    // 指向根节点的地址
    temp = *root;
    // 先插入
    for ( ;; ) {
        // 插入节点的键比当前节点的小，并且当前节点没有左孩子了，则插入节点作为当前节点的左孩子
        if (node->key < temp->key) {
            if (temp->left == sentinel) {
                temp->left = node;
                break;
            }
            // 还有左孩子，则一直找到最左的孩子或者大于插入节点的值的节点
            temp = temp->left;
            continue;
        }
        // 到这插入节点的值比当前节点大，并且当前节点的由孩子为空，则成为当前节点的右孩子
        if (temp->right == sentinel) {
            temp->right = node;
            break;
        }
        // 否则一直找右孩子
        temp = temp->right;
        continue;
    }
    // 完成插入，temp保存了被插入的节点，则成为插入节点的父节点
    node->parent = temp;
    node->left = sentinel;
    node->right = sentinel;


    /* re-balance tree */
    // 把插入节点变为红色，满足尽可能多的约束
    ngx_rbt_red(node);
    // 父节点是红色的话，需要开始调整
    // node节点是根节点的孩子时，不进入while。node不是根节点并且父节点是红色，说明父节点还有父节点，即父节点不是根节点，因为根节点必须是黑色
    while (node != *root && ngx_rbt_is_red(node->parent)) {
        // node的父节点是node父节点的父节点的左孩子
        if (node->parent == node->parent->parent->left) {
            temp = node->parent->parent->right;
            // 当前节点父节点的父节点的右孩子也是红色，则父节点要变成黑色
            if (ngx_rbt_is_red(temp)) {
                // 父节点要变成黑色
                ngx_rbt_black(node->parent);
                // 父节点的兄弟节点也变黑
                ngx_rbt_black(temp);
                // 父节点的父节点变红
                ngx_rbt_red(node->parent->parent);
                // 指向父节点的父节点，继续判断
                node = node->parent->parent;

            } else {
                // 当前节点父节点的父节点的右孩子是黑色，且当前节点是父节点的右孩子
                if (node == node->parent->right) {
                    node = node->parent;
                    // 左旋转父节点，父节点变成当前节点的左孩子
                    ngx_rbtree_left_rotate(root, sentinel, node);
                }
                // 父节点变红
                ngx_rbt_black(node->parent);
                // 父节点的父节点变红
                ngx_rbt_red(node->parent->parent);
                // 父节点的父节点右旋，满足五个性质
                ngx_rbtree_right_rotate(root, sentinel, node->parent->parent);
            }

        } else {
            // node的父节点是node父节点的父节点的右孩子
            temp = node->parent->parent->left;
            // 同上
            if (ngx_rbt_is_red(temp)) {
                ngx_rbt_black(node->parent);
                ngx_rbt_black(temp);
                ngx_rbt_red(node->parent->parent);
                node = node->parent->parent;

            } else {
                if (node == node->parent->left) {
                    node = node->parent;
                    ngx_rbtree_right_rotate(root, sentinel, node);
                }

                ngx_rbt_black(node->parent);
                ngx_rbt_red(node->parent->parent);
                ngx_rbtree_left_rotate(root, sentinel, node->parent->parent);
            }
        }

    }

    ngx_rbt_black(*root);
}


void ngx_rbtree_delete(ngx_rbtree_t **root, ngx_rbtree_t *sentinel,
                       ngx_rbtree_t *node)
{
    ngx_int_t      is_red;
    ngx_rbtree_t  *subst, *temp, *w;

    /* a binary tree delete */

    if (node->left == sentinel) {
        temp = node->right;
        subst = node;

    } else if (node->right == sentinel) {
        temp = node->left;
        subst = node;

    } else {
        subst = ngx_rbtree_min(node->right, sentinel);

        if (subst->left != sentinel) {
            temp = subst->left;
        } else {
            temp = subst->right;
        }
    }

    if (subst == *root) {
        *root = temp;
        ngx_rbt_black(temp);

        /* DEBUG stuff */
        node->left = NULL;
        node->right = NULL;
        node->parent = NULL;
        node->key = 0;

        return;
    }

    is_red = ngx_rbt_is_red(subst);

    if (subst == subst->parent->left) {
        subst->parent->left = temp;

    } else {
        subst->parent->right = temp;
    }

    if (subst == node) {

        temp->parent = subst->parent;

    } else {

        if (subst->parent == node) {
            temp->parent = subst;

        } else {
            temp->parent = subst->parent;
        }

        subst->left = node->left;
        subst->right = node->right;
        subst->parent = node->parent;
        ngx_rbt_copy_color(subst, node);

        if (node == *root) {
            *root = subst;

        } else {
            if (node == node->parent->left) {
                node->parent->left = subst;
            } else {
                node->parent->right = subst;
            }
        }

        if (subst->left != sentinel) {
            subst->left->parent = subst;
        }

        if (subst->right != sentinel) {
            subst->right->parent = subst;
        }

        /* DEBUG stuff */
        node->left = NULL;
        node->right = NULL;
        node->parent = NULL;
        node->key = 0;
    }

    if (is_red) {
        return;
    }

    /* a delete fixup */

    while (temp != *root && ngx_rbt_is_black(temp)) {

        if (temp == temp->parent->left) {
            w = temp->parent->right;

            if (ngx_rbt_is_red(w)) {
                ngx_rbt_black(w);
                ngx_rbt_red(temp->parent);
                ngx_rbtree_left_rotate(root, sentinel, temp->parent);
                w = temp->parent->right;
            }

            if (ngx_rbt_is_black(w->left) && ngx_rbt_is_black(w->right)) {
                ngx_rbt_red(w);
                temp = temp->parent;

            } else {
                if (ngx_rbt_is_black(w->right)) {
                    ngx_rbt_black(w->left);
                    ngx_rbt_red(w);
                    ngx_rbtree_right_rotate(root, sentinel, w);
                    w = temp->parent->right;
                }

                ngx_rbt_copy_color(w, temp->parent);
                ngx_rbt_black(temp->parent);
                ngx_rbt_black(w->right);
                ngx_rbtree_left_rotate(root, sentinel, temp->parent);
                temp = *root;
            }

        } else {
            w = temp->parent->left;

            if (ngx_rbt_is_red(w)) {
                ngx_rbt_black(w);
                ngx_rbt_red(temp->parent);
                ngx_rbtree_right_rotate(root, sentinel, temp->parent);
                w = temp->parent->left;
            }

            if (ngx_rbt_is_black(w->left) && ngx_rbt_is_black(w->right)) {
                ngx_rbt_red(w);
                temp = temp->parent;

            } else {
                if (ngx_rbt_is_black(w->left)) {
                    ngx_rbt_black(w->right);
                    ngx_rbt_red(w);
                    ngx_rbtree_left_rotate(root, sentinel, w);
                    w = temp->parent->left;
                }

                ngx_rbt_copy_color(w, temp->parent);
                ngx_rbt_black(temp->parent);
                ngx_rbt_black(w->left);
                ngx_rbtree_right_rotate(root, sentinel, temp->parent);
                temp = *root;
            }
        }
    }

    ngx_rbt_black(temp);
}

/*
 左旋转，
 1 node成为右节点的左孩子，node右孩子的左孩子成为node的右孩子,
 2 node成为左节点的右孩子，node左孩子的右孩子成为node的左孩子
*/
ngx_inline void ngx_rbtree_left_rotate(ngx_rbtree_t **root,
                                       ngx_rbtree_t *sentinel,
                                       ngx_rbtree_t *node)
{
    ngx_rbtree_t  *temp;

    temp = node->right;
    node->right = temp->left;

    if (temp->left != sentinel) {
        temp->left->parent = node;
    }
    // 右孩子替代父节点的位置
    temp->parent = node->parent;
    // node是根节点则更新根节点
    if (node == *root) {
        *root = temp;
    // 右孩子替代父节点的位置,node是父节点的左节点，则更新父节点的左孩子，否则更新右孩子
    } else if (node == node->parent->left) {
        node->parent->left = temp;
    } else {
        node->parent->right = temp;
    }
    // node节点成为右孩子的左孩子，更新node的parent
    temp->left = node;
    node->parent = temp;
}


ngx_inline void ngx_rbtree_right_rotate(ngx_rbtree_t **root,
                                        ngx_rbtree_t *sentinel,
                                        ngx_rbtree_t *node)
{
    ngx_rbtree_t  *temp;

    temp = node->left;
    node->left = temp->right;

    if (temp->right != sentinel) {
        temp->right->parent = node;
    }

    temp->parent = node->parent;

    if (node == *root) {
        *root = temp;

    } else if (node == node->parent->right) {
        node->parent->right = temp;

    } else {
        node->parent->left = temp;
    }

    temp->right = node;
    node->parent = temp;
}
