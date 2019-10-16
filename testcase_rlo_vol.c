/*
 * prov_test.c
 *
 *  Created on: Aug 19, 2019
 *      Author: tonglin
 */

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <mpi.h>
#include "hdf5.h"
#include "H5VL_rlo.h"
#include "util_debug.h"
#include "proposal.h"

/* Remember to set these environment variables:

HDF5_VOL_CONNECTOR=rlo_pass_through under_vol=0;under_info={}
HDF5_PLUGIN_PATH=/Users/koziol/HDF5/dev/ind_meta/rlo_vol

*/
int my_rank;
int comm_size;
extern int MY_RANK_DEBUG;
time_stamp public_get_time_stamp_us(){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return 1000000 * tv.tv_sec + tv.tv_usec;
}

unsigned long dt_commit_test(int benchmark_type, const char* file_name, hid_t fapl, int num_ops){
    hid_t file_id = H5Fcreate(file_name, H5F_ACC_TRUNC, H5P_DEFAULT, fapl);

    char dt_name[32] = "";

    unsigned long t1 = public_get_time_stamp_us();
    hid_t int_id;
    if(benchmark_type ==0){
        for(int i = 0; i < comm_size; i++){
            for(int j = 0; j < num_ops; j++){
                sprintf(dt_name, "int_%d_%d", i, j);
                //printf("committing data type, my rank = %d, dt_name = [%s]\n", my_rank, dt_name);
                int_id = H5Tcopy(H5T_NATIVE_INT);
                if(H5Tcommit2(file_id, dt_name, int_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT) < 0){
                    printf("H5Tcommit2 failed.\n");
                }
                H5Tclose(int_id);
            }
        }
    } else {//RLO
        for(int j = 0; j < num_ops; j++){
            sprintf(dt_name, "int_%d_%d", my_rank, j);
            //printf("committing data type, dt_name = [%s]\n", dt_name);
            int_id = H5Tcopy(H5T_NATIVE_INT);
            if(H5Tcommit2(file_id, dt_name, int_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT) < 0){
                printf("H5Tcommit2 failed.\n");
            }
            H5Tclose(int_id);
        }
    }
    unsigned long t2 = public_get_time_stamp_us();

    H5Fclose(file_id);
    return t2 - t1;
}

