#ifndef __t2u_rbtree_h__
#define __t2u_rbtree_h__

enum rb_color
{
    RB_BLACK,
    RB_RED,
};

typedef struct rbtree_node
{
    struct rbtree_node *parent;
    struct rbtree_node *left;
    struct rbtree_node *right;
    enum rb_color color;
    void *key;
    void *data;
} rbtree_node;

typedef int (*rbtree_cmp_proc)(void *key_a, void *key_b);

typedef struct rbtree
{
    struct rbtree_node* root;
    rbtree_cmp_proc compare; 
} rbtree;

struct rbtree* rbtree_init(rbtree_cmp_proc fn);
int rbtree_insert(struct rbtree *tree, void *key, void *data);
void* rbtree_lookup(struct rbtree *tree,void *key);
int rbtree_remove(struct rbtree *tree, void *key);

#endif /* __t2u_rbtree_h__ */
