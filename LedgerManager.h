/*
 * VotingManager.h
 *
 *  Created on: Aug 20, 2019
 *      Author: Tonglin Li
 */

// "Public" calls that the VOL and Voting mechanisms make
#ifndef LEDGER_MANAGER_H_
#define LEDGER_MANAGER_H_

#include "proposal.h"
#include "VotingManager.h"
#include "util_debug.h"
//typedef struct metadata_update_engine metadata_engine;

typedef struct ledger_manager {
    gen_queue ledger_q;
}
ledger_mgr;


ledger_mgr* LM_ledger_manager_init();
int LM_ledger_manager_term(ledger_mgr* lm);

int LM_add_ledger(ledger_mgr* lm, Queue_node* new_node);
int LM_remove_ledger(ledger_mgr* lm, Queue_node* to_remove);
Queue_node* LM_get_oldest_record(ledger_mgr* lm, time_stamp* pp_time_out);
int LM_ledger_cnt(ledger_mgr* lm);
int LM_iterate(ledger_mgr *lm, gen_queue_iter_cb cb, void *cb_ctx);

#endif /* LEDGER_MANAGER_H_ */

