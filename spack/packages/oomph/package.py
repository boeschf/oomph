from spack import *
import os

class Oomph(CMakePackage, CudaPackage, ROCmPackage):
    """dummy placeholder for oomph dependencies"""
    homepage    = "https://127.0.0.1/readme.html"
    generator   = "Ninja"
    maintainers = ["biddisco"]
    version("develop")

    # we have only tested/supported a subset of potential libfabrics providers
    fabrics = (
        "cxi", "efa", "gni", "psm2", "tcp", "verbs",
    )

    variant(
        "ofi",
        default="tcp",
        description="A list of enabled OFI fabrics",
        values=fabrics,
        multi=False,
    )

    # ------------------------------------------------------------------------
    # Exactly one of +cuda and +rocm need to be set
    # ------------------------------------------------------------------------
    conflicts("+cuda +rocm")

    # ------------------------------------------------------------------------
    # variants
    # ------------------------------------------------------------------------
    variant("ucx", default=False, description="Enable ucx support")
    variant("ofi", default=False, description="Enable ofi libfabric support")
    variant("testing", default=False, description="Enable testing")

    # ------------------------------------------------------------------------
    # build time dependencies
    # ------------------------------------------------------------------------
    depends_on("ninja", type="build")
    depends_on("cmake@3.22:", type="build")

    # ------------------------------------------------------------------------
    # generic c++ libs needed by several projects
    # ------------------------------------------------------------------------
    depends_on("boost +atomic+chrono+container+context+coroutine+date_time+filesystem+program_options+regex+serialization+system+test+thread+mpi+graph+json cxxstd=17")
    #depends_on("fmt")

    # ------------------------------------------------------------------------
    # GPU/cuda/rocm
    # ------------------------------------------------------------------------
    depends_on("cuda",       when="+cuda")
    depends_on("rocm-core",  when="+rocm")

    # ------------------------------------------------------------------------
    # allocators/memory
    # ------------------------------------------------------------------------
    depends_on("hwloc +cuda", when="+cuda ^gcc@11.4:")
    depends_on("hwloc ~cuda", when="gcc@:11.4")
    depends_on("numactl")


    # ------------------------------------------------------------------------
    # mpi and parallel io
    # ------------------------------------------------------------------------
    depends_on("mpi")
    depends_on("ucx +thread_multiple", when="+ucx")
    depends_on("libfabric@1.17:", when="+ofi")

    # ------------------------------------------------------------------------
    # testing
    # ------------------------------------------------------------------------
    depends_on("googletest")

    # ------------------------------------------------------------------------
    def cmake_args(self):
        """Populate cmake arguments for Mercury."""
        spec = self.spec
        define = self.define
        define_from_variant = self.define_from_variant
        parallel_tests = "+mpi" in spec and self.run_tests

        cmake_args = [
            define_from_variant("OOMPH_WITH_LIBFABRIC", "ofi"),
            define_from_variant("OOMPH_WITH_UCX", "ucx"),
        ]

        if "+ofi" in spec:
            ofi_fabrics = spec["libfabric"].variants["fabrics"].value
            ofi_fabrics = next((fabric for fabric in ofi_fabrics if fabric in self.fabrics), None)
            if ofi_fabrics is None:
                raise ValueError("No matching fabric found in ofi_fabrics and fabrics")
            cmake_args.append(f"OOMPH_LIBFABRIC_PROVIDER={ofi_fabrics}")
        print (cmake_args)
        return cmake_args

    # ------------------------------------------------------------------------
