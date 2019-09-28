/*
 * metadata_update_helper.c
 *
 *  Created on: Aug 20, 2019
 *      Author: Tonglin Li
 */

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <sys/time.h>

#include "metadata_update_helper.h"
extern int MY_RANK_DEBUG;
// ========================== Public functions ==========================

void _checkout_proposal_make_progress(metadata_manager* mm);
int MM_ledger_process(metadata_manager* mm);

metadata_manager* MM_metadata_update_helper_init(int mode, int world_size, unsigned long time_window_size,
        int (*h5_namespace_judgement)(), void* app_ctx, VotingPlugin* vp,
        int (*cb_execute)(void *h5_ctx, void *proposal_buf)) {
    //assert(time_window_size >= 1500 && "Window size should greater than 1500 us");

    metadata_manager* mm = calloc(1, sizeof(metadata_manager));
    mm->app_ctx = app_ctx;
    mm->mode = mode;
    mm->world_size = world_size;
    mm->time_window_size = time_window_size;

    mm->vm = VM_voting_manager_init(vp, h5_namespace_judgement, app_ctx);
    //printf("%s:%d:mode = %d, world_size = %d, window size =  %d\n", __func__, __LINE__, mode, world_size, time_window_size);
    _checkout_proposal_make_progress(mm);
    mm->lm = LM_ledger_manager_init();
    //DEBUG_PRINT
    mm->em = EM_execution_manager_init(cb_execute, app_ctx);
    DEBUG_PRINT
    return mm;
}

int MM_metadata_update_helper_term(metadata_manager* mm){
    assert(mm);
    // Wait for other proposals
//    time_stamp now = MM_get_time_stamp_us();
//    while((MM_get_time_stamp_us() - now) < mm->time_window_size)
//        MM_ledger_process(mm);
//    EM_execute_all(mm->em);
    DEBUG_PRINT

    VM_voting_manager_term(mm->vm);
    LM_ledger_manager_term(mm->lm);
    EM_execution_manager_term(mm->em);
    return -1;
}

void _checkout_proposal_make_progress(metadata_manager* mm){
    void* new_proposal_buf = NULL;
    while(VM_checkout_proposal(mm->vm, &new_proposal_buf)){//newly received an approved proposal_buf
        //DEBUG_PRINT
        assert(new_proposal_buf);
        Queue_node* new_node = gen_queue_node_new(new_proposal_buf);
        LM_add_ledger(mm->lm, new_node);
        //printf("%s:%d: rank = %d,  checked out: pid = %d, ledger_cnt = %d, pp_time = %lu, now = %lu\n",
        //        __func__, __LINE__, MY_RANK_DEBUG,
        //        ((proposal*)(new_proposal_buf))->pid, mm->lm->ledger_q.node_cnt, ((proposal*)(new_proposal_buf))->time, MM_get_time_stamp_us());
    }
}

//Move every thing in the LQ to EQ and ensure time window.
int MM_ledger_process(metadata_manager* mm){
    assert(mm);
    VM_voting_make_progress(mm->vm);
    _checkout_proposal_make_progress(mm);
    int ledger_cnt = LM_ledger_cnt(mm->lm);
    while(ledger_cnt > 0){
        time_stamp pp_time = 0;

        // printf("%s:%d: rank = %d, pid = %d, ledger_cnt = %d\n",
        //         __func__, __LINE__, MY_RANK_DEBUG, getpid(),
        //         ledger_cnt);

        Queue_node* old_pp = LM_get_oldest_record(mm->lm, &pp_time);
        time_stamp now = MM_get_time_stamp_us();
        if((now - pp_time) > mm->time_window_size){
            LM_remove_ledger(mm->lm, old_pp);
            EM_add_proposal(mm->em, old_pp);

            //printf("%s:%d: rank = %d, ledger_cnt = %d, moving to exe: pid = %d, pp_time = %lu, now = %lu, delta = %lu, exe_cnt = %d\n",
            //        __func__, __LINE__, MY_RANK_DEBUG, ledger_cnt,
            //        ((proposal*)(old_pp->data))->pid, pp_time, now, now - pp_time, mm->em->execution_q.node_cnt);
        }else{
            // DO NOT break here, a corner case is covered here, make sure every time all items will be moved to EQ.
        }

        VM_voting_make_progress(mm->vm);
        _checkout_proposal_make_progress(mm);
        ledger_cnt = LM_ledger_cnt(mm->lm);
    }
    return -1;
}

