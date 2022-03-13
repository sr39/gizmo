#!/bin/bash
#SBATCH --job-name=v1adm     # create a short name for your job
#SBATCH --nodes=5               # node count
# SBATCH --exclusive              # fully dedicate single node 
#SBATCH -p physics
#SBATCH --ntasks-per-node=30
#SBATCH --cpus-per-task=1        # cpu-cores per task (>1 if multi-threaded tasks)
#SBATCH --mem=200G               # memory for entire job
#SBATCH --time=72:00:00          # total run time limit (HH:MM:SS)
#SBATCH --output=/scratch/gpfs/sr39/m12ires450000_v2adm/output/%x-%j.out # output file
#SBATCH --mail-type=all          # send email on job start, end, and fail                                                            
#SBATCH --mail-user=sandiproy@princeton.edu                                                                                                            

module purge
module load openmpi/gcc/1.10.2/64 gsl/2.4 fftw/gcc/openmpi-1.10.2/3.3.4 hdf5/gcc/1.8.16

MYPATH=/home/sr39/gizmo
#srun $MYPATH/GIZMO /tigress/sr39/gas_disk_test_Feb2020/gas_spin_analhalo_w1/gas_spin_w1.params 1
#srun $MYPATH/GIZMO_ncrit /tigress/sr39/gizmo_adm_test_v2/starcriteria_test/ncrit_100.params
srun $MYPATH/FIRE2_adm_domains16 /scratch/gpfs/sr39/m12ires450000_v2adm/gizmo_parameters.txt
