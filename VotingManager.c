// Implementation of "generic" voting operations
// Invokes "voting mechanism" operations through function pointers
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "proposal.h"
#include "VotingManager.h"
#include "LedgerManager.h"


extern int MY_RANK_DEBUG;
// ========================== Public functions ==========================
typedef struct vp_info{

}vp_info;

VotingPlugin* VM_voting_plugin_new(){
    VotingPlugin* vp = calloc(1, sizeof(VotingPlugin));
    return vp;
}

voting_mgr* VM_voting_manager_init(VotingPlugin* plugin,
        int (*h5_namespace_judgement)(),
        void* h5ctx){
    assert(plugin);
    voting_mgr* vm = calloc(1, sizeof(voting_mgr));
    int ret;
    DEBUG_PRINT
    vm->voting_plugin = plugin;

    //vm->vp_context points to a a plugin handle such as a RLO_engine
    DEBUG_PRINT
    ret = (vm->voting_plugin->vp_init)
            ( h5_namespace_judgement, h5ctx, plugin->vp_ctx_in, &(vm->vp_context));
    assert(ret == 0);
    assert(vm->vp_context);//eng
    return vm;
}

int VM_voting_manager_term(voting_mgr* vm){
    int ret;

    assert(vm);

    ret = (vm->voting_plugin->vp_finalize)(vm->vp_context);
    assert(ret);

    free(vm);

    return -1;
}

int VM_voting_make_progress(voting_mgr* vm){
    assert(vm);
    return (vm->voting_plugin->vp_make_progress)(vm->vp_context);
}

int VM_submit_proposal_for_voting(voting_mgr* vm, proposal* p){
    assert(vm && p);
    p->state = PS_IN_PROGRESS;
    return (vm->voting_plugin->vp_submit_proposal)(vm->vp_context, p);
}

int VM_submit_bcast(voting_mgr* vm, proposal* p){
    assert(vm && p);
    p->state = PS_APPROVED;
    return (vm->voting_plugin->vp_submit_bcast)(vm->vp_context, p);
}
//I won't receive my own decision along with the proposal,
//so it will not appear in the ledger, will need to add to ledger manually.
int VM_check_my_proposal_state(voting_mgr* vm, proposal_id pid){
    assert(vm);
    DEBUG_PRINT
    VM_voting_make_progress(vm);
    DEBUG_PRINT
    int ret =  (vm->voting_plugin->vp_check_my_proposal_state)(vm->vp_context, pid);
    return ret;
    //DEBUG_PRINT
}

//convert RLO_proposal to proposal

//checkout a proposal that's approved, including my own.
// Will need pid, proposal data and timestamp, and vote calue(all decision).
int VM_checkout_proposal(voting_mgr* vm, void** prop_buf){
    assert(vm);
    void* recv_p = NULL;
    //DEBUG_PRINT
    int ret = (vm->voting_plugin->vp_checkout_proposal)(vm->vp_context, &recv_p);
    //DEBUG_PRINT

    if(ret){//proposal found
        //proposal* t = proposal_decoder(recv_p);
        //printf("%s:%u, my rank = %d, new proposal received! pid = %d\n", __func__, __LINE__, MY_RANK_DEBUG, t->pid);
        //free(t);
        *prop_buf = recv_p;//queue_node_new(recv_p);
        ret = 1;
    }else{//nothing found
        //DEBUG_PRINT
        *prop_buf = NULL;
        ret = 0;
    }

//    if(recv_p){
//        DEBUG_PRINT
//        *p_out = recv_p;//queue_node_new(recv_p);
//        ret = 1;
//    } else {
//        DEBUG_PRINT
//        *p_out = NULL;
//        ret = 0;
//    }

    return ret;
}

int VM_rm_my_proposal(voting_mgr* vm){
    //DEBUG_PRINT
    (vm->voting_plugin->vp_rm_my_proposal)(vm->vp_context);
    //DEBUG_PRINT
    return 0;
}
// ========================== Private functions ==========================

