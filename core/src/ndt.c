#include <ndtnetpp_core/ndt.h>

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

void *pcl_worker(void *arg) {

    // get the worker arguments
    struct pcl_worker_args_t *args = (struct pcl_worker_args_t *) arg;

    // get the point range for the worker from the worker id
    unsigned long start = args->worker_id * (args->num_points/ NUM_PCL_WORKERS);
    unsigned long end = (args->worker_id + 1) * (args->num_points / NUM_PCL_WORKERS);

    // iterate over the points
    for(unsigned long i = start; i < end; i++) {

        // check if the point cloud is finished
        if(i >= args->num_points)
            break;

        // get the voxel indexes for the point
        unsigned int voxel_x, voxel_y, voxel_z;
        double x_offset = 0, y_offset = 0, z_offset = 0;
        if(metric_to_voxel_space(&args->point_cloud[i*3], args->voxel_size, args->len_x, args->len_y, args->len_z, 
                                args->x_offset, args->y_offset, args->z_offset,
                                &voxel_x, &voxel_y, &voxel_z) < 0) {
            fprintf(stderr, "Error converting point to voxel space!\n");
            return NULL;
        }
        unsigned long voxel_index;
        if(voxel_pos_to_index(voxel_x, voxel_y, voxel_z, args->len_x, args->len_y, args->len_z, &voxel_index) < 0) {
            fprintf(stderr, "Error converting voxel position to index!\n");
            return NULL;
        }

        // lock the mutex for the voxel
        if(pthread_mutex_lock(&args->mutex_array[voxel_index]) != 0) {
            fprintf(stderr, "Error locking distribution mutex: %s\n", strerror(errno));
            return NULL;
        }

        // wait for the condition variable
        while(args->nd_array[voxel_index].being_updated) {
            if(pthread_cond_wait(&args->cond_array[voxel_index], &args->mutex_array[voxel_index]) != 0) {
                fprintf(stderr, "Error waiting for condition variable: %s\n", strerror(errno));
                return NULL;
            }
        }

        // update the normal distribution for the voxel
        args->nd_array[voxel_index].num_samples++;
        // iterate the 3 dimensions of the point sample
        for(int j = 0; j < 3; j++) {
            // copy the old mean
            args->nd_array[voxel_index].old_mean[j] = args->nd_array[voxel_index].mean[j];
            // update the mean
            args->nd_array[voxel_index].mean[j] += (args->point_cloud[i*3+j] - args->nd_array[voxel_index].mean[j]) / args->nd_array[voxel_index].num_samples;
            // update the sum of squared differences for the variances
            args->nd_array[voxel_index].m2[j] += (args->point_cloud[i*3+j] - args->nd_array[voxel_index].old_mean[j]) * (args->point_cloud[i*3+j] - args->nd_array[voxel_index].mean[j]);

            // iterate the other dimensions to update the covariance matrix
            for(int k = 0; k < 3; k++) {
                // it's a diagonal, so it's a variance and not a covariance
                if(j == k)
                    continue;

                // update the covariance matrix
                args->nd_array[voxel_index].covariance[j*3+k] += (args->point_cloud[i*3+j] - args->nd_array[voxel_index].mean[j]) * (args->point_cloud[i*3+k] - args->nd_array[voxel_index].mean[k]) / args->nd_array[voxel_index].num_samples;            
            }
        }

        // update the variances
        for(int j = 0; j < 3; j++) {
            args->nd_array[voxel_index].covariance[j*3+j] = args->nd_array[voxel_index].m2[j] / args->nd_array[voxel_index].num_samples;
        }

        // update the class if classes were provided
        if(args->classes != NULL) {
            // get the class of the point
            unsigned short point_class = args->classes[i];
            // update the class of the distribution
            args->nd_array[voxel_index].num_class_samples[point_class]++;

            // find the most frequent class
            unsigned int max_class_samples = 0;
            for(unsigned short j = 0; j <= args->num_classes; j++) {
                if(args->nd_array[voxel_index].num_class_samples[j] > max_class_samples) {
                    max_class_samples = args->nd_array[voxel_index].num_class_samples[j];
                    args->nd_array[voxel_index].class = j;
                }
            }
        }
        
        // unlock the mutex for the voxel
        if(pthread_mutex_unlock(&args->mutex_array[voxel_index]) != 0) {
            fprintf(stderr, "Error unlocking distribution mutex: %s\n", strerror(errno));
            return NULL;
        }

        // signal the condition variable
        if(pthread_cond_signal(&args->cond_array[voxel_index]) != 0) {
            fprintf(stderr, "Error signaling condition variable: %s\n", strerror(errno));
            return NULL;
        }
    }
}


