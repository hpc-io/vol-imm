/*
 * VotingManager.h
 *
 *  Created on: Aug 19, 2019
 *      Author: Tonglin Li
 */
#ifndef VOTING_MANAGER_H_
#define VOTING_MANAGER_H_

#include "util_debug.h"
#include "util_queue.h"
#include "proposal.h"


// "Public" calls that the VOL makes


// Interface of voting mechanism: all mechanisms need to implement these.
// Each action(steps of a voting process) is presented with a function pointer
// What actions are needed is still in question:
//      - Each action is a step in voting, is POSIX based voting also follows the way RLO works?
//      - Some action is only for RLO but not for POSIX:
//      - we don't even know if there are queues for the posix method.
//          - vote_back, collecting votes in a tree overlay.

typedef enum VotingPlugin_type{
    VP_MPI,
    VP_POSIX,
    VP_DEFAULT
}vp_type;

typedef struct VotingPluginCtx{
    vp_type vp_type;
    void* comm; //communicator, for MPI, it's of MPI_COMM type.
}vp_ctx_in;

typedef struct VotingMechanism{
    void* vp_ctx_in;//RLO_engine_t
    //vp_info_in: init info for vp, such as mpi_comm and mpi_info.
    int (*vp_init)(int (*app_cb)(), void* app_ctx, void* vp_info_in, void** vp_ctx_out);          // Initialize the voting mechanism
                                // (Probably needs to pass pointer to Ledger to output)
    int (*vp_submit_proposal)(void* vp_ctx, proposal* proposal_in);    // Submit a new proposal
    int (*vp_submit_bcast)(void* vp_ctx, proposal* proposal_in);
    int (*vp_check_my_proposal_state)(void* vp_ctx, proposal_id pid);
    int (*vp_rm_my_proposal)(void* vp_ctx);
    int (*vp_checkout_proposal)(void* vp_ctx, void** prop_buf);
        //check next proposal that's been approved.
        //the output will be put in ledger_queue.
        //output must follow the definition of "proposal" in proposal.h.
    // ...
    int (*vp_make_progress)(void* vp_ctx); // may not for posix
        // make_progress_gen(): completing voting
        // if(receive_decision()==1)
        //      put record to record_ready_queue
    int (*vp_get_my_rank)(void* vp_ctx);


    int (*vp_finalize)(void* vp_ctx);      // Shut down the voting mechanism
} VotingPlugin;



//typedef struct metadata_update_engine metadata_manager;

typedef struct voting_manager {
    VotingPlugin* voting_plugin;
    void* vp_context; //Store communicator/engine/...
}voting_mgr;

VotingPlugin* VM_voting_plugin_new();
voting_mgr* VM_voting_manager_init(VotingPlugin* plugin,
        int (*h5_namespace_judgement)(), void* h5ctx);

int VM_voting_manager_term(voting_mgr* vm);
int VM_voting_make_progress(voting_mgr* vm);
int VM_submit_proposal_for_voting(voting_mgr* vm, proposal* p);
int VM_submit_bcast(voting_mgr* vm, proposal* p);
int VM_check_my_proposal_state(voting_mgr* vm, proposal_id pid);

//checkout a proposal that's approved, including my own.
int VM_checkout_proposal(voting_mgr* vm, void** prop_buf_out);

int VM_rm_my_proposal(voting_mgr* vm);
//{
//    VotingMachine vm;//rlo/posix
//    vm->submit_proposal;
//    int all_done; //all proposals done??
//    while(!all_done){
//        vm->outlayer_make_progress;
//        vm->check_my_proposal_state;
//    }
//    return 0;
//}






#endif /* VOTING_MANAGER_H_ */


