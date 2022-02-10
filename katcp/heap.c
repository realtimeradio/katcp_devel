#ifdef KATCP_HEAP_TIMERS
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "heap.h"

#define HEAP_ROOT_INDEX   0
#define HEAP_MAGIC        0xFACEFACE

#define HEAP_LEFT_CHILD(parent_index)   (2 * (parent_index) + 1)
#define HEAP_RIGHT_CHILD(parent_index)  (2 * (parent_index) + 2)
#define HEAP_PARENT(child_index)        (int) (((child_index) - 1) / 2)

#define CASCADE_UP      1
#define CASCADE_DOWN    2

/* WARNING: following variadic macro only allowed in C99 and later*/
#ifdef DEBUG
  #define dbg_fprintf(...) fprintf(__VA_ARGS__)
#else
  #define dbg_fprintf(...)
#endif

/*
FIXME's:
  - how do we handle data references that's added multiple times - is it possible?
    -> as long as data instance sticks around, things should be ok, but if it's freed => problems

TODO's
  - neaten up this file - remove commented code, reorder functions into logic sections
*/

struct heap{
  int h_magic;
  int h_size;
  struct heap_node **h_array;
  int (*h_min_cmp)(void *data_x, void *data_y);   /* register a callback to compare data; if x < y return 1 else 0*/
  int (*h_print)(struct heap *h);                 /* register a callback to print heap; */
};

struct heap_node{
  int n_index;          /* node knows it's position in the tree */
  void *n_data;         /* pointer to the actual data */
};

/* internal mid level heap api */
/* internal node management */
static void destroy_node_heap(struct heap *h, struct heap_node *n);
static struct heap_node *create_node_heap(struct heap *h);
static int swap_nodes_heap(struct heap *h, int index_a, int index_b);
static struct heap_node *get_node_ref_by_index_heap(struct heap *h, int index);

/* internal heap management */
static int va_add_to_heap(struct heap *h, void *data);
static int grow_by_one_heap(struct heap *h);
static int cascade_up_heap(struct heap *h, int from);
static int cascade_down_heap(struct heap *h, int from);


/********************************************/

/* create and initialize the heap and return handle to it */
struct heap *init_heap(int (*min_cmp)(void *data_x, void *data_y), int (*print)(struct heap *h)){
  struct heap *h;

  if (NULL == min_cmp){
    dbg_fprintf(stderr, "heap (init): need a callback to handle minimum compare\n");
    return NULL;
  }

  h = (struct heap *) malloc(sizeof(struct heap));
  if (NULL == h){
    dbg_fprintf(stderr, "heap (init): allocation failure\n");
    return NULL;
  }

  h->h_magic = HEAP_MAGIC;
  h->h_size = 0;
  h->h_array = NULL;
  h->h_min_cmp = min_cmp;
  h->h_print = (print) ? print : NULL;

  dbg_fprintf(stderr, "heap (init)<%p>: created heap\n", h);

  return h;
}


static void destroy_node_heap(struct heap *h, struct heap_node *n){
  if (NULL == n){
    return;
  }

  dbg_fprintf(stderr, "heap<%p> (destroy node): free'ing node<%p> with index %d\n", h, n, n->n_index);

  n->n_index = (-1);
  n->n_data = NULL;

  free(n);
}


void destroy_heap(struct heap *h){
  int i;

  if (NULL == h){
    return;
  }

#if 0
  if (0 == h->h_size){
    free(h);
    return;
  }
#endif

  for (i = 0; i < h->h_size; i++){
    destroy_node_heap(h, h->h_array[i]);
    h->h_array[i] = NULL;
  }

  free(h->h_array);

  h->h_magic = 0;
  h->h_size = 0;
  h->h_array = NULL;
  h->h_min_cmp = NULL;
  h->h_print = NULL;

  free(h);
}


void *get_data_ref_by_index_heap(struct heap *h, int index){

  /* check state */
  if (NULL == h){
    return NULL;
  }

  /* bounds checking */
  if ((index < 0) || (index >= h->h_size)){
    return NULL;
  }

  return h->h_array[index]->n_data;
}


int get_size_of_heap(struct heap *h){
  return (NULL == h) ? 0 : h->h_size;
}


