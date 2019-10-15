/*
 * util_queue.c
 *
 *  Created on: Aug 19, 2019
 *      Author: tonglin
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include"util_queue.h"

int gen_queue_node_delete(Queue_node* node){
    if(node->data)
        free(node->data);
    node->next = NULL;
    node->prev = NULL;
    free(node);
    return 0;
}

Queue_node* gen_queue_node_new(void* data){
    Queue_node* new_node = calloc(1, sizeof(Queue_node));
    new_node->data = data;
    new_node->prev = NULL;
    new_node->next = NULL;
    return new_node;
}

int gen_queue_init(gen_queue* q){
    if(!q)
        return -1;
    q->head = NULL;
    q->tail = NULL;
    q->node_cnt = 0;
    return 0;
}

//Insert a node before the head
int gen_queue_insert_head(gen_queue* q, Queue_node* node){
    assert(q);
    assert(node);
    node->next = q->head;
    node->prev = NULL;
    q->head->prev = node;
    q->head = node;
    q->node_cnt++;
    return 0;
}

int gen_queue_append(gen_queue* q, Queue_node* node){
    assert(q);
    assert(node);

    if(q->head == q->tail){
        if(!q->head){ // add as the 1st node
            node->prev = NULL;
            node->next = NULL;
            q->head = node;
            q->tail = node;
        } else {//add as 2nd node
            node->prev = q->tail;
            node->next = NULL;
            q->head->next = node;
            q->tail = node;
        }
    } else {
        node->prev = q->tail;
        node->next = NULL;
        q->tail->next = node;
        q->tail = node;
    }
    q->node_cnt++;
    return 0;
}

int gen_queue_concat(gen_queue* q1, gen_queue* q2){
    assert(q1);
    assert(q2);

    if(!q2->head){//q2 is empty
        return 0;
    }

    if(q1->head == q1->tail){
        if(!q1->head){ // add as the 1st node
            q1->head = q2->head;
        } else {//add as 2nd node
            q1->head->next = q2->head;
            q2->head->prev = q1->head;
        }
    } else {// more than 1 node
        q1->tail->next = q2->head;
        q2->head->prev = q1->tail;
    }

    q1->tail = q2->tail;
    q1->node_cnt += q2->node_cnt;
    q1->q_state = Q_ACTIVE;

    q2->head = NULL;
    q2->tail = NULL;
    q2->node_cnt = 0;
    q2->q_state = Q_NON_ACTIVE;

    return -1;
}

//assume msg must be in the queue.
int gen_queue_remove(gen_queue* q, Queue_node* node, int to_free_node){
    assert(q);
    assert(node);
    int ret = -1;
    if(q->head == q->tail){//0 or 1 node
        if(!q->head){
            return -1;
        } else {//the only node in the queue
            q->head = NULL;
            q->tail = NULL;
            ret = 1;
        }
    } else {//more than 1 nodes in queue
        if(node == q->head){//remove head node
            q->head = q->head->next;
            q->head->prev = NULL;
        } else if (node == q->tail){// non-head msg
            q->tail = q->tail->prev;
            q->tail->next = NULL;
        } else{ // in the middle of queue
            node->prev->next = node->next;
            node->next->prev = node->prev;
        }
        ret = 1;
    }

    node->prev = NULL;
    node->next = NULL;
    q->node_cnt--;

    if(to_free_node)
        free(node);

    return ret;
}

int gen_queue_clear(gen_queue* q, int to_free){
    assert(q);
    Queue_node* cur = q->head;
    while(cur){
        Queue_node* t = cur;
        cur = cur->next;
        gen_queue_remove(q, t, to_free);

    }
    q->node_cnt = 0;
    q->q_state = -1;
    return 0;
}

int gen_queue_test(int cnt){
    gen_queue q;
    q.head = NULL;
    q.tail = NULL;
    q.node_cnt = 0;

    for(int i = 0; i < cnt; i++){
        Queue_node* new_node = calloc(1, sizeof(Queue_node));
        new_node->num = i;
        //new_node->fwd_done = 1;
        //new_node->pickup_done = 1;
        gen_queue_append(&q, new_node);
    }
    Queue_node* new_head = calloc(1, sizeof(Queue_node));
    new_head->num = 99;
    gen_queue_insert_head(&q, new_head);
    Queue_node* cur = q.head;
    printf("cur = %p\n", cur);
    while(cur){
        printf("Looping queue after appending: cur->num = %d\n", cur->num);
        cur = cur->next;
    }

    cur = q.head;
    printf("cur = %p, q.cnt = %d\n", cur, q.node_cnt);
    while(cur){
        printf("Remove element: cur->num = %d\n", cur->num);
        Queue_node* t = cur->next;
        gen_queue_remove(&q, cur, 1);
        cur = t;
    }

    cur = q.head;
    printf("After removing, q.head = %p, q.cnt = %d\n", cur, q.node_cnt);

    while(cur){
        printf("Looping queue after removing: cur->num = %d\n", cur->num);
        cur = cur->next;
    }

    return 0;
}

Queue_node* find_min(gen_queue* q){
    Queue_node* cur = q->head;
    Queue_node* min = q->head;
    while(cur){
        if(!cur->next)//end of queue
            break;
//        assert(cur->data);
//        proposal* r = (proposal*)(cur->data);
//        assert(cur->next->data);
//        proposal* r_next = (proposal*)(cur->next->data);

        // printf("%s:rank %d: CHECKING ORDER: pid = %d, time = %.ld\n", __func__, MY_RANK_DEBUG, r->pid, r->time);
        if(min->num > cur->next->num){// found older recorder
            printf("current min = %d, change to %d, ", min->num, cur->next->num);
            min = cur->next;
            printf("min = %d now.\n", min->num);
            //cur = cur->next;
        } else {// <=
        }
        cur = cur->next;
    }
    return min;
}

int add_test_node(gen_queue* q, int num ){
    Queue_node* new_node = calloc(1, sizeof(Queue_node));
    new_node->num = num;
    gen_queue_append(q, new_node);
    return 0;
}
int print_test_queue(gen_queue* q){
    Queue_node* cur = q->head;
    printf("Listing node: ");
    while(cur){
        printf(" %d, ", cur->num);
        cur = cur->next;
    }
    printf(".\n\n");
    return 0;
}

int find_min_test(int cnt){
    gen_queue q;
    q.head = NULL;
    q.tail = NULL;
    q.node_cnt = 0;

    add_test_node(&q, 1);
    add_test_node(&q, 4);
    add_test_node(&q, 3);
    add_test_node(&q, 2);
    print_test_queue(&q);
    /*
    for(int i = 0; i < cnt; i++){
        Queue_node* new_node = calloc(1, sizeof(Queue_node));
        new_node->num = i;
        //new_node->fwd_done = 1;
        //new_node->pickup_done = 1;
        gen_queue_append(&q, new_node);
    }
*/



    while(q.head){
        Queue_node* min = find_min(&q);
        printf("Find min = %d\n", min->num);
        gen_queue_remove(&q, min, 1);
        //printf("queue.node_cnt = %d\n", q.node_cnt);
        print_test_queue(&q);
    }
    return 0;

}
/*
int main(int argc, char* argv[]){
    find_min_test(atoi(argv[1]));
    return 0;

}
*/

int gen_queue_iterate(gen_queue *q, gen_queue_iter_cb cb, void *cb_ctx)
{
    Queue_node *cur;

    assert(q);
    assert(cb);

    cur = q->head;
    while(cur) {
        Queue_node *next;

        // Protect against current node being removed from queue
        next = cur->next;

        // Invoke callback
        (*cb)(cur, cb_ctx);

        // Advance to next node, using previously saved 'next' pointer
        cur = next;
    }

    return 0;
}

