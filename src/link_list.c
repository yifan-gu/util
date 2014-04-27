#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "link_list.h"

#define NEW_INSTANCE(ret, structure)                                    \
        if (((ret) = malloc(sizeof(structure))) == NULL) {              \
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);     \
        }

static node_t *new_node(void *item, size_t item_size)
{
        node_t *node;
        NEW_INSTANCE(node, node_t);

        node->item = malloc(item_size);
        if (!node->item) {
                error_at_line(-1, errno, __FILE__, __LINE__, NULL);
        }
        memcpy(node->item, item, item_size);

        node->prev = NULL;
        node->next = NULL;

        return node;
}

void ll_init_list(list_t *list, size_t item_size, dtor_t dtor)
{
        node_t *head_node, *tail_node;
        NEW_INSTANCE(head_node, node_t);
        NEW_INSTANCE(tail_node, node_t);

        list->head = head_node;
        head_node->next = tail_node;
        head_node->prev = NULL;
        head_node->item = NULL;

        list->tail = tail_node;
        tail_node->prev = head_node;
        tail_node->next = NULL;
        tail_node->item = NULL;

        list->len = 0;
        list->item_size = item_size;
        list->dtor = dtor;
}

list_t *ll_new_list(size_t item_size, dtor_t dtor)
{
        list_t *list;
        NEW_INSTANCE(list, list_t);
        ll_init_list(list, item_size, dtor);
        return list;
}

static void ll_clean_node(list_t *list, node_t *node)
{
        if (!list->dtor) {
                list->dtor = free;
        }
        list->dtor(node->item);
        free(node);
}

// append to the tail
int ll_append(list_t *list, void *item)
{
        node_t *node = new_node(item, list->item_size);
        node_t *tail = list->tail;

        node->next = tail;
        node->prev = tail->prev;
        tail->prev->next = node;
        tail->prev = node;

        list->len++;
        return list->len;
}

int ll_append_ref(list_t *list, void *item)
{
        node_t *node;
        node_t *tail = list->tail;
        NEW_INSTANCE(node, node_t);
        node->item = item;

        node->next = tail;
        node->prev = tail->prev;
        tail->prev->next = node;
        tail->prev = node;

        list->len++;
        return list->len;
}

int ll_append_node(list_t *list, node_t *node)
{
        node_t *tail = list->tail;
        node->next = tail;
        node->prev = tail->prev;
        tail->prev->next = node;
        tail->prev = node;

        list->len++;
        return list->len;
}

// remove the last item
int ll_remove(list_t *list, void *item)
{
        if (list->len == 0) {
                fprintf(stderr, "nothing to remove\n");
                return -1;
        }

        node_t *last = list->tail->prev;
        memcpy(item, last->item, list->item_size);

        last->prev->next = last->next;
        last->next->prev = last->prev;
        ll_clean_node(list, last);

        list->len--;
        return list->len;
}

// push item to the head
int ll_push(list_t *list, void *item)
{
        node_t *node = new_node(item, list->item_size);
        node_t *head = list->head;

        node->prev = head;
        node->next = head->next;
        head->next->prev = node;
        head->next = node;

        list->len++;
        return list->len;
}

int ll_push_ref(list_t *list, void *item)
{
        node_t *node;
        node_t *head = list->head;
        NEW_INSTANCE(node, node_t);
        node->item = item;

        node->prev = head;
        node->next = head->next;
        head->next->prev = node;
        head->next = node;

        list->len++;
        return list->len;
}

int ll_push_node(list_t *list, node_t *node)
{
        node_t *head = list->head;

        node->prev = head;
        node->next = head->next;
        head->next->prev = node;
        head->next = node;

        list->len++;
        return list->len;
}

// pop the first item
int ll_pop(list_t *list, void *item)
{
        if (list->len <= 0) {
                fprintf(stderr, "nothing to remove\n");
                return -1;
        }

        node_t *first = list->head->next;
        memcpy(item, first->item, list->item_size);

        first->prev->next = first->next;
        first->next->prev = first->prev;
        ll_clean_node(list, first);

        list->len--;
        return list->len;
}

int ll_free_node(list_t *list, node_t *node)
{
        node->prev->next = node->next;
        node->next->prev = node->prev;
        ll_clean_node(list, node);

        list->len--;
        return list->len;
}

int ll_remove_node(list_t *list, node_t *node)
{
        node->prev->next = node->next;
        node->next->prev = node->prev;

        list->len--;
        return list->len;
}

int ll_get_node_item(list_t *list, node_t *node, void *item)
{
        memcpy(item, node->item, list->item_size);
        return 0;
}

int ll_deinit_list(list_t *list)
{
        node_t *node, *next_node;

        if (list->len == 0) {
                free(list->head);
                free(list->tail);
                return 0;
        }

        node = list->head->next;
        next_node = node->next;

        for (; next_node != list->tail;) {
                ll_free_node(list, node);
                node = next_node;
                next_node = node->next;
        }
        ll_free_node(list, node);

        free(list->head);
        free(list->tail);
        return 0;
}

// delete the whole list
int ll_delete_list(list_t *list)
{
        ll_deinit_list(list);
        free(list);
        return 0;
}

#ifdef TESTLINKLIST
// testing
int main(int argc, char *argv[])
{
        int array[] = {1, 2, 3, 4, 5, 6, 7};

        list_t* list = ll_new_list(sizeof(int), NULL);

        for (int i = 0; i < sizeof(array) / sizeof(int); i++) {
                ll_append(list, &array[i]);
        }
        printf("list->len %zu\n", list->len);

        while (list->len > 0) {
                int item;
                ll_pop(list, &item);
                printf("item %d\n", item);
        }

        for (int i = 0; i < sizeof(array) / sizeof(int); i++) {
                ll_push(list, &array[i]);
        }
        printf("list->len %zu\n", list->len);

        while (list->len > 0) {
                int item;
                ll_remove(list, &item);
                printf("item %d\n", item);
        }
        ll_delete_list(list);

        list = ll_new_list(sizeof(int), NULL);
        for (int i = 0; i < sizeof(array) / sizeof(int); i++) {
                ll_push(list, &array[i]);
        }

        printf("traverse\n");
        node_t * node;
        for (ll_traverse(list, node)) {
                int item = *(int *)node->item;
                printf("item %d\n", item);
        }

        printf("delete the 4th node:\n");
        int i = 0;
        for (ll_traverse(list, node)) {
                if (i == 3) {
                        int item;
                        ll_get_node_item(list, node, &item);
                        printf("get item %d\n", item);
                        ll_remove_node(list, node);
                        break;
                }
                i++;
        }

        printf("traverse\n");
        for (ll_traverse(list, node)) {
                int item = *(int *)node->item;
                printf("item %d\n", item);
        }

        ll_delete_list(list);

        return 0;
}

#endif
