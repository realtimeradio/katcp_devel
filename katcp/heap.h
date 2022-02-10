#ifndef _HEAP_H_
#define _HEAP_H_

#ifdef __cplusplus
extern "C" {
#endif

struct heap;

struct heap_node;

struct heap *init_heap(int (*min_cmp)(void *data_x, void *data_y), int (*print)(struct heap *h));
void destroy_heap(struct heap *h);

int add_to_heap(struct heap *h, unsigned int count, ...);
int update_node_on_heap(struct heap *h, int index, void *data);
int update_top_of_heap(struct heap *h, void *data);

void *get_top_of_heap(struct heap *h);
void *remove_top_of_heap(struct heap *h);

int show_heap(struct heap *h);

void *get_data_ref_by_index_heap(struct heap *h, int index);
int get_size_of_heap(struct heap *h);

#ifdef __cplusplus
}
#endif

#endif
