#ifndef NORMAL_DISTRIBUTIONS_H_
#define NORMAL_DISTRIBUTIONS_H_

/*
 MIT License

 Copyright (c) 2024 Carlos Cabaço Tojal

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.

 */

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <pthread.h>

#include <ndnet_core/voxel.h>
#include <ndnet_core/pointclouds.h>
#include <ndnet_core/matrix.h>

#define NUM_PCL_WORKERS 8 // number of workers for bulk point cloud processing tasks

struct normal_distribution_t {
    unsigned long index; // index of the distribution
    double mean[3]; // xyz mean of the distribution (3-d)
    double old_mean[3]; // last mean iteration (3-d)
    double covariance[9]; // flattened covariance matrix (9-d)
    double m2[3]; // sum of squared differences. used to compute variances
    unsigned long num_samples; // number of samples
    unsigned short class; // most frequent class of the distribution
    unsigned int *num_class_samples; // number of samples per class
    bool being_updated; // flag to indicate if the distribution is being updated
};

struct pcl_worker_args_t {
    double *point_cloud; // pointer to the point cloud
    unsigned long num_points; // number of points in the point cloud
    unsigned short *classes; // pointer to the point classes
    unsigned short num_classes;
    struct normal_distribution_t *nd_array; // pointer to the array of normal distributions
    pthread_mutex_t *mutex_array; // pointer to the array of mutexes
    pthread_cond_t *cond_array; // pointer to the array of condition variables
    double voxel_size; // voxel size for distribution sampling
    int len_x; // number of voxels in the "x" dimension
    int len_y; // number of voxels in the "y" dimension
    int len_z; // number of voxels in the "z" dimension
    double x_offset; // offset in the "x" dimension
    double y_offset; // offset in the "y" dimension
    double z_offset; // offset in the "z" dimension
    int worker_id; // worker id
};

#ifdef __cplusplus
extern "C" {
#endif

/*! \brief Worker routine for normal distribution update. */
void *pcl_worker(void *arg);

/*! \brief Estimate the normal distributions on the point cloud. Estimate a normal distribution per voxel of size "voxel_size".
    \param point_cloud Pointer to the point cloud.
    \param num_points Number of points in the point cloud.
    \param classes Point classes array.
    \param num_classes Number of classes.
    \param voxel_size Voxel size for distribution sampling.
    \param len_x Number of voxels in the "x" dimension.
    \param len_y Number of voxels in the "y" dimension.
    \param len_z Number of voxels in the "z" dimension.
    \param nd_array Pointer to the array of normal distributions. Will be overwritten.
    \param num_nds Number of normal distributions. Will be overwritten.
*/
int estimate_ndt(double *point_cloud, unsigned long num_points, 
                    unsigned short *classes, unsigned short num_classes,
                    double voxel_size,
                    int len_x, int len_y, int len_z,
                    double x_offset, double y_offset, double z_offset,
                    struct normal_distribution_t *nd_array,
                    unsigned long *num_nds);

/*! \brief Print the normal distribution.
    \param nd Normal distribution.
*/
void print_nd(struct normal_distribution_t nd);

/*! \brief Print the normal distributions.
    \param nd_array Pointer to the array of normal distributions.
    \param num_nds Number of normal distributions.
*/
void print_nds(struct normal_distribution_t *nd_array, int len_x, int len_y, int len_z);

#ifdef __cplusplus
}
#endif

#endif // NORMAL_DISTRIBUTIONS_H_