/* grow the array of heap_node pointers by one, on success return its index into the array. on error return -1*/
static int grow_by_one_heap(struct heap *h){
  struct heap_node **tmp = NULL;

  if (NULL == h){
    dbg_fprintf(stderr, "heap (grow): need a valid heap state\n");
    return (-1);
  }

  tmp = (struct heap_node **) realloc(h->h_array, sizeof(struct heap_node *) * (h->h_size + 1));
  if (NULL == tmp){
    dbg_fprintf(stderr, "heap<%p> (grow): reallocation failure\n", h);
    return (-1);
  }

  h->h_array = tmp;
  h->h_array[h->h_size] = NULL;

  h->h_size++;

  return (h->h_size - 1);   /* return positional index of new node in array*/
}


/* create heap node and return the handle to it */
static struct heap_node *create_node_heap(struct heap *h){
  struct heap_node *n;

  n = (struct heap_node *) malloc(sizeof(struct heap_node));
  if (NULL == n){
    dbg_fprintf(stderr, "heap<%p> (create node): allocation failure\n", h);
    return NULL;
  }

  n->n_index = (-1);
  n->n_data = NULL;

  return n;
}


int add_to_heap(struct heap *h, unsigned int count, ...){
  va_list args;
  int i;
  void *d;

  if ((NULL == h) || (HEAP_MAGIC != h->h_magic)){
    dbg_fprintf(stderr, "heap (add): need a valid heap state\n");
    return (-1);
  }

  if (0 == count){
    dbg_fprintf(stderr, "heap (add): need at least one valid data reference\n");
    return (-1);
  }

  va_start(args, count);
  
  for(i = 0; i < count; i++){
    d = va_arg(args, void *);
    va_add_to_heap(h, d);
  }

  va_end(args);

  return 0;
}


/* add a value to the heap, return index on success, -1 on error */
static int va_add_to_heap(struct heap *h, void *data){
  int index;
  struct heap_node *n;

  if ((NULL == h) || (HEAP_MAGIC != h->h_magic)){
    dbg_fprintf(stderr, "heap (add): need a valid heap state\n");
    return (-1);
  }

  if (!data){
    dbg_fprintf(stderr, "heap<%p> (add): need a valid data pointer - node not added\n", h);
    return (-1);
  }

  index = grow_by_one_heap(h);
  if ((-1) == index){
    dbg_fprintf(stderr, "heap<%p> (add): could not grow heap\n", h);
    return (-1);
  }

  n = create_node_heap(h);
  if (NULL == n){
    dbg_fprintf(stderr, "heap<%p> (add): could not create heap node\n", h);
    return (-1);
  }

  n->n_index = index;
  n->n_data = data;

  h->h_array[index] = n;

  cascade_up_heap(h, h->h_size - 1);  /* bubble up from last node added to heap tree */

  dbg_fprintf(stderr, "heap<%p> (add): added node<%p> to heap at index %d with reference %p\n", h, n, n->n_index, n->n_data);

  return index;
}


static int cascade_up_heap(struct heap *h, int from){
  int child_index;
  int parent_index;
  struct heap_node *child_node;
  struct heap_node *parent_node;
  
  if (NULL == h){
    dbg_fprintf(stderr, "heap (cascade up): need a valid heap state\n");
    return (-1);
  }
  
  if (h->h_size < 2){
#if 0
    dbg_fprintf(stderr, "heap (cascade up): heap contains %s\n", (h->h_size == 0) ? "no elements" : "one element");
#endif
    return 0;
  }

  /* bounds checking... */

  if (from < 1){  /* level 0 - can't cascade up from root position */
    return 0;
  }

  if (from > (h->h_size - 1)){  /* out of bounds */
    return -1;
  }

  child_index = from;    /* from which node index to start the cascading upwards */
  parent_index = HEAP_PARENT(child_index);
  child_node = h->h_array[child_index];
  parent_node = h->h_array[parent_index];

  while ((*(h->h_min_cmp))(child_node->n_data, parent_node->n_data)){
    swap_nodes_heap(h, parent_index, child_index);
    child_index = parent_index;
    if (child_index < 1){
      break;
    }
    parent_index = HEAP_PARENT(child_index);
    child_node = h->h_array[child_index];
    parent_node = h->h_array[parent_index];
  }

  return 0;
}


