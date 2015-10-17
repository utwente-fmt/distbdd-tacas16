# DistBDD: Distributed Binary Decision Diagrams
This repository hosts the full source code of the algorithms and operations described in the paper on Distributed Binary Decision Diagrams, submitted to TACAS 2016. Also the models from the BEEM benchmark set, as well as scripts for compilation and execution are provided.

The main author of the DistBDD paper can be contacted via w.h.m.oortwijn@utwente.nl

Prerequisites
---
For best performance we recommend using an Infiniband network that supports Remote Direct Memory Access (RDMA), as the algorithms are specifically designed to target RDMA. Nonetheless, also normal Ethernet networks and SMP clusters are supported. Furthermore, DistBDD has the following requirements:
- Berkeley UPC, version 2.20.2: http://upc.lbl.gov/
- The GNU Compiler Collection (GCC), we used version 4.8.3

Configuring Berkeley UPC
---
DistBDD requires UPC to be configured to handle large amounts of memory. A standard configuration will also work, but then the sizes of the shared data structures are limited to only a few megabytes. Configure UPC with the following options:
- `./configure --without-mpi-cc --disable-aligned-segments --enable-allow-gcc4 --enable-sptr-struct --enable-pshm --disable-pshm-posix --enable-pshm-sysv`

Note that this configuration disables MPI, as it assumes the use of RDMA. If you intend to run DistBDD on a network of machines that does not support Infiniband verbs (RDMA), then MPI can be used via the following configuration:
- `./configure --disable-aligned-segments --enable-allow-gcc4 --enable-sptr-struct --enable-pshm --disable-pshm-posix --enable-pshm-sysv`

Compiling DistBDD
---
The command: `./compile.sh mxm` will compile DistBDD to target the `mxm` (Mellanox Messaging Accelerator) communication library, which we used for our experimental evaluation. If `mxm` is not supported by your hardware, also `ibv` (Infiniband verbs), `mpi`, `udp`, and `smp` are supported.

For the purely sequential (single machine, single-threaded) runs we used a different build. This build spawns a number of parallel threads on a single machine, so that each thread contributes to the shared data structures. Then, only the first thread becomes active to achieve a sequential run, and all other threads remain idle. The command: `/compile_seq.sh mxm` will compile DistDD for purely sequential runs.

Running DistBDD
---
Single-machine runs can be executed via `./run.sh 4 at.5.8`, which performs reachability analysis over the `at.5.8` BEEM model by using 1 machine and 4 threads. Instead of `at.5.8`, also other models from the `models` folder can be picked.

For runs that require more than one machine, we use the SLURM scheduler to set-up UPC on all participating machines. In that case, the command `./run_dist.sh 8 12 at.5.8` can be used, which runs DistBDD on 8 machines (with exclusive access), with 12 threads per machine. After completion, the result is written to the file `result/at.5.8/result-8-12.out`.
