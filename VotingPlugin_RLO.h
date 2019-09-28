/*
 * Voting_RLO.h
 *
 *  Created on: Sep 4, 2019
 *      Author: tonglin
 */

#ifndef VOTINGPLUGIN_RLO_H_
#define VOTINGPLUGIN_RLO_H_

#include "rootless_ops.h"       // Only included here
#include "util_queue.h"
#include "proposal.h"
#include "VotingManager.h"
#include "util_debug.h"
// ========================== Public functions ==========================
//func_cb: judgement callback
typedef struct VP_ctx_out_RLO{
    RLO_engine_t* eng;
}VP_ctx;

typedef struct vp_info_in_RLO{
    MPI_Comm mpi_comm;
    MPI_Info mpi_info;
}vp_info_rlo;

int vp_init_RLO(int (*h5_judgement)(), void* h5ctx, void* vp_info_in, void** vp_ctx_out);

int vp_finalize_RLO(void* vp_ctx);

int vp_submit_proposal_RLO(void* vp_ctx, proposal* proposal);
int vp_submit_bcast_RLO(void* vp_ctx, proposal* proposal);

int vp_check_my_proposal_state_RLO(void* vp_ctx, proposal_id pid);

int vp_checkout_proposal_RLO(void* vp_ctx, void** prop_buf_out);

int vp_rm_my_proposal_RLO(void* vp_ctx);

int vp_make_progress_RLO(void* vp_ctx);

//int vp_get_my_rank_RLO(void* vp_ctx);
#endif /* VOTINGPLUGIN_RLO_H_ */