static int swap_nodes_heap(struct heap *h, int index_a, int index_b){
  struct heap_node *tmp, *alpha, *beta;
  int index_tmp;

  if (NULL == h){
    return (-1);
  }

  /* some basic logic tests...*/
  if ((index_a < 0) || (index_a >= h->h_size)){
    return (-1);
  }

  if ((index_b < 0) || (index_b >= h->h_size)){
    return (-1);
  }

  if (index_a == index_b){
    return 0;
  }

  alpha = h->h_array[index_a];
  beta = h->h_array[index_b];

  /* first swap indices...*/
  index_tmp = alpha->n_index;
  alpha->n_index = beta->n_index;
  beta->n_index = index_tmp;

  /* ...now swap node pointers in array */
  tmp = alpha;
  h->h_array[index_a] = beta;
  h->h_array[index_b] = tmp;

  dbg_fprintf(stderr, "heap<%p> (swap): node<%p> with new index %d and node<%p> with new index %d swapped\n", h, h->h_array[index_a], index_a, h->h_array[index_b], index_b);

  return 0;
}


static struct heap_node *get_node_ref_by_index_heap(struct heap *h, int index){
  if (NULL == h){
    return NULL;
  }

  /* basic logic tests...*/
  if ((index < 0) || (index >= h->h_size)){
    return NULL;
  }

  return h->h_array[index];
}


static int cascade_down_heap(struct heap *h, int from){
  /* struct heap_node *child_node, *parent_node; */
  int l_child_index, r_child_index, parent_index, child_index;
  int compare;
  int run;

  if (NULL == h){
    dbg_fprintf(stderr, "heap (cascade down): need a valid heap state\n");
    return (-1);
  }

  /* short-circuit...*/
  /* ...if zero or one element in heap - cascading not necessary */
  if (h->h_size < 2){
#if 0
    dbg_fprintf(stderr, "heap (cascade down): heap contains %s\n", (h->h_size == 0) ? "no elements" : "one element");
#endif
    return 0;
  }

  /* ...if 'from' index out of bounds */
  if ((from >= h->h_size) || (from < HEAP_ROOT_INDEX)){
    return (-1);
  }

  parent_index = from;  /* start from specified index */
  run = 1;

  do{
    l_child_index = HEAP_LEFT_CHILD(parent_index);
    r_child_index = HEAP_RIGHT_CHILD(parent_index);

    /* array bounds checking */
    if (l_child_index >= h->h_size){
      return 0;   /* no children -> stop */
    } else if (r_child_index >= h->h_size){
      child_index = l_child_index;    /* there's only one child i.e. a left child */
      run = 0;                        /* stop after this iteration since there's no more children */
    } else {
      /* compare data of children to get the index of the smallest */
      compare = (*(h->h_min_cmp))(h->h_array[l_child_index]->n_data, h->h_array[r_child_index]->n_data);
      child_index = compare ? l_child_index : r_child_index;
    }

    /* now compare child and parent data */
    compare = (*(h->h_min_cmp))(h->h_array[child_index]->n_data, h->h_array[parent_index]->n_data);
    if (compare){
      swap_nodes_heap(h, child_index, parent_index);
    } else {
      run = 0;    /* stop since node in correct position */
    }

    parent_index = child_index;

  } while(run);

  return 0;
}


int update_node_on_heap(struct heap *h, int index, void *data){
  struct heap_node *n;
  /* int direction; */
  int ret = 0;

  if ((NULL == h) || (HEAP_MAGIC != h->h_magic)){
    dbg_fprintf(stderr, "heap (update node): need a valid heap state\n");
    return (-1);
  }

  if (!data){
    dbg_fprintf(stderr, "heap<%p> (update node): need a valid data pointer - node not updated\n", h);
    return (-1);
  }

  /* check index bounds */
  if ((index < HEAP_ROOT_INDEX) || (index >= h->h_size)){
    dbg_fprintf(stderr, "heap<%p> (update node): index - out of bounds error\n", h);
    return (-1);
  }

  n = h->h_array[index];

  /* direction = ((*(h->h_min_cmp))(data, n->n_data) == 1) ? CASCADE_UP : CASCADE_DOWN; */

  n->n_data = data;

  /* dbg_fprintf(stderr, "heap<%p> (update node): data <%p> - cascade direction <%s>\n", h, data, direction == CASCADE_UP ? "up" : "down"); */

#if 0
  switch(direction){
    case CASCADE_UP:
      ret = cascade_up_heap(h, index);
      break;

    case CASCADE_DOWN:
      ret = cascade_down_heap(h, index);
      break;

    default:
      /* should never reach here */
      break;
  }
#endif

  /* try to cascade up... */
  ret += cascade_up_heap(h, index);
  /* try to cascade down... */
  ret += cascade_down_heap(h,index);
  /* ...probably not the most elegant solution */

  return ret;
}