int estimate_ndt(double *point_cloud, unsigned long num_points,
                    unsigned short *classes, unsigned short num_classes,
                    double voxel_size,
                    int len_x, int len_y, int len_z,
                    double x_offset, double y_offset, double z_offset,
                    struct normal_distribution_t *nd_array,
                    unsigned long *num_nds) {

    *num_nds = 0;

    #pragma omp parallel for
    for(int i = 0; i < len_x * len_y * len_z; i++) {
        // initialize the normal distributions
        nd_array[i].num_samples = 0;
        nd_array[i].index = i;
        nd_array[i].num_class_samples = NULL;
        // if classes were provided, allocate memory for the number of samples per class
        // initialize with zeross
        if(classes != NULL) {
            nd_array[i].num_class_samples = (unsigned int *) calloc((num_classes + 1), sizeof(unsigned int));
            if(nd_array[i].num_class_samples == NULL) {
                fprintf(stderr, "Error allocating memory for class samples: %s\n", strerror(errno));
                return -1;
            }
        }
        for(int j = 0; j < 3; j++) {
            nd_array[i].mean[j] = 0;
            nd_array[i].m2[j] = 0;
            for(int k = 0; k < 3; k++) {
                nd_array[i].covariance[j*3+k] = 0;
            }
        }
        nd_array[i].being_updated = false;
    }

    // create an array of mutexes, one per voxel
    pthread_mutex_t *mutex_array = (pthread_mutex_t *) malloc(len_x * len_y * len_z * sizeof(pthread_mutex_t));
    if(mutex_array == NULL) {
        fprintf(stderr, "Error allocating memory for distribution mutexes: %s\n", strerror(errno));
        return -2;
    }

    // initialize the mutexes
    #pragma omp parallel for
    for(int i = 0; i < len_x * len_y * len_z; i++) {
        if(pthread_mutex_init(&mutex_array[i], NULL) != 0) {
            fprintf(stderr, "Error initializing distribution mutex: %s\n", strerror(errno));
            return -3;
        }
    }

    // create an array of condition variables, one per voxel
    pthread_cond_t *cond_array = (pthread_cond_t *) malloc(len_x * len_y * len_z * sizeof(pthread_cond_t));
    if(cond_array == NULL) {
        fprintf(stderr, "Error allocating memory for condition variables: %s\n", strerror(errno));
        return -5;
    }

    // initialize the condition variables
    #pragma omp parallel for
    for(int i = 0; i < len_x * len_y * len_z; i++) {
        if(pthread_cond_init(&cond_array[i], NULL) != 0) {
            fprintf(stderr, "Error initializing condition variable: %s\n", strerror(errno));
            return -6;
        }
    }

    // allocate a pool of threads
    pthread_t *threads = (pthread_t *) malloc(NUM_PCL_WORKERS * sizeof(pthread_t));
    if(threads == NULL) {
        fprintf(stderr, "Error allocating memory for threads: %s\n", strerror(errno));
        return -7;
    }

    // create an array of worker arguments
    struct pcl_worker_args_t *args_array = (struct pcl_worker_args_t *) calloc(NUM_PCL_WORKERS, sizeof(struct pcl_worker_args_t));
    if(args_array == NULL) {
        fprintf(stderr, "Error allocating memory for worker arguments: %s\n", strerror(errno));
        return -8;
    }

    // create the threads
    for(int i = 0; i < NUM_PCL_WORKERS; i++) {

        // create the worker arguments
        struct pcl_worker_args_t *args = &args_array[i];
        args->point_cloud = point_cloud;
        args->num_points = num_points;
        args->classes = classes;
        args->num_classes = num_classes;
        args->nd_array = nd_array;
        args->mutex_array = mutex_array;
        args->cond_array = cond_array;
        args->voxel_size = voxel_size;
        args->len_x = len_x;
        args->len_y = len_y;
        args->len_z = len_z;
        args->x_offset = x_offset;
        args->y_offset = y_offset;
        args->z_offset = z_offset;
        args->worker_id = i;

        if(pthread_create(&threads[i], NULL, pcl_worker, (void *) args) != 0) {
            fprintf(stderr, "Error creating thread: %s\n", strerror(errno));
            return -9;
        }
    }

    // wait for the threads to finish
    for(int i = 0; i < NUM_PCL_WORKERS; i++) {
        if(pthread_join(threads[i], NULL) != 0) {
            fprintf(stderr, "Error joining thread: %s\n", strerror(errno));
            return -10;
        }
    }

    // destroy the mutexes
    // destroy distribution mutexes
    for(unsigned long i = 0; i < len_x * len_y * len_z; i++) {
        if(pthread_mutex_destroy(&mutex_array[i]) < 0) {
            fprintf(stderr, "Error destroying distribution mutex: %s\n", strerror(errno));
            return -7;
        }
    }

    // count the number of normal distributions
    for(unsigned long i = 0; i < len_x * len_y * len_z; i++) {
        if(nd_array[i].num_samples > 0) {
            (*num_nds)++;
        }
    }

    // free the array of mutexes
    free(mutex_array);

    // free the array of condition variables
    free(cond_array);

    // free the pool of threads
    free(threads);

    // free the array of worker arguments
    free(args_array);

    // return 0 in case of success
    return 0;  
}

