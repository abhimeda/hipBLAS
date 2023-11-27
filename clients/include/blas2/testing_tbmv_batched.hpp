/* ************************************************************************
 * Copyright (C) 2016-2023 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * ************************************************************************ */

#include <fstream>
#include <iostream>
#include <stdlib.h>
#include <vector>

#include "testing_common.hpp"

/* ============================================================================================ */

using hipblasTbmvBatchedModel
    = ArgumentModel<e_a_type, e_uplo, e_transA, e_diag, e_M, e_K, e_lda, e_incx, e_batch_count>;

inline void testname_tbmv_batched(const Arguments& arg, std::string& name)
{
    hipblasTbmvBatchedModel{}.test_name(arg, name);
}

template <typename T>
void testing_tbmv_batched(const Arguments& arg)
{
    bool FORTRAN = arg.api == hipblas_client_api::FORTRAN;
    auto hipblasTbmvBatchedFn
        = FORTRAN ? hipblasTbmvBatched<T, true> : hipblasTbmvBatched<T, false>;

    hipblasFillMode_t  uplo        = char2hipblas_fill(arg.uplo);
    hipblasOperation_t transA      = char2hipblas_operation(arg.transA);
    hipblasDiagType_t  diag        = char2hipblas_diagonal(arg.diag);
    int                M           = arg.M;
    int                K           = arg.K;
    int                lda         = arg.lda;
    int                incx        = arg.incx;
    int                batch_count = arg.batch_count;

    int    abs_incx = incx >= 0 ? incx : -incx;
    size_t A_size   = size_t(lda) * M;

    hipblasStatus_t status = HIPBLAS_STATUS_SUCCESS;

    hipblasLocalHandle handle(arg);

    // argument sanity check, quick return if input parameters are invalid before allocating invalid
    // memory
    bool invalid_size = M < 0 || K < 0 || lda < K + 1 || !incx || batch_count < 0;
    if(invalid_size || !M || !batch_count)
    {
        hipblasStatus_t actual = hipblasTbmvBatchedFn(
            handle, uplo, transA, diag, M, K, nullptr, lda, nullptr, incx, batch_count);
        EXPECT_HIPBLAS_STATUS(
            actual, (invalid_size ? HIPBLAS_STATUS_INVALID_VALUE : HIPBLAS_STATUS_SUCCESS));
        return;
    }

    double gpu_time_used, hipblas_error;

    // arrays of pointers-to-host on host
    host_batch_vector<T> hA(A_size, 1, batch_count);
    host_batch_vector<T> hx(M, incx, batch_count);
    host_batch_vector<T> hx_cpu(M, incx, batch_count);
    host_batch_vector<T> hx_res(M, incx, batch_count);

    // device arrays
    device_batch_vector<T> dA(A_size, 1, batch_count);
    device_batch_vector<T> dx(M, incx, batch_count);

    CHECK_HIP_ERROR(dA.memcheck());
    CHECK_HIP_ERROR(dx.memcheck());

    hipblas_init_vector(hA, arg, hipblas_client_never_set_nan, true);
    hipblas_init_vector(hx, arg, hipblas_client_never_set_nan, false, true);

    hx_cpu.copy_from(hx);

    CHECK_HIP_ERROR(dx.transfer_from(hx));
    CHECK_HIP_ERROR(dA.transfer_from(hA));

    if(arg.unit_check || arg.norm_check)
    {
        /* =====================================================================
            HIPBLAS
        =================================================================== */
        CHECK_HIPBLAS_ERROR(hipblasTbmvBatchedFn(handle,
                                                 uplo,
                                                 transA,
                                                 diag,
                                                 M,
                                                 K,
                                                 dA.ptr_on_device(),
                                                 lda,
                                                 dx.ptr_on_device(),
                                                 incx,
                                                 batch_count));

        CHECK_HIP_ERROR(hx_res.transfer_from(dx));

        /* =====================================================================
           CPU BLAS
        =================================================================== */
        for(int b = 0; b < batch_count; b++)
        {
            cblas_tbmv<T>(uplo, transA, diag, M, K, hA[b], lda, hx_cpu[b], incx);
        }

        // enable unit check, notice unit check is not invasive, but norm check is,
        // unit check and norm check can not be interchanged their order
        if(arg.unit_check)
        {
            unit_check_general<T>(1, M, batch_count, abs_incx, hx_cpu, hx_res);
        }
        if(arg.norm_check)
        {
            hipblas_error = norm_check_general<T>('F', 1, M, abs_incx, hx_cpu, hx_res, batch_count);
        }
    }

    if(arg.timing)
    {
        CHECK_HIP_ERROR(dx.transfer_from(hx));

        hipStream_t stream;
        CHECK_HIPBLAS_ERROR(hipblasGetStream(handle, &stream));

        int runs = arg.cold_iters + arg.iters;
        for(int iter = 0; iter < runs; iter++)
        {
            if(iter == arg.cold_iters)
                gpu_time_used = get_time_us_sync(stream);

            CHECK_HIPBLAS_ERROR(hipblasTbmvBatchedFn(handle,
                                                     uplo,
                                                     transA,
                                                     diag,
                                                     M,
                                                     K,
                                                     dA.ptr_on_device(),
                                                     lda,
                                                     dx.ptr_on_device(),
                                                     incx,
                                                     batch_count));
        }
        gpu_time_used = get_time_us_sync(stream) - gpu_time_used;

        hipblasTbmvBatchedModel{}.log_args<T>(std::cout,
                                              arg,
                                              gpu_time_used,
                                              tbmv_gflop_count<T>(M, K),
                                              tbmv_gbyte_count<T>(M, K),
                                              hipblas_error);
    }
}