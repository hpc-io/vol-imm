Building and installing the Independent Metadata Modification VOL connector:
----------------------------------------------------------------------------

- Edit the Makefile to reflect the correct compiler, and installed location of
    the HDF5 and 'rootless' packages.

- Run 'make'.  This should build a shared library that contains the IMM
    VOL connector, along with a testing and benchmarking program called
    testcase_rlo_vol.

- Run the testcase_rlo_vol program to verify correct operation:
        "mpirun -np 8 ./testcase_rlo_vol"


Using the Independent Metadata Modification VOL connector in your code:
-----------------------------------------------------------------------

- Application developers can either set environment variables to indicate
    the location of the IMM VOL connector and its parameters:
        % export HDF5_PLUGIN_PATH=/path/to/imm/vol/connector/shared/library
        % export HDF5_VOL_CONNECTOR="imm time_window=10000;under_vol=0;under_info={}"

    When the environment variables are set and an application is linked against
    a version of HDF5 that supports the VOL framework (v1.12+), the IMM VOL
    connector will automatically be loaded and used by the application.

- Application developers can also change their application code to use the
    IMM VOL connector explicitly, by configuring a H5VL_rlo_pass_through_info_t
    struct and setting it on a file access property list to use when opening
    a file.  See the code in testcase_rlo_vol.c that does this, for an
    example.

- The 'time_window' parameter, for both the environment variable and the
    field in H5VL_rlo_pass_through_info_t is the number of microseconds to
    wait for receiving out-of-order operations from other MPI ranks.  This
    value may be set as low are 2000us for small levels of concurrency, but
    mey require larger values for high #'s of MPI ranks.  In the future, this
    limitation (and this parameter) may be removed.


Guidelines for Independent Metadata Modification in your application:
---------------------------------------------------------------------

- When the IMM VOL connector is _not_ used, the HDF5 library requires operations
    that modify the metadata for an HDF5 file to be performed collectively.
    The list of HDF5 API routines with this requirements is available
    here: https://confluence.hdfgroup.org/display/HDF5/Collective+Calling+Requirements+in+Parallel+HDF5+Applications

- When the IMM VOL connector _is_ used, the following HDF5 API routines may
    be called independently: H5Dcreate2, H5Gcreate2, H5Acreate2   This
    document will be updated as more operations are added over time.


Getting help with the Independent Metadata Modification VOL Connector:
----------------------------------------------------------------------

- Please contact Tonglin (Tony) Li at: tonglinli (@) lbl.gov or Quincey
    Koziol at: koziol (@) lbl.gov for assistance with the IMM VOL connector.

