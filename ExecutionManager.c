/*
 * ExecutionManager.c
 *
 *  Created on: Aug 22, 2019
 *      Author: tonglin
 */
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <sys/time.h>
#include "proposal.h"
#include "ExecutionManager.h"
extern int MY_RANK_DEBUG;
execution_mgr* EM_execution_manager_init(int (*cb_execute)(void *h5_ctx, void *proposal_buf),
        void* app_ctx){
    execution_mgr* em = calloc(1, sizeof(execution_mgr));
    em->app_ctx = app_ctx;
    gen_queue_init(&(em->execution_q));
    em->execute_cb = cb_execute;
    return em;
}

int EM_execution_manager_term(execution_mgr* em){
    return -1;
}

int EM_add_proposal(execution_mgr* em, Queue_node* pp){
    assert(em && pp);
    //printf("%s: rank %d, add to execution queue: pid = %d, timestamp = %lu\n",
    //        __func__, MY_RANK_DEBUG, ((proposal*)(pp->data))->pid, ((proposal*)(pp->data))->time);
    gen_queue_append(&(em->execution_q), pp);
    //pp->data is a proposal buf.
    //p->state = PS_READY_EXECUTE;
    return -1;
}

int EM_execute_one(execution_mgr* em, void* pp){
    assert(em && pp);
    return -1;
}

int EM_execute(execution_mgr* em, void* pbuf_in){
    //int h5_op_type, void* h5_ctx, proposal* proposal
    return (em->execute_cb)(em->app_ctx, pbuf_in);
}

Queue_node* EM_get_oldest_record(execution_mgr* em, time_stamp* pp_time_out){
    assert(em && pp_time_out);
    if(em->execution_q.node_cnt == 0 || em->execution_q.q_state != Q_ACTIVE)
        return NULL;

    Queue_node* cur = em->execution_q.head;
    Queue_node* old = em->execution_q.head;
    extern int MY_RANK_DEBUG;
    while(cur){
        if(!cur->next)//end of queue
            break;
        assert(cur->data);
        assert(cur->next->data);
        proposal* r_next = (proposal*)(cur->next->data);

        if(((proposal*)(old->data))->time > r_next->time)// found older recorder
            old = cur->next;
        else if(((proposal*)(old->data))->time == r_next->time){
            if(((proposal*)(old->data))->pid > r_next->pid)//small pid wins
                old = cur->next;
        }
        cur = cur->next;
    }

    *pp_time_out = ((proposal*)(old->data))->time;
    //printf("%s:%d: rank = %d, node cnt = %d, oldest pid = %d, time = %lu\n",
    //        __func__, __LINE__, MY_RANK_DEBUG, em->execution_q.node_cnt,
    //       ((proposal*)(old->data))->pid, ((proposal*)(old->data))->time);
    return old;
}
//NOTE: this means only one local proposal can be executed in a execution_queue looping.
time_stamp EM_get_time_stamp_us(){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return 1000000 * tv.tv_sec + tv.tv_usec;
}
int EM_execute_all(execution_mgr* em){
    assert(em);
    //DEBUG_PRINT
    //printf("%s: rank %d, Start to execute, cnt = %d, current time = %lu\n",
    //        __func__, MY_RANK_DEBUG, em->execution_q.node_cnt, EM_get_time_stamp_us());
    while(em->execution_q.head){
        time_stamp t;
        Queue_node* old = EM_get_oldest_record(em, &t);
        void* prop_buf = old->data;
        //printf("%s: rank = %d, to execute: pid = %d, time = %lu, node cnt = %d\n",
        //        __func__, MY_RANK_DEBUG,
        //        ((proposal*)(old->data))->pid, ((proposal*)(old->data))->time, em->execution_q.node_cnt);
        EM_execute(em, prop_buf);
        gen_queue_remove(&(em->execution_q), old, 1);
    }
    return -1;
}
