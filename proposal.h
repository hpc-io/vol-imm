/*
 * proposal.h
 *
 *  Created on: Aug 22, 2019
 *      Author: tonglin
 */

#ifndef PROPOSAL_H_
#define PROPOSAL_H_

typedef int proposal_id;
typedef unsigned long time_stamp;

//Defining possible states of a proposal from application side
typedef enum proposal_state{
    PS_IN_PROGRESS,
    PS_APPROVED, //finished voting, got voted YES, but not executed yet.
    PS_DENIED, ///finished voting, got voted NO or recalled.
    PS_READY_EXECUTE,
    PS_EXECUTED, //all done
    PS_DEFAULT  //just created, not submitted yet
}proposal_state;

typedef struct proposal_record{
    proposal_id pid;
    proposal_state state;
    time_stamp time;
    int isLocal;
    int op_type;
    size_t p_data_len;
    void* proposal_data;//for VOL
    void* result_obj_local;//for output
}proposal;

proposal* compose_proposal(proposal_id pid, int op_type, void* p_data, size_t p_data_len);
time_stamp set_proposal_time(proposal* p);
proposal_id new_proposal_ID();
size_t proposal_encoder(proposal* p, void**buf_out);
proposal* proposal_decoder(void* buf);
void proposal_test(proposal* p);
#endif /* PROPOSAL_H_ */
