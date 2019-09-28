/*
 * metadata_update_helper.h
 *
 *  Created on: Aug 20, 2019
 *      Author: Tonglin Li
 */

#ifndef METADATA_UPDATE_HELPER_H_
#define METADATA_UPDATE_HELPER_H_


#include "proposal.h"
#include "VotingManager.h"
#include "LedgerManager.h"
#include "ExecutionManager.h"
#include "util_debug.h"


typedef struct metadata_update_engine{
    int mode; //0 for regular, 1 for risky.
    time_stamp time_window_size;
    time_stamp current_base_time;//reference time for current window.
    int world_size;
    void* app_ctx;
    // Components
    voting_mgr* vm;
    ledger_mgr* lm;
    execution_mgr* em;

//    int my_rank;
}metadata_manager;

typedef struct execution_pack{
    int op_type;
    void* h5_ctx;
    proposal* proposal;
}exec_pack;
/**
 * Things to complete:
 *      - setup and start metadata_engine;
 *      - setup and start ledger manager;
 *      - setup and start voting manager;
 *      - make the first call of updata_helper_make_progress()
 * @param window_size: time_window size, in microsec
 * @param
 * @return a metadata_engine pointer
 */
metadata_manager* MM_metadata_update_helper_init(int mode, int world_size, unsigned long time_window_size,
    int(*h5_namespace_judgement)(), void* h5ctx, VotingPlugin* vp,
    int (*cb_execute)(void *h5_ctx, void *proposal_buf));

int MM_metadata_update_helper_term(metadata_manager* meta_eng);
int MM_updata_helper_make_progress_1st_step(metadata_manager* meta_eng);
int MM_updata_helper_make_progress(metadata_manager* meta_eng);
int MM_voting_proccess(metadata_manager* meta_eng);
int MM_ledger_proccess(metadata_manager* meta_eng);
int MM_execution_proccess(metadata_manager* meta_eng);
int MM_submit_proposal(metadata_manager* meta_eng, proposal* p);

//int get_my_verdict(metadata_engine* meta_eng);

time_stamp MM_get_time_stamp_us();//time_stamp in microsec

#endif /* METADATA_UPDATE_HELPER_H_ */