int MM_move_all_ledger(metadata_manager* mm){
    assert(mm);
    DEBUG_PRINT
    int ledger_cnt = LM_ledger_cnt(mm->lm);
    while(ledger_cnt > 0){
        time_stamp pp_time = 0;

        // printf("%s:%d: rank = %d, pid = %d, ledger_cnt = %d\n",
        //         __func__, __LINE__, MY_RANK_DEBUG, getpid(),
        //         ledger_cnt);

        Queue_node* old_pp = LM_get_oldest_record(mm->lm, &pp_time);
        LM_remove_ledger(mm->lm, old_pp);
        EM_add_proposal(mm->em, old_pp);
            //printf("%s:%d: rank = %d, ledger_cnt = %d, moving to exe: pid = %d, pp_time = %lu, now = %lu, delta = %lu, exe_cnt = %d\n",
            //        __func__, __LINE__, MY_RANK_DEBUG, ledger_cnt,
            //        ((proposal*)(old_pp->data))->pid, pp_time, now, now - pp_time, mm->em->execution_q.node_cnt);
        VM_voting_make_progress(mm->vm);
        ledger_cnt = LM_ledger_cnt(mm->lm);
    }
    return -1;
}

//int _submit_direct(metadata_manager* mm, proposal* p){
//    assert(mm && p);
//    int ret = -1;
//
//}
int MM_submit_proposal(metadata_manager* mm, proposal* p){
    assert(mm && p);
    int ret = -1;
    p->isLocal = 0;
    DEBUG_PRINT
    if(mm->mode == 1){//rebular mode
        DEBUG_PRINT
        //encoding proposal and send over network in this call.
        ret = VM_submit_proposal_for_voting(mm->vm, p);
        proposal_id pid = p->pid;

        proposal_state my_ps = PS_IN_PROGRESS;
        while(my_ps == PS_IN_PROGRESS){
            _checkout_proposal_make_progress(mm);
            my_ps = VM_check_my_proposal_state(mm->vm, pid);
        }

        if(my_ps == PS_APPROVED) {
            //printf("%s:%d: my rank = %d, my proposal got approved! moving to ledger queue.\n", __func__, __LINE__, MY_RANK_DEBUG);

            p->isLocal = 1;
            void* local_prop_buf = NULL;
            proposal_encoder(p, &local_prop_buf);
            Queue_node* my_node = gen_queue_node_new(local_prop_buf);
            LM_add_ledger(mm->lm, my_node);

            // Wait for this proposal to age long enough
            while((MM_get_time_stamp_us() - p->time) < mm->time_window_size){
                VM_voting_make_progress(mm->vm);
                _checkout_proposal_make_progress(mm);
                //MM_ledger_process(mm);
            }
            ret = 1;
        } else {
            ret = 0;
        }
        MM_ledger_process(mm);
        //printf("%s:%d: my rank = %d Counting qs: ledg_cnt = %d, exe_cnt = %d, now = %lu\n",
        //        __func__, __LINE__, MY_RANK_DEBUG, mm->lm->ledger_q.node_cnt, mm->em->execution_q.node_cnt,
        //    MM_get_time_stamp_us());
        EM_execute_all(mm->em);
        DEBUG_PRINT
        if(VM_check_my_proposal_state(mm->vm, pid) == PS_APPROVED){
            DEBUG_PRINT
            ret = 1;
        }else{
            DEBUG_PRINT
            ret = 0;
        }
        VM_rm_my_proposal(mm->vm);
        //DEBUG_PRINT
    } else if(mm->mode == 2){
        DEBUG_PRINT
        //bcast proposal
        VM_submit_bcast(mm->vm, p);
        void* local_prop_buf = NULL;
        DEBUG_PRINT
        VM_voting_make_progress(mm->vm);//make progress on RLO
        p->isLocal = 1;
        proposal_encoder(p, &local_prop_buf);
        Queue_node* my_node = gen_queue_node_new(local_prop_buf);
        LM_add_ledger(mm->lm, my_node);
        DEBUG_PRINT
        int lg_cnt = LM_ledger_cnt(mm->lm);
        while(lg_cnt < mm->world_size){
            VM_voting_make_progress(mm->vm);//make progress on RLO
            _checkout_proposal_make_progress(mm);
            lg_cnt = LM_ledger_cnt(mm->lm);
            //printf("%s:%d: rank = %d, ledger cnt = %d\n", __func__, __LINE__, MY_RANK_DEBUG, lg_cnt);
            //usleep(300*1000);
        }
        //usleep(500);
        DEBUG_PRINT
        MM_move_all_ledger(mm);
        DEBUG_PRINT
        EM_execute_all(mm->em);
        //put my proposal to ledger queue
        //wait until world_size - 1 msgs are checked out in ledger.
        //move to execution_q and execute all.
        ret = 1;
    }
    DEBUG_PRINT
    return ret;
}


int MM_updata_helper_make_progress(metadata_manager* mm){
    assert(mm);
    //DEBUG_PRINT

    MM_ledger_process(mm);//process ledger, move expired ones to exe_q;

    EM_execute_all(mm->em);

    return 0;
}

int get_my_verdict(metadata_manager* meta_eng){return -1;}

time_stamp MM_get_time_stamp_us(){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return 1000000 * tv.tv_sec + tv.tv_usec;
}

// ========================== Private functions ==========================
