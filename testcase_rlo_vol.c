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

time_stamp public_get_time_stamp_us(){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return 1000000 * tv.tv_sec + tv.tv_usec;
}

unsigned long ds_test(int benchmark_type, const char* file_name, hid_t fapl, int num_ops){
    DEBUG_PRINT
    hid_t dataset_id, dataspace_id;
    herr_t status;
    char ds_name[32] = "";
    hsize_t     dims[2];
    int         i, j;
    int dset_data[4][6];

    hid_t file_id = H5Fcreate(file_name, H5F_ACC_TRUNC, H5P_DEFAULT, fapl);//H5P_DEFAULT

    /* Create the data space for the dataset. */
    dims[0] = 4;
    dims[1] = 6;
    dataspace_id = H5Screate_simple(2, dims, NULL);

    unsigned long t1 = public_get_time_stamp_us();
    if(benchmark_type == 0){//baseline
        for(i = 0; i < comm_size; i++) {

            for(int j = 0; j < num_ops; j++){
                sprintf(ds_name, "/dset_%d_%d", i, j);
                dataset_id = H5Dcreate2(file_id, ds_name, H5T_NATIVE_INT, dataspace_id,
                        H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
                status = H5Dclose(dataset_id);
            }
            /* Open an existing dataset. */

        //    status = H5Dread(dataset_id, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, dset_data);

            /* Close the dataset. */

            DEBUG_PRINT
        }
    } else {//RLO
        for(int j = 0; j < num_ops; j++){
            sprintf(ds_name, "/dset_%d_%d", my_rank, j);
            dataset_id = H5Dcreate2(file_id, ds_name, H5T_NATIVE_INT, dataspace_id,
                    H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
            status = H5Dclose(dataset_id);
        }
        DEBUG_PRINT
    }
    unsigned long t2 = public_get_time_stamp_us();
    /* Create the dataset. */


    /* Terminate access to the data space. */
    status = H5Sclose(dataspace_id);
    //printf("6: space closed.\n");
    DEBUG_PRINT

    /* Close the file. */
    status = H5Fclose(file_id);
    DEBUG_PRINT
    //printf("7: file closed.\n");


    /* Re-open the file in serial and verify contents created independently */
//    file_id = H5Fopen(file_name, H5F_ACC_RDONLY, H5P_DEFAULT);

//    for(i = 0; i < comm_size; i++) {
//        /* Open an existing dataset. */
//        sprintf(ds_name, "/dset_%d", i);
//        dataset_id = H5Dopen2(file_id, ds_name, H5P_DEFAULT);
//    //    status = H5Dread(dataset_id, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, dset_data);
//        /* Close the dataset. */
//        status = H5Dclose(dataset_id);
//        DEBUG_PRINT
//
//    }
    //printf("Dataset test completed. rank = %d\n", my_rank);
//    H5Fclose(file_id);
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
    //file_id = H5Fopen(file_name, H5F_ACC_RDONLY, H5P_DEFAULT);
    //printf("8\n");

//    for(int i = 0; i < comm_size; i++) {
//        //printf("9\n");
//        sprintf(group_name, "/group_%d", i);
//        group_id = H5Gopen2(file_id, group_name, H5P_DEFAULT);
//
//        status = H5Gclose(group_id);
//        //printf("11\n");
//        DEBUG_PRINT
////    printf("13\n");
//    }
    //H5Fclose(file_id);
    //printf("Group test completed. rank = %d\n", my_rank);
    return t2 - t1;
}

unsigned long attr_test(int benchmark_type, const char* file_name, hid_t fapl, int num_ops){
    hid_t       file_id, dataset_id, attribute_id, dataspace_id;  /* identifiers */
    hsize_t     dims;
    int         attr_data[2];
    herr_t      status;
    char ds_name[32] = "";
    char attr_name[32] = "";
    /* Initialize the attribute data. */
    attr_data[0] = 100;
    attr_data[1] = 200;

    int* attr_data_array = calloc(comm_size, sizeof(int));
    for(int i = 0; i < comm_size; i++){
        attr_data_array[i] = i;
    }

    /* Create a file. */
    file_id = H5Fcreate(file_name, H5F_ACC_TRUNC, H5P_DEFAULT, fapl);

    dims = 10;
    dataspace_id = H5Screate_simple(1, &dims, NULL);

    /* Create a dataset. */
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

                status = H5Aclose(attribute_id);

                /* Close to the dataset. */
                status = H5Dclose(dataset_id);
                //printf("%s:%u, rank = %d dataset closed. \n", __func__, __LINE__, my_rank);
                /* Close the file. */
            }
        }


    } else {//RLO
        for(int j = 0; j < num_ops; j++){
            sprintf(ds_name, "/dset_%d_%d", my_rank, j);
            dataset_id = H5Dcreate2(file_id, ds_name, H5T_NATIVE_INT, dataspace_id,
                                   H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

            //printf("%s:%u, rank = %d Dataset created. ds_name = [%s]\n", __func__, __LINE__, my_rank, ds_name);
            /* Create the data space for the attribute. */
            dims = 2;

            /* Create a dataset attribute. */
            sprintf(attr_name, "/attr_%d_%d", my_rank, j);
            attribute_id = H5Acreate2 (dataset_id, attr_name, H5T_NATIVE_INT, dataspace_id,
                                      H5P_DEFAULT, H5P_DEFAULT);
            //printf("%s:%u, rank = %d Attribute created. attr_name = [%s]\n", __func__, __LINE__, my_rank, attr_name);

            /* Write the attribute data. */
            //status = H5Awrite(attribute_id, H5T_NATIVE_INT, &(attr_data_array[my_rank]));
            //printf("%s:%u, rank = %d Attribute write complete. \n", __func__, __LINE__, my_rank);
            /* Close the attribute. */
            status = H5Aclose(attribute_id);
            //printf("%s:%u, rank = %d Attribute closed. \n", __func__, __LINE__, my_rank);
            /* Close the dataspace. */

            /* Close to the dataset. */
            status = H5Dclose(dataset_id);
            //printf("%s:%u, rank = %d dataset closed. \n", __func__, __LINE__, my_rank);
        }

    }
    unsigned long t2 = public_get_time_stamp_us();
    status = H5Sclose(dataspace_id);
    status = H5Fclose(file_id);
    //printf("Attribute test completed. rank = %d\n", my_rank);
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

    DEBUG_PRINT
    hid_t file_id = H5Fcreate(file_name, H5F_ACC_TRUNC, H5P_DEFAULT, fapl);
    DEBUG_PRINT

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
    DEBUG_PRINT

    /* Create chunked datasets independently */
    DEBUG_PRINT
    sprintf(ds_name, "/dset_%d", my_rank);
    dataset_id = H5Dcreate2(file_id, ds_name, H5T_NATIVE_INT, dataspace_id,
                           H5P_DEFAULT, dcpl_id, H5P_DEFAULT);
    DEBUG_PRINT

    //    /* Write the dataset. */
    //    status = H5Dwrite(dataset_id, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, dset_data);
    //    printf("10\n");

    /* End access to the dataset and release resources used by it. */
    status = H5Dclose(dataset_id);
    DEBUG_PRINT

    /* Terminate access to the data space. */
    status = H5Sclose(dataspace_id);
    DEBUG_PRINT

    /* Close the file. */
    status = H5Fclose(file_id);
    DEBUG_PRINT


#ifndef QAK
{
    hsize_t     new_size[2];

    /* Re-open the file and extend the datasets */
    file_id = H5Fopen(file_name, H5F_ACC_RDWR, fapl);

    /* Open an existing dataset. */
    sprintf(ds_name, "/dset_%d", my_rank);
    dataset_id = H5Dopen2(file_id, ds_name, H5P_DEFAULT);

    /* Extend each dataset to different sizes for each rank */
    new_size[0] = 40 * (my_rank + 2); /* rank 0 = 80, rank 1 = 120, etc. */
    new_size[1] = 60;
    status = H5Dset_extent(dataset_id, new_size);
    DEBUG_PRINT

    /* Close the dataset. */
    status = H5Dclose(dataset_id);
    DEBUG_PRINT

    H5Fclose(file_id);
}
#endif /* QAK */


    /* Re-open the file in serial and verify contents created independently */
    file_id = H5Fopen(file_name, H5F_ACC_RDONLY, H5P_DEFAULT);

    for(i = 0; i < comm_size; i++) {
        /* Open an existing dataset. */
        sprintf(ds_name, "/dset_%d", i);
        dataset_id = H5Dopen2(file_id, ds_name, H5P_DEFAULT);

    //    status = H5Dread(dataset_id, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, dset_data);

        /* Close the dataset. */
        status = H5Dclose(dataset_id);
        DEBUG_PRINT
    }

    H5Fclose(file_id);

    return 0;
}

