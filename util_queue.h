/*
 * util_queue.h
 *
 *  Created on: Aug 19, 2019
 *      Author: tonglin
 */

#ifndef UTIL_QUEUE_H_
#define UTIL_QUEUE_H_
//typedef struct node_queue
typedef struct queue_element Queue_node;
struct queue_element{
    void* data;
    int num;
    Queue_node* prev;
    Queue_node* next;
};

typedef enum Queue_state{
    Q_ACTIVE,
    Q_NON_ACTIVE,
    Q_DEFAULT
}Queue_state;

typedef struct generic_queue {
    Queue_node* head;
    Queue_node* tail;
    int node_cnt;
    Queue_state q_state; //active or not
}gen_queue;

typedef int (*gen_queue_iter_cb)(Queue_node *node, void *ctx);

Queue_node* gen_queue_node_new(void* data);
int gen_queue_node_delete(Queue_node* node);
int gen_queue_init(gen_queue* q);
int gen_queue_insert_head(gen_queue* q, Queue_node* node);
int gen_queue_append(gen_queue* q, Queue_node* msg);

// Concatenate two queues: append nodes from q2 to the end of q1, clear q2.
int gen_queue_concat(gen_queue* q1, gen_queue* q2);
int gen_queue_remove(gen_queue* q, Queue_node* node, int to_free_node);//free node if set to_free_node to 1.
int gen_queue_clear(gen_queue* q, int to_free);//remove everything and free space.

// Iterate over all queue nodes (safe for possible current node deletion in iterator)
int gen_queue_iterate(gen_queue *q, gen_queue_iter_cb cb, void *cb_ctx);

#endif /* UTIL_QUEUE_H_ */

