// Implementation of ledger manager (both for VOL & voting mechanism use)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

#include "proposal.h"
#include "util_queue.h"
#include "metadata_update_helper.h"
#include "LedgerManager.h"

// ========================== Public functions ==========================
ledger_mgr* LM_ledger_manager_init(){
    ledger_mgr* lm = calloc(1, sizeof(ledger_mgr));
    gen_queue_init(&(lm->ledger_q));
    return lm;
}

int LM_ledger_manager_term(ledger_mgr* lm){
    assert(lm);
    return -1;
}

int LM_add_ledger(ledger_mgr* lm, Queue_node* new_node){
    gen_queue_append(&(lm->ledger_q), new_node);
    return 0;
}

int LM_remove_ledger(ledger_mgr* lm, Queue_node* to_remove){
    gen_queue_remove(&(lm->ledger_q), to_remove, 0);// remove but not to free it.
    return 0;
}

int LM_iterate(ledger_mgr *lm, gen_queue_iter_cb cb, void *cb_ctx)
{
    gen_queue_iterate(&(lm->ledger_q), cb, cb_ctx);
    return 0;
}

int LM_ledger_cnt(ledger_mgr* lm){
    assert(lm);
    return lm->ledger_q.node_cnt;
}

Queue_node* LM_get_oldest_record(ledger_mgr* lm, time_stamp* pp_time_out){
    assert(lm && pp_time_out);
    if(lm->ledger_q.node_cnt == 0 || lm->ledger_q.q_state != Q_ACTIVE)
        return NULL;

    Queue_node* cur = lm->ledger_q.head;
    Queue_node* old = lm->ledger_q.head;

    proposal* r_next;
    proposal* old_prop;
    extern int MY_RANK_DEBUG;
    while(cur){
        if(!cur->next)//end of queue
            break;
        assert(cur->data);
        assert(cur->next->data);
        //proposal_buf_test(old->data);
        //proposal_buf_test(cur->next->data);
        old_prop = proposal_decoder(old->data);

        r_next = proposal_decoder(cur->next->data);

        if(old_prop->time > r_next->time)// found older recorder
            old = cur->next;
        else if(old_prop->time == r_next->time){
            if(old_prop->pid > r_next->pid)//small pid wins
                old = cur->next;
        }
        cur = cur->next;
        free(old_prop);
        free(r_next);
    }
    old_prop = proposal_decoder(old->data);
    *pp_time_out = old_prop->time;
    free(old_prop);
    // printf("%s:rank %d: CHECKING ORDER: oldest proposal id = %d, time = %.ld\n",
    //         __func__, MY_RANK_DEBUG, ((proposal*)(old->data))->pid, ((proposal*)(old->data))->time);
    return old;
}



// ========================== Private functions ==========================