int main(int argc, char* argv[])
{
    hid_t       fapl;//, dataspace_id;  /* identifiers */
    const char* file_name = "rlo_test.h5";

    int dset_data[4][6];//DO NOT delete, will crash.

    MPI_Init(NULL, NULL);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &comm_size);

    printf("HDF5 RLO VOL test start...pid = %d, rank = %d\n", getpid(), my_rank);
//

    int benchmark_type = 0;
    unsigned long time_window = 50;
    //printf("1\n");
    if(argc == 4){
        benchmark_type = atoi(argv[1]);
        time_window = atoi(argv[2]);
        int sleep_time = atoi(argv[3]);
        //printf("Waiting %d sec for gdb to attach....pid = %d\n", sleep_time, getpid());
        sleep(sleep_time);
    } else if(argc == 3){
        //set to use RLO VOL
        benchmark_type = atoi(argv[1]);
        time_window = atoi(argv[2]);
    } else {
        //use RLO VOL
        time_window = 5*1000;
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
        printf("1.52, rlo_vol_info.under_vol_id = %llx\n", rlo_vol_info.under_vol_id);

        rlo_vol_info.under_vol_info = NULL;
        rlo_vol_info.mpi_comm = MPI_COMM_WORLD;
        rlo_vol_info.mpi_info = MPI_INFO_NULL;
        rlo_vol_info.time_window_size = time_window;
        rlo_vol_info.mode = benchmark_type;
        rlo_vol_info.world_size = comm_size;
        //printf("%s:%d:mode = %d, world_size = %lu, window size =  %d\n", __func__, __LINE__, benchmark_type, comm_size, time_window);
        H5Pset_vol(fapl, rlo_vol_id, &rlo_vol_info);
        //printf("1.6\n");

        H5VLclose(rlo_vol_id);
    }

    int num_ops = 100;
    //========================  Sub Test cases  ======================
    unsigned long t;
    t = ds_test(benchmark_type, file_name, fapl, num_ops);
    printf("HDF5 RLO VOL test done. ds_test take %lu usec, avg = %d\n", t, t/num_ops);

    t = group_test(benchmark_type, file_name, fapl, num_ops);
    printf("HDF5 RLO VOL test done. group_test took %lu usec, avg = %d \n", t, t/num_ops);

    t = attr_test(benchmark_type, file_name, fapl, num_ops);
    printf("HDF5 RLO VOL test done. attr_test took %lu usec,  avg = %d \n", t, t/num_ops);

    // dset_extend_test(benchmark_type, file_name, fapl);
    //=================================================================
    H5Pclose(fapl);
    //printf("2.5\n");
    H5close();
    //rintf("HDF5 library shut down.\n");

    DEBUG_PRINT

    MPI_Finalize();
    return 0;
}