unsigned long ds_test(int benchmark_type, const char* file_name, hid_t fapl, int num_ops){
    DEBUG_PRINT
    hid_t dataset_id, dataspace_id;
    herr_t status;
    char ds_name[32] = "";
    hsize_t     dims[2];
    int         i, j;
    int dset_data[4][6];
    /* Initialize the dataset. */
    for (i = 0; i < 4; i++)
       for (j = 0; j < 6; j++)
          dset_data[i][j] = i * 6 + j + 1;

    hid_t file_id = H5Fcreate(file_name, H5F_ACC_TRUNC, H5P_DEFAULT, fapl);//H5P_DEFAULT

    /* Create the data space for the dataset. */
    dims[0] = 4;
    dims[1] = 6;
    dataspace_id = H5Screate_simple(2, dims, NULL);

    unsigned long t1 = public_get_time_stamp_us();
    if(benchmark_type == 0){//baseline
        for(i = 0; i < comm_size; i++) {
            for(j = 0; j < num_ops; j++) {
                sprintf(ds_name, "/dset_%d_%d", i, j);
                dataset_id = H5Dcreate2(file_id, ds_name, H5T_NATIVE_INT, dataspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
                status = H5Dwrite(dataset_id, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, dset_data);
                status = H5Dread(dataset_id, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, dset_data);
                status = H5Dclose(dataset_id);
            }
        }
    } else {//RLO
        for(int j = 0; j < num_ops; j++){
            sprintf(ds_name, "/dset_%d_%d", my_rank, j);
            dataset_id = H5Dcreate2(file_id, ds_name, H5T_NATIVE_INT, dataspace_id, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
            status = H5Dwrite(dataset_id, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, dset_data);
            status = H5Dread(dataset_id, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, dset_data);
            status = H5Dclose(dataset_id);
        }
        DEBUG_PRINT
    }
    unsigned long t2 = public_get_time_stamp_us();

    status = H5Sclose(dataspace_id);

    status = H5Fclose(file_id);

    /* Re-open the file in serial and verify contents created independently */
    file_id = H5Fopen(file_name, H5F_ACC_RDONLY, H5P_DEFAULT);

    for(i = 0; i < comm_size; i++) {
        for(int j = 0; j < num_ops; j++){
            sprintf(ds_name, "/dset_%d_%d", i, j);
            dataset_id = H5Dopen2(file_id, ds_name, H5P_DEFAULT);
            status = H5Dread(dataset_id, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, dset_data);
            status = H5Dclose(dataset_id);
            DEBUG_PRINT
        }
    }
    H5Fclose(file_id);
    return t2 - t1;
}

unsigned long group_test(int benchmark_type, const char* file_name, hid_t fapl, int num_ops){
    DEBUG_PRINT
    hid_t group_id;
    herr_t status;
    char group_name[32] = "";

    /* Create a new file using default properties. */
    hid_t file_id = H5Fcreate(file_name, H5F_ACC_TRUNC, H5P_DEFAULT, fapl);
    unsigned long t1 = public_get_time_stamp_us();
    if(benchmark_type == 0){//baseline
        for(int i = 0; i < comm_size; i++) {
            DEBUG_PRINT
            for(int j = 0; j < num_ops; j++){
                sprintf(group_name, "/group_%d_%d", i, j);
                group_id = H5Gcreate2(file_id, group_name, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
                DEBUG_PRINT
                status = H5Gclose(group_id);
                DEBUG_PRINT
            }
        }
    } else {//RLO
        for(int j = 0; j < num_ops; j++){
            sprintf(group_name, "/group_%d_%d", my_rank, j);
            group_id = H5Gcreate2(file_id, group_name, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
            status = H5Gclose(group_id);
        }
    }
    unsigned long t2 = public_get_time_stamp_us();
    H5Fclose(file_id);

    /* Re-open the file in serial and verify contents created independently */
    file_id = H5Fopen(file_name, H5F_ACC_RDONLY, H5P_DEFAULT);

    for(int i = 0; i < comm_size; i++) {
        for(int j = 0; j < num_ops; j++){
            sprintf(group_name, "/group_%d_%d", i, j);
            group_id = H5Gopen2(file_id, group_name, H5P_DEFAULT);
            status = H5Gclose(group_id);
            DEBUG_PRINT
        }
    }
    H5Fclose(file_id);
    return t2 - t1;
}

unsigned long attr_test(int benchmark_type, const char* file_name, hid_t fapl, int num_ops){
    hid_t       file_id, dataset_id, attribute_id, dataspace_id;  /* identifiers */
    hsize_t     dims;
//    int         attr_data[2];
    herr_t      status;
    char ds_name[32] = "";
    char attr_name[32] = "";
    dims = 1;

    int* attr_data_array = calloc(comm_size, sizeof(int));
    for(int i = 0; i < comm_size; i++){
        attr_data_array[i] = i;
    }

    file_id = H5Fcreate(file_name, H5F_ACC_TRUNC, H5P_DEFAULT, fapl);

    dataspace_id = H5Screate_simple(1, &dims, NULL);

    unsigned long t1 = public_get_time_stamp_us();

    if(benchmark_type == 0){//baseline
        for(int i = 0; i < comm_size; i++) {
            for(int j = 0; j < num_ops; j++){
                DEBUG_PRINT
                sprintf(ds_name, "/dset_%d_%d", i, j);
                dataset_id = H5Dcreate2(file_id, ds_name, H5T_NATIVE_INT, dataspace_id,
                                       H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

                sprintf(attr_name, "/attr_%d_%d", i, j);
                attribute_id = H5Acreate2 (dataset_id, attr_name, H5T_NATIVE_INT, dataspace_id,
                                          H5P_DEFAULT, H5P_DEFAULT);

                status = H5Awrite(attribute_id, H5T_NATIVE_INT, &(attr_data_array[my_rank]));

                status = H5Aclose(attribute_id);

                status = H5Dclose(dataset_id);
            }
        }
    } else {//RLO
        for(int j = 0; j < num_ops; j++){
            sprintf(ds_name, "/dset_%d_%d", my_rank, j);
            dataset_id = H5Dcreate2(file_id, ds_name, H5T_NATIVE_INT, dataspace_id,
                                   H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

            sprintf(attr_name, "/attr_%d_%d", my_rank, j);
            attribute_id = H5Acreate2 (dataset_id, attr_name, H5T_NATIVE_INT, dataspace_id,
                                      H5P_DEFAULT, H5P_DEFAULT);

            status = H5Awrite(attribute_id, H5T_NATIVE_INT, &(attr_data_array[my_rank]));
            status = H5Aclose(attribute_id);
            status = H5Dclose(dataset_id);
        }
    }
    unsigned long t2 = public_get_time_stamp_us();
    status = H5Sclose(dataspace_id);
    status = H5Fclose(file_id);
    return t2 - t1;
}

int dset_extend_test(int benchmark_type, const char* file_name, hid_t fapl)
{
    hid_t dataset_id, dataspace_id;
    hid_t dcpl_id;
    herr_t status;
    char ds_name[32] = "";
    hsize_t     dims[2];
    hsize_t     max_dims[2];
    hsize_t     chunk_dims[2];
    int         i, j;
    int dset_data[4][6];
    hsize_t     new_size[2];

    hid_t file_id = H5Fcreate(file_name, H5F_ACC_TRUNC, H5P_DEFAULT, fapl);

    unsigned long t1 = 0;
    unsigned long t2 = 0;
    /* Create an unlimited dataspace for the dataset. */
    dims[0] = 40;
    dims[1] = 60;
    max_dims[0] = H5S_UNLIMITED;
    max_dims[1] = 60;
    dataspace_id = H5Screate_simple(2, dims, max_dims);

    /* Set up chunked dataset creation parameters */
    dcpl_id = H5Pcreate(H5P_DATASET_CREATE);
    chunk_dims[0] = 10;
    chunk_dims[1] = 10;
    status = H5Pset_chunk(dcpl_id, 2, chunk_dims);

    if(benchmark_type == 0){
        H5Fcreate(file_name, H5F_ACC_TRUNC, H5P_DEFAULT, fapl);
        for(int i = 0; i< comm_size; i++){
            sprintf(ds_name, "/dset_%d", i);
            dataset_id = H5Dcreate2(file_id, ds_name, H5T_NATIVE_INT, dataspace_id,
                                   H5P_DEFAULT, dcpl_id, H5P_DEFAULT);
            status = H5Dclose(dataset_id);
        }

        H5Fclose(file_id);

        file_id = H5Fopen(file_name, H5F_ACC_RDWR, fapl);

        for(int j = 0; j < comm_size; j++){
            sprintf(ds_name, "/dset_%d", j);
            //printf("2nd Dopen: ds_name = %s, my rank = %d\n", ds_name, my_rank);
            dataset_id = H5Dopen2(file_id, ds_name, H5P_DEFAULT);

            t1 = public_get_time_stamp_us();
            /* Extend each dataset to different sizes for each rank */
            new_size[0] = 40 * (j + 2); /* rank 0 = 80, rank 1 = 120, etc. */
            new_size[1] = 60;

            status = H5Dset_extent(dataset_id, new_size);

            t2 = public_get_time_stamp_us();

            status = H5Dclose(dataset_id);
        }

        H5Fclose(file_id);

        /* Re-open the file in serial and verify contents created independently */
        file_id = H5Fopen(file_name, H5F_ACC_RDONLY, H5P_DEFAULT);

        for(i = 0; i < comm_size; i++) {
            /* Open an existing dataset. */
            sprintf(ds_name, "/dset_%d", i);
            dataset_id = H5Dopen2(file_id, ds_name, H5P_DEFAULT);
            status = H5Dclose(dataset_id);
            DEBUG_PRINT
        }
        status = H5Sclose(dataspace_id);
        H5Fclose(file_id);
        DEBUG_PRINT
    } else {//RLO
         /* Create an unlimited dataspace for the dataset. */
         dims[0] = 40;
         dims[1] = 60;
         max_dims[0] = H5S_UNLIMITED;
         max_dims[1] = 60;

         chunk_dims[0] = 10;
         chunk_dims[1] = 10;
         status = H5Pset_chunk(dcpl_id, 2, chunk_dims);
         DEBUG_PRINT

         /* Create chunked datasets independently */
         DEBUG_PRINT
         sprintf(ds_name, "/dset_%d", my_rank);
         dataset_id = H5Dcreate2(file_id, ds_name, H5T_NATIVE_INT, dataspace_id,
                                H5P_DEFAULT, dcpl_id, H5P_DEFAULT);

         /* End access to the dataset and release resources used by it. */
         status = H5Dclose(dataset_id);

         status = H5Fclose(file_id);

         /* Re-open the file and extend the datasets */
         file_id = H5Fopen(file_name, H5F_ACC_RDWR, fapl);

         /* Open an existing dataset. */
         sprintf(ds_name, "/dset_%d", my_rank);
         dataset_id = H5Dopen2(file_id, ds_name, H5P_DEFAULT);

         /* Extend each dataset to different sizes for each rank */
         new_size[0] = 40 * (my_rank + 2);
         new_size[1] = 60;
         t1 = public_get_time_stamp_us();
         status = H5Dset_extent(dataset_id, new_size);
         t2 = public_get_time_stamp_us();
         status = H5Dclose(dataset_id);

         H5Fclose(file_id);

         file_id = H5Fopen(file_name, H5F_ACC_RDONLY, H5P_DEFAULT);

         for(i = 0; i < comm_size; i++) {
             sprintf(ds_name, "/dset_%d", i);
             dataset_id = H5Dopen2(file_id, ds_name, H5P_DEFAULT);
             status = H5Dclose(dataset_id);
             DEBUG_PRINT
         }
         status = H5Sclose(dataspace_id);
         H5Fclose(file_id);
    }
    return t2 - t1;
}

int main(int argc, char* argv[])
{
    hid_t fapl;
    const char* file_name = "rlo_test.h5";

    MPI_Init(NULL, NULL);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);

    printf("HDF5 RLO VOL test start...pid = %d, rank = %d\n", getpid(), my_rank);
    MY_RANK_DEBUG = my_rank;
    int benchmark_type = 0;
    unsigned long time_window = 50;
    //printf("1\n");
    if(argc == 4){
        benchmark_type = atoi(argv[1]);
        time_window = atoi(argv[2]);
        int sleep_time = atoi(argv[3]);

        sleep(sleep_time);
    } else if(argc == 3){
        //set to use RLO VOL
        benchmark_type = atoi(argv[1]);
        time_window = atoi(argv[2]);
    } else if(argc == 2){
        //set to use RLO VOL
        benchmark_type = atoi(argv[1]);
        time_window = 10*1000;
    } else {        //use RLO VOL
        time_window = 10*1000;
    }
    //printf("1.2, time_window = %lu\n", time_window);
    /* Create a new file using default properties. */

    fapl = H5Pcreate(H5P_FILE_ACCESS);
    //printf("1.3\n");

    H5Pset_fapl_mpio(fapl, MPI_COMM_WORLD, MPI_INFO_NULL);
    //printf("1.4\n");

    if(benchmark_type == 0){//baseline

    } else {//RLO_VOL
        //hid_t rlo_vol_id = H5VLregister_connector_by_name("rlo_pass_through", H5P_DEFAULT);
        //printf("1.5, rlo_vol_id = %llx\n", rlo_vol_id);
        extern const H5VL_class_t H5VL_rlo_pass_through_g;
       	hid_t rlo_vol_id = H5VLregister_connector(&H5VL_rlo_pass_through_g, H5P_DEFAULT);
        H5VL_rlo_pass_through_info_t rlo_vol_info;

        //hid_t baseline_vol_id = H5VLregister_connector_by_value(0, H5P_DEFAULT);

        rlo_vol_info.under_vol_id = H5VLregister_connector_by_value(0, H5P_DEFAULT);
        // printf("1.52, rlo_vol_info.under_vol_id = %llx\n", rlo_vol_info.under_vol_id);

        rlo_vol_info.under_vol_info = NULL;
        rlo_vol_info.mpi_comm = MPI_COMM_WORLD;
        rlo_vol_info.mpi_info = MPI_INFO_NULL;
        rlo_vol_info.time_window_size = time_window;
        rlo_vol_info.mode = benchmark_type;
        rlo_vol_info.world_size = comm_size;
        rlo_vol_info.my_rank = my_rank;
        H5Pset_vol(fapl, rlo_vol_id, &rlo_vol_info);
        //printf("1.6\n");

        H5VLclose(rlo_vol_id);
    }

    int num_ops = 1;
    //========================  Sub Test cases  ======================
    unsigned long t;
    t = ds_test(benchmark_type, file_name, fapl, num_ops);
    printf("HDF5 RLO VOL test done. ds_test take %lu usec, avg = %lu\n", t, (t / num_ops));

    t = group_test(benchmark_type, file_name, fapl, num_ops);
    printf("HDF5 RLO VOL test done. group_test took %lu usec, avg = %lu\n", t, (t / num_ops));

    t = attr_test(benchmark_type, file_name, fapl, num_ops);
    printf("HDF5 RLO VOL test done. attr_test took %lu usec,  avg = %lu\n", t, (t / num_ops));

    t= dset_extend_test(benchmark_type, file_name, fapl);
    printf("HDF5 RLO VOL test done. dset_extend_test took %lu usec,  avg = %lu\n", t, t);

    t = dt_commit_test(benchmark_type, file_name, fapl, num_ops);
    printf("HDF5 RLO VOL test done. dt_commit_test took %lu usec,  avg = %lu\n", t, (t / num_ops));
    //=================================================================
    H5Pclose(fapl);

    H5close();
    //rintf("HDF5 library shut down.\n");

    DEBUG_PRINT

    MPI_Finalize();
    return 0;
}

