/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the root of the source code       *
 * distribution tree, or in https://support.hdfgroup.org/ftp/HDF5/releases.  *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * Purpose:	The public header file for the pass-through VOL connector.
 */

#ifndef _H5VL_rlo_H
#define _H5VL_rlo_H

#include "metadata_update_helper.h"
#include "util_debug.h"
/* Identifier for the pass-through VOL connector */
#define H5VL_RLO_PASSTHRU	(H5VL_rlo_pass_through_register())

/* Characteristics of the pass-through VOL connector */
#define H5VL_RLO_PASSTHRU_NAME        "rlo_pass_through"
#define H5VL_RLO_PASSTHRU_VALUE       515           /* VOL connector ID */
#define H5VL_RLO_PASSTHRU_VERSION     0

/* Pass-through VOL connector info */

typedef struct H5VL_rlo_pass_through_info_t {
    hid_t under_vol_id;         /* VOL ID for under VOL */
    void *under_vol_info;       /* VOL info for under VOL */
    unsigned long time_window_size;
    int mode; //1 for regular, 2 for risky
    //Public:from property list, for duping.
    MPI_Comm mpi_comm;
    MPI_Info mpi_info;
    int world_size;
    int my_rank;
} H5VL_rlo_pass_through_info_t;
//this is initialized with the 2 env vars .
/* Use herr_t
 *
 * connector_name = "rlo_pass_through";
 * vipl_id =
 * hid_t ? = H5VLregister_connector_by_name(const char *connector_name, hid_t vipl_id);
 *
 *
 * H5Pset_vol(hid_t plist_id, hid_t new_vol_id, const void *new_vol_info);
 *
 *
 * */

#ifdef __cplusplus
extern "C" {
#endif

/* "Public" routines needed for dynamicly loading the shared library */
H5PL_type_t H5PLget_plugin_type(void);
const void *H5PLget_plugin_info(void);

H5_DLL hid_t H5VL_rlo_pass_through_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _H5VL_rlo_H */

