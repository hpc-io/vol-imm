#include "VotingPlugin_RLO.h"
extern int MY_RANK_DEBUG;
//VotingPlugin ROOTLESS[1] = {
//    RLO_init,           // 'init' implementation
//    RLO_submit,         // 'submit' implementation
//    RLO_finalize,       // 'finalize' implementation
//};

// ========================== Public functions ==========================
//func_cb: judgement callback


int vp_init_RLO(int (*h5_judgement)(), void* h5ctx, void* vp_info_in, void** vp_ctx_out){
    vp_info_rlo* vp_info = (vp_info_rlo*)vp_info_in;
    DEBUG_PRINT
    MPI_Comm comm = vp_info->mpi_comm;

    void* proposal_action = NULL;
    DEBUG_PRINT
    RLO_engine_t* eng = RLO_progress_engine_new(comm, RLO_MSG_SIZE_MAX, h5_judgement, h5ctx, proposal_action);
    DEBUG_PRINT
    *vp_ctx_out = (void*)eng;
    return 0;
}

int vp_finalize_RLO(void* vp_ctx){
    int ret;
    assert(vp_ctx);
    RLO_engine_t* eng = (RLO_engine_t*)vp_ctx;
    ret = RLO_progress_engine_cleanup(eng);
    assert(ret == 0);
    return 1;
}

int vp_submit_bcast_RLO(void* vp_ctx, proposal* proposal_in){//direct bcast, no voting
    assert(vp_ctx && proposal_in);
    RLO_engine_t* eng = (RLO_engine_t*)vp_ctx;

    //proposal_test(proposal_in);
    void* proposal_buf = NULL;

    size_t prop_total_size = proposal_encoder(proposal_in, &proposal_buf);
    void* pbuf_buf = NULL;
    size_t pbuf_len = 0;
    pbuf_serialize(proposal_in->pid, 1, 0, prop_total_size, proposal_buf, &pbuf_buf, &pbuf_len);
    RLO_msg_t* bcast_msg = RLO_msg_new_bc(eng, pbuf_buf, pbuf_len);
    DEBUG_PRINT
    RLO_bcast_gen(eng, bcast_msg, RLO_BCAST);
    DEBUG_PRINT
    return 0;
}

int vp_submit_proposal_RLO(void* vp_ctx, proposal* proposal_in){
    assert(vp_ctx && proposal_in);
    RLO_engine_t* eng = (RLO_engine_t*)vp_ctx;

    //proposal_test(proposal_in);
    void* proposal_buf = NULL;

    size_t prop_total_size = proposal_encoder(proposal_in, &proposal_buf);

    //printf("%s:%u, test p_data len = %lu, prop_total_size = %lu, pid = %d\n", __func__, __LINE__, proposal_in->p_data_len, prop_total_size, proposal_in->pid);
    return RLO_submit_proposal(eng, proposal_buf, prop_total_size, proposal_in->pid);
}

int vp_check_my_proposal_state_RLO(void* vp_ctx, proposal_id pid){
    assert(vp_ctx);
    //DEBUG_PRINT
    RLO_engine_t* eng = (RLO_engine_t*)vp_ctx;
    RLO_Req_stat ps = RLO_check_my_proposal_state(eng, pid);
    //DEBUG_PRINT
    proposal_state ret = PS_DEFAULT;
    switch(ps){
        case RLO_IN_PROGRESS:
            //DEBUG_PRINT
            ret = PS_IN_PROGRESS;
            break;
        case RLO_COMPLETED:
            DEBUG_PRINT
            ret = PS_APPROVED;
            break;
        case RLO_FAILED:
            DEBUG_PRINT
            ret = PS_DENIED;
            break;
        case RLO_INVALID:
            DEBUG_PRINT
            break;
        default:
            DEBUG_PRINT
            ret = PS_DEFAULT;
            break;
    }
    //DEBUG_PRINT
    return ret;
}

int vp_checkout_proposal_RLO(void* vp_ctx, void** prop_buf){
    assert(vp_ctx);


    RLO_engine_t* eng = (RLO_engine_t*)vp_ctx;
    //pickup here
    RLO_user_msg* msg_out = NULL;
    //printf("%s:%u - rank = %d\n", __func__, __LINE__, MY_RANK_DEBUG);
    int ret = RLO_user_pickup_next(eng, &msg_out);

    if(ret){
        assert(msg_out->data);
        //DEBUG_PRINT
        //printf("%s:%u, my_rank = %d, msg_out = %p, type = %d, msg_out.data = %p, data_len = %lu, data = [%p]\n",
        //        __func__, __LINE__, MY_RANK_DEBUG, msg_out, msg_out->type, msg_out->data, msg_out->data_len, msg_out->data);
        //

        PBuf* b = NULL;//calloc(1, sizeof(PBuf));
        pbuf_deserialize(msg_out->data + sizeof(size_t), &b);
        //printf("%s:%u, my_rank = %d, b.pid = %d, b.data_len = %lu, should be a proposal_buf size.\n",__func__, __LINE__, MY_RANK_DEBUG, b->pid, b->data_len);
        *prop_buf = calloc(1, b->data_len);

        memcpy(*prop_buf, b->data, b->data_len);

        RLO_user_msg_recycle(eng, msg_out);
        pbuf_free(b);
        return 1;
    }

    //DEBUG_PRINT
    return 0;
}

int vp_make_progress_RLO(void* vp_ctx){
    assert(vp_ctx);
    // RLO_engine_t* eng = (RLO_engine_t*)vp_ctx;

    RLO_make_progress();
    //DEBUG_PRINT
    return 0;
}

int vp_rm_my_proposal_RLO(void* vp_ctx){
    assert(vp_ctx);
    //RLO_engine_t* eng = (RLO_engine_t*)vp_ctx;
    //????
    return 0;
}

int vp_get_my_rank_RLO(void* vp_ctx){
    DEBUG_PRINT
    assert(vp_ctx);
    RLO_engine_t* eng = (RLO_engine_t*)vp_ctx;
    return RLO_get_eng_rank(eng);
}
// ========================== Private functions ==========================

// for RLO
