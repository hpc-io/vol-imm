/*
 * proposal.c
 *
 *  Created on: Aug 22, 2019
 *      Author: tonglin
 */

#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "proposal.h"

time_stamp proposal_get_time_usec() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return 1000000 * tv.tv_sec + tv.tv_usec;
}

proposal* compose_proposal(proposal_id pid, int op_type, void* p_data, size_t p_data_len){
    proposal* p = calloc(1, sizeof(proposal) + p_data_len);
    p->pid = pid;
    p->state = PS_DEFAULT;
    p->time = proposal_get_time_usec();
    p->isLocal = 0;//set to 1 ONLY when approved and before execute locally.
    p->op_type = op_type;
    p->p_data_len = p_data_len;
    p->proposal_data = p_data;//calloc(1, p_data_len);
    //memcpy(p->proposal_data, p_data, p_data_len);
    p->result_obj_local = NULL;
    //printf("%s:%d: test proposal_data len = %lu, data = %p\n", __func__, __LINE__, p->p_data_len, p->proposal_data);
    return p;
}

void proposal_test(proposal* p){
    assert(p);
    printf("%s:%d: pid = %d, op_type = %d, isLocal = %d, state = %d, time =%lu, p_data_len = %lu, p_data = %p, result_obj_local = %p\n",
            __func__, __LINE__, p->pid, p->op_type, p->isLocal, p->state, p->time, p->p_data_len, p->proposal_data, p->result_obj_local);

}

size_t proposal_encoder(proposal* p, void**buf_out){
    size_t total_size = sizeof(proposal) + p->p_data_len;
    //printf("%s:%d: pid = %d\n", __func__, __LINE__, p->pid);
    //printf("%s:%d: p->p_data_len = %lu, p_data = %p\n", __func__, __LINE__, p->p_data_len, p->proposal_data);
    *buf_out = calloc(1, sizeof(proposal) + p->p_data_len);

    void* cur = *buf_out;

    *(proposal_id*)cur = p->pid;
    cur = (char*)cur + sizeof(proposal_id);

    *(proposal_state*)cur = p->state;
    cur = (char*)cur + sizeof(proposal_state);

    *(time_stamp*)cur = p->time;
    cur = (char*)cur + sizeof(time_stamp);

    *(int*)cur = p->isLocal;
    cur = (char*)cur + sizeof(int);

    *(int*)cur = p->op_type;
    cur = (char*)cur + sizeof(int);

    *(size_t*)cur = p->p_data_len;
    cur = (char*)cur + sizeof(size_t);

    memcpy(cur, p->proposal_data, p->p_data_len);
    //cur = (char*)cur + p->p_data_len;

    return total_size;
}

//This is called only when you have a proposal_buf
proposal* proposal_decoder(void* buf_in){
    proposal* p = calloc(1, sizeof(proposal));

    p->pid = *(proposal_id*)buf_in;
    buf_in = (char*)buf_in + sizeof(proposal_id);
    //printf("%s:%d: pid = %d\n", __func__, __LINE__, p->pid);

    p->state = *(proposal_state*)buf_in;
    buf_in = (char*)buf_in + sizeof(proposal_state);
    //printf("%s:%d: p->state = %d\n", __func__, __LINE__, p->state);

    p->time = *(time_stamp*)buf_in;
    buf_in = (char*)buf_in + sizeof(time_stamp);
    //printf("%s:%d: p->time = %lu\n", __func__, __LINE__, p->time);

    p->isLocal = *(int*)buf_in;
    buf_in = (char*)buf_in + sizeof(int);
    //printf("%s:%d: p->isLocal = %d\n", __func__, __LINE__, p->isLocal);

    p->op_type = *(int*)buf_in;
    buf_in = (char*)buf_in + sizeof(int);
    //printf("%s:%d: p->op_type = %d\n", __func__, __LINE__, p->op_type);

    p->p_data_len = *(size_t*)buf_in;
    buf_in = (char*)buf_in + sizeof(size_t);
    //printf("%s:%d: p->p_data_len = %lu\n", __func__, __LINE__, p->p_data_len);

    p->proposal_data = calloc(1, p->p_data_len);
    memcpy(p->proposal_data, buf_in, p->p_data_len);

    buf_in = (char*)buf_in + p->p_data_len;

    // a remote proposal won't carry a resulting obj, so it's safe to set to NULL.
    // This field should be assigned only when the proposal is to execute locally (then carry a resulting obj.)
    p->result_obj_local = NULL;
    return p;
}

time_stamp set_proposal_time(proposal* p){
    assert(p);
    p->time = proposal_get_time_usec();
    return p->time;
}

proposal_id new_proposal_ID(){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return getpid() + tv.tv_usec;
}
