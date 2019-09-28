/*
 * ExecutionManager.h
 *
 *  Created on: Aug 22, 2019
 *      Author: tonglin
 */

#ifndef EXECUTIONMANAGER_H_
#define EXECUTIONMANAGER_H_

#include "util_queue.h"
#include "proposal.h"
#include "util_debug.h"
typedef struct execution_manager {
    gen_queue execution_q;
    void* app_ctx;
    int (*execute_cb)(void* h5_ctx, void* proposal_buf);//int h5_op_type,
}
execution_mgr;

execution_mgr* EM_execution_manager_init( int (*cb_execute)(void *h5_ctx, void *proposal_buf),
    void* app_ctx);
int EM_execution_manager_term(execution_mgr* em);
int EM_add_proposal(execution_mgr* em, Queue_node* pp);
int EM_execute(execution_mgr* em, void* proposal_buf);
int EM_execute_all(execution_mgr* em);
#endif /* EXECUTIONMANAGER_H_ */
