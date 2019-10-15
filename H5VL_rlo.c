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
 * Purpose:     This is a "pass through" VOL connector, which forwards each
 *              VOL callback to an underlying connector.
 *
 *              It is designed as an example VOL connector for developers to
 *              use when creating new connectors, especially connectors that
 *              are outside of the HDF5 library.  As such, it should _NOT_
 *              include _any_ private HDF5 header files.  This connector should
 *              therefore only make public HDF5 API calls and use standard C /
 *              POSIX calls.
 *
 *              Note that the HDF5 error stack must be preserved on code paths
 *              that could be invoked when the underlying VOL connector's
 *              callback can fail.
 *
 */


/* Header files needed */
/* Do NOT include private HDF5 files here! */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Public HDF5 file */
#include "hdf5.h"

/* This connector's header */
#include "H5VL_rlo.h"

/* Interfaces to other components */
//#include "VotingManager.h"
//#include "LedgerManager.h"
#include "metadata_update_helper.h"
#include "VotingPlugin_RLO.h" //plugin


/**********/
/* Macros */
/**********/

/* Whether to display log messge when callback is invoked */
/* (Uncomment to enable) */
/* #define ENABLE_RLO_PASSTHRU_LOGGING */

/* Hack for missing va_copy() in old Visual Studio editions
 * (from H5win2_defs.h - used on VS2012 and earlier)
 */
#if defined(_WIN32) && defined(_MSC_VER) && (_MSC_VER < 1800)
#define va_copy(D,S)      ((D) = (S))
#endif

/*************************/
/* Globals for debugging */
/*************************/
extern int MY_RANK_DEBUG;

/************/
/* Typedefs */
/************/

// For parent object type
typedef enum {
    VL_FILE, VL_GROUP,
    VL_DATASET, VL_ATTRIBUTES, VL_NAMED_DATATYPE,
    VL_INVALID
} rlo_obj_type_t;

// Types of operations on the container
typedef enum {
    FILE_CLOSE,
    DS_CREATE, DS_OPEN, DS_EXTEND, DS_CLOSE,
    GROUP_CREATE, GROUP_OPEN, GROUP_CLOSE,
    ATTR_CREATE, ATTR_WRITE,
    DT_COMMIT
} VL_op_type;

// "Proposal execution context" for operations on a file
typedef struct prop_ctx {
    /* # of objects sharing this context */
    unsigned int ref_count;

    /* Constant, set at file open / create */
    void *under_file;           // "Under object" for the file, after RLO connector has opened it
    hid_t under_vol_id;         // Local, same per file
    int comm_size;              // # of ranks in file's communicator
    int my_rank;                // My rank in file's communicator
    metadata_manager *mm;       // Metadata manager for the file

    /* File closing information */
    unsigned close_count;       // # of file close operations seen, from all ranks
    hbool_t is_collective;
    void* under_obj;    //already opened obj
    /* OUT, set in execution callback and retrieved in VOL callback */
    void *resulting_obj_out;    //set with cb_exe results
} prop_ctx;

/* The pass through VOL info object */
typedef struct H5VL_rlo_pass_through_t {//envelop A
    /* Specific information for a particular object */
    rlo_obj_type_t obj_type;                /* Type of object */
    void   *under_object;       /* Info object for underlying VOL connector */
                                // ("B's envelope")

    /* Shared information, for all objects */
    prop_ctx *p_ctx;            // Pointer to shared context info */
} H5VL_rlo_pass_through_t;

/* The pass through VOL wrapper context */
typedef struct H5VL_rlo_pass_through_wrap_ctx_t {//A's manual
    /* For this VOL connector */
    prop_ctx *p_ctx;            // Shared context for operations on a file

    /* From underlying VOL connector */
    void *under_wrap_ctx;       /* Object wrapping context for under VOL */
                                // ("B's manual")
} H5VL_rlo_pass_through_wrap_ctx_t;


/********************* */
/* Function prototypes */
/********************* */

/* Helper routines */
static herr_t H5VL_rlo_pass_through_file_specific_reissue(void *obj, hid_t connector_id,
    H5VL_file_specific_t specific_type, hid_t dxpl_id, void **req, ...);
static herr_t H5VL_rlo_pass_through_request_specific_reissue(void *obj, hid_t connector_id,
    H5VL_request_specific_t specific_type, ...);
static herr_t H5VL_rlo_pass_through_link_create_reissue(H5VL_link_create_type_t create_type,
    void *obj, const H5VL_loc_params_t *loc_params, hid_t connector_id,
    hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req, ...);
static H5VL_rlo_pass_through_t *H5VL_rlo_pass_through_new_obj(void *under_obj, rlo_obj_type_t obj_type,
    prop_ctx *p_ctx);
static herr_t H5VL_rlo_pass_through_free_obj(H5VL_rlo_pass_through_t *obj);

/* "Management" callbacks */
static herr_t H5VL_rlo_pass_through_init(hid_t vipl_id);
static herr_t H5VL_rlo_pass_through_term(void);

/* VOL info callbacks */
static void *H5VL_rlo_pass_through_info_copy(const void *info);
static herr_t H5VL_rlo_pass_through_info_cmp(int *cmp_value, const void *info1, const void *info2);
static herr_t H5VL_rlo_pass_through_info_free(void *info);
static herr_t H5VL_rlo_pass_through_info_to_str(const void *info, char **str);
static herr_t H5VL_rlo_pass_through_str_to_info(const char *str, void **info);

/* VOL object wrap / retrieval callbacks */
static void *H5VL_rlo_pass_through_get_object(const void *obj);
static herr_t H5VL_rlo_pass_through_get_wrap_ctx(const void *obj, void **wrap_ctx);
static void *H5VL_rlo_pass_through_wrap_object(void *obj, H5I_type_t obj_type,
    void *wrap_ctx);
static void *H5VL_rlo_pass_through_unwrap_object(void *obj);
static herr_t H5VL_rlo_pass_through_free_wrap_ctx(void *obj);