void ndt_downsample(double *point_cloud, unsigned short point_dim, unsigned long num_points,
                    unsigned short *classes, unsigned short num_classes,
                    unsigned long num_desired_points,
                    double *downsampled_point_cloud, unsigned long *num_downsampled_points,
                    double *covariances,
                    unsigned short *downsampled_classes) {

    // get the point cloud limits
    double max_x, max_y, max_z;
    double min_x, min_y, min_z;
    get_pointcloud_limits(point_cloud, point_dim, num_points, &max_x, &max_y, &max_z, &min_x, &min_y, &min_z);

    // estimate the voxel size
    double voxel_size;
    int len_x, len_y, len_z;
    double x_offset, y_offset, z_offset;

    // create a grid of normal distributions
    struct normal_distribution_t *nd_array = NULL;

    double guess = (double) (MAX_VOXEL_GUESS - MIN_VOXEL_GUESS) / 2.0;
    double min_guess = MIN_VOXEL_GUESS;
    double max_guess = MAX_VOXEL_GUESS;

    unsigned long num_nds;
    unsigned int iter = 0;
    do {

        estimate_voxel_grid(max_x, max_y, max_z, min_x, min_y, min_z, guess, &len_x, &len_y, &len_z,
                            &x_offset, &y_offset, &z_offset);

        // allocate the normal distributions
        nd_array = (struct normal_distribution_t *) realloc(nd_array, len_x * len_y * len_z * sizeof(struct normal_distribution_t));
        if(nd_array == NULL) {
            fprintf(stderr, "Error allocating memory for normal distributions: %s\n", strerror(errno));
            return;
        }

        if(estimate_ndt(point_cloud, num_points, classes, num_classes, guess, len_x, len_y, len_z, x_offset, y_offset, z_offset, nd_array, &num_nds) < 0) {
            fprintf(stderr, "Error estimating normal distributions!\n");
            return;
        }

        // adjust the voxel size guess limits for binary search
        if(num_nds > num_desired_points * (1+DOWNSAMPLE_UPPER_THRESHOLD)) {
            min_guess = guess;
        } else if(num_nds < num_desired_points) {
            max_guess = guess;
        } else {
            // reached a valid number of normal distributions
            break;
        }

        // get the next guess
        guess = min_guess + (max_guess - min_guess) / 2.0;

        iter++;

    } while(iter < MAX_GUESS_ITERATIONS);

    voxel_size = guess;

    if(iter == MAX_GUESS_ITERATIONS) {
        fprintf(stderr, "Reached maximum number of iterations!\n");
        return;
    }

    // compute the divergences and remove the distributions with the smallest divergence
    unsigned long num_valid_nds;
    prune_nds(nd_array, len_x, len_y, len_z, num_desired_points, &num_valid_nds);

    // downsample the point cloud, iterating the voxels
    unsigned long downsampled_index = 0;
    #pragma omp parallel for
    for(int z = 0; z < len_z; z++) {
        for(int y = 0; y < len_y; y++) {
            for(int x = 0; x < len_x; x++) {

                // get the index of the current voxel
                unsigned long index;
                if(voxel_pos_to_index(x, y, z, len_x, len_y, len_z, &index) < 0) {
                    fprintf(stderr, "Error converting voxel position to index!\n");
                    return;
                }

                // verify if the voxel has samples
                if(nd_array[index].num_samples == 0)
                    continue;

                // get the point in metric space
                double point[3];
                voxel_to_metric_space(x, y, z, len_x, len_y, len_z, x_offset, y_offset, z_offset, voxel_size, point);

                // copy the point to the downsampled point cloud
                for(int i = 0; i < 3; i++) {
                    downsampled_point_cloud[downsampled_index*3 + i] = point[i];
                }
                // copy the covariance matrix
                memcpy(&covariances[downsampled_index*9], nd_array[index].covariance, 9 * sizeof(double));
                // copy the class
                if(classes != NULL) {
                    downsampled_classes[downsampled_index] = nd_array[index].class;
                }

                downsampled_index++;
            }
        }
    }

    // free the normal distributions
    // free the classes count pointer
    free(nd_array->num_class_samples);
    free(nd_array);

    // set the number of downsampled points
    *num_downsampled_points = downsampled_index;

    // print_matrix(downsampled_point_cloud, *num_downsampled_points, 3);
}

