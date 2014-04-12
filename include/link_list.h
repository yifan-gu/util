#ifndef _LINK_LIST_H
#define _LINK_LIST_H

#ifndef DTOR
#define DTOR
typedef void (*dtor_t)(void *);
#endif

typedef struct node_s {
        struct node_s *prev;
        struct node_s *next;
        void *item;
}node_t;

typedef struct list_s {
        node_t *head;
        node_t *tail;
        size_t item_size;
        size_t len;
        dtor_t dtor;
}list_t;

list_t * ll_new_list(size_t item_size, dtor_t dtor); // already inited
void ll_init_list(list_t *list, size_t item_size, dtor_t dtor);

int ll_append(list_t *list, void *item);
int ll_append_ref(list_t *list, void *item);
int ll_append_node(list_t *list, node_t *node);
int ll_remove(list_t *list, void *item);

int ll_push(list_t *list, void *item);
int ll_push_ref(list_t *list, void *item);
int ll_push_node(list_t *list, node_t *node);
int ll_pop(list_t *list, void *item);

// only remove the node from the list
int ll_remove_node(list_t *list, node_t *node);

// remove and free the node
int ll_free_node(list_t *list, node_t *node);
int ll_get_node_item(list_t *list, node_t *node, void *item);
int ll_deinit_list(list_t *list);
int ll_delete_list(list_t *list);

#define ll_traverse(list, node)                                         \
        (node) = (list)->head->next; (node) != (list)->tail; (node) = (node)->next

#endif