/* Attribute callbacks */
static void *H5VL_rlo_pass_through_attr_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t type_id, hid_t space_id, hid_t acpl_id, hid_t aapl_id, hid_t dxpl_id, void **req);
static void *H5VL_rlo_pass_through_attr_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t aapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_rlo_pass_through_attr_read(void *attr, hid_t mem_type_id, void *buf, hid_t dxpl_id, void **req);
static herr_t H5VL_rlo_pass_through_attr_write(void *attr, hid_t mem_type_id, const void *buf, hid_t dxpl_id, void **req);
static herr_t H5VL_rlo_pass_through_attr_get(void *obj, H5VL_attr_get_t get_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_rlo_pass_through_attr_specific(void *obj, const H5VL_loc_params_t *loc_params, H5VL_attr_specific_t specific_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_rlo_pass_through_attr_optional(void *obj, hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_rlo_pass_through_attr_close(void *attr, hid_t dxpl_id, void **req);

/* Dataset callbacks */
static void *H5VL_rlo_pass_through_dataset_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t lcpl_id, hid_t type_id, hid_t space_id, hid_t dcpl_id, hid_t dapl_id, hid_t dxpl_id, void **req);
static void *H5VL_rlo_pass_through_dataset_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t dapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_rlo_pass_through_dataset_read(void *dset, hid_t mem_type_id, hid_t mem_space_id,
                                    hid_t file_space_id, hid_t plist_id, void *buf, void **req);
static herr_t H5VL_rlo_pass_through_dataset_write(void *dset, hid_t mem_type_id, hid_t mem_space_id, hid_t file_space_id, hid_t plist_id, const void *buf, void **req);
static herr_t H5VL_rlo_pass_through_dataset_get(void *dset, H5VL_dataset_get_t get_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_rlo_pass_through_dataset_specific(void *obj, H5VL_dataset_specific_t specific_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_rlo_pass_through_dataset_optional(void *obj, hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_rlo_pass_through_dataset_close(void *dset, hid_t dxpl_id, void **req);

/* Datatype callbacks */
static void *H5VL_rlo_pass_through_datatype_commit(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t type_id, hid_t lcpl_id, hid_t tcpl_id, hid_t tapl_id, hid_t dxpl_id, void **req);
static void *H5VL_rlo_pass_through_datatype_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t tapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_rlo_pass_through_datatype_get(void *dt, H5VL_datatype_get_t get_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_rlo_pass_through_datatype_specific(void *obj, H5VL_datatype_specific_t specific_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_rlo_pass_through_datatype_optional(void *obj, hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_rlo_pass_through_datatype_close(void *dt, hid_t dxpl_id, void **req);

/* File callbacks */
static void *H5VL_rlo_pass_through_file_create(const char *name, unsigned flags, hid_t fcpl_id, hid_t fapl_id, hid_t dxpl_id, void **req);
static void *H5VL_rlo_pass_through_file_open(const char *name, unsigned flags, hid_t fapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_rlo_pass_through_file_get(void *file, H5VL_file_get_t get_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_rlo_pass_through_file_specific(void *file, H5VL_file_specific_t specific_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_rlo_pass_through_file_optional(void *file, hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_rlo_pass_through_file_close(void *file, hid_t dxpl_id, void **req);

/* Group callbacks */
static void *H5VL_rlo_pass_through_group_create(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t lcpl_id, hid_t gcpl_id, hid_t gapl_id, hid_t dxpl_id, void **req);
static void *H5VL_rlo_pass_through_group_open(void *obj, const H5VL_loc_params_t *loc_params, const char *name, hid_t gapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_rlo_pass_through_group_get(void *obj, H5VL_group_get_t get_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_rlo_pass_through_group_specific(void *obj, H5VL_group_specific_t specific_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_rlo_pass_through_group_optional(void *obj, hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_rlo_pass_through_group_close(void *grp, hid_t dxpl_id, void **req);

/* Link callbacks */
static herr_t H5VL_rlo_pass_through_link_create(H5VL_link_create_type_t create_type, void *obj, const H5VL_loc_params_t *loc_params, hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_rlo_pass_through_link_copy(void *src_obj, const H5VL_loc_params_t *loc_params1, void *dst_obj, const H5VL_loc_params_t *loc_params2, hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_rlo_pass_through_link_move(void *src_obj, const H5VL_loc_params_t *loc_params1, void *dst_obj, const H5VL_loc_params_t *loc_params2, hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_rlo_pass_through_link_get(void *obj, const H5VL_loc_params_t *loc_params, H5VL_link_get_t get_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_rlo_pass_through_link_specific(void *obj, const H5VL_loc_params_t *loc_params, H5VL_link_specific_t specific_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_rlo_pass_through_link_optional(void *obj, hid_t dxpl_id, void **req, va_list arguments);

/* Object callbacks */
static void *H5VL_rlo_pass_through_object_open(void *obj, const H5VL_loc_params_t *loc_params, H5I_type_t *opened_type, hid_t dxpl_id, void **req);
static herr_t H5VL_rlo_pass_through_object_copy(void *src_obj, const H5VL_loc_params_t *src_loc_params, const char *src_name, void *dst_obj, const H5VL_loc_params_t *dst_loc_params, const char *dst_name, hid_t ocpypl_id, hid_t lcpl_id, hid_t dxpl_id, void **req);
static herr_t H5VL_rlo_pass_through_object_get(void *obj, const H5VL_loc_params_t *loc_params, H5VL_object_get_t get_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_rlo_pass_through_object_specific(void *obj, const H5VL_loc_params_t *loc_params, H5VL_object_specific_t specific_type, hid_t dxpl_id, void **req, va_list arguments);
static herr_t H5VL_rlo_pass_through_object_optional(void *obj, hid_t dxpl_id, void **req, va_list arguments);

/* Async request callbacks */
static herr_t H5VL_rlo_pass_through_request_wait(void *req, uint64_t timeout, H5ES_status_t *status);
static herr_t H5VL_rlo_pass_through_request_notify(void *obj, H5VL_request_notify_t cb, void *ctx);
static herr_t H5VL_rlo_pass_through_request_cancel(void *req);
static herr_t H5VL_rlo_pass_through_request_specific(void *req, H5VL_request_specific_t specific_type, va_list arguments);
static herr_t H5VL_rlo_pass_through_request_optional(void *req, va_list arguments);
static herr_t H5VL_rlo_pass_through_request_free(void *req);

/*******************/
/* Local variables */
/*******************/

/* Pass through VOL connector class struct */
const H5VL_class_t H5VL_rlo_pass_through_g = {
    H5VL_RLO_PASSTHRU_VERSION,                      /* version      */
    (H5VL_class_value_t)H5VL_RLO_PASSTHRU_VALUE,    /* value        */
    H5VL_RLO_PASSTHRU_NAME,                         /* name         */
    0,                                              /* capability flags */
    H5VL_rlo_pass_through_init,                         /* initialize   */
    H5VL_rlo_pass_through_term,                         /* terminate    */
    {                                           /* info_cls */
        sizeof(H5VL_rlo_pass_through_info_t),           /* size    */
        H5VL_rlo_pass_through_info_copy,                /* copy    */
        H5VL_rlo_pass_through_info_cmp,                 /* compare */
        H5VL_rlo_pass_through_info_free,                /* free    */
        H5VL_rlo_pass_through_info_to_str,              /* to_str  */
        H5VL_rlo_pass_through_str_to_info,              /* from_str */
    },
    {                                           /* wrap_cls */
        H5VL_rlo_pass_through_get_object,               /* get_object   */
        H5VL_rlo_pass_through_get_wrap_ctx,             /* get_wrap_ctx */
        H5VL_rlo_pass_through_wrap_object,              /* wrap_object  */
        H5VL_rlo_pass_through_unwrap_object,            /* unwrap_object */
        H5VL_rlo_pass_through_free_wrap_ctx,            /* free_wrap_ctx */
    },
    {                                           /* attribute_cls */
        H5VL_rlo_pass_through_attr_create,              /* create */
        H5VL_rlo_pass_through_attr_open,                /* open */
        H5VL_rlo_pass_through_attr_read,                /* read */
        H5VL_rlo_pass_through_attr_write,               /* write */
        H5VL_rlo_pass_through_attr_get,                 /* get */
        H5VL_rlo_pass_through_attr_specific,            /* specific */
        H5VL_rlo_pass_through_attr_optional,            /* optional */
        H5VL_rlo_pass_through_attr_close                /* close */
    },
    {                                           /* dataset_cls */
        H5VL_rlo_pass_through_dataset_create,           /* create */
        H5VL_rlo_pass_through_dataset_open,             /* open */
        H5VL_rlo_pass_through_dataset_read,             /* read */
        H5VL_rlo_pass_through_dataset_write,            /* write */
        H5VL_rlo_pass_through_dataset_get,              /* get */
        H5VL_rlo_pass_through_dataset_specific,         /* specific */
        H5VL_rlo_pass_through_dataset_optional,         /* optional */
        H5VL_rlo_pass_through_dataset_close             /* close */
    },
    {                                           /* datatype_cls */
        H5VL_rlo_pass_through_datatype_commit,          /* commit */
        H5VL_rlo_pass_through_datatype_open,            /* open */
        H5VL_rlo_pass_through_datatype_get,             /* get_size */
        H5VL_rlo_pass_through_datatype_specific,        /* specific */
        H5VL_rlo_pass_through_datatype_optional,        /* optional */
        H5VL_rlo_pass_through_datatype_close            /* close */
    },
    {                                           /* file_cls */
        H5VL_rlo_pass_through_file_create,              /* create */
        H5VL_rlo_pass_through_file_open,                /* open */
        H5VL_rlo_pass_through_file_get,                 /* get */
        H5VL_rlo_pass_through_file_specific,            /* specific */
        H5VL_rlo_pass_through_file_optional,            /* optional */
        H5VL_rlo_pass_through_file_close                /* close */
    },
    {                                           /* group_cls */
        H5VL_rlo_pass_through_group_create,             /* create */
        H5VL_rlo_pass_through_group_open,               /* open */
        H5VL_rlo_pass_through_group_get,                /* get */
        H5VL_rlo_pass_through_group_specific,           /* specific */
        H5VL_rlo_pass_through_group_optional,           /* optional */
        H5VL_rlo_pass_through_group_close               /* close */
    },
    {                                           /* link_cls */
        H5VL_rlo_pass_through_link_create,              /* create */
        H5VL_rlo_pass_through_link_copy,                /* copy */
        H5VL_rlo_pass_through_link_move,                /* move */
        H5VL_rlo_pass_through_link_get,                 /* get */
        H5VL_rlo_pass_through_link_specific,            /* specific */
        H5VL_rlo_pass_through_link_optional,            /* optional */
    },
    {                                           /* object_cls */
        H5VL_rlo_pass_through_object_open,              /* open */
        H5VL_rlo_pass_through_object_copy,              /* copy */
        H5VL_rlo_pass_through_object_get,               /* get */
        H5VL_rlo_pass_through_object_specific,          /* specific */
        H5VL_rlo_pass_through_object_optional,          /* optional */
    },
    {                                           /* request_cls */
        H5VL_rlo_pass_through_request_wait,             /* wait */
        H5VL_rlo_pass_through_request_notify,           /* notify */
        H5VL_rlo_pass_through_request_cancel,           /* cancel */
        H5VL_rlo_pass_through_request_specific,         /* specific */
        H5VL_rlo_pass_through_request_optional,         /* optional */
        H5VL_rlo_pass_through_request_free              /* free */
    },
    NULL                                        /* optional */
};

/* The connector identification number, initialized at runtime */
static hid_t H5VL_RLO_PASSTHRU_g = H5I_INVALID_HID;


/* Routines needed for dynamic loading */
H5PL_type_t H5PLget_plugin_type(void) {return H5PL_TYPE_VOL;}
const void *H5PLget_plugin_info(void) {return &H5VL_rlo_pass_through_g;}




// ===========================================================================
// ===========================================================================
// ===========================================================================

// Encoding the loc_params should be possible without complications
typedef struct proposal_param_ds_create{
    hid_t type_id;
    hid_t space_id;

    hid_t lcpl_id;
    hid_t dcpl_id;
    hid_t dapl_id;
    hid_t dxpl_id;

    rlo_obj_type_t parent_type; //the container is a file or group
    haddr_t parent_obj_addr;//search locally for a number, if it's a group/file ONLY. diff by under_obj_type in ...pass_through_t

    size_t loc_param_size;
    size_t name_size;
    char* name;
    //void* under_object; //local only, per operation:: search local version of this obj by obj_no
    H5VL_loc_params_t* loc_params; //need en/decoder, , per operation: // H5VL_loc_params_t*

}param_ds_create;

typedef struct proposal_dt_commit_param{
    hid_t type_id;
    hid_t lcpl_id;
    hid_t tcpl_id;
    hid_t tapl_id;
    hid_t dxpl_id;

    rlo_obj_type_t parent_type;
    haddr_t parent_obj_addr;

    size_t loc_param_size;
    H5VL_loc_params_t *loc_params;
    size_t name_size;
    char *name;
}param_dt_commit;

typedef struct proposal_param_ds_extend {
    haddr_t dset_addr;
    int rank;
    hsize_t *new_size;
}param_ds_extend;

typedef struct proposal_param_group{
    hid_t lcpl_id;
    hid_t gcpl_id;
    hid_t gapl_id;
    hid_t dxpl_id;

    rlo_obj_type_t parent_type;
    haddr_t parent_obj_addr;

    size_t name_size;
    char *name;
    size_t loc_param_size;
    H5VL_loc_params_t *loc_params;
}param_group;

typedef struct proposal_param_attr{
    hid_t type_id;
    hid_t space_id;

    hid_t acpl_id;
    hid_t aapl_id;
    hid_t dxpl_id;

    rlo_obj_type_t parent_type;
    haddr_t parent_obj_addr;

    size_t name_size;
    char *name;
    size_t loc_param_size;
    H5VL_loc_params_t* loc_params;
}param_attr;

typedef struct proposal_param_attr_write{
    hid_t mem_type_id;
    hid_t dxpl_id;

    rlo_obj_type_t parent_type;
    haddr_t parent_obj_addr;

    size_t attr_name_size;
    char* attr_name;
    size_t buf_size;
    void *buf;

}param_attr_wr;

static metadata_manager *metadata_helper_init(const H5VL_rlo_pass_through_info_t *info_in,
    prop_ctx *h5_ctx);

void* t_encode(hid_t type_id, size_t* size);
void* p_encode(hid_t pl_id, size_t* size);
void* s_encode(hid_t space_id, size_t* size);

int loc_params_decoder(void* param_pack_in, H5VL_loc_params_t** param_out);
int loc_params_encoder(H5VL_loc_params_t* param_in, void** param_pack_out);
int loc_param_test(H5VL_loc_params_t* param_in);

int ds_create_encoder(param_ds_create* param_in, void** proposal_data_out);
int ds_create_decoder(void* proposal_data_in, param_ds_create* param_out);
void prop_param_ds_create_test(param_ds_create* param_in);

int ds_extenbd_decoder(void* proposal_data_in, param_ds_extend* param_out);

int group_create_encoder(param_group* param_in, void** proposal_data_out);
int group_create_decoder(void* proposal_data_in, param_group* param_out);

int attr_create_encoder(param_attr* param_in, void** proposal_data_out);
int attr_create_decoder(void* proposal_data_in, param_attr* param_out);
int attr_param_close(param_attr* param){return 0;}

int attr_write_encoder(param_attr_wr* param_in, void** proposal_data_out);
int attr_write_decoder(void* proposal_data_in, param_attr_wr* param_out);
//typedef struct proposal_dt_commit_param{
//    hid_t type_id;
//    hid_t lcpl_id;
//    hid_t tcpl_id;
//    hid_t tapl_id;
//    hid_t dxpl_id;
//    size_t loc_size;
//    H5VL_loc_params_t *loc_params;
//    size_t name_size;
//    char *name;
//}param_dt_commit;

size_t dt_commit_encoder(param_dt_commit* param_in, void** proposal_data_out);
int dt_commit_decoder(void* proposal_data_in, param_dt_commit* param_out);

size_t dt_commit_encoder(param_dt_commit* param_in, void** proposal_data_out){
    size_t total_size = 0;
    assert(param_in && param_in->loc_params);

    void* param_pack = NULL;
    size_t loc_param_size = loc_params_encoder(param_in->loc_params, &param_pack);

    param_in->loc_param_size = loc_param_size;

    size_t type_id_size;
    void* tid_buf = t_encode(param_in->type_id, &type_id_size);

    size_t lcpl_size;
    void* lcpl_buf = p_encode(param_in->lcpl_id, &lcpl_size);

    size_t tcpl_size;
    void* tcpl_buf = p_encode(param_in->tcpl_id, &tcpl_size);

    size_t tapl_size;
    void* tapl_buf = p_encode(param_in->tapl_id, &tapl_size);

    size_t dxpl_size;
    void* dxpl_buf = p_encode(param_in->dxpl_id, &dxpl_size);

    total_size =
            sizeof(size_t) + type_id_size +
            sizeof(size_t) + lcpl_size +
            sizeof(size_t) + tcpl_size +
            sizeof(size_t) + tapl_size +
            sizeof(size_t) + dxpl_size +
            sizeof(rlo_obj_type_t) +    //parent_type
            sizeof(haddr_t) +           //parent_addr
            sizeof(size_t) + loc_param_size +
            sizeof(size_t) + param_in->name_size;

    if(!(*proposal_data_out)){
        //DEBUG_PRINT
        *proposal_data_out = calloc(1, total_size);
    }

    void* cur = *proposal_data_out;

    *(size_t*) cur = type_id_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, tid_buf, type_id_size);
    cur = (char*)cur + type_id_size;
    free(tid_buf);

    *(size_t*) cur = lcpl_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, lcpl_buf, lcpl_size);
    cur = (char*)cur + lcpl_size;
    free(lcpl_buf);

    *(size_t*)cur = tcpl_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, tcpl_buf, tcpl_size);
    cur = (char*)cur + tcpl_size;
    free(tcpl_buf);

    *(size_t*)cur = tapl_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, tapl_buf, tapl_size);
    cur = (char*)cur + tapl_size;
    free(tapl_buf);

    *(size_t*)cur = dxpl_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, dxpl_buf, dxpl_size);
    cur = (char*)cur + dxpl_size;
    free(dxpl_buf);

    *(rlo_obj_type_t*)cur = param_in->parent_type;
    cur = (char*)cur + sizeof(rlo_obj_type_t);

    *(haddr_t*)cur = param_in->parent_obj_addr;
    cur = (char*)cur + sizeof(haddr_t);

    *(size_t*)cur = param_in->name_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, param_in->name, param_in->name_size);
    cur = (char*)cur + param_in->name_size;

    *(size_t*)cur = param_in->loc_param_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, param_pack, param_in->loc_param_size);

    return total_size;
}

int dt_commit_decoder(void* proposal_data_in, param_dt_commit* param_out){
    DEBUG_PRINT
    if(!param_out)
        param_out = calloc(1, sizeof(param_attr));

    size_t type_id_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    param_out->type_id = H5Tdecode(proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + type_id_size;
    //==========================================================

    size_t lcpl_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    param_out->lcpl_id = H5Pdecode(proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + lcpl_size;

    size_t tcpl_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    param_out->tcpl_id = H5Pdecode(proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + tcpl_size;

    size_t tapl_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    param_out->tapl_id = H5Pdecode(proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + tapl_size;

    size_t dxpl_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    param_out->dxpl_id = H5Pdecode(proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + dxpl_size;
    //==========================================================

    param_out->parent_type = *((rlo_obj_type_t*)proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + sizeof(rlo_obj_type_t);

    param_out->parent_obj_addr = *((haddr_t*)proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + sizeof(haddr_t);
    //==========================================================

    param_out->name_size = *((size_t*)proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);

    param_out->name = calloc(1,  param_out->name_size);
    memcpy(param_out->name , proposal_data_in, param_out->name_size);
    proposal_data_in = (char*)proposal_data_in + param_out->name_size;

    param_out->loc_param_size = *((size_t*)proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);

    void* param_pack = proposal_data_in;// + param_out->uobj_size;
    H5VL_loc_params_t* loc_params = NULL;
    loc_params_decoder(param_pack, &loc_params);

    param_out->loc_params = loc_params;

    return 1;
}

int attr_write_encoder(param_attr_wr* param_in, void** proposal_data_out){
    size_t type_id_size;
    void* tid_buf = t_encode(param_in->mem_type_id, &type_id_size);

    size_t dxpl_size;
    void* dxpl_buf = p_encode(param_in->dxpl_id, &dxpl_size);

    size_t total_size =
            sizeof(size_t) + type_id_size +
            sizeof(size_t) + dxpl_size +
            sizeof(rlo_obj_type_t) +    //parent_type
            sizeof(haddr_t) +           //parent_addr
            sizeof(size_t) + param_in->attr_name_size + //name
            sizeof(size_t) + param_in->buf_size;    //buf

    if(!(*proposal_data_out)){
        //DEBUG_PRINT
        *proposal_data_out = calloc(1, total_size);
    }

    void* cur = *proposal_data_out;

    *(size_t*) cur = type_id_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, tid_buf, type_id_size);
    cur = (char*)cur + type_id_size;
    free(tid_buf);

    *(size_t*) cur = dxpl_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, dxpl_buf, dxpl_size);
    cur = (char*)cur + dxpl_size;
    free(dxpl_buf);

    *(rlo_obj_type_t*)cur = param_in->parent_type;
    cur = (char*)cur + sizeof(rlo_obj_type_t);

    *(haddr_t*)cur = param_in->parent_obj_addr;
    cur = (char*)cur + sizeof(haddr_t);

    //param_in->attr_name_size = strlen(param_in->attr_name) + 1;
    *(size_t*)cur = param_in->attr_name_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, param_in->attr_name, param_in->attr_name_size);
    cur = (char*)cur + param_in->attr_name_size;

    *(size_t*)cur = param_in->buf_size;
    cur = (char*)cur + sizeof(size_t);
    if(param_in->buf_size > 0){
        memcpy(cur, param_in->buf, param_in->buf_size);
    } else
        cur = NULL;

    return total_size;
}

int attr_write_decoder(void* proposal_data_in, param_attr_wr* param_out){
    if(!param_out)
        param_out = calloc(1, sizeof(param_attr));

    // Decoding hid_ts now...
    size_t type_id_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    param_out->mem_type_id = H5Tdecode(proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + type_id_size;

    size_t dxpl_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    param_out->dxpl_id = H5Pdecode(proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + dxpl_size;

    param_out->parent_type = *((rlo_obj_type_t*)proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + sizeof(rlo_obj_type_t);

    param_out->parent_obj_addr = *(haddr_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(haddr_t);

    param_out->attr_name_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    param_out->attr_name = calloc(1, param_out->attr_name_size);
    memcpy(param_out->attr_name, proposal_data_in, param_out->attr_name_size);
    proposal_data_in = (char*)proposal_data_in + param_out->attr_name_size;

    param_out->buf_size = *((size_t*)proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    param_out->buf = calloc(1, param_out->buf_size);
    memcpy(param_out->buf, proposal_data_in, param_out->buf_size);

    return 0;
}

int attr_create_encoder(param_attr* param_in, void** proposal_data_out){
    assert(param_in && param_in->loc_params);

    void* param_pack = NULL;
    size_t loc_param_size = loc_params_encoder(param_in->loc_params, &param_pack);

    param_in->loc_param_size = loc_param_size;

    size_t type_id_size;
    void* tid_buf = t_encode(param_in->type_id, &type_id_size);

    size_t space_id_size;
    void* sid_buf = s_encode(param_in->space_id, &space_id_size);

    size_t acpl_size;
    void* acpl_buf = p_encode(param_in->acpl_id, &acpl_size);

    size_t aapl_size;
    void* aapl_buf = p_encode(param_in->aapl_id, &aapl_size);

    size_t dxpl_size;
    void* dxpl_buf = p_encode(param_in->dxpl_id, &dxpl_size);

    size_t total_size = type_id_size + space_id_size
            + acpl_size + aapl_size + dxpl_size
            + 5 * sizeof(size_t)
            + sizeof(rlo_obj_type_t) + sizeof(haddr_t)
            + loc_param_size + param_in->name_size
            + 2 * sizeof(size_t);

    if(!(*proposal_data_out)){
        //DEBUG_PRINT
        *proposal_data_out = calloc(1, total_size);
    }

    void* cur = *proposal_data_out;

    *(size_t*) cur = type_id_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, tid_buf, type_id_size);
    cur = (char*)cur + type_id_size;
    free(tid_buf);

    *(size_t*) cur = space_id_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, sid_buf, space_id_size);
    cur = (char*)cur + space_id_size;
    free(sid_buf);
    //======================
    *(size_t*) cur = acpl_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, acpl_buf, acpl_size);
    cur = (char*)cur + acpl_size;
    free(acpl_buf);

    *(size_t*) cur = aapl_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, aapl_buf, aapl_size);
    cur = (char*)cur + aapl_size;
    free(aapl_buf);

    *(size_t*) cur = dxpl_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, dxpl_buf, dxpl_size);
    cur = (char*)cur + dxpl_size;
    free(dxpl_buf);
    // ==========  hid_t bufs done  ==========

    *(rlo_obj_type_t*)cur = param_in->parent_type;
    cur = (char*)cur + sizeof(rlo_obj_type_t);

    *((haddr_t*)(cur)) = param_in->parent_obj_addr;
    cur = ((char*)cur + sizeof(haddr_t));
    //======================
    *(size_t*)cur = param_in->name_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, param_in->name, param_in->name_size);
    cur = (char*)cur + param_in->name_size;

    *(size_t*)cur = param_in->loc_param_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, param_pack, param_in->loc_param_size);

    //printf("%s: %d: rank = %d, PRINTING SIZES: tid_size = %lu, sid_size = %lu, acpl_size = %lu, aapl_size = %lu, dxpl_size = %lu\n",
    //        __func__, __LINE__, MY_RANK_DEBUG,
    //        type_id_size, space_id_size , acpl_size , aapl_size , dxpl_size );


    return total_size;
}

int attr_create_decoder(void* proposal_data_in, param_attr* param_out){
    DEBUG_PRINT
    if(!param_out)
        param_out = calloc(1, sizeof(param_attr));

    // Decoding hid_ts now...
    size_t type_id_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    param_out->type_id = H5Tdecode(proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + type_id_size;

    size_t space_id_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    param_out->space_id = H5Sdecode(proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + space_id_size;
    //==========================================================

    size_t acpl_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    param_out->acpl_id = H5Pdecode(proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + acpl_size;

    size_t aapl_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    param_out->aapl_id = H5Pdecode(proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + aapl_size;

    size_t dxpl_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    param_out->dxpl_id = H5Pdecode(proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + dxpl_size;
    //==========================================================

    param_out->parent_type = *((rlo_obj_type_t*)proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + sizeof(rlo_obj_type_t);

    param_out->parent_obj_addr = *((haddr_t*)proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + sizeof(haddr_t);
    //==========================================================

    param_out->name_size = *((size_t*)proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);

    param_out->name = calloc(1,  param_out->name_size);
    memcpy(param_out->name , proposal_data_in, param_out->name_size);
    proposal_data_in = (char*)proposal_data_in + param_out->name_size;

    param_out->loc_param_size = *((size_t*)proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);

    void* param_pack = proposal_data_in;// + param_out->uobj_size;
    H5VL_loc_params_t* loc_params = NULL;
    loc_params_decoder(param_pack, &loc_params);

    param_out->loc_params = loc_params;

    //printf("%s: %d: rank = %d,  PRINTING SIZES: tid_size = %lu, sid_size = %lu, acpl_size = %lu, aapl_size = %lu, dxpl_size = %lu\n",
    //        __func__, __LINE__, MY_RANK_DEBUG,
    //        type_id_size, space_id_size , acpl_size , aapl_size , dxpl_size );
    return 1;
}

void prop_param_attr_create_test(param_attr* param_in){
    assert(param_in);
    printf("%s: %d: rank = %d, type_id = %llx, space_id = %llx, acpl_id = %llx, aapl_id = %llx, dxpl_id = %llx, loc_param_size = %lu, name_size = %lu, name = [%s], parent_type = %d\n",
           __func__, __LINE__, MY_RANK_DEBUG,
           param_in->type_id,
           param_in->space_id,
           param_in->acpl_id,
           param_in->aapl_id,
           param_in->dxpl_id,
           param_in->loc_param_size,
           param_in->name_size,
           param_in->name,
           param_in->parent_type);
    loc_param_test(param_in->loc_params);
}
int group_create_encoder(param_group* param_in, void** proposal_data_out){

    assert(param_in && param_in->loc_params);
    // Encode under_object information (type & objno, if group)

    void* param_pack = NULL;
    size_t loc_param_size = loc_params_encoder(param_in->loc_params, &param_pack);
    param_in->loc_param_size = loc_param_size;

    param_in->name_size = strlen(param_in->name) + 1;

    size_t lcpl_size;
    void* lcpl_buf = p_encode(param_in->lcpl_id, &lcpl_size);

    size_t gcpl_size;
    void* gcpl_buf = p_encode(param_in->gcpl_id, &gcpl_size);

    size_t gapl_size;
    void* gapl_buf = p_encode(param_in->gapl_id, &gapl_size);

    size_t dxpl_size;
    void* dxpl_buf = p_encode(param_in->dxpl_id, &dxpl_size);

    size_t total_size = lcpl_size + gcpl_size + gapl_size + dxpl_size
            + 4 * sizeof(size_t)
            + sizeof(rlo_obj_type_t) + sizeof(haddr_t)
            + loc_param_size + param_in->name_size
            + 2 * sizeof(size_t);

    if(!(*proposal_data_out)){
        //DEBUG_PRINT
        *proposal_data_out = calloc(1, total_size);
    }

    void* cur = *proposal_data_out;

    // Start to copy mems

    *(size_t*) cur = lcpl_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, lcpl_buf, lcpl_size);
    cur = (char*)cur + lcpl_size;
    free(lcpl_buf);

    *(size_t*) cur = gcpl_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, gcpl_buf, gcpl_size);
    cur = (char*)cur + gcpl_size;
    free(gcpl_buf);

    *(size_t*) cur = gapl_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, gapl_buf, gapl_size);
    cur = (char*)cur + gapl_size;
    free(gapl_buf);

    *(size_t*) cur = dxpl_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, dxpl_buf, dxpl_size);
    cur = (char*)cur + dxpl_size;
    free(dxpl_buf);
    // ==========  hid_t bufs done  ==========

    *(rlo_obj_type_t*)cur = param_in->parent_type;
    cur = (char*)cur + sizeof(rlo_obj_type_t);
    //printf("%s: %d: param_in->parent_type = %d\n", __func__, __LINE__, param_in->parent_type);
    *((haddr_t*)(cur)) = param_in->parent_obj_addr;
    cur = ((char*)cur + sizeof(haddr_t));
    //printf("%s: %d: param_in->parent_obj_addr = %lu\n", __func__, __LINE__, param_in->parent_obj_addr);

    *(size_t*)cur = param_in->name_size;
    cur = (char*)cur + sizeof(size_t);
    //printf("%s: %d: param_in->name_size = %lu\n", __func__, __LINE__, param_in->name_size);

    memcpy(cur, param_in->name, param_in->name_size);
    cur = (char*)cur + param_in->name_size;

    *(size_t*)cur = param_in->loc_param_size;
    cur = (char*)cur + sizeof(size_t);
    //printf("%s: %d: param_in->loc_param_size = %lu\n", __func__, __LINE__, param_in->loc_param_size);

    memcpy(cur, param_pack, param_in->loc_param_size);
    cur = (char*)cur + param_in->loc_param_size;

    return total_size;
}

int group_create_decoder(void* proposal_data_in, param_group* param_out){
    if(!param_out)
        param_out = calloc(1, sizeof(param_group));

    size_t lcpl_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    param_out->lcpl_id = H5Pdecode(proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + lcpl_size;

    size_t gcpl_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    param_out->gcpl_id = H5Pdecode(proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + gcpl_size;

    size_t gapl_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    param_out->gapl_id = H5Pdecode(proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + gapl_size;

    size_t dxpl_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    param_out->dxpl_id = H5Pdecode(proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + dxpl_size;

    param_out->parent_type = *((rlo_obj_type_t*)proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + sizeof(rlo_obj_type_t);

    param_out->parent_obj_addr = *((haddr_t*)proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + sizeof(haddr_t);

    param_out->name_size = *((size_t*)proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);

    param_out->name = calloc(1,  param_out->name_size);
    memcpy(param_out->name , proposal_data_in, param_out->name_size);
    proposal_data_in = (char*)proposal_data_in + param_out->name_size;

    param_out->loc_param_size = *((size_t*)proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    void* param_pack = proposal_data_in;

    H5VL_loc_params_t* loc_params = NULL;
    loc_params_decoder(param_pack, &loc_params);

    param_out->loc_params = loc_params;

    return 1;
}

int group_encoder_test(const H5VL_loc_params_t* loc, const char *name, hid_t lcpl_id, hid_t gcpl_id, hid_t gapl_id,
        hid_t dxpl_id){
    int ret = -1;
    DEBUG_PRINT
    param_group* gp = calloc(1, sizeof(param_group));
    gp->lcpl_id = lcpl_id;
    gp->gcpl_id = gcpl_id;
    gp->gapl_id = gapl_id;
    gp->dxpl_id = dxpl_id;

    gp->name = (char*)name;
    gp->loc_params = (H5VL_loc_params_t*)loc;

    void* buf_out = NULL;
    DEBUG_PRINT
    group_create_encoder(gp, &buf_out);
    DEBUG_PRINT
    param_group* gp2 = calloc(1, sizeof(param_group));
    group_create_decoder(buf_out, gp2);
    DEBUG_PRINT
    int com = strcmp((const char*)(gp->name), (const char*)(gp2->name));
    assert(com == 0);
    assert(gp->loc_params->obj_type == gp2->loc_params->obj_type);
    assert(gp->loc_params->type == gp2->loc_params->type);
    DEBUG_PRINT
    return ret;
}

int ds_create_decoder(void* proposal_data_in, param_ds_create* param_out){
    //DEBUG_PRINT
    if(!param_out)
        param_out = calloc(1, sizeof(param_ds_create));

    // Decoding hid_ts now...
    size_t tid_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);

    param_out->type_id = H5Tdecode(proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + tid_size;

    size_t sid_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);

    param_out->space_id = H5Sdecode(proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + sid_size;

    size_t lcpl_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    param_out->lcpl_id = H5Pdecode(proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + lcpl_size;

    size_t dcpl_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    param_out->dcpl_id = H5Pdecode(proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + dcpl_size;

    size_t dapl_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    param_out->dapl_id = H5Pdecode(proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + dapl_size;

    size_t dxpl_size = *(size_t*)proposal_data_in;
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);
    param_out->dxpl_id = H5Pdecode(proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + dxpl_size;

    //===========================================================================
    param_out->parent_type = *((rlo_obj_type_t*)proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + sizeof(rlo_obj_type_t);

    param_out->parent_obj_addr = *((haddr_t*)proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + sizeof(haddr_t);

    param_out->name_size = *((size_t*)proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);

    param_out->name = calloc(1,  param_out->name_size);
    memcpy(param_out->name , proposal_data_in, param_out->name_size);
    proposal_data_in = (char*)proposal_data_in + param_out->name_size;

    param_out->loc_param_size = *((size_t*)proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + sizeof(size_t);

    void* param_pack = proposal_data_in;// + param_out->uobj_size;
    H5VL_loc_params_t* loc_params = NULL;
    loc_params_decoder(param_pack, &loc_params);

    param_out->loc_params = loc_params;

    return 1;
}


void prop_param_ds_create_test(param_ds_create* param_in){
    assert(param_in);
    printf("%s: %d: loc_param_size = %lu, name_size = %lu, name = [%s], parent_type = %d\n",
           __func__, __LINE__, param_in->loc_param_size, param_in->name_size, param_in->name, param_in->parent_type);
    loc_param_test(param_in->loc_params);
}

//for making a proposal to submit: param to proposal_data
int ds_create_encoder(param_ds_create* param_in, void** proposal_data_out){
    assert(param_in && param_in->loc_params);

    void* param_pack = NULL;
    size_t loc_param_size = loc_params_encoder(param_in->loc_params, &param_pack);

    param_in->loc_param_size = loc_param_size;

    size_t type_id_size;
    void* tid_buf = t_encode(param_in->type_id, &type_id_size);

    size_t space_id_size;
    void* sid_buf = s_encode(param_in->space_id, &space_id_size);

    size_t lcpl_size;
    void* lcpl_buf = p_encode(param_in->lcpl_id, &lcpl_size);

    size_t dcpl_size;
    void* dcpl_buf = p_encode(param_in->dcpl_id, &dcpl_size);

    size_t dapl_size;
    void* dapl_buf = p_encode(param_in->dapl_id, &dapl_size);

    size_t dxpl_size;
    void* dxpl_buf = p_encode(param_in->dxpl_id, &dxpl_size);

    size_t total_size = type_id_size + space_id_size
            + lcpl_size + dcpl_size + dapl_size + dxpl_size
            + 6 * sizeof(size_t)
            + sizeof(rlo_obj_type_t) + sizeof(haddr_t)
            + loc_param_size + param_in->name_size
            + 2 * sizeof(size_t);

    if(!(*proposal_data_out)){
        //DEBUG_PRINT
        *proposal_data_out = calloc(1, total_size);
    }

    void* cur = *proposal_data_out;

    // Start to copy mems

    *(size_t*) cur = type_id_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, tid_buf, type_id_size);
    cur = (char*)cur + type_id_size;
    free(tid_buf);

    *(size_t*) cur = space_id_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, sid_buf, space_id_size);
    cur = (char*)cur + space_id_size;
    free(sid_buf);

    *(size_t*) cur = lcpl_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, lcpl_buf, lcpl_size);
    cur = (char*)cur + lcpl_size;
    free(lcpl_buf);

    *(size_t*) cur = dcpl_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, dcpl_buf, dcpl_size);
    cur = (char*)cur + dcpl_size;
    free(dcpl_buf);

    *(size_t*) cur = dapl_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, dapl_buf, dapl_size);
    cur = (char*)cur + dapl_size;
    free(dapl_buf);

    *(size_t*) cur = dxpl_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, dxpl_buf, dxpl_size);
    cur = (char*)cur + dxpl_size;
    free(dxpl_buf);
    // ==========  hid_t bufs done  ==========

    *(rlo_obj_type_t*)cur = param_in->parent_type;
    cur = (char*)cur + sizeof(rlo_obj_type_t);

    *((haddr_t*)(cur)) = param_in->parent_obj_addr;
    cur = ((char*)cur + sizeof(haddr_t));



    *(size_t*)cur = param_in->name_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, param_in->name, param_in->name_size);
    cur = (char*)cur + param_in->name_size;

    *(size_t*)cur = param_in->loc_param_size;
    cur = (char*)cur + sizeof(size_t);
    memcpy(cur, param_pack, param_in->loc_param_size);
    return total_size;
}

herr_t get_native_info(void* obj, hid_t vol_id, hid_t dxpl_id, void **req, ...)
{
    herr_t r = 1;
    va_list args;
    int got_info;

    va_start(args, req);//add an new arg after req
    got_info = H5VLobject_optional(obj, vol_id, dxpl_id, req, args);
    va_end(args);

    if(got_info < 0)
        return -1;

    return r;
}

herr_t attr_get(void* obj, hid_t vol_id, H5VL_attr_get_t get_type,
        hid_t dxpl_id, void **req, ...){
    va_list args;
    herr_t status;

    va_start(args, req);
    status = H5VLattr_get(obj, vol_id, get_type, dxpl_id, req, args);
    va_end(args);

    if(status < 0)
        return -1;

    return 0;
}

char* attr_get_name(void* under_vol_obj, hid_t vol_id){
    H5VL_loc_params_t   loc_params;
    loc_params.type = H5VL_OBJECT_BY_SELF;
    hid_t attr_id;

    loc_params.obj_type = H5I_ATTR;

    /* Get the attribute name */
    size_t buf_size;
    attr_get(under_vol_obj, vol_id, H5VL_ATTR_GET_NAME, H5P_DEFAULT, NULL, &loc_params, 0, NULL, &buf_size);
    void* buf = calloc(1, buf_size + 1);
    attr_get(under_vol_obj, vol_id, H5VL_ATTR_GET_NAME, H5P_DEFAULT, NULL, &loc_params, buf_size + 1, buf, &buf_size);
    return (char*) buf;
}

herr_t ds_get(void* obj, hid_t vol_id, H5VL_dataset_get_t get_type,
    hid_t dxpl_id, void **req, ...)
{
    va_list args;
    herr_t status;

    va_start(args, req);
    status = H5VLdataset_get(obj, vol_id, get_type, dxpl_id, req, args);
    va_end(args);

    return status;
}

herr_t ds_specific(void* obj, hid_t vol_id, H5VL_dataset_specific_t specific_type,
    hid_t dxpl_id, void **req, ...)
{
    va_list args;
    herr_t status;
    DEBUG_PRINT
    va_start(args, req);
    DEBUG_PRINT
    status = H5VLdataset_specific(obj, vol_id, specific_type, dxpl_id, req, args);
    DEBUG_PRINT
    va_end(args);
    DEBUG_PRINT
    return status;
}


//for making a "extend dataset" proposal to submit: param to proposal_data
size_t ds_extend_encoder(void *obj, hid_t under_vol_id, hsize_t *new_size,
    void** proposal_data_out)
{
    H5VL_loc_params_t param_tmp;
    H5O_info_t oinfo;
    hid_t space_id;
    int ds_rank;
    size_t data_size = 0;
    void* cur;
    herr_t status;

    assert(obj);
    assert(under_vol_id > 0);
    assert(new_size);

    DEBUG_PRINT
    /* Retrieve the address for the underlying object */
    param_tmp.type = H5VL_OBJECT_BY_SELF;
    param_tmp.obj_type = H5I_DATASET;
    get_native_info(obj, under_vol_id, H5P_DEFAULT, NULL, H5VL_NATIVE_OBJECT_GET_INFO, &param_tmp, &oinfo, H5O_INFO_BASIC);
    DEBUG_PRINT

    /* Retrieve the current dataspace for the dataset */
    status = ds_get(obj, under_vol_id, H5VL_DATASET_GET_SPACE, H5P_DEFAULT, NULL, &space_id);
    DEBUG_PRINT

    /* Get the rank of the dataset (controls the # of dims) */
    ds_rank = H5Sget_simple_extent_ndims(space_id);
printf("%d:%s:%d: ds_rank = %d\n", MY_RANK_DEBUG, __func__, __LINE__, ds_rank);

    /* Close the dataspace */
    status = H5Sclose(space_id);
    DEBUG_PRINT

    /* Compute the size of the buffer needed */
    data_size = sizeof(haddr_t) + sizeof(int) + (ds_rank * sizeof(hsize_t));

    /* Allocate output buffer */
    if(!(*proposal_data_out))
        *proposal_data_out = calloc(1, data_size);

    /* Copy parameters */
    cur = *proposal_data_out;

    /* Address of dataset to extend */
    *((haddr_t*)(cur)) = oinfo.addr;
    cur = ((char*)cur + sizeof(haddr_t));

    /* Rank of dataset (controls # of dims copied) */
    *(int *)cur = ds_rank;
    cur = (char*)cur + sizeof(int);

    /* Copy the new dataset dimensions */
    memcpy(cur, new_size, ds_rank * sizeof(hsize_t));

    return data_size;
}

int ds_extend_decoder(void* proposal_data_in, param_ds_extend* param_out)
{
    if(!param_out)
        param_out = calloc(1, sizeof(param_ds_extend));

    param_out->dset_addr = *((haddr_t*)proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + sizeof(haddr_t);

    param_out->rank = *((int *)proposal_data_in);
    proposal_data_in = (char*)proposal_data_in + sizeof(int);

    param_out->new_size = calloc(1,  param_out->rank * sizeof(hsize_t));
    memcpy(param_out->new_size, proposal_data_in, (param_out->rank * sizeof(hsize_t)));

    return 1;
}

int ds_extend_param_close(param_ds_extend * param)
{
    free(param->new_size);
    free(param);

    return 0;
}

int loc_param_test(H5VL_loc_params_t* param_in){
    DEBUG_PRINT
    printf("%s:%d: H5VL_OBJECT_BY_SELF loc type = %d, obj_type = %d\n", __func__, __LINE__, param_in->type, param_in->obj_type);
    switch(param_in->type){//loc_type
        case H5VL_OBJECT_BY_SELF:
            printf("%s:%d: H5VL_OBJECT_BY_SELF: no extra fields.\n", __func__, __LINE__);

            break;
        case H5VL_OBJECT_BY_IDX:
            printf("%s:%d: H5VL_OBJECT_BY_IDX: name = [%s], skipped other fields.\n", __func__, __LINE__, param_in->loc_data.loc_by_idx.name);
            break;
        case H5VL_OBJECT_BY_ADDR:
            printf("%s:%d: H5VL_OBJECT_BY_ADDR: addr = %lu \n", __func__, __LINE__, param_in->loc_data.loc_by_addr.addr);
            break;
        case H5VL_OBJECT_BY_REF:
            printf("%s:%d: H5VL_OBJECT_BY_REF \n", __func__, __LINE__);

            break;
        case H5VL_OBJECT_BY_NAME:
            printf("%s:%d: H5VL_OBJECT_BY_NAME \n", __func__, __LINE__);
            break;
        default:
            printf("%s:%d: Unknown loc type = %d\n", __func__, __LINE__, param_in->type);
            assert(0 && "Unknown loc type");
            break;
    }
    DEBUG_PRINT
    //printf("loc_param_test() done.\n\n");
    return 0;
}

int loc_params_encoder(H5VL_loc_params_t* param_in, void** param_pack_out){
    size_t total_size = 0;
    size_t union_size = 0;

    size_t name_size;
    size_t pl_size = 0;
    void* pl_buf = NULL;

    void* buf_union = NULL;
    void* buf_cur = NULL;

    //DEBUG_PRINT
    switch(param_in->type){//loc_type
        case H5VL_OBJECT_BY_SELF:

            union_size = 0;
            break;
        case H5VL_OBJECT_BY_IDX:
            pl_size = 0;
            pl_buf = p_encode(param_in->loc_data.loc_by_idx.lapl_id, &pl_size);
            name_size = strlen(param_in->loc_data.loc_by_idx.name) + 1;
            union_size = sizeof(size_t)     //name_size
                    + name_size             //name
                    + sizeof(H5_index_t)    //idx_type
                    + sizeof(H5_iter_order_t)   //order
                    + sizeof(hsize_t)       //n
                    + sizeof(size_t)        //pl_size
                    + pl_size;              //pl
            buf_union = calloc(1, union_size);

            buf_cur = buf_union;

            *(size_t*)buf_cur = name_size;
            memcpy(buf_cur, param_in->loc_data.loc_by_idx.name, name_size);
            buf_cur = (char*)buf_cur + name_size;

            *(H5_index_t*)buf_cur = param_in->loc_data.loc_by_idx.idx_type;
            buf_cur = (char*)buf_cur + sizeof(H5_index_t);

            *(H5_iter_order_t*)buf_cur = param_in->loc_data.loc_by_idx.order;
            buf_cur = (char*)buf_cur + sizeof(H5_iter_order_t);

            *(hsize_t*)buf_cur = param_in->loc_data.loc_by_idx.n;
            buf_cur = (char*)buf_cur + sizeof(hsize_t);

            *(size_t*)buf_cur = pl_size;
            buf_cur = (char*)buf_cur + sizeof(size_t);
            memcpy(buf_cur, pl_buf, pl_size);
            free(pl_buf);
            break;

        case H5VL_OBJECT_BY_ADDR:
            union_size = sizeof(haddr_t);
            buf_union = calloc(1, union_size);
            buf_cur = buf_union;
            *((haddr_t*)buf_cur) = param_in->loc_data.loc_by_addr.addr;
            buf_cur = ((char *)buf_cur) + sizeof(haddr_t);
            break;

        case H5VL_OBJECT_BY_REF:
            printf("%s:%d: H5VL_OBJECT_BY_REF loc type = %d\n", __func__, __LINE__, param_in->type);
            assert(0 && "H5VL_OBJECT_BY_REF Not currently supported");
            break;

        case H5VL_OBJECT_BY_NAME:
            pl_size = 0;
            pl_buf = p_encode(param_in->loc_data.loc_by_name.lapl_id, &pl_size);
            name_size = strlen(param_in->loc_data.loc_by_name.name) + 1;
            union_size = sizeof(size_t) + pl_size + sizeof(size_t) + name_size;
            buf_union = calloc(1, union_size);
            buf_cur = buf_union;

            *(size_t*)buf_cur = pl_size;
            buf_cur = (char*)buf_cur + sizeof(size_t);
            memcpy(buf_cur, pl_buf, pl_size);
            buf_cur = (char*)buf_cur + pl_size;

            *(size_t*)buf_cur = name_size;
            buf_cur = (char*)buf_cur + sizeof(size_t);
            memcpy(buf_cur, param_in->loc_data.loc_by_name.name, name_size);
            buf_cur = (char*)buf_cur +  name_size;

            break;
        default:
            printf("%s:%d: Unknown loc type = %d\n", __func__, __LINE__, param_in->type);
            assert(0 && "Unknown loc type");
            break;
    }

    total_size = sizeof(H5I_type_t) + sizeof(H5VL_loc_type_t)
            + union_size;       // union_buf

    *param_pack_out = calloc(total_size, sizeof(char));
    void* cur = *param_pack_out;

    *((H5I_type_t*)cur) = param_in->obj_type;
    cur = ((char *)cur) + sizeof(H5I_type_t);

    *((H5VL_loc_type_t*)cur) = param_in->type;
    cur = ((char *)cur) + sizeof(H5VL_loc_type_t);

    memcpy(cur, buf_union, union_size);

    if(!buf_union)
        free(buf_union);

    return total_size;
}

int loc_params_decoder(void* param_pack_in, H5VL_loc_params_t** param_out){
    if(!*param_out)
        *param_out = calloc(1, sizeof(H5VL_loc_params_t));

    H5VL_loc_params_t* cur = *param_out;

    cur->obj_type = *((H5I_type_t*)param_pack_in);

    param_pack_in = (char*)param_pack_in + sizeof(H5I_type_t);

    cur->type = *((H5VL_loc_type_t*)param_pack_in);
    param_pack_in = (char*)param_pack_in + sizeof(H5VL_loc_type_t);

    size_t name_size = 0;
    size_t pl_size = 0;
    switch(cur->type){
        case H5VL_OBJECT_BY_SELF:
            //no need to se union
            break;
        case H5VL_OBJECT_BY_IDX:
            name_size = *(size_t*)param_pack_in;
            param_pack_in = (char*)param_pack_in + sizeof(size_t);
            cur->loc_data.loc_by_idx.name = calloc(1, name_size);
            memcpy((char*)(cur->loc_data.loc_by_idx.name), param_pack_in, name_size);
            param_pack_in = (char*)param_pack_in + name_size;

            cur->loc_data.loc_by_idx.idx_type = *(H5_index_t*)param_pack_in;
            param_pack_in = (char*)param_pack_in + sizeof(H5_index_t);

            cur->loc_data.loc_by_idx.order = *(H5_iter_order_t*)param_pack_in;
            param_pack_in = (char*)param_pack_in + sizeof(H5_iter_order_t);

            cur->loc_data.loc_by_idx.n = *(hsize_t*) param_pack_in;
            param_pack_in = (char*)param_pack_in + sizeof(hsize_t);

            cur->loc_data.loc_by_idx.lapl_id = H5Pdecode(param_pack_in);
            break;

        case H5VL_OBJECT_BY_ADDR:
            cur->loc_data.loc_by_addr.addr = *(haddr_t*)param_pack_in;
            param_pack_in = (char*)param_pack_in + sizeof(haddr_t);
            break;

        case H5VL_OBJECT_BY_REF:
            assert(0 && "Not currently supported");
            break;

        case H5VL_OBJECT_BY_NAME:
            pl_size = *(size_t*)param_pack_in;
            param_pack_in = (char*)param_pack_in + sizeof(size_t);

            cur->loc_data.loc_by_name.lapl_id = H5Pdecode(param_pack_in);
            param_pack_in = (char*)param_pack_in + pl_size;

            name_size = *(size_t*)param_pack_in;
            param_pack_in = (char*)param_pack_in + sizeof(size_t);

            cur->loc_data.loc_by_name.name = calloc(1, name_size);
            memcpy((char*)(cur->loc_data.loc_by_name.name), param_pack_in, name_size);
            break;

        default:
            assert(0 && "Unknown loc type");
            break;
    }
    return 0;
}

int ds_create_param_close(param_ds_create* param){
    return 0;
}
//execute a received proposal.

//callback function used in RLO framework for voting.
int h5_judgement(const void *proposal_buf, void *app_ctx) {
    prop_ctx *ctx = (prop_ctx *)app_ctx;

    proposal* proposal = proposal_decoder((void*)proposal_buf);
    //proposal_test(proposal);

    if((MM_get_time_stamp_us() - proposal->time) >  ctx->mm->time_window_size ){//received proposal is too old.
        printf("%s:%d: rank = %d, proposal too old, voted NO. pid = %d, pp_time = %lu \n",
                __func__, __LINE__, MY_RANK_DEBUG, proposal->pid, proposal->time);
        return 0;
    }
    // how many wrappers around the proposal?
    return 1;
}

int _file_close_cb_sub(prop_ctx *execute_ctx, proposal *proposal)
{
    // Increment # of file close operations seen, from all ranks
    execute_ctx->close_count++;
    DEBUG_PRINT
    return 0;
}

//ret_value = H5VLattr_write(o->under_object, o->under_vol_id, mem_type_id, buf, dxpl_id, req);
int _attr_write_cb_sub(prop_ctx *execute_ctx, proposal* proposal) {
    param_attr_wr* param = calloc(1, sizeof(param_attr_wr));
    attr_write_decoder(proposal->proposal_data, param);
    //printf("%d:%s:%d: rank = %d, \n", getpid(), __func__, __LINE__, MY_RANK_DEBUG);

    //search local under_object by obj_id
    void* under_object_local;
    H5VL_loc_params_t under_loc_params;
    under_loc_params.type = H5VL_OBJECT_BY_ADDR;
    under_loc_params.loc_data.loc_by_addr.addr = param->parent_obj_addr;
    under_loc_params.obj_type = H5I_FILE;//still file ????
    DEBUG_PRINT

    H5I_type_t opened_type = 0;
    under_object_local = H5VLobject_open(execute_ctx->under_file, &under_loc_params,
                    execute_ctx->under_vol_id,
                    &opened_type, //output: opened type
                    param->dxpl_id,
                    NULL);

    assert(under_object_local);
    DEBUG_PRINT

    // open attr
    hid_t aapl_id = H5P_DEFAULT;
    H5VL_loc_params_t loc_param_attr;
    loc_param_attr.type = H5VL_OBJECT_BY_SELF;

    switch(param->parent_type){
        case VL_GROUP:
            DEBUG_PRINT
            loc_param_attr.obj_type = H5I_GROUP;
            break;

        case VL_DATASET:
            DEBUG_PRINT
            loc_param_attr.obj_type = H5I_DATASET;
            break;

        case VL_NAMED_DATATYPE:
            DEBUG_PRINT
            loc_param_attr.obj_type = H5I_DATATYPE;
            break;

        default:
            DEBUG_PRINT
            printf("%d:%s:%d: rank = %d, Unknown/unsupported parent type = %d\n", getpid(), __func__, __LINE__,
                    MY_RANK_DEBUG, param->parent_type);
            assert(0 && "Wrong type: attr's parent obj type could only be GROUP/DATASET/NAMED_DATATYPE.");
            break;

    }
    DEBUG_PRINT
    //printf("attr_name = [%s]\n", param->attr_name);
    void* attr = H5VLattr_open(under_object_local, &loc_param_attr, execute_ctx->under_vol_id, param->attr_name, aapl_id, param->dxpl_id, NULL);
    assert(attr);
    //write attr
    herr_t ret_value = H5VLattr_write(attr, execute_ctx->under_vol_id, param->mem_type_id, param->buf, param->dxpl_id, NULL);
    //close attr
    H5VLattr_close(attr, execute_ctx->under_vol_id, param->dxpl_id, NULL);

    //close obj
    switch(param->parent_type){
        case VL_GROUP:
            DEBUG_PRINT
            H5VLgroup_close(under_object_local, execute_ctx->under_vol_id, param->dxpl_id, NULL);
            break;

        case VL_DATASET:
            DEBUG_PRINT
            H5VLdataset_close(under_object_local, execute_ctx->under_vol_id, param->dxpl_id, NULL);
            break;

        case VL_NAMED_DATATYPE:
            DEBUG_PRINT
            H5VLdatatype_close(under_object_local, execute_ctx->under_vol_id, param->dxpl_id, NULL);
            break;

        default:
            DEBUG_PRINT
            printf("%d:%s:%d: rank = %d, Unknown/unsupported parent type = %d\n", getpid(), __func__, __LINE__,
                    MY_RANK_DEBUG, param->parent_type);
            assert(0 && "Wrong type: attr's parent obj type could only be GROUP/DATASET/NAMED_DATATYPE.");
            break;

    }
//    printf("%d:%s:%d: rank = %d, CHECKING ORDER: ds_name = %s, time = %lu\n", getpid(), __func__, __LINE__,
//            MY_RANK_DEBUG, param->name, proposal->time);

    // Close the temporary obj of group (not file)
    if(proposal->isLocal){
        return ret_value;
    }

    DEBUG_PRINT
    return 0;
}

int _dt_commit_cb_sub(prop_ctx *execute_ctx, proposal* proposal){
    param_dt_commit* param = calloc(1, sizeof(param_dt_commit));
    dt_commit_decoder(proposal->proposal_data, param);
    DEBUG_PRINT
    //search local under_object by obj_id
    void* under_object_local;

    if (param->parent_type == VL_GROUP) {
        //DEBUG_PRINT
        H5VL_loc_params_t under_loc_params;
        under_loc_params.obj_type = H5I_FILE;//H5I_GROUP;
        under_loc_params.type = H5VL_OBJECT_BY_ADDR;
        under_loc_params.loc_data.loc_by_addr.addr = param->parent_obj_addr;
        H5I_type_t opened_type = 0;
        under_object_local = H5VLobject_open(execute_ctx->under_file, &under_loc_params,
                execute_ctx->under_vol_id,
                &opened_type, //output: opened type
                param->dxpl_id,
                NULL); //req
    } else { //file
        DEBUG_PRINT
        //under_loc_params.obj_type =
        under_object_local = execute_ctx->under_file;
    }
    assert(under_object_local);
    DEBUG_PRINT
    //printf("%s:%d: rank = %d, CHECKING ORDER: ds_name = %s, time = %lu\n", __func__, __LINE__,
    //        MY_RANK_DEBUG, param->name, proposal->time);
    void* under_dt_object = H5VLdatatype_commit(
            under_object_local,
            param->loc_params,
            execute_ctx->under_vol_id,
            param->name,
            param->type_id,
            param->lcpl_id,
            param->tcpl_id,
            param->tapl_id,
            param->dxpl_id,
            NULL);

    DEBUG_PRINT
    //printf("%s:%d: rank = %d,returning ds(name = %s) obj = %p\n",  __func__, __LINE__,
    //        MY_RANK_DEBUG, param->name, under_dataset_object );
    assert(under_dt_object);
    if (proposal->isLocal) { // TODO: how to set local flag? Maybe not part of the proposa?  Maybe part of the callback parameters from the progress engine
        DEBUG_PRINT
        execute_ctx->resulting_obj_out = under_dt_object;
    } else {
        DEBUG_PRINT
        H5VLdatatype_close(under_dt_object, execute_ctx->under_vol_id, param->dxpl_id, NULL);
        DEBUG_PRINT
    }
    //DEBUG_PRINT

    // Close the temporary obj of group (not file)
    if (param->parent_type == VL_GROUP) {
        H5VLgroup_close(under_object_local, execute_ctx->under_vol_id, param->dxpl_id, NULL);
    }
    free(param);
    //DEBUG_PRINT
    return 0;
}

int _attr_create_cb_sub(prop_ctx *execute_ctx, proposal* proposal) {
    param_attr* param = calloc(1, sizeof(param_attr));
    attr_create_decoder(proposal->proposal_data, param);
    //printf("%d:%s:%d: rank = %d, \n", getpid(), __func__, __LINE__, MY_RANK_DEBUG);
    //prop_param_attr_create_test(param);

    //search local under_object by obj_id
    void* under_object_local;
    H5VL_loc_params_t under_loc_params;
    under_loc_params.type = H5VL_OBJECT_BY_ADDR;
    under_loc_params.loc_data.loc_by_addr.addr = param->parent_obj_addr;
    under_loc_params.obj_type = H5I_FILE;
    DEBUG_PRINT

    H5I_type_t opened_type = 0;
    under_object_local = H5VLobject_open(execute_ctx->under_file, &under_loc_params,
                    execute_ctx->under_vol_id,
                    &opened_type, //output: opened type
                    param->dxpl_id,
                    NULL);

    assert(under_object_local);
    DEBUG_PRINT
    //printf("%d:%s:%d: rank = %d, CHECKING ORDER: ds_name = %s, time = %lu\n", getpid(), __func__, __LINE__,
    //        MY_RANK_DEBUG, param->name, proposal->time);

    void* under_attr_object = H5VLattr_create(
            under_object_local, //local only
            param->loc_params, //need en/decoder
            execute_ctx->under_vol_id, //local only
            param->name, param->type_id, param->space_id,
            param->acpl_id, param->aapl_id,
            param->dxpl_id,
            NULL); // NULL for now
    //DEBUG_PRINT
    assert(under_attr_object);

    if (proposal->isLocal) {
        execute_ctx->resulting_obj_out = under_attr_object;
    } else {
        //DEBUG_PRINT
        H5VLattr_close(under_attr_object, execute_ctx->under_vol_id, param->dxpl_id, NULL);
    }
    //DEBUG_PRINT
    attr_param_close(param);  // TODO: to implement

    // Close the temporary obj of group (not file)

    switch(param->parent_type){
        case VL_GROUP:
            DEBUG_PRINT
            H5VLgroup_close(under_object_local, execute_ctx->under_vol_id, param->dxpl_id, NULL);
            break;

        case VL_DATASET:
            DEBUG_PRINT
            H5VLdataset_close(under_object_local, execute_ctx->under_vol_id, param->dxpl_id, NULL);
            break;

        case VL_NAMED_DATATYPE:
            DEBUG_PRINT
            H5VLdatatype_close(under_object_local, execute_ctx->under_vol_id, param->dxpl_id, NULL);
            break;

        default:
            DEBUG_PRINT
            //printf("%d:%s:%d: rank = %d, Unknown/unsupported parent type = %d\n", getpid(), __func__, __LINE__,
            //        MY_RANK_DEBUG, param->parent_type);
            assert(0 && "Wrong type: attr's parent obj type could only be GROUP/DATASET/NAMED_DATATYPE.");
            break;

    }
    free(param);
    //DEBUG_PRINT
    return 0;
}

int _ds_create_cb_sub(prop_ctx *execute_ctx, proposal* proposal) {
    param_ds_create* param = calloc(1, sizeof(param_ds_create));
    ds_create_decoder(proposal->proposal_data, param);
    DEBUG_PRINT
    //search local under_object by obj_id
    void* under_object_local;
    // Use under object type to call H5VLgroup_open or H5VLfile_open
    // with with the objno value (for a group), then use that temporary
    // under_object as the parameter for H5VLdataset_create
    if (param->parent_type == VL_GROUP) {
        //DEBUG_PRINT
        H5VL_loc_params_t under_loc_params;
        under_loc_params.obj_type = H5I_FILE;//H5I_GROUP;
        under_loc_params.type = H5VL_OBJECT_BY_ADDR;
        under_loc_params.loc_data.loc_by_addr.addr = param->parent_obj_addr;
        H5I_type_t opened_type = 0;
        under_object_local = H5VLobject_open(execute_ctx->under_file, &under_loc_params,
                execute_ctx->under_vol_id,
                &opened_type, //output: opened type
                param->dxpl_id,
                NULL); //req
    } else { //file
        DEBUG_PRINT
        //under_loc_params.obj_type =
        under_object_local = execute_ctx->under_file;
    }
    assert(under_object_local);
    DEBUG_PRINT
    //printf("%s:%d: rank = %d, CHECKING ORDER: ds_name = %s, time = %lu\n", __func__, __LINE__,
    //        MY_RANK_DEBUG, param->name, proposal->time);
    void* under_dataset_object = H5VLdataset_create(
            under_object_local, //local only
            param->loc_params, //need en/decoder
            execute_ctx->under_vol_id, //local only
            param->name, param->lcpl_id, param->type_id, param->space_id, param->dcpl_id, param->dapl_id,
            param->dxpl_id,
            NULL); // NULL for now
    DEBUG_PRINT
    //printf("%s:%d: rank = %d,returning ds(name = %s) obj = %p\n",  __func__, __LINE__,
    //        MY_RANK_DEBUG, param->name, under_dataset_object );
    assert(under_dataset_object);
    if (proposal->isLocal) { // TODO: how to set local flag? Maybe not part of the proposa?  Maybe part of the callback parameters from the progress engine
        DEBUG_PRINT
        execute_ctx->resulting_obj_out = under_dataset_object;
    } else {
        DEBUG_PRINT
        H5VLdataset_close(under_dataset_object, execute_ctx->under_vol_id, param->dxpl_id, NULL);
        DEBUG_PRINT
    }
    //DEBUG_PRINT
    ds_create_param_close(param);  // TODO: to implement

    // Close the temporary obj of group (not file)
    if (param->parent_type == VL_GROUP) {
        H5VLgroup_close(under_object_local, execute_ctx->under_vol_id, param->dxpl_id, NULL);
    }
    free(param);
    //DEBUG_PRINT
    return 0;
}

int _ds_extend_cb_sub(prop_ctx *execute_ctx, proposal* proposal)
{
    param_ds_extend* param;
    void* under_object_local;
    herr_t ret_value;

    param = calloc(1, sizeof(param_ds_extend));
    DEBUG_PRINT
    ds_extend_decoder(proposal->proposal_data, param);
    DEBUG_PRINT

    if(!proposal->isLocal){
        H5VL_loc_params_t under_loc_params;
        H5I_type_t opened_type = 0;

        /* Push a fresh HDF5 library state */
        H5VLpush_lib_state();

        under_loc_params.obj_type = H5I_FILE;
        under_loc_params.type = H5VL_OBJECT_BY_ADDR;
        under_loc_params.loc_data.loc_by_addr.addr = param->dset_addr;
        under_object_local = H5VLobject_open(execute_ctx->under_file, &under_loc_params,
                execute_ctx->under_vol_id,
                &opened_type, //output: opened type
                H5P_DEFAULT,
                NULL); //req
        assert(under_object_local);

        /* Restore the previous HDF5 library state */
        H5VLpop_lib_state();
    }else{
        under_object_local = execute_ctx->under_obj;
    }

    DEBUG_PRINT
    //printf("%d:%s:%d: rank = %d, CHECKING ORDER: ds_addr = %llu, time = %lu, new_size[0] = %llu, new_size[1] = %llu\n", getpid(), __func__, __LINE__,
    //        MY_RANK_DEBUG, (unsigned long long)param->dset_addr, proposal->time, param->new_size[0], param->new_size[1]);
    DEBUG_PRINT
    ret_value = ds_specific(under_object_local, execute_ctx->under_vol_id, H5VL_DATASET_SET_EXTENT, H5P_DEFAULT, NULL, param->new_size);
    DEBUG_PRINT

    ds_extend_param_close(param);
    DEBUG_PRINT
    // Close the temporary obj of dataset
    if(!proposal->isLocal){
        H5VLdataset_close(under_object_local, execute_ctx->under_vol_id, H5P_DEFAULT, NULL);
    }
    DEBUG_PRINT
    if(proposal->isLocal){
        return ret_value;
    }
    return 0;
}

int _group_create_cb_sub(prop_ctx* execute_ctx, proposal* proposal){
    param_group* param = calloc(1, sizeof(param_group));
    group_create_decoder(proposal->proposal_data, param);
    //
    void* under_object_local;
    // Use under object type to call H5VLgroup_open or H5VLfile_open
    // with with the objno value (for a group), then use that temporary
    // under_object as the parameter for H5VLdataset_create
    DEBUG_PRINT
    if (param->parent_type == VL_GROUP) {
        DEBUG_PRINT

        H5VL_loc_params_t under_loc_params;
        under_loc_params.obj_type = H5I_FILE;//H5I_GROUP;
        under_loc_params.type = H5VL_OBJECT_BY_ADDR;
        under_loc_params.loc_data.loc_by_addr.addr = param->parent_obj_addr;
        H5I_type_t opened_type = 0;
        under_object_local = H5VLobject_open(execute_ctx->under_file, &under_loc_params,
                execute_ctx->under_vol_id,
                &opened_type, //output: opened type
                param->dxpl_id,
                NULL); //req
    } else { //file
        DEBUG_PRINT
        under_object_local = execute_ctx->under_file;
    }
    assert(under_object_local);
    DEBUG_PRINT
    //printf("%d:%s:%d: rank = %d, CHECKING ORDER: group_name = %s, time = %lu\n", getpid(), __func__, __LINE__,
    //        MY_RANK_DEBUG, param->name, proposal->time);
    DEBUG_PRINT
    void* under_group_object = H5VLgroup_create(under_object_local, param->loc_params, execute_ctx->under_vol_id,
            param->name, param->lcpl_id, param->gcpl_id,  param->gapl_id, param->dxpl_id, NULL);
    assert(under_group_object);
    if (proposal->isLocal) { // TODO: how to set local flag? Maybe not part of the proposa?  Maybe part of the callback parameters from the progress engine
        //proposal->result_obj_local = under_dataset_object;
        //DEBUG_PRINT
        execute_ctx->resulting_obj_out = under_group_object;
    } else {
        DEBUG_PRINT
        H5VLgroup_close(under_group_object, execute_ctx->under_vol_id, param->dxpl_id, NULL);
    }

    if (param->parent_type == VL_GROUP) {
        H5VLgroup_close(under_object_local, execute_ctx->under_vol_id, param->dxpl_id, NULL);
    }
    free(param);
    return 0;
}

int cb_execute_H5VL_RLO( void* h5_ctx, void* proposal_buf)
{   //assert(0);
    //DEBUG_PRINT
    proposal* proposal = proposal_decoder(proposal_buf);
    //proposal_test(proposal);
    prop_ctx *execute_ctx = (prop_ctx *)h5_ctx;
    //DEBUG_PRINT
    //printf("%s:%d: test proposal_data len = %lu, pid = %d, op_type = %d, state = %d, time = %lu\n",
    //        __func__, __LINE__, proposal->p_data_len, proposal->pid, proposal->op_type, proposal->state, proposal->time);
    switch(proposal->op_type) {
        case FILE_CLOSE:
            DEBUG_PRINT
            _file_close_cb_sub(execute_ctx, proposal);
            break;

        case DS_CREATE:
            DEBUG_PRINT
            _ds_create_cb_sub(execute_ctx, proposal);
            break;

        case DS_EXTEND:
            DEBUG_PRINT
            _ds_extend_cb_sub(execute_ctx, proposal);
            break;

        case GROUP_CREATE:
            DEBUG_PRINT
            _group_create_cb_sub(execute_ctx, proposal);
            break;

        case ATTR_CREATE:
            DEBUG_PRINT
            _attr_create_cb_sub(execute_ctx, proposal);
            break;

        case ATTR_WRITE:
            DEBUG_PRINT
            _attr_write_cb_sub(execute_ctx, proposal);
            break;

        case DT_COMMIT:
            DEBUG_PRINT
            _dt_commit_cb_sub(execute_ctx, proposal);
            break;

        default:
            DEBUG_PRINT
            printf("%s:%d: Unknown op type for execution callback: proposal->op_type = %d\n", __func__,__LINE__, proposal->op_type);
            assert(0 && "Unknown op type for execution callback.");
            break;
    }
    return -1;
}

// As part of the "encode" for the 'under_object', if it's a group, retrieve its objno and encode that value
// along with its type (file or group)

void* t_encode(hid_t type_id, size_t* size){
    size_t buf_size = 0;
    void *buf = NULL;
    H5Tencode(type_id, NULL, &buf_size);//get buf size of this type
    buf = malloc(buf_size);
    H5Tencode(type_id, buf, &buf_size);// make an instance of this type?
    *size = buf_size;
    return buf;
    //hid_t new_typeId = H5Tdecode(buf);// get type id from an instance (in a buf) of this type
}

void* s_encode(hid_t space_id, size_t* size){
    size_t buf_size = 0;
    void *buf = NULL;
    H5Sencode(space_id, NULL, &buf_size, H5P_DEFAULT);//get buf size of this type
    buf = malloc(buf_size);
    H5Sencode(space_id, buf, &buf_size, H5P_DEFAULT);// make an instance of this type?
    *size = buf_size;
    return buf;
    //hid_t new_typeId = H5Tdecode(buf);// get type id from an instance (in a buf) of this type
}

void* p_encode(hid_t pl_id, size_t* size){
    size_t buf_size = 0;
    void *buf = NULL;
    H5Pencode(pl_id, NULL, &buf_size, H5P_DEFAULT);//get buf size of this type
    buf = malloc(buf_size);
    H5Pencode(pl_id, buf, &buf_size, H5P_DEFAULT);// make an instance of this type?
    *size = buf_size;
    return buf;
    //hid_t new_typeId = H5Tdecode(buf);// get type id from an instance (in a buf) of this type
}

// ===========================================================================
// ===========================================================================
// ===========================================================================


static metadata_manager *
metadata_helper_init(const H5VL_rlo_pass_through_info_t *info_in,
    prop_ctx *h5_app_ctx)
{
    metadata_manager *mm;       // Metadata manager for a file
    VotingPlugin *vp;
    vp_info_rlo *vp_info_in;
    VP_ctx* vp_ctx_out;

    mm = calloc(1, sizeof(metadata_manager));
    vp = VM_voting_plugin_new();//empty for now.
    vp_info_in = calloc(1, sizeof(vp_info_rlo));

    MPI_Comm_dup(info_in->mpi_comm, &(vp_info_in->mpi_comm));
    if(info_in->mpi_info != MPI_INFO_NULL)
        MPI_Info_dup(info_in->mpi_info, &(vp_info_in->mpi_info));
    else
        vp_info_in->mpi_info = MPI_INFO_NULL;
    MY_RANK_DEBUG = info_in->my_rank;
    vp_ctx_out = calloc(1, sizeof(vp_ctx_out));

    vp->vp_ctx_in = vp_info_in;
    vp->vp_init = &vp_init_RLO;//vp_init_RLO(&h5_judgement, h5_app_ctx, vp_info_in, &(vp_ctx_out->eng));
    vp->vp_make_progress = &vp_make_progress_RLO;
    vp->vp_check_my_proposal_state = &vp_check_my_proposal_state_RLO;
    vp->vp_checkout_proposal = &vp_checkout_proposal_RLO;
    vp->vp_finalize = &vp_finalize_RLO;
    vp->vp_rm_my_proposal = &vp_rm_my_proposal_RLO;
    vp->vp_submit_proposal = &vp_submit_proposal_RLO;
    vp->vp_submit_bcast = &vp_submit_bcast_RLO;

    //printf("%s:%d:mode = %d, world_size = %d, window size =  %d\n", __func__, __LINE__, info_in->mode, info_in->world_size, info_in->time_window_size);
    mm = MM_metadata_update_helper_init(info_in->mode, info_in->world_size,
            info_in->time_window_size, &h5_judgement, h5_app_ctx, vp, &cb_execute_H5VL_RLO);

    return mm;
}


/*-------------------------------------------------------------------------
 * Function:    prop_ctx_new
 *
 * Purpose:     Allocate and initialize a prop_ctx object
 *
 * Note:        This passes the partially initialized prop_ctx to the
 *              metadata manager, and then sets the metadata manager pointer
 *              in the prop_ctx with the returned value.   So, anything that
 *              the metadata manager needs to use in the prop_ctx must be
 *              initialized prior to that call.
 *
 * Return:      Success:    Pointer to new prop_ctx
 *              Failure:    NULL
 *
 * Programmer:  Quincey Koziol
 *              Sunday, September 29, 2019
 *
 *-------------------------------------------------------------------------
 */
static prop_ctx *
prop_ctx_new(void *under, const H5VL_rlo_pass_through_info_t *info, hbool_t is_collective)
{
    prop_ctx* h5_ctx;

    h5_ctx = calloc(1, sizeof(prop_ctx));
    h5_ctx->under_file = under;
    h5_ctx->under_vol_id = info->under_vol_id;
    h5_ctx->is_collective = is_collective;
    H5Iinc_ref(h5_ctx->under_vol_id);
    MPI_Comm_size(info->mpi_comm, &h5_ctx->comm_size);
    MPI_Comm_rank(info->mpi_comm, &h5_ctx->my_rank);
    h5_ctx->mm = metadata_helper_init(info, h5_ctx);

    return h5_ctx;
} /* end prop_ctx_new() */

/*-------------------------------------------------------------------------
 * Function:    prop_ctx_inc_rc
 *
 * Purpose:     Increment the refcount for a prop_ctx object
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 * Programmer:  Quincey Koziol
 *              Saturday, September 28, 2019
 *
 *-------------------------------------------------------------------------
 */
static int
prop_ctx_inc_rc(prop_ctx *p_ctx)
{
    assert(p_ctx);

    // Increment ref count
    p_ctx->ref_count++;

    return 0;
} /* end prop_ctx_inc_rc() */

/*-------------------------------------------------------------------------
 * Function:    prop_ctx_dec_rc
 *
 * Purpose:     Decrement the refcount for a prop_ctx object, freeing it
 *              when the refcount drops to zero
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 * Programmer:  Quincey Koziol
 *              Saturday, September 28, 2019
 *
 *-------------------------------------------------------------------------
 */
static int
prop_ctx_dec_rc(prop_ctx *p_ctx)
{
    assert(p_ctx);

    // Decrement refcount and free resources when it reaches 0
    p_ctx->ref_count--;
    if(0 == p_ctx->ref_count) {
        hid_t err_id;

        // Decrement count on underlying VOL connector
        // (Ignore error return from H5Idec_ref and suppress HDF5 error stack, for now)
        err_id = H5Eget_current_stack();
        H5Idec_ref(p_ctx->under_vol_id);
        H5Eset_current_stack(err_id);

        // Shut down metadata manager framework(s) for file
        MM_metadata_update_helper_term(p_ctx->mm);

        // Release prop_ctx
        free(p_ctx);
    }

    return 0;
} /* end prop_ctx_dec_rc() */

/*-------------------------------------------------------------------------
 * Function:    H5VL__pass_through_new_obj
 *
 * Purpose:     Create a new pass through object for an underlying object
 *
 * Return:      Success:    Pointer to the new pass through object
 *              Failure:    NULL
 *
 * Programmer:  Quincey Koziol
 *              Monday, December 3, 2018
 *
 *-------------------------------------------------------------------------
 */
static H5VL_rlo_pass_through_t *
H5VL_rlo_pass_through_new_obj(void *under_obj, rlo_obj_type_t obj_type,
    prop_ctx *p_ctx)
{
    H5VL_rlo_pass_through_t *new_obj;

    assert(p_ctx);

    // Allocate and initialize specific object info
    new_obj = (H5VL_rlo_pass_through_t *)calloc(1, sizeof(H5VL_rlo_pass_through_t));
    new_obj->under_object = under_obj;
    new_obj->obj_type = obj_type;

    /* Share the file's context info */
    new_obj->p_ctx = p_ctx;
    prop_ctx_inc_rc(p_ctx);

    DEBUG_PRINT

    return new_obj;
} /* end H5VL__pass_through_new_obj() */


/*-------------------------------------------------------------------------
 * Function:    H5VL__pass_through_free_obj
 *
 * Purpose:     Release a pass through object
 *
 * Note:	Take care to preserve the current HDF5 error stack
 *		when calling HDF5 API calls.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 * Programmer:  Quincey Koziol
 *              Monday, December 3, 2018
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_free_obj(H5VL_rlo_pass_through_t *obj)
{
    DEBUG_PRINT

    assert(obj->p_ctx);

    // Decrement count on shared context
    prop_ctx_dec_rc(obj->p_ctx);

    obj->p_ctx = NULL;
    free(obj);

    return 0;
} /* end H5VL__pass_through_free_obj() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_register
 *
 * Purpose:     Register the pass-through VOL connector and retrieve an ID
 *              for it.
 *
 * Return:      Success:    The ID for the pass-through VOL connector
 *              Failure:    -1
 *
 * Programmer:  Quincey Koziol
 *              Wednesday, November 28, 2018
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5VL_rlo_pass_through_register(void)
{
    /* Singleton register the pass-through VOL connector ID */
    if(H5VL_RLO_PASSTHRU_g < 0)
        H5VL_RLO_PASSTHRU_g = H5VLregister_connector(&H5VL_rlo_pass_through_g, H5P_DEFAULT);

    return H5VL_RLO_PASSTHRU_g;
} /* end H5VL_rlo_pass_through_register() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_init
 *
 * Purpose:     Initialize this VOL connector, performing any necessary
 *              operations for the connector that will apply to all containers
 *              accessed with the connector.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_init(hid_t __attribute__((unused)) vipl_id)
{
#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL INIT\n");
#endif

    DEBUG_PRINT

    return 0;
} /* end H5VL_rlo_pass_through_init() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_term
 *
 * Purpose:     Terminate this VOL connector, performing any necessary
 *              operations for the connector that release connector-wide
 *              resources (usually created / initialized with the 'init'
 *              callback).
 *
 * Return:      Success:    0
 *              Failure:    (Can't fail)
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_term(void)
{
#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL TERM\n");
#endif

    DEBUG_PRINT

    /* Reset VOL ID */
    H5VL_RLO_PASSTHRU_g = H5I_INVALID_HID;

    DEBUG_PRINT

    return 0;
} /* end H5VL_rlo_pass_through_term() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_info_copy
 *
 * Purpose:     Duplicate the connector's info object.
 *
 * Returns:     Success:    New connector info object
 *              Failure:    NULL
 *
 *---------------------------------------------------------------------------
 */
static void *
H5VL_rlo_pass_through_info_copy(const void *_info)
{
    const H5VL_rlo_pass_through_info_t *info = (const H5VL_rlo_pass_through_info_t *)_info;
    H5VL_rlo_pass_through_info_t *new_info;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL INFO Copy\n");
#endif

    /* Allocate new VOL info struct for the pass through connector */
    new_info = (H5VL_rlo_pass_through_info_t *)calloc(1, sizeof(H5VL_rlo_pass_through_info_t));

    /* Increment reference count on underlying VOL ID, and copy the VOL info */
    new_info->under_vol_id = info->under_vol_id;
    //printf("H5VL_rlo_pass_through_info_copy: info->under_vol_id = %llx\n", info->under_vol_id);
    //sleep(3);
    MPI_Comm_dup(info->mpi_comm, &(new_info->mpi_comm));

    if(info->mpi_info == MPI_INFO_NULL)
        new_info->mpi_info = MPI_INFO_NULL;
    else
        MPI_Info_dup(info->mpi_info, &(new_info->mpi_info));

    H5Iinc_ref(new_info->under_vol_id);
    if(info->under_vol_info)
        H5VLcopy_connector_info(new_info->under_vol_id, &(new_info->under_vol_info), info->under_vol_info);

    new_info->time_window_size = info->time_window_size;
    new_info->mode = info->mode;
    new_info->world_size = info->world_size;
    new_info->my_rank = info->my_rank;
    return new_info;
} /* end H5VL_rlo_pass_through_info_copy() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_info_cmp
 *
 * Purpose:     Compare two of the connector's info objects, setting *cmp_value,
 *              following the same rules as strcmp().
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_info_cmp(int *cmp_value, const void *_info1, const void *_info2)
{
    const H5VL_rlo_pass_through_info_t *info1 = (const H5VL_rlo_pass_through_info_t *)_info1;
    const H5VL_rlo_pass_through_info_t *info2 = (const H5VL_rlo_pass_through_info_t *)_info2;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL INFO Compare\n");
#endif

    /* Sanity checks */
    assert(info1);
    assert(info2);

    /* Initialize comparison value */
    *cmp_value = 0;

    /* Compare under VOL connector classes */
    H5VLcmp_connector_cls(cmp_value, info1->under_vol_id, info2->under_vol_id);
    if(*cmp_value != 0)
        return 0;

    /* Compare under VOL connector info objects */
    H5VLcmp_connector_info(cmp_value, info1->under_vol_id, info1->under_vol_info, info2->under_vol_info);
    if(*cmp_value != 0)
        return 0;

    return 0;
} /* end H5VL_rlo_pass_through_info_cmp() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_info_free
 *
 * Purpose:     Release an info object for the connector.
 *
 * Note:	Take care to preserve the current HDF5 error stack
 *		when calling HDF5 API calls.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_info_free(void *_info)
{
    H5VL_rlo_pass_through_info_t *info = (H5VL_rlo_pass_through_info_t *)_info;
    hid_t err_id;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL INFO Free\n");
#endif

    err_id = H5Eget_current_stack();

    /* Release underlying VOL ID and info */
    if(info->under_vol_info)
        H5VLfree_connector_info(info->under_vol_id, info->under_vol_info);
    H5Idec_ref(info->under_vol_id);

    if(info->mpi_info != MPI_INFO_NULL)
        MPI_Info_free(&(info->mpi_info));

    H5Eset_current_stack(err_id);

    /* Free pass through info object itself */
    free(info);

    return 0;
} /* end H5VL_rlo_pass_through_info_free() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_info_to_str
 *
 * Purpose:     Serialize an info object for this connector into a string
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_info_to_str(const void *_info, char **str)
{
    const H5VL_rlo_pass_through_info_t *info = (const H5VL_rlo_pass_through_info_t *)_info;
    H5VL_class_value_t under_value = (H5VL_class_value_t)-1;
    char *under_vol_string = NULL;
    size_t under_vol_str_len = 0;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL INFO To String\n");
#endif

    /* Get value and string for underlying VOL connector */
    H5VLget_value(info->under_vol_id, &under_value);
    H5VLconnector_info_to_str(info->under_vol_info, info->under_vol_id, &under_vol_string);

    /* Determine length of underlying VOL info string */
    if(under_vol_string)
        under_vol_str_len = strlen(under_vol_string);

    /* Allocate space for our info */
    *str = (char *)H5allocate_memory(32 + under_vol_str_len, (hbool_t)0);
    assert(*str);

    /* Encode our info
     * Normally we'd use snprintf() here for a little extra safety, but that
     * call had problems on Windows until recently. So, to be as platform-independent
     * as we can, we're using sprintf() instead.
     */
    sprintf(*str, "under_vol=%u;under_info={%s}", (unsigned)under_value, (under_vol_string ? under_vol_string : ""));

    return 0;
} /* end H5VL_rlo_pass_through_info_to_str() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_str_to_info
 *
 * Purpose:     Deserialize a string into an info object for this connector.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_str_to_info(const char *str, void **_info)
{
    H5VL_rlo_pass_through_info_t *info;
    unsigned under_vol_value;
    const char *under_vol_info_start, *under_vol_info_end;
    hid_t under_vol_id;
    void *under_vol_info = NULL;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL INFO String To Info\n");
#endif

printf("%s: str = '%s'\n", __func__, str);
    /* Retrieve the underlying VOL connector value and info */
    sscanf(str, "under_vol=%u;", &under_vol_value);
    under_vol_id = H5VLregister_connector_by_value((H5VL_class_value_t)under_vol_value, H5P_DEFAULT);
    under_vol_info_start = strchr(str, '{');
    under_vol_info_end = strrchr(str, '}');
    assert(under_vol_info_end > under_vol_info_start);
    if(under_vol_info_end != (under_vol_info_start + 1)) {
        char *under_vol_info_str;

        under_vol_info_str = (char *)malloc((size_t)(under_vol_info_end - under_vol_info_start));
        memcpy(under_vol_info_str, under_vol_info_start + 1, (size_t)((under_vol_info_end - under_vol_info_start) - 1));
        *(under_vol_info_str + (under_vol_info_end - under_vol_info_start)) = '\0';

        H5VLconnector_str_to_info(under_vol_info_str, under_vol_id, &under_vol_info);

        free(under_vol_info_str);
    } /* end else */

    /* Allocate new pass-through VOL connector info and set its fields */
    info = (H5VL_rlo_pass_through_info_t *)calloc(1, sizeof(H5VL_rlo_pass_through_info_t));
    info->under_vol_id = under_vol_id;
    info->under_vol_info = under_vol_info;

    /* Set return value */
    *_info = info;

    return 0;
} /* end H5VL_rlo_pass_through_str_to_info() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_get_object
 *
 * Purpose:     Retrieve the 'data' for a VOL object.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static void *
H5VL_rlo_pass_through_get_object(const void *obj)
{
    const H5VL_rlo_pass_through_t *o = (const H5VL_rlo_pass_through_t *)obj;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL Get object\n");
#endif

    return H5VLget_object(o->under_object, o->p_ctx->under_vol_id);
} /* end H5VL_rlo_pass_through_get_object() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_get_wrap_ctx
 *
 * Purpose:     Retrieve a "wrapper context" for an object
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */

//VOL framework calls this routine
static herr_t
H5VL_rlo_pass_through_get_wrap_ctx(const void *obj, void **wrap_ctx)
{
    //obj: a valid "A envelope"
    const H5VL_rlo_pass_through_t *o = (const H5VL_rlo_pass_through_t *)obj;
    H5VL_rlo_pass_through_wrap_ctx_t *new_wrap_ctx;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL WRAP CTX Get\n");
#endif

    /* Allocate new VOL object wrapping context for the pass through connector */
    new_wrap_ctx = (H5VL_rlo_pass_through_wrap_ctx_t *)calloc(1, sizeof(H5VL_rlo_pass_through_wrap_ctx_t));

    /* Get pointer to this file's execution context, and increment its refcount */
    new_wrap_ctx->p_ctx = o->p_ctx;
    new_wrap_ctx->p_ctx->ref_count++;

    /* Get wrap context for underlying VOL connector */
    H5VLget_wrap_ctx(o->under_object, o->p_ctx->under_vol_id, &new_wrap_ctx->under_wrap_ctx);

    /* Set wrap context to return */
    *wrap_ctx = new_wrap_ctx;

    return 0;
} /* end H5VL_rlo_pass_through_get_wrap_ctx() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_wrap_object
 *
 * Purpose:     Use a "wrapper context" to wrap a data object
 *
 * Return:      Success:    Pointer to wrapped object
 *              Failure:    NULL
 *
 *---------------------------------------------------------------------------
 */
static void *
H5VL_rlo_pass_through_wrap_object(void *obj, H5I_type_t obj_type, void *_wrap_ctx)
{
    H5VL_rlo_pass_through_wrap_ctx_t *wrap_ctx = (H5VL_rlo_pass_through_wrap_ctx_t *)_wrap_ctx;
    H5VL_rlo_pass_through_t *new_obj;
    void *under;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL WRAP Object\n");
#endif

    //_wrap_ctx: manual for making A envelop.
    /* Wrap the object with the underlying VOL */
    // Assuming VOL stack: A B C D; A is top VOL and D is terminal.


    // obj: type D.

    // Now I'm A, and I call this H5VL_provenance_wrap_object().
    //      - meaning A tries to pass an obj to B
    // A knows nothing abnout the under layer VOL, which is B.
    // so it get to know that from wrap_ctx, which include under_vol_id_B,
    // and a wrap_ctx (under_wrap_ctx) that specify the layer under B, such as C.
    under = H5VLwrap_object(obj, obj_type, wrap_ctx->p_ctx->under_vol_id, wrap_ctx->under_wrap_ctx);


    // A ask a B envelop from B, info of B is in wrap_ctx
    // then, the returned "under" is a B envelop filled with A object, thus can be understood (and given to) by B.

    if(under){
        rlo_obj_type_t rlo_type;
        switch(obj_type){
            case H5I_FILE:
                rlo_type = VL_FILE;
                break;
            case H5I_GROUP:
                rlo_type = VL_GROUP;
                break;
            case H5I_DATATYPE:
                rlo_type = VL_NAMED_DATATYPE;
                break;
            case H5I_DATASET:
                rlo_type = VL_DATASET;
                break;
            case H5I_ATTR:
                rlo_type = VL_ATTRIBUTES;
                break;

            default:
                assert(0 && "Unknown object type");
                break;
        }

        // TODO: to confirm:
        //  - Then what's new_obj (A envelop) here, for whom?

        //Not for B, because under is the one for B.
        //      looks like this new_obj is to make a generic envelop (G) to hold "under",
        //          -
        //      and the frame work knows where to deliver the envelop G.
        //      And the "new_obj" is the envelop G that B will receive.
        //      optional: then when to use unwrap?
        //  - so this wrap_objct() is the only place to prepare(pack) things for other VOLs.

        //  - And unwrap() is the only place to unpack packages from other VOLs.

        /*  wrap_ctx->under_vol_id is under_vol_id_B  */

        new_obj = H5VL_rlo_pass_through_new_obj(under, rlo_type, wrap_ctx->p_ctx);

    }
    else
        new_obj = NULL;

    return new_obj; // A envelop
} /* end H5VL_rlo_pass_through_wrap_object() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_unwrap_object
 *
 * Purpose:     Unwrap a wrapped object, discarding the wrapper, but returning
 *		underlying object.
 *
 * Return:      Success:    Pointer to unwrapped object
 *              Failure:    NULL
 *
 *---------------------------------------------------------------------------
 */
static void *
H5VL_rlo_pass_through_unwrap_object(void *obj)
{

    //  the input obj is a generic envelop G,
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    void *under;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL UNWRAP Object\n");
#endif

    DEBUG_PRINT

    /* Unrap the object with the underlying VOL */
    under = H5VLunwrap_object(o->under_object, o->p_ctx->under_vol_id);

    if(under)
        H5VL_rlo_pass_through_free_obj(o);

    DEBUG_PRINT

    return under;
} /* end H5VL_rlo_pass_through_unwrap_object() */


/*---------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_free_wrap_ctx
 *
 * Purpose:     Release a "wrapper context" for an object
 *
 * Note:	Take care to preserve the current HDF5 error stack
 *		when calling HDF5 API calls.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_free_wrap_ctx(void *_wrap_ctx)
{
    H5VL_rlo_pass_through_wrap_ctx_t *wrap_ctx = (H5VL_rlo_pass_through_wrap_ctx_t *)_wrap_ctx;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL WRAP CTX Free\n");
#endif

    /* Release underlying VOL ID and wrap context */
    if(wrap_ctx->under_wrap_ctx)
        H5VLfree_wrap_ctx(wrap_ctx->under_wrap_ctx, wrap_ctx->p_ctx->under_vol_id);

    // Decrement refcount on prop_ctx
    prop_ctx_dec_rc(wrap_ctx->p_ctx);

    /* Free pass through wrap context object itself */
    free(wrap_ctx);

    return 0;
} /* end H5VL_rlo_pass_through_free_wrap_ctx() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_attr_create
 *
 * Purpose:     Creates an attribute on an object.
 *
 * Return:      Success:    Pointer to attribute object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_rlo_pass_through_attr_create(void *obj, const H5VL_loc_params_t *loc_params,
    const char *name, hid_t type_id, hid_t space_id, hid_t acpl_id,
    hid_t aapl_id, hid_t dxpl_id, void **req)
{
    H5VL_rlo_pass_through_t *attr;
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL ATTRIBUTE Create\n");
#endif

    param_attr param_in;

    param_in.space_id = space_id;
    param_in.type_id = type_id;
    param_in.acpl_id = acpl_id;
    param_in.aapl_id = aapl_id;

    param_in.dxpl_id = dxpl_id;

    param_in.name_size = strlen(name) + 1;
    param_in.loc_param_size = 0; // will be set in encoding function.

    param_in.parent_type = loc_params->obj_type;
    param_in.loc_params = (H5VL_loc_params_t*)loc_params;
    param_in.name = (char*)name;

    //Get parent object native id/addr
    H5VL_loc_params_t param_tmp;
    H5O_info_t oinfo;
    param_tmp.type = H5VL_OBJECT_BY_SELF;
    param_tmp.obj_type = loc_params->obj_type;
    get_native_info(o->under_object, o->p_ctx->under_vol_id, dxpl_id, NULL,
            H5VL_NATIVE_OBJECT_GET_INFO, &param_tmp, &oinfo, H5O_INFO_BASIC);
    param_in.parent_obj_addr = oinfo.addr;

    switch(loc_params->obj_type){//parent type
        case H5I_FILE:
            param_in.parent_type = VL_FILE;
            break;
        case H5I_GROUP:
            param_in.parent_type = VL_GROUP;
            break;
        case H5I_DATASET:
            param_in.parent_type = VL_DATASET;
            break;
        case H5I_DATATYPE:
            param_in.parent_type = VL_NAMED_DATATYPE;
            break;
        default:
            assert(0 && "Wrong type: Parent obj type could only be FILE or GROUP.");
            break;
    }

    //DEBUG_PRINT
    void* proposal_data = NULL;
    size_t p_data_size = attr_create_encoder(&param_in, &proposal_data);
    DEBUG_PRINT
    //prop_param_attr_create_test(&param_in);

    // param_attr t;
    // attr_create_decoder(proposal_data, &t);
    // prop_param_attr_create_test(&t);

    proposal_id pid = MY_RANK_DEBUG;
    proposal* p = compose_proposal(pid, ATTR_CREATE, proposal_data, p_data_size);//

    assert(o->p_ctx);
    assert(o->p_ctx->mm);
    ((prop_ctx*)(o->p_ctx->mm->app_ctx))->resulting_obj_out = NULL;
    int ret = MM_submit_proposal(o->p_ctx->mm, p);

    if(ret == 1)
        p->result_obj_local = ((prop_ctx*)(o->p_ctx->mm->app_ctx))->resulting_obj_out;
    //DEBUG_PRINT

    if(p->result_obj_local) {
        DEBUG_PRINT
        attr = H5VL_rlo_pass_through_new_obj(p->result_obj_local, VL_ATTRIBUTES, o->p_ctx);
    } /* end if */
    else
        attr = NULL;

    return (void*)attr;
} /* end H5VL_rlo_pass_through_attr_create() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_attr_open
 *
 * Purpose:     Opens an attribute on an object.
 *
 * Return:      Success:    Pointer to attribute object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_rlo_pass_through_attr_open(void *obj, const H5VL_loc_params_t *loc_params,
    const char *name, hid_t aapl_id, hid_t dxpl_id, void **req)
{
    H5VL_rlo_pass_through_t *attr;
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    void *under;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL ATTRIBUTE Open\n");
#endif

    under = H5VLattr_open(o->under_object, loc_params, o->p_ctx->under_vol_id, name, aapl_id, dxpl_id, req);
    if(under)
        attr = H5VL_rlo_pass_through_new_obj(under, VL_ATTRIBUTES, o->p_ctx);
    else
        attr = NULL;

    return (void *)attr;
} /* end H5VL_rlo_pass_through_attr_open() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_attr_read
 *
 * Purpose:     Reads data from attribute.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_attr_read(void *attr, hid_t mem_type_id, void *buf,
    hid_t dxpl_id, void **req)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)attr;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL ATTRIBUTE Read\n");
#endif

    ret_value = H5VLattr_read(o->under_object, o->p_ctx->under_vol_id, mem_type_id, buf, dxpl_id, req);

    return ret_value;
} /* end H5VL_rlo_pass_through_attr_read() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_attr_write
 *
 * Purpose:     Writes data to attribute.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_attr_write(void *attr, hid_t mem_type_id, const void *buf,
    hid_t dxpl_id, void **req)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)attr;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL ATTRIBUTE Write\n");
#endif
    H5O_info_t oinfo;
    hid_t space_id;
    H5VL_loc_params_t param_tmp;

    herr_t status;
    int no_elem = 0;

    param_attr_wr* param_in = calloc(1, sizeof(param_attr_wr));

    param_in->mem_type_id = mem_type_id;
    param_in->dxpl_id = dxpl_id;

    //parent_obj type and addr
    param_tmp.type = H5VL_OBJECT_BY_SELF;
    param_tmp.obj_type = H5I_ATTR;
    get_native_info(o->under_object, o->p_ctx->under_vol_id, H5P_DEFAULT, NULL,
            H5VL_NATIVE_OBJECT_GET_INFO, &param_tmp, &oinfo, H5O_INFO_BASIC);
    param_in->parent_obj_addr = oinfo.addr;

    switch(oinfo.type){
        case H5O_TYPE_GROUP:
            param_in->parent_type = VL_GROUP;
            break;
        case H5O_TYPE_NAMED_DATATYPE:
            param_in->parent_type = VL_NAMED_DATATYPE;
            break;
        case H5O_TYPE_DATASET:
            param_in->parent_type = VL_DATASET;
            break;

        default:
            printf("%s:%u, Unknown type = %d\n", __func__, __LINE__, oinfo.type);
            assert(0 && "Unknown object type");
            break;
    }



        //printf("%s:%d: getpid() = %d, my_rank = %d, checking void* buf = %d\n", __func__, __LINE__, getpid(), o->p_ctx->my_rank, *(int*)buf);


    //attr_name and size
    char* attr_name = attr_get_name(o->under_object, o->p_ctx->under_vol_id);
    param_in->attr_name_size = strlen(attr_name) + 1;
    param_in->attr_name = attr_name;

    //printf("Verifying attr_name: attr_name = [%s], param_in->attr_name = [%s]\n", attr_name, param_in->attr_name);

    //calculate buf size
    status = attr_get(o->under_object, o->p_ctx->under_vol_id, H5VL_ATTR_GET_SPACE, H5P_DEFAULT, NULL, &space_id);

    no_elem = H5Sget_simple_extent_npoints(space_id);
    status = H5Sclose(space_id);
    param_in->buf_size = no_elem * H5Tget_size(mem_type_id);
    param_in->buf = (void*)buf;
    //printf("%d:%s:%d: no_elem = %d, buf_size = %lu\n", MY_RANK_DEBUG, __func__, __LINE__, no_elem, param_in->buf_size);


    //composing proposal
    void* attr_param_data = NULL;
    size_t proposal_size = attr_write_encoder(param_in, &attr_param_data);
    proposal_id pid = getpid();//MY_RANK_DEBUG;//getpid();
    proposal* p = compose_proposal(pid, ATTR_WRITE, attr_param_data, proposal_size);//

    assert(o->p_ctx);
    assert(o->p_ctx->mm);

    ((prop_ctx*)(o->p_ctx->mm->app_ctx))->resulting_obj_out = NULL;

    ret_value = MM_submit_proposal(o->p_ctx->mm, p);
    return ret_value;
} /* end H5VL_rlo_pass_through_attr_write() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_attr_get
 *
 * Purpose:     Gets information about an attribute
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_attr_get(void *obj, H5VL_attr_get_t get_type, hid_t dxpl_id,
    void **req, va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL ATTRIBUTE Get\n");
#endif

    ret_value = H5VLattr_get(o->under_object, o->p_ctx->under_vol_id, get_type, dxpl_id, req, arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_attr_get() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_attr_specific
 *
 * Purpose:     Specific operation on attribute
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_attr_specific(void *obj, const H5VL_loc_params_t *loc_params,
    H5VL_attr_specific_t specific_type, hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL ATTRIBUTE Specific\n");
#endif

    ret_value = H5VLattr_specific(o->under_object, loc_params, o->p_ctx->under_vol_id, specific_type, dxpl_id, req, arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_attr_specific() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_attr_optional
 *
 * Purpose:     Perform a connector-specific operation on an attribute
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_attr_optional(void *obj, hid_t dxpl_id, void **req,
    va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL ATTRIBUTE Optional\n");
#endif

    ret_value = H5VLattr_optional(o->under_object, o->p_ctx->under_vol_id, dxpl_id, req, arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_attr_optional() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_attr_close
 *
 * Purpose:     Closes an attribute.
 *
 * Return:      Success:    0
 *              Failure:    -1, attr not closed.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_attr_close(void *attr, hid_t dxpl_id, void **req)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)attr;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL ATTRIBUTE Close\n");
#endif

    ret_value = H5VLattr_close(o->under_object, o->p_ctx->under_vol_id, dxpl_id, req);

    /* Release our wrapper, if underlying attribute was closed */
    if(ret_value >= 0)
        H5VL_rlo_pass_through_free_obj(o);

    return ret_value;
} /* end H5VL_rlo_pass_through_attr_close() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_dataset_create
 *
 * Purpose:     Creates a dataset in a container
 *
 * Return:      Success:    Pointer to a dataset object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */

static void *
H5VL_rlo_pass_through_dataset_create(void *obj, const H5VL_loc_params_t *loc_params,
    const char *name, hid_t lcpl_id, hid_t type_id, hid_t space_id,
    hid_t dcpl_id, hid_t dapl_id, hid_t dxpl_id, void **req)
{
    H5VL_rlo_pass_through_t *dset;
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL DATASET Create\n");
#endif

    //DEBUG_PRINT

    //loc_param_test((H5VL_loc_params_t *)loc_params);
    //printf("\n\n");
    //loc_params->loc_data.

    param_ds_create param_in;

    //H5Tencode()
    param_in.type_id = type_id;
    //H5Sencode()
    param_in.space_id = space_id;

    //H5Pencode()
    param_in.lcpl_id = lcpl_id;
    param_in.dcpl_id = dcpl_id;
    param_in.dapl_id = dapl_id;
    param_in.dxpl_id = dxpl_id;

    param_in.name_size = strlen(name) + 1;
    param_in.loc_param_size = 0; // will be set in encoding function.

    param_in.loc_params = (H5VL_loc_params_t*)loc_params;
    param_in.name = (char*)name;

    //Get parent object native id/addr
    H5VL_loc_params_t param_tmp;
    H5O_info_t oinfo;
    param_tmp.type = H5VL_OBJECT_BY_SELF;
    param_tmp.obj_type = loc_params->obj_type;
    get_native_info(o->under_object, o->p_ctx->under_vol_id, dxpl_id, NULL, H5VL_NATIVE_OBJECT_GET_INFO, &param_tmp, &oinfo, H5O_INFO_BASIC);
    param_in.parent_obj_addr = oinfo.addr;

    switch(loc_params->obj_type){//parent type
        case H5I_FILE:
            param_in.parent_type = VL_FILE;
            break;
        case H5I_GROUP:
            param_in.parent_type = VL_GROUP;
            break;
        default:
            assert(0 && "Wrong type: Parent obj type could only be FILE or GROUP.");
            break;
    }
    //DEBUG_PRINT
    void* proposal_data = NULL;

    size_t p_data_size = ds_create_encoder(&param_in, &proposal_data);

    DEBUG_PRINT

    proposal_id pid = MY_RANK_DEBUG;//getpid();
    proposal* p = compose_proposal(pid, DS_CREATE, proposal_data, p_data_size);
    //printf("%s:%d: Original Proposal pid = %d, p_data_len = %lu\n", __func__, __LINE__, p->pid, p->p_data_len);
    assert(o->p_ctx);
    //DEBUG_PRINT
    assert(o->p_ctx->mm);

    ((prop_ctx*)(o->p_ctx->mm->app_ctx))->resulting_obj_out = NULL;
    DEBUG_PRINT
    int ret = MM_submit_proposal(o->p_ctx->mm, p);
    DEBUG_PRINT
    if(ret == 1){
        DEBUG_PRINT
        p->result_obj_local = ((prop_ctx*)(o->p_ctx->mm->app_ctx))->resulting_obj_out;
        //printf("%s:%d: resulting obj = %p\n", __func__, __LINE__, p->result_obj_local);
    }else{
        printf("%s:%d: ret = %d\n", __func__, __LINE__, ret);
    }
    DEBUG_PRINT

    if(p->result_obj_local) {
        DEBUG_PRINT
        dset = H5VL_rlo_pass_through_new_obj(p->result_obj_local, VL_DATASET, o->p_ctx);
    } /* end if */
    else
        dset = NULL;
    DEBUG_PRINT
    return (void *)dset;
} /* end H5VL_rlo_pass_through_dataset_create() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_dataset_open
 *
 * Purpose:     Opens a dataset in a container
 *
 * Return:      Success:    Pointer to a dataset object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_rlo_pass_through_dataset_open(void *obj, const H5VL_loc_params_t *loc_params,
    const char *name, hid_t dapl_id, hid_t dxpl_id, void **req)
{
    H5VL_rlo_pass_through_t *dset;
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    void *under;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL DATASET Open\n");
#endif

    under = H5VLdataset_open(o->under_object, loc_params, o->p_ctx->under_vol_id, name, dapl_id, dxpl_id, req);
    if(under)
        dset = H5VL_rlo_pass_through_new_obj(under, VL_DATASET, o->p_ctx);
    else
        dset = NULL;

    return (void *)dset;
} /* end H5VL_rlo_pass_through_dataset_open() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_dataset_read
 *
 * Purpose:     Reads data elements from a dataset into a buffer.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_dataset_read(void *dset, hid_t mem_type_id, hid_t mem_space_id,
    hid_t file_space_id, hid_t plist_id, void *buf, void **req)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)dset;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL DATASET Read\n");
#endif

    ret_value = H5VLdataset_read(o->under_object, o->p_ctx->under_vol_id, mem_type_id, mem_space_id, file_space_id, plist_id, buf, req);

    return ret_value;
} /* end H5VL_rlo_pass_through_dataset_read() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_dataset_write
 *
 * Purpose:     Writes data elements from a buffer into a dataset.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_dataset_write(void *dset, hid_t mem_type_id, hid_t mem_space_id,
    hid_t file_space_id, hid_t plist_id, const void *buf, void **req)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)dset;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL DATASET Write\n");
#endif

    ret_value = H5VLdataset_write(o->under_object, o->p_ctx->under_vol_id, mem_type_id, mem_space_id, file_space_id, plist_id, buf, req);

    return ret_value;
} /* end H5VL_rlo_pass_through_dataset_write() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_dataset_get
 *
 * Purpose:     Gets information about a dataset
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_dataset_get(void *dset, H5VL_dataset_get_t get_type,
    hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)dset;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL DATASET Get\n");
#endif

    ret_value = H5VLdataset_get(o->under_object, o->p_ctx->under_vol_id, get_type, dxpl_id, req, arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_dataset_get() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_dataset_specific
 *
 * Purpose:     Specific operation on a dataset
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_dataset_specific(void *obj, H5VL_dataset_specific_t specific_type,
    hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL H5Dspecific\n");
#endif
    DEBUG_PRINT
    /* Different actions depending on the type of specific operation */
    switch(specific_type) {
        case H5VL_DATASET_SET_EXTENT:
            {
                hsize_t *new_size;

                /* Get the arguments for the 'set_extent' check */
                new_size = va_arg(arguments, hsize_t *);

{
printf("%u:%s:%u - new_size = [%llu, %llu]\n",
MY_RANK_DEBUG, __func__, __LINE__, (unsigned long long)new_size[0],
        (unsigned long long)new_size[1]);
}

                void* proposal_data = NULL;
                size_t p_data_size = ds_extend_encoder(o->under_object, o->p_ctx->under_vol_id, new_size, &proposal_data);
                DEBUG_PRINT

                proposal_id pid = MY_RANK_DEBUG;//getpid();
                proposal* p = compose_proposal(pid, DS_EXTEND, proposal_data, p_data_size);
                o->p_ctx->under_obj = o->under_object;
                assert(o->p_ctx);
                assert(o->p_ctx->mm);

                DEBUG_PRINT
                ret_value = MM_submit_proposal(o->p_ctx->mm, p);
                DEBUG_PRINT

                // assert(0 && "Yay!");
            }
            break;

        default:
            assert(0 && "Not supported");
            break;
    } /* end switch */

    // ret_value = H5VLdataset_specific(o->under_object, o->p_ctx->under_vol_id, specific_type, dxpl_id, req, arguments);
    return ret_value;
} /* end H5VL_rlo_pass_through_dataset_specific() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_dataset_optional
 *
 * Purpose:     Perform a connector-specific operation on a dataset
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_dataset_optional(void *obj, hid_t dxpl_id, void **req,
    va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL DATASET Optional\n");
#endif

    ret_value = H5VLdataset_optional(o->under_object, o->p_ctx->under_vol_id, dxpl_id, req, arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_dataset_optional() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_dataset_close
 *
 * Purpose:     Closes a dataset.
 *
 * Return:      Success:    0
 *              Failure:    -1, dataset not closed.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_dataset_close(void *dset, hid_t dxpl_id, void **req)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)dset;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL DATASET Close\n");
#endif

    DEBUG_PRINT
    ret_value = H5VLdataset_close(o->under_object, o->p_ctx->under_vol_id, dxpl_id, req);

    /* Release our wrapper, if underlying dataset was closed */
    if(ret_value >= 0)
        H5VL_rlo_pass_through_free_obj(o);

    return ret_value;
} /* end H5VL_rlo_pass_through_dataset_close() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_datatype_commit
 *
 * Purpose:     Commits a datatype inside a container.
 *
 * Return:      Success:    Pointer to datatype object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */


static void *
H5VL_rlo_pass_through_datatype_commit(void *obj, const H5VL_loc_params_t *loc_params,
    const char *name, hid_t type_id, hid_t lcpl_id, hid_t tcpl_id, hid_t tapl_id,
    hid_t dxpl_id, void **req)
{
    H5VL_rlo_pass_through_t *dt;
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    void *under;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL DATATYPE Commit\n");
#endif
    param_dt_commit param_in;

    param_in.type_id = type_id;

    param_in.lcpl_id = lcpl_id;
    param_in.tcpl_id = tcpl_id;
    param_in.tapl_id = tapl_id;
    param_in.dxpl_id = dxpl_id;

    //by self, identical with ds_create
    //Get parent object native id/addr
    H5VL_loc_params_t param_tmp;
    H5O_info_t oinfo;
    param_tmp.type = H5VL_OBJECT_BY_SELF;
    param_tmp.obj_type = loc_params->obj_type;
    get_native_info(o->under_object, o->p_ctx->under_vol_id, dxpl_id, NULL, H5VL_NATIVE_OBJECT_GET_INFO, &param_tmp, &oinfo, H5O_INFO_BASIC);
    param_in.parent_obj_addr = oinfo.addr;

    switch(loc_params->obj_type){//parent type
        case H5I_FILE:
            param_in.parent_type = VL_FILE;
            break;
        case H5I_GROUP:
            param_in.parent_type = VL_GROUP;
            break;
        default:
            assert(0 && "Wrong type: Parent obj type could only be FILE or GROUP.");
            break;
    }
    //DEBUG_PRINT

    param_in.loc_param_size = 0; // will be set in encoding function.
    param_in.loc_params = (H5VL_loc_params_t*)loc_params;
    param_in.name_size = strlen(name) + 1;
    param_in.name = (char*)name;

    void* proposal_data = NULL;

    size_t p_data_size = dt_commit_encoder(&param_in, &proposal_data);

    DEBUG_PRINT

    proposal_id pid = MY_RANK_DEBUG;//getpid();
    proposal* p = compose_proposal(pid, DT_COMMIT, proposal_data, p_data_size);

    assert(o->p_ctx);
    //DEBUG_PRINT
    assert(o->p_ctx->mm);

    ((prop_ctx*)(o->p_ctx->mm->app_ctx))->resulting_obj_out = NULL;
    DEBUG_PRINT
    int ret = MM_submit_proposal(o->p_ctx->mm, p);

    //under = H5VLdatatype_commit(o->under_object, loc_params, o->p_ctx->under_vol_id, name, type_id, lcpl_id, tcpl_id, tapl_id, dxpl_id, req);

    if(ret == 1){
        DEBUG_PRINT
        p->result_obj_local = ((prop_ctx*)(o->p_ctx->mm->app_ctx))->resulting_obj_out;
        //printf("%s:%d: resulting obj = %p\n", __func__, __LINE__, p->result_obj_local);
    }else{
        printf("%s:%d: ret = %d\n", __func__, __LINE__, ret);
    }
    DEBUG_PRINT

    if(p->result_obj_local) {
        DEBUG_PRINT
        dt = H5VL_rlo_pass_through_new_obj(p->result_obj_local, VL_NAMED_DATATYPE, o->p_ctx);
    } /* end if */
    else
        dt = NULL;

//    if(under)
//        dt = H5VL_rlo_pass_through_new_obj(under, VL_NAMED_DATATYPE, o->p_ctx);
//    else
//        dt = NULL;

    return (void *)dt;
} /* end H5VL_rlo_pass_through_datatype_commit() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_datatype_open
 *
 * Purpose:     Opens a named datatype inside a container.
 *
 * Return:      Success:    Pointer to datatype object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_rlo_pass_through_datatype_open(void *obj, const H5VL_loc_params_t *loc_params,
    const char *name, hid_t tapl_id, hid_t dxpl_id, void **req)
{
    H5VL_rlo_pass_through_t *dt;
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    void *under;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL DATATYPE Open\n");
#endif

    under = H5VLdatatype_open(o->under_object, loc_params, o->p_ctx->under_vol_id, name, tapl_id, dxpl_id, req);
    if(under)
        dt = H5VL_rlo_pass_through_new_obj(under, VL_NAMED_DATATYPE, o->p_ctx);
    else
        dt = NULL;

    return (void *)dt;
} /* end H5VL_rlo_pass_through_datatype_open() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_datatype_get
 *
 * Purpose:     Get information about a datatype
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_datatype_get(void *dt, H5VL_datatype_get_t get_type,
    hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)dt;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL DATATYPE Get\n");
#endif

    ret_value = H5VLdatatype_get(o->under_object, o->p_ctx->under_vol_id, get_type, dxpl_id, req, arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_datatype_get() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_datatype_specific
 *
 * Purpose:     Specific operations for datatypes
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_datatype_specific(void *obj, H5VL_datatype_specific_t specific_type,
    hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL DATATYPE Specific\n");
#endif

    ret_value = H5VLdatatype_specific(o->under_object, o->p_ctx->under_vol_id, specific_type, dxpl_id, req, arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_datatype_specific() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_datatype_optional
 *
 * Purpose:     Perform a connector-specific operation on a datatype
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_datatype_optional(void *obj, hid_t dxpl_id, void **req,
    va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL DATATYPE Optional\n");
#endif

    ret_value = H5VLdatatype_optional(o->under_object, o->p_ctx->under_vol_id, dxpl_id, req, arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_datatype_optional() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_datatype_close
 *
 * Purpose:     Closes a datatype.
 *
 * Return:      Success:    0
 *              Failure:    -1, datatype not closed.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_datatype_close(void *dt, hid_t dxpl_id, void **req)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)dt;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL DATATYPE Close\n");
#endif

    assert(o->under_object);

    ret_value = H5VLdatatype_close(o->under_object, o->p_ctx->under_vol_id, dxpl_id, req);

    /* Release our wrapper, if underlying datatype was closed */
    if(ret_value >= 0)
        H5VL_rlo_pass_through_free_obj(o);

    return ret_value;
} /* end H5VL_rlo_pass_through_datatype_close() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_file_create
 *
 * Purpose:     Creates a container using this connector
 *
 * Return:      Success:    Pointer to a file object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_rlo_pass_through_file_create(const char *name, unsigned flags, hid_t fcpl_id,
    hid_t fapl_id, hid_t dxpl_id, void **req)
{
    H5VL_rlo_pass_through_info_t *info;
    H5VL_rlo_pass_through_t *file;
    hid_t under_fapl_id;
    void *under;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL FILE Create\n");
#endif

    /* Get copy of our VOL info from FAPL */
    H5Pget_vol_info(fapl_id, (void **)&info);
    hbool_t is_collective;
    H5Pget_all_coll_metadata_ops(fapl_id, &is_collective);

    //printf("%s:%d:mode = %d, world_size = %d, window size =  %d\n", __func__, __LINE__, info->world_size, info->world_size, info->time_window_size);
    /* Copy the FAPL */
    under_fapl_id = H5Pcopy(fapl_id);

    /* Set the VOL ID and info for the underlying FAPL */
    H5Pset_vol(under_fapl_id, info->under_vol_id, info->under_vol_info);

    /* Open the file with the underlying VOL connector */
    under = H5VLfile_create(name, flags, fcpl_id, under_fapl_id, dxpl_id, req);
    if(under) {
        DEBUG_PRINT
        prop_ctx* h5_ctx;

        // Note that this is unsafe when multiple files are opened
        MPI_Comm_rank(info->mpi_comm, &MY_RANK_DEBUG);

        // Create new prop_ctx for this file
        h5_ctx = prop_ctx_new(under, info, is_collective);
        assert(h5_ctx);

        file = H5VL_rlo_pass_through_new_obj(under, VL_FILE, h5_ctx);
        DEBUG_PRINT
    } /* end if */
    else
        file = NULL;

    /* Close underlying FAPL */
    H5Pclose(under_fapl_id);

    /* Release copy of our VOL info */
    H5VL_rlo_pass_through_info_free(info);

    return (void *)file;
} /* end H5VL_rlo_pass_through_file_create() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_file_open
 *
 * Purpose:     Opens a container created with this connector
 *
 * Return:      Success:    Pointer to a file object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_rlo_pass_through_file_open(const char *name, unsigned flags, hid_t fapl_id,
    hid_t dxpl_id, void **req)
{
    H5VL_rlo_pass_through_info_t *info;
    H5VL_rlo_pass_through_t *file;
    hid_t under_fapl_id;
    void *under;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL FILE Open\n");
#endif

    /* Get copy of our VOL info from FAPL */
    H5Pget_vol_info(fapl_id, (void **)&info);
    hbool_t is_collective;
    H5Pget_all_coll_metadata_ops(fapl_id, &is_collective);
    /* Copy the FAPL */
    under_fapl_id = H5Pcopy(fapl_id);

    /* Set the VOL ID and info for the underlying FAPL */
    H5Pset_vol(under_fapl_id, info->under_vol_id, info->under_vol_info);


    /* Open the file with the underlying VOL connector */
    under = H5VLfile_open(name, flags, under_fapl_id, dxpl_id, req);
    if(under) {
        DEBUG_PRINT
        prop_ctx* h5_ctx;

        // Note that this is unsafe when multiple files are opened
        MPI_Comm_rank(info->mpi_comm, &MY_RANK_DEBUG);

        // Create new prop_ctx for this file
        h5_ctx = prop_ctx_new(under, info, is_collective);
        assert(h5_ctx);

        file = H5VL_rlo_pass_through_new_obj(under, VL_FILE, h5_ctx);
        DEBUG_PRINT
    } /* end if */
    else
        file = NULL;

    /* Close underlying FAPL */
    H5Pclose(under_fapl_id);

    /* Release copy of our VOL info */
    H5VL_rlo_pass_through_info_free(info);

    return (void *)file;
} /* end H5VL_rlo_pass_through_file_open() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_file_get
 *
 * Purpose:     Get info about a file
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_file_get(void *file, H5VL_file_get_t get_type, hid_t dxpl_id,
    void **req, va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)file;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL FILE Get\n");
#endif

    ret_value = H5VLfile_get(o->under_object, o->p_ctx->under_vol_id, get_type, dxpl_id, req, arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_file_get() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_file_specific_reissue
 *
 * Purpose:     Re-wrap vararg arguments into a va_list and reissue the
 *              file specific callback to the underlying VOL connector.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_file_specific_reissue(void *obj, hid_t connector_id,
    H5VL_file_specific_t specific_type, hid_t dxpl_id, void **req, ...)
{
    va_list arguments;
    herr_t ret_value;

    va_start(arguments, req);
    ret_value = H5VLfile_specific(obj, connector_id, specific_type, dxpl_id, req, arguments);
    va_end(arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_file_specific_reissue() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_file_specific
 *
 * Purpose:     Specific operation on file
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_file_specific(void *file, H5VL_file_specific_t specific_type,
    hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)file;
    hid_t under_vol_id = -1;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL FILE Specific\n");
#endif

    /* Unpack arguments to get at the child file pointer when mounting a file */
    if(specific_type == H5VL_FILE_MOUNT) {
        H5I_type_t loc_type;
        const char *name;
        H5VL_rlo_pass_through_t *child_file;
        hid_t plist_id;

        /* Retrieve parameters for 'mount' operation, so we can unwrap the child file */
        loc_type = (H5I_type_t)va_arg(arguments, int); /* enum work-around */
        name = va_arg(arguments, const char *);
        child_file = (H5VL_rlo_pass_through_t *)va_arg(arguments, void *);
        plist_id = va_arg(arguments, hid_t);

        /* Keep the correct underlying VOL ID for possible async request token */
        under_vol_id = o->p_ctx->under_vol_id;

        /* Re-issue 'file specific' call, using the unwrapped pieces */
        ret_value = H5VL_rlo_pass_through_file_specific_reissue(o->under_object, o->p_ctx->under_vol_id, specific_type, dxpl_id, req, (int)loc_type, name, child_file->under_object, plist_id);
    } /* end if */
    else if(specific_type == H5VL_FILE_IS_ACCESSIBLE || specific_type == H5VL_FILE_DELETE) {
        H5VL_rlo_pass_through_info_t *info;
        hid_t fapl_id, under_fapl_id;
        const char *name;
        htri_t *ret;

        /* Get the arguments for the 'is accessible' check */
        fapl_id = va_arg(arguments, hid_t);
        name    = va_arg(arguments, const char *);
        ret     = va_arg(arguments, htri_t *);

        /* Get copy of our VOL info from FAPL */
        H5Pget_vol_info(fapl_id, (void **)&info);

        /* Copy the FAPL */
        under_fapl_id = H5Pcopy(fapl_id);

        /* Set the VOL ID and info for the underlying FAPL */
        H5Pset_vol(under_fapl_id, info->under_vol_id, info->under_vol_info);

        /* Keep the correct underlying VOL ID for possible async request token */
        under_vol_id = info->under_vol_id;

        /* Re-issue 'file specific' call */
        ret_value = H5VL_rlo_pass_through_file_specific_reissue(NULL, info->under_vol_id, specific_type, dxpl_id, req, under_fapl_id, name, ret);

        /* Close underlying FAPL */
        H5Pclose(under_fapl_id);

        /* Release copy of our VOL info */
        H5VL_rlo_pass_through_info_free(info);
    } /* end else-if */
    else {
        va_list my_arguments;

        /* Make a copy of the argument list for later, if reopening */
        if(specific_type == H5VL_FILE_REOPEN)
            va_copy(my_arguments, arguments);

        /* Keep the correct underlying VOL ID for possible async request token */
        under_vol_id = o->p_ctx->under_vol_id;

        ret_value = H5VLfile_specific(o->under_object, o->p_ctx->under_vol_id, specific_type, dxpl_id, req, arguments);

        /* Wrap file struct pointer, if we reopened one */
        if(specific_type == H5VL_FILE_REOPEN) {
            if(ret_value >= 0) {
                void      **ret = va_arg(my_arguments, void **);

                if(ret && *ret)
                    *ret = H5VL_rlo_pass_through_new_obj(*ret, VL_FILE, o->p_ctx);
            } /* end if */

            /* Finish use of copied vararg list */
            va_end(my_arguments);
        } /* end if */
    } /* end else */

    return ret_value;
} /* end H5VL_rlo_pass_through_file_specific() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_file_optional
 *
 * Purpose:     Perform a connector-specific operation on a file
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_file_optional(void *file, hid_t dxpl_id, void **req,
    va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)file;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL File Optional\n");
#endif

    ret_value = H5VLfile_optional(o->under_object, o->p_ctx->under_vol_id, dxpl_id, req, arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_file_optional() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_file_close
 *
 * Purpose:     Closes a file.
 *
 * Return:      Success:    0
 *              Failure:    -1, file not closed.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_file_close(void *file, hid_t dxpl_id, void **req)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)file;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL FILE Close\n");
#endif

    assert(o->p_ctx);
    DEBUG_PRINT

    // Let everyone know we are ready to close this file
    proposal_id pid = MY_RANK_DEBUG;
    proposal* p = compose_proposal(pid, FILE_CLOSE, NULL, 0);
    MM_submit_proposal(o->p_ctx->mm, p);
    DEBUG_PRINT

    // If all the other ranks' file close proposals haven't been received,
    // loop calling the metadata manager to process proposals until the
    // file close refcount reaches the communicator's size (i.e. all
    // ranks are now ready to close the file)
    if(o->p_ctx->close_count < o->p_ctx->comm_size)
        do {DEBUG_PRINT
            usleep(1000);
            MM_make_progress(o->p_ctx->mm);
        } while(o->p_ctx->close_count < o->p_ctx->comm_size);
    DEBUG_PRINT
    // Close underlying file
    ret_value = H5VLfile_close(o->under_object, o->p_ctx->under_vol_id, dxpl_id, req);
    DEBUG_PRINT
    // Release our wrapper, if underlying file was closed
    // (Releases the execution context also)
    if(ret_value >= 0)
        H5VL_rlo_pass_through_free_obj(o);

    return 0;
} /* end H5VL_rlo_pass_through_file_close() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_group_create
 *
 * Purpose:     Creates a group inside a container
 *
 * Return:      Success:    Pointer to a group object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_rlo_pass_through_group_create(void *obj, const H5VL_loc_params_t *loc_params,
    const char *name, hid_t lcpl_id, hid_t gcpl_id, hid_t gapl_id,
    hid_t dxpl_id, void **req)
{
    H5VL_rlo_pass_through_t *group;
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL GROUP Create\n");
#endif

    param_group param_in;
    param_in.lcpl_id = lcpl_id;
    param_in.gcpl_id = gcpl_id;
    param_in.gapl_id = gapl_id;
    param_in.dxpl_id = dxpl_id;

    param_in.name_size = strlen(name) + 1;
    param_in.loc_param_size = 0; // will be set in encoding function.

    param_in.parent_type = loc_params->obj_type;
    param_in.loc_params = (H5VL_loc_params_t*)loc_params;
    param_in.name = (char*)name;

    //Get parent object native id/addr
    H5VL_loc_params_t param_tmp;
    H5O_info_t oinfo;
    param_tmp.type = H5VL_OBJECT_BY_SELF;
    param_tmp.obj_type = loc_params->obj_type;
    get_native_info(o->under_object, o->p_ctx->under_vol_id, dxpl_id, NULL, H5VL_NATIVE_OBJECT_GET_INFO, &param_tmp, &oinfo, H5O_INFO_BASIC);
    param_in.parent_obj_addr = oinfo.addr;

    switch(loc_params->obj_type){//parent type
        case H5I_FILE:
            param_in.parent_type = VL_FILE;
            break;
        case H5I_GROUP:
            param_in.parent_type = VL_GROUP;
            break;
        default:
            assert(0 && "Wrong type: Parent obj type could only be FILE or GROUP.");
            break;
    }
    //DEBUG_PRINT
    void* proposal_data = NULL;

    size_t p_data_size = group_create_encoder(&param_in, &proposal_data);
    group_encoder_test(loc_params, name, lcpl_id, gcpl_id, gapl_id, dxpl_id);
    //DEBUG_PRINT

    proposal_id pid = MY_RANK_DEBUG;//getpid();
    proposal* p = compose_proposal(pid, GROUP_CREATE, proposal_data, p_data_size);

    assert(o->p_ctx);
    assert(o->p_ctx->mm);

    ((prop_ctx*)(o->p_ctx->mm->app_ctx))->resulting_obj_out = NULL;
    if(MM_submit_proposal(o->p_ctx->mm, p) == 1)
        p->result_obj_local = ((prop_ctx*)(o->p_ctx->mm->app_ctx))->resulting_obj_out;

    if(p->result_obj_local)
        group = H5VL_rlo_pass_through_new_obj(p->result_obj_local, VL_GROUP, o->p_ctx);
    else
        group = NULL;

    return (void *)group;
} /* end H5VL_rlo_pass_through_group_create() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_group_open
 *
 * Purpose:     Opens a group inside a container
 *
 * Return:      Success:    Pointer to a group object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_rlo_pass_through_group_open(void *obj, const H5VL_loc_params_t *loc_params,
    const char *name, hid_t gapl_id, hid_t dxpl_id, void **req)
{
    H5VL_rlo_pass_through_t *group;
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    void *under;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL GROUP Open\n");
#endif

    under = H5VLgroup_open(o->under_object, loc_params, o->p_ctx->under_vol_id, name, gapl_id, dxpl_id, req);
    if(under)
        group = H5VL_rlo_pass_through_new_obj(under, VL_GROUP, o->p_ctx);
    else
        group = NULL;

    return (void *)group;
} /* end H5VL_rlo_pass_through_group_open() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_group_get
 *
 * Purpose:     Get info about a group
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_group_get(void *obj, H5VL_group_get_t get_type, hid_t dxpl_id,
    void **req, va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL GROUP Get\n");
#endif

    ret_value = H5VLgroup_get(o->under_object, o->p_ctx->under_vol_id, get_type, dxpl_id, req, arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_group_get() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_group_specific
 *
 * Purpose:     Specific operation on a group
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_group_specific(void *obj, H5VL_group_specific_t specific_type,
    hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL GROUP Specific\n");
#endif

    ret_value = H5VLgroup_specific(o->under_object, o->p_ctx->under_vol_id, specific_type, dxpl_id, req, arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_group_specific() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_group_optional
 *
 * Purpose:     Perform a connector-specific operation on a group
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_group_optional(void *obj, hid_t dxpl_id, void **req,
    va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL GROUP Optional\n");
#endif

    ret_value = H5VLgroup_optional(o->under_object, o->p_ctx->under_vol_id, dxpl_id, req, arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_group_optional() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_group_close
 *
 * Purpose:     Closes a group.
 *
 * Return:      Success:    0
 *              Failure:    -1, group not closed.
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_group_close(void *grp, hid_t dxpl_id, void **req)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)grp;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL H5Gclose\n");
#endif

    if(o->p_ctx->is_collective){//call rlo_vol, otherwise use regular ones.
        //look at file_close.
        //do this for ds_close, typeclose too
    } else {
        ret_value = H5VLgroup_close(o->under_object, o->p_ctx->under_vol_id, dxpl_id, req);
    }


    /* Release our wrapper, if underlying file was closed */
    if(ret_value >= 0)
        H5VL_rlo_pass_through_free_obj(o);

    return ret_value;
} /* end H5VL_rlo_pass_through_group_close() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_link_create_reissue
 *
 * Purpose:     Re-wrap vararg arguments into a va_list and reissue the
 *              link create callback to the underlying VOL connector.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_link_create_reissue(H5VL_link_create_type_t create_type,
    void *obj, const H5VL_loc_params_t *loc_params, hid_t connector_id,
    hid_t lcpl_id, hid_t lapl_id, hid_t dxpl_id, void **req, ...)
{
    va_list arguments;
    herr_t ret_value;

    va_start(arguments, req);
    ret_value = H5VLlink_create(create_type, obj, loc_params, connector_id, lcpl_id, lapl_id, dxpl_id, req, arguments);
    va_end(arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_link_create_reissue() */

/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_link_create
 *
 * Purpose:     Creates a hard / soft / UD / external link.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_link_create(H5VL_link_create_type_t create_type, void *obj,
    const H5VL_loc_params_t *loc_params, hid_t lcpl_id, hid_t lapl_id,
    hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    hid_t under_vol_id = -1;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL LINK Create\n");
#endif

    /* Try to retrieve the "under" VOL id */
    if(o)
        under_vol_id = o->p_ctx->under_vol_id;

    /* Fix up the link target object for hard link creation */
    if(H5VL_LINK_CREATE_HARD == create_type) {
        void         *cur_obj;
        H5VL_loc_params_t cur_params;

        /* Retrieve the object & loc params for the link target */
        cur_obj = va_arg(arguments, void *);
        cur_params = va_arg(arguments, H5VL_loc_params_t);

        /* If it's a non-NULL pointer, find the 'under object' and re-set the property */
        if(cur_obj) {
            /* Check if we still need the "under" VOL ID */
            if(under_vol_id < 0)
                under_vol_id = ((H5VL_rlo_pass_through_t *)cur_obj)->p_ctx->under_vol_id;

            /* Set the object for the link target */
            cur_obj = ((H5VL_rlo_pass_through_t *)cur_obj)->under_object;
        } /* end if */

        /* Re-issue 'link create' call, using the unwrapped pieces */
        ret_value = H5VL_rlo_pass_through_link_create_reissue(create_type, (o ? o->under_object : NULL), loc_params, under_vol_id, lcpl_id, lapl_id, dxpl_id, req, cur_obj, cur_params);
    } /* end if */
    else
        ret_value = H5VLlink_create(create_type, (o ? o->under_object : NULL), loc_params, under_vol_id, lcpl_id, lapl_id, dxpl_id, req, arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_link_create() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_link_copy
 *
 * Purpose:     Renames an object within an HDF5 container and copies it to a new
 *              group.  The original name SRC is unlinked from the group graph
 *              and then inserted with the new name DST (which can specify a
 *              new path for the object) as an atomic operation. The names
 *              are interpreted relative to SRC_LOC_ID and
 *              DST_LOC_ID, which are either file IDs or group ID.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_link_copy(void *src_obj, const H5VL_loc_params_t *loc_params1,
    void *dst_obj, const H5VL_loc_params_t *loc_params2, hid_t lcpl_id,
    hid_t lapl_id, hid_t dxpl_id, void **req)
{
    H5VL_rlo_pass_through_t *o_src = (H5VL_rlo_pass_through_t *)src_obj;
    H5VL_rlo_pass_through_t *o_dst = (H5VL_rlo_pass_through_t *)dst_obj;
    hid_t under_vol_id = -1;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL LINK Copy\n");
#endif

    /* Retrieve the "under" VOL id */
    if(o_src)
        under_vol_id = o_src->p_ctx->under_vol_id;
    else if(o_dst)
        under_vol_id = o_dst->p_ctx->under_vol_id;
    assert(under_vol_id > 0);

    ret_value = H5VLlink_copy((o_src ? o_src->under_object : NULL), loc_params1, (o_dst ? o_dst->under_object : NULL), loc_params2, under_vol_id, lcpl_id, lapl_id, dxpl_id, req);

    return ret_value;
} /* end H5VL_rlo_pass_through_link_copy() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_link_move
 *
 * Purpose:     Moves a link within an HDF5 file to a new group.  The original
 *              name SRC is unlinked from the group graph
 *              and then inserted with the new name DST (which can specify a
 *              new path for the object) as an atomic operation. The names
 *              are interpreted relative to SRC_LOC_ID and
 *              DST_LOC_ID, which are either file IDs or group ID.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_link_move(void *src_obj, const H5VL_loc_params_t *loc_params1,
    void *dst_obj, const H5VL_loc_params_t *loc_params2, hid_t lcpl_id,
    hid_t lapl_id, hid_t dxpl_id, void **req)
{
    H5VL_rlo_pass_through_t *o_src = (H5VL_rlo_pass_through_t *)src_obj;
    H5VL_rlo_pass_through_t *o_dst = (H5VL_rlo_pass_through_t *)dst_obj;
    hid_t under_vol_id = -1;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL LINK Move\n");
#endif

    /* Retrieve the "under" VOL id */
    if(o_src)
        under_vol_id = o_src->p_ctx->under_vol_id;
    else if(o_dst)
        under_vol_id = o_dst->p_ctx->under_vol_id;
    assert(under_vol_id > 0);

    ret_value = H5VLlink_move((o_src ? o_src->under_object : NULL), loc_params1, (o_dst ? o_dst->under_object : NULL), loc_params2, under_vol_id, lcpl_id, lapl_id, dxpl_id, req);

    return ret_value;
} /* end H5VL_rlo_pass_through_link_move() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_link_get
 *
 * Purpose:     Get info about a link
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_link_get(void *obj, const H5VL_loc_params_t *loc_params,
    H5VL_link_get_t get_type, hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL LINK Get\n");
#endif

    ret_value = H5VLlink_get(o->under_object, loc_params, o->p_ctx->under_vol_id, get_type, dxpl_id, req, arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_link_get() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_link_specific
 *
 * Purpose:     Specific operation on a link
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_link_specific(void *obj, const H5VL_loc_params_t *loc_params,
    H5VL_link_specific_t specific_type, hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL LINK Specific\n");
#endif

    ret_value = H5VLlink_specific(o->under_object, loc_params, o->p_ctx->under_vol_id, specific_type, dxpl_id, req, arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_link_specific() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_link_optional
 *
 * Purpose:     Perform a connector-specific operation on a link
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_link_optional(void *obj, hid_t dxpl_id, void **req,
    va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL LINK Optional\n");
#endif

    ret_value = H5VLlink_optional(o->under_object, o->p_ctx->under_vol_id, dxpl_id, req, arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_link_optional() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_object_open
 *
 * Purpose:     Opens an object inside a container.
 *
 * Return:      Success:    Pointer to object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5VL_rlo_pass_through_object_open(void *obj, const H5VL_loc_params_t *loc_params,
    H5I_type_t *opened_type, hid_t dxpl_id, void **req)
{
    H5VL_rlo_pass_through_t *new_obj;
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    void *under;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL OBJECT Open\n");
#endif

    under = H5VLobject_open(o->under_object, loc_params, o->p_ctx->under_vol_id, opened_type, dxpl_id, req);
    if(under)
        new_obj = H5VL_rlo_pass_through_new_obj(under, VL_INVALID, o->p_ctx);
    else
        new_obj = NULL;

    return (void *)new_obj;
} /* end H5VL_rlo_pass_through_object_open() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_object_copy
 *
 * Purpose:     Copies an object inside a container.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_object_copy(void *src_obj, const H5VL_loc_params_t *src_loc_params,
    const char *src_name, void *dst_obj, const H5VL_loc_params_t *dst_loc_params,
    const char *dst_name, hid_t ocpypl_id, hid_t lcpl_id, hid_t dxpl_id,
    void **req)
{
    H5VL_rlo_pass_through_t *o_src = (H5VL_rlo_pass_through_t *)src_obj;
    H5VL_rlo_pass_through_t *o_dst = (H5VL_rlo_pass_through_t *)dst_obj;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL OBJECT Copy\n");
#endif

    ret_value = H5VLobject_copy(o_src->under_object, src_loc_params, src_name, o_dst->under_object, dst_loc_params, dst_name, o_src->p_ctx->under_vol_id, ocpypl_id, lcpl_id, dxpl_id, req);

    return ret_value;
} /* end H5VL_rlo_pass_through_object_copy() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_object_get
 *
 * Purpose:     Get info about an object
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_object_get(void *obj, const H5VL_loc_params_t *loc_params, H5VL_object_get_t get_type, hid_t dxpl_id, void **req, va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL OBJECT Get\n");
#endif

    ret_value = H5VLobject_get(o->under_object, loc_params, o->p_ctx->under_vol_id, get_type, dxpl_id, req, arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_object_get() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_object_specific
 *
 * Purpose:     Specific operation on an object
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_object_specific(void *obj, const H5VL_loc_params_t *loc_params,
    H5VL_object_specific_t specific_type, hid_t dxpl_id, void **req,
    va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL OBJECT Specific\n");
#endif

    ret_value = H5VLobject_specific(o->under_object, loc_params, o->p_ctx->under_vol_id, specific_type, dxpl_id, req, arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_object_specific() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_object_optional
 *
 * Purpose:     Perform a connector-specific operation for an object
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_object_optional(void *obj, hid_t dxpl_id, void **req,
    va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL OBJECT Optional\n");
#endif

    ret_value = H5VLobject_optional(o->under_object, o->p_ctx->under_vol_id, dxpl_id, req, arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_object_optional() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_request_wait
 *
 * Purpose:     Wait (with a timeout) for an async operation to complete
 *
 * Note:        Releases the request if the operation has completed and the
 *              connector callback succeeds
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_request_wait(void *obj, uint64_t timeout,
    H5ES_status_t *status)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL REQUEST Wait\n");
#endif

    ret_value = H5VLrequest_wait(o->under_object, o->p_ctx->under_vol_id, timeout, status);

    if(ret_value >= 0 && *status != H5ES_STATUS_IN_PROGRESS)
        H5VL_rlo_pass_through_free_obj(o);

    return ret_value;
} /* end H5VL_rlo_pass_through_request_wait() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_request_notify
 *
 * Purpose:     Registers a user callback to be invoked when an asynchronous
 *              operation completes
 *
 * Note:        Releases the request, if connector callback succeeds
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_request_notify(void *obj, H5VL_request_notify_t cb, void *ctx)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL REQUEST Wait\n");
#endif

    ret_value = H5VLrequest_notify(o->under_object, o->p_ctx->under_vol_id, cb, ctx);

    if(ret_value >= 0)
        H5VL_rlo_pass_through_free_obj(o);

    return ret_value;
} /* end H5VL_rlo_pass_through_request_notify() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_request_cancel
 *
 * Purpose:     Cancels an asynchronous operation
 *
 * Note:        Releases the request, if connector callback succeeds
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_request_cancel(void *obj)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL REQUEST Cancel\n");
#endif

    ret_value = H5VLrequest_cancel(o->under_object, o->p_ctx->under_vol_id);

    if(ret_value >= 0)
        H5VL_rlo_pass_through_free_obj(o);

    return ret_value;
} /* end H5VL_rlo_pass_through_request_cancel() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_request_specific_reissue
 *
 * Purpose:     Re-wrap vararg arguments into a va_list and reissue the
 *              request specific callback to the underlying VOL connector.
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_request_specific_reissue(void *obj, hid_t connector_id,
    H5VL_request_specific_t specific_type, ...)
{
    va_list arguments;
    herr_t ret_value;

    va_start(arguments, specific_type);
    ret_value = H5VLrequest_specific(obj, connector_id, specific_type, arguments);
    va_end(arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_request_specific_reissue() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_request_specific
 *
 * Purpose:     Specific operation on a request
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_request_specific(void *obj, H5VL_request_specific_t specific_type,
    va_list arguments)
{
    herr_t ret_value = -1;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL REQUEST Specific\n");
#endif

    if(H5VL_REQUEST_WAITANY == specific_type ||
            H5VL_REQUEST_WAITSOME == specific_type ||
            H5VL_REQUEST_WAITALL == specific_type) {
        va_list tmp_arguments;
        size_t req_count;

        /* Sanity check */
        assert(obj == NULL);

        /* Get enough info to call the underlying connector */
        va_copy(tmp_arguments, arguments);
        req_count = va_arg(tmp_arguments, size_t);

        /* Can only use a request to invoke the underlying VOL connector when there's >0 requests */
        if(req_count > 0) {
            void **req_array;
            void **under_req_array;
            uint64_t timeout;
            H5VL_rlo_pass_through_t *o;
            size_t u;               /* Local index variable */

            /* Get the request array */
            req_array = va_arg(tmp_arguments, void **);

            /* Get a request to use for determining the underlying VOL connector */
            o = (H5VL_rlo_pass_through_t *)req_array[0];

            /* Create array of underlying VOL requests */
            under_req_array = (void **)malloc(req_count * sizeof(void **));
            for(u = 0; u < req_count; u++)
                under_req_array[u] = ((H5VL_rlo_pass_through_t *)req_array[u])->under_object;

            /* Remove the timeout value from the vararg list (it's used in all the calls below) */
            timeout = va_arg(tmp_arguments, uint64_t);

            /* Release requests that have completed */
            if(H5VL_REQUEST_WAITANY == specific_type) {
                size_t *index;          /* Pointer to the index of completed request */
                H5ES_status_t *status;  /* Pointer to the request's status */

                /* Retrieve the remaining arguments */
                index = va_arg(tmp_arguments, size_t *);
                assert(*index <= req_count);
                status = va_arg(tmp_arguments, H5ES_status_t *);

                /* Reissue the WAITANY 'request specific' call */
                ret_value = H5VL_rlo_pass_through_request_specific_reissue(o->under_object, o->p_ctx->under_vol_id, specific_type, req_count, under_req_array, timeout, index, status);

                /* Release the completed request, if it completed */
                if(ret_value >= 0 && *status != H5ES_STATUS_IN_PROGRESS) {
                    H5VL_rlo_pass_through_t *tmp_o;

                    tmp_o = (H5VL_rlo_pass_through_t *)req_array[*index];
                    H5VL_rlo_pass_through_free_obj(tmp_o);
                } /* end if */
            } /* end if */
            else if(H5VL_REQUEST_WAITSOME == specific_type) {
                size_t *outcount;               /* # of completed requests */
                unsigned *array_of_indices;     /* Array of indices for completed requests */
                H5ES_status_t *array_of_statuses; /* Array of statuses for completed requests */

                /* Retrieve the remaining arguments */
                outcount = va_arg(tmp_arguments, size_t *);
                assert(*outcount <= req_count);
                array_of_indices = va_arg(tmp_arguments, unsigned *);
                array_of_statuses = va_arg(tmp_arguments, H5ES_status_t *);

                /* Reissue the WAITSOME 'request specific' call */
                ret_value = H5VL_rlo_pass_through_request_specific_reissue(o->under_object, o->p_ctx->under_vol_id, specific_type, req_count, under_req_array, timeout, outcount, array_of_indices, array_of_statuses);

                /* If any requests completed, release them */
                if(ret_value >= 0 && *outcount > 0) {
                    unsigned *idx_array;    /* Array of indices of completed requests */

                    /* Retrieve the array of completed request indices */
                    idx_array = va_arg(tmp_arguments, unsigned *);

                    /* Release the completed requests */
                    for(u = 0; u < *outcount; u++) {
                        H5VL_rlo_pass_through_t *tmp_o;

                        tmp_o = (H5VL_rlo_pass_through_t *)req_array[idx_array[u]];
                        H5VL_rlo_pass_through_free_obj(tmp_o);
                    } /* end for */
                } /* end if */
            } /* end else-if */
            else {      /* H5VL_REQUEST_WAITALL == specific_type */
                H5ES_status_t *array_of_statuses; /* Array of statuses for completed requests */

                /* Retrieve the remaining arguments */
                array_of_statuses = va_arg(tmp_arguments, H5ES_status_t *);

                /* Reissue the WAITALL 'request specific' call */
                ret_value = H5VL_rlo_pass_through_request_specific_reissue(o->under_object, o->p_ctx->under_vol_id, specific_type, req_count, under_req_array, timeout, array_of_statuses);

                /* Release the completed requests */
                if(ret_value >= 0) {
                    for(u = 0; u < req_count; u++) {
                        if(array_of_statuses[u] != H5ES_STATUS_IN_PROGRESS) {
                            H5VL_rlo_pass_through_t *tmp_o;

                            tmp_o = (H5VL_rlo_pass_through_t *)req_array[u];
                            H5VL_rlo_pass_through_free_obj(tmp_o);
                        } /* end if */
                    } /* end for */
                } /* end if */
            } /* end else */

            /* Release array of requests for underlying connector */
            free(under_req_array);
        } /* end if */

        /* Finish use of copied vararg list */
        va_end(tmp_arguments);
    } /* end if */
    else
        assert(0 && "Unknown 'specific' operation");

    return ret_value;
} /* end H5VL_rlo_pass_through_request_specific() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_request_optional
 *
 * Purpose:     Perform a connector-specific operation for a request
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_request_optional(void *obj, va_list arguments)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL REQUEST Optional\n");
#endif

    ret_value = H5VLrequest_optional(o->under_object, o->p_ctx->under_vol_id, arguments);

    return ret_value;
} /* end H5VL_rlo_pass_through_request_optional() */


/*-------------------------------------------------------------------------
 * Function:    H5VL_rlo_pass_through_request_free
 *
 * Purpose:     Releases a request, allowing the operation to complete without
 *              application tracking
 *
 * Return:      Success:    0
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5VL_rlo_pass_through_request_free(void *obj)
{
    H5VL_rlo_pass_through_t *o = (H5VL_rlo_pass_through_t *)obj;
    herr_t ret_value;

#ifdef ENABLE_RLO_PASSTHRU_LOGGING
    printf("------- PASS THROUGH VOL REQUEST Free\n");
#endif

    ret_value = H5VLrequest_free(o->under_object, o->p_ctx->under_vol_id);

    if(ret_value >= 0)
        H5VL_rlo_pass_through_free_obj(o);

    return ret_value;
} /* end H5VL_rlo_pass_through_request_free() */