void print_nd(struct normal_distribution_t nd) {

    printf("Normal distribution %lu\n", nd.index);
    printf("Number of samples: %lu\n", nd.num_samples);
    printf("Mean: %f %f %f\n", nd.mean[0], nd.mean[1], nd.mean[2]);
    printf("Covariance:\n");
    print_matrix(nd.covariance, 3, 3);
    printf("\n");
}

void print_nds(struct normal_distribution_t *nd_array, int len_x, int len_y, int len_z) {

    unsigned int x, y, z;
    
    for(unsigned long i = 0; i < len_x * len_y * len_z; i++) {
        if(index_to_voxel_pos(i, len_x, len_y, len_z, &x, &y, &z) < 0) {
            fprintf(stderr, "Error getting voxel position!\n");
            return;
        }
        print_nd(nd_array[i]);
        printf("Neighbor of:\n");
        for(short j = 0; j < DIRECTION_LEN; j++) {
            unsigned long n_index;
            if(get_neighbor_index(i, len_x, len_y, len_z, j, &n_index) == -4)
                continue;
            unsigned int x1, y1, z1;
            if(index_to_voxel_pos(n_index, len_x, len_y, len_z, &x1, &y1, &z1) < 0) {
                fprintf(stderr, "Error getting neighbor voxel position!\n");
                return;
            }
            printf("(%d, %d, %d)\n", x1, y1, z1);
        }
        printf("---------------------------\n");
    }
}