int update_top_of_heap(struct heap *h, void *data){
  struct heap_node *n;

  if ((NULL == h) || (HEAP_MAGIC != h->h_magic)){
    dbg_fprintf(stderr, "heap (update): need a valid heap state\n");
    return (-1);
  }

  if (!data){
    dbg_fprintf(stderr, "heap<%p> (update): need a valid data pointer - node not updated\n", h);
    return (-1);
  }

  n = h->h_array[HEAP_ROOT_INDEX];

  n->n_data = data;

  cascade_down_heap(h, HEAP_ROOT_INDEX);

  dbg_fprintf(stderr, "heap<%p> (update): updated node<%p> to heap at index %d with reference %p\n", h, n, n->n_index, n->n_data);

  return 0;
}


void *get_top_of_heap(struct heap *h){
  if ((NULL == h) || (HEAP_MAGIC != h->h_magic)){
    dbg_fprintf(stderr, "heap (get): need a valid heap state\n");
    return NULL;
  }

  if (0 == h->h_size){
    dbg_fprintf(stderr, "heap<%p> (get): heap is empty\n", h);
    return NULL;
  }

  return h->h_array[HEAP_ROOT_INDEX]->n_data;
}


void *remove_top_of_heap(struct heap *h){
  void *data;
  int last_node_index; 
  struct heap_node *n;

  if ((NULL == h) || (HEAP_MAGIC != h->h_magic)){
    dbg_fprintf(stderr, "heap (remove): need a valid heap state\n");
    return NULL;
  }

  switch (h->h_size){
    case 0:
      dbg_fprintf(stderr, "heap<%p> (remove): heap is empty\n", h);
      return NULL;

    case 1:
      data = h->h_array[HEAP_ROOT_INDEX]->n_data;
      last_node_index = HEAP_ROOT_INDEX;            /* last node in heap tree*/
      break;

    default:
      data = h->h_array[HEAP_ROOT_INDEX]->n_data;
      last_node_index = h->h_size - 1;              /* last node in heap tree*/

      if (swap_nodes_heap(h, HEAP_ROOT_INDEX, last_node_index)){
        dbg_fprintf(stderr, "heap<%p> (remove): internal node swap error\n", h);
        return NULL;
      }
  }

  n = get_node_ref_by_index_heap(h, last_node_index);
  if (NULL == n){
    dbg_fprintf(stderr, "heap<%p> (remove): internal node reference error\n", h);
    return NULL;
  }

  destroy_node_heap(h, n);   /* TODO: don't destroy node, keep space for future heap growth */

  h->h_array[last_node_index] = NULL;
  h->h_size--;

  dbg_fprintf(stderr, "heap<%p> (remove): heap has %d %s\n", h, h->h_size, (h->h_size == 1) ? "node" : "nodes");

  cascade_down_heap(h, HEAP_ROOT_INDEX);

  return data;
}


int show_heap(struct heap *h){
  int i;

  if ((NULL == h) || (HEAP_MAGIC != h->h_magic)){
    dbg_fprintf(stderr, "heap<%p> (show): no heap state handle\n", h);
    return (-1);
  }

  if (h->h_size < 1){
    dbg_fprintf(stderr, "heap<%p> (show): heap is empty\n", h);
    return (-1);
  }

  if (NULL == h->h_print){
    dbg_fprintf(stderr, "heap<%p> (show): no callback to handle displaying of heap\n", h);
    return (-1);
  }

  dbg_fprintf(stderr, "heap<%p> (show): invoking heap print function at <%p>\n", h, h->h_print);
  i = (*(h->h_print))(h);

  return i;
}
#endif
