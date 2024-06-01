import numpy as np
import open3d as o3d
import ctypes

# C structure for the normal distribution
class normal_distribution_t(ctypes.Structure):
    __fields__ = [
        {"index": ctypes.c_int32},
        {"mean": ctypes.c_double * 3},
        {"old_mean": ctypes.c_double * 3},
        {"covariance": ctypes.c_double * 9},
        {"m2": ctypes.c_double * 3},
        {"num_samples": ctypes.c_int32},
        {"class": ctypes.c_int16},
        {"num_class_samples": ctypes.c_int32},
        {"being_updated": ctypes.c_bool}
    ]


# C structure for the Kullback-Leibler divergence
class kl_divergence_t(ctypes.Structure):
    __fields__ = [
        {"divergence": ctypes.c_double},
        {"p": ctypes.POINTER(normal_distribution_t)},
        {"q": ctypes.POINTER(normal_distribution_t)}
    ]

# import the core_legacy shared library
core = ctypes.cdll.LoadLibrary('core_legacy/build/libndtnetpp.so')

class NDT_Sampler:
    """A class to downsample point clouds using the Normal Distribution Transform (NDT) algorithm."""

    def __init__(self, pointcloud: np.ndarray, classes: np.ndarray = None, num_classes: int = None) -> None:
        """
        Initializes the NDT_Sampler class.

        Args:
            pointcloud (np.ndarray): The point cloud to downsample.
            classes (np.ndarray, optional): The classes of the points in the point cloud. Defaults to None.

        Returns:
            None
        """
        self.pointcloud: np.ndarray = pointcloud
        self.covariances: np.ndarray = None
        self.classes: np.ndarray = classes
        self.num_classes: int = num_classes if num_classes is not None else 0
        self.num_points: int = len(pointcloud)

        self.nd_array_ptr: ctypes.POINTER = ctypes.POINTER(normal_distribution_t)()
        self.num_valid_nds: ctypes.POINTER = ctypes.pointer(ctypes.c_ulong(0))

        self.kl_divergences_ptr: ctypes.POINTER = ctypes.POINTER(kl_divergence_t)()
        self.num_kl_divergences: ctypes.POINTER = ctypes.pointer(ctypes.c_ulong(0))

        self.len_x = ctypes.pointer(ctypes.c_uint(0))
        self.len_y = ctypes.pointer(ctypes.c_uint(0))
        self.len_z = ctypes.pointer(ctypes.c_uint(0))

        self.offset_x = ctypes.pointer(ctypes.c_double(0.0))
        self.offset_y = ctypes.pointer(ctypes.c_double(0.0))
        self.offset_z = ctypes.pointer(ctypes.c_double(0.0))

        self.voxel_size = ctypes.pointer(ctypes.c_double(0.0))

        self.destroyed = False


    def cleanup(self) -> None:

        del self.pointcloud
        del self.covariances
        del self.classes
        del self.len_x
        del self.len_y
        del self.len_z
        del self.offset_x
        del self.offset_y
        del self.offset_z
        del self.voxel_size

        # free the normal distribution array
        core.free_nds(self.nd_array_ptr, self.num_points)

        # free the Kullback-Leibler divergence array
        core.free_kl_divergences(self.kl_divergences_ptr)

        del self.nd_array_ptr
        del self.num_valid_nds
        del self.kl_divergences_ptr
        del self.num_kl_divergences

        self.destroyed = True


    def __del__(self) -> None:
        """
        Destructor for the NDT_Sampler class.

        Returns:
            None
        """


        if not self.destroyed:
            self.cleanup()
    
    
    def downsample(self, num_desired_points: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        """
        Downsamples the point cloud using the NDT algorithm.

        Args:
            num_desired_points (int): The number of desired points in the downsampled point cloud.

        Returns:
            tuple[np.ndarray, np.ndarray, np.ndarray]: The downsampled point cloud, the covariances, and the classes.
        """

        # create the point cloud pointer
        self.pcl_ptr = self.pointcloud.ctypes.data_as(ctypes.POINTER(ctypes.c_double))

        # create a new point cloud array
        self.new_pcl = np.zeros((num_desired_points, 3), dtype=np.float64)
        self.new_pcl_ptr = self.new_pcl.ctypes.data_as(ctypes.POINTER(ctypes.c_double))

        # create a pointer to store the number of downsampled points
        self.num_downsampled_points = ctypes.pointer(ctypes.c_ulong(0))

        # create a pointer to the classes array
        self.classes_ptr = None
        if self.classes is not None:
            self.classes_ptr = self.classes.ctypes.data_as(ctypes.POINTER(ctypes.c_ushort))

        # create a pointer to the new classes array
        self.new_classes = np.zeros(num_desired_points, dtype=np.int16)
        self.new_classes_ptr = self.new_classes.ctypes.data_as(ctypes.POINTER(ctypes.c_ushort))

        # create a pointer for the covariance
        self.covs = np.zeros((num_desired_points, 9), dtype=np.float64)
        self.covariances_ptr = self.covs.ctypes.data_as(ctypes.POINTER(ctypes.c_double))

        # set the argument types
        core.ndt_downsample.argtypes = [
            ctypes.POINTER(ctypes.c_double), ctypes.c_ushort, ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint),
            ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_ushort), ctypes.c_ushort,
            ctypes.c_ulong,
            ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_ushort),
            ctypes.POINTER(ctypes.POINTER(normal_distribution_t)), ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.POINTER(kl_divergence_t)), ctypes.POINTER(ctypes.c_ulong)
        ]

        # create a normal distribution array pointer reference
        self.nd_array_ptr_ref = ctypes.pointer(self.nd_array_ptr)

        # create a divergence array pointer reference
        self.kl_divergences_ptr_ref = ctypes.pointer(self.kl_divergences_ptr)

        # downsample the point cloud
        core.ndt_downsample(self.pcl_ptr, 3, self.num_points,
                            self.len_x, self.len_y, self.len_z,
                            self.offset_x, self.offset_y, self.offset_z,
                            self.voxel_size,
                            self.classes_ptr, self.num_classes,
                            num_desired_points, 
                            self.new_pcl_ptr, self.num_downsampled_points,
                            self.covariances_ptr,
                            self.new_classes_ptr,
                            self.nd_array_ptr_ref, self.num_valid_nds,
                            self.kl_divergences_ptr_ref, self.num_kl_divergences)
        
        self.num_points = num_desired_points

        self.pointcloud = self.new_pcl.copy()
        self.covariances = self.covs.copy()
        self.classes = self.new_classes.copy()

        del self.new_pcl
        del self.new_pcl_ptr
        del self.covs
        del self.covariances_ptr
        del self.classes_ptr
        del self.new_classes
        del self.new_classes_ptr
        del self.pcl_ptr
        del self.num_downsampled_points
        del self.nd_array_ptr_ref
        del self.kl_divergences_ptr_ref
        
        return self.pointcloud, self.covariances, self.classes

    def prune(self, new_desired_points: int) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        """
        Prunes the downsampled point cloud to the desired number of points based on its Kullback-Leibler divergences.

        Args:
            new_desired_points (int): The number of desired points in the pruned point cloud.

        Returns:
            tuple[np.ndarray, np.ndarray, np.ndarray]: The pruned point cloud, the covariances, and the classes.
        """

        # set the argument types
        core.prune_nds.argtypes = [
            ctypes.POINTER(normal_distribution_t),
            ctypes.c_uint, ctypes.c_uint, ctypes.c_uint,
            ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(kl_divergence_t), ctypes.POINTER(ctypes.c_ulong)
        ]
        
        # prune the normal distributions with the lowest Kullback-Leibler divergences
        core.prune_nds(self.nd_array_ptr,
                       self.len_x.contents.value, self.len_y.contents.value, self.len_z.contents.value,
                       new_desired_points, self.num_valid_nds,
                       self.kl_divergences_ptr, self.num_kl_divergences)

        # convert the normal distribution array to a point cloud
        new_pcl = np.zeros((new_desired_points, 3))
        new_pcl_ptr = new_pcl.ctypes.data_as(ctypes.POINTER(ctypes.c_double))

        # create a pointer for the number of points
        num_points_ptr = ctypes.pointer(ctypes.c_ulong(0))

        # create a pointer for the covariance
        covariances = np.zeros((new_desired_points, 9), dtype=np.float64)
        covariances_ptr = covariances.ctypes.data_as(ctypes.POINTER(ctypes.c_double))

        # create a pointer for the new classes
        new_classes = np.zeros(new_desired_points, dtype=np.int16)
        new_classes_ptr = new_classes.ctypes.data_as(ctypes.POINTER(ctypes.c_ushort))

        # set the argument types
        core.to_point_cloud.argtypes = [
            ctypes.POINTER(normal_distribution_t),
            ctypes.c_uint, ctypes.c_uint, ctypes.c_uint,
            ctypes.c_double, ctypes.c_double, ctypes.c_double,
            ctypes.c_double,
            ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_ulong),
            ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_ushort)
        ]

        # convert the normal distributions to a point cloud
        core.to_point_cloud(self.nd_array_ptr,
                            self.len_x.contents.value, self.len_y.contents.value, self.len_z.contents.value,
                            self.offset_x.contents.value, self.offset_y.contents.value, self.offset_z.contents.value,
                            self.voxel_size.contents.value,
                            new_pcl_ptr, num_points_ptr,
                            covariances_ptr, 
                            new_classes_ptr)
        
        self.num_points = new_desired_points
        
        self.pointcloud = new_pcl
        self.covariances = covariances
        self.classes = new_classes

        return new_pcl, covariances, new_classes
        