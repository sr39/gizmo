#include <stdio.h>
void output_compile_time_options(void)
{
printf(
"        USE_FFTW3\n"
"        ALLOW_IMBALANCED_GASPARTICLELOAD\n"
"        OUTPUT_POSITIONS_IN_DOUBLE\n"
"        GALSF\n"
"        ADAPTIVE_GRAVSOFT_FORGAS\n"
"        EOS_GAMMA=(5.0/3.0)\n"
"        HYDRO_MESHLESS_FINITE_MASS\n"
"        GALSF_SFR_VIRIAL_SF_CRITERION=0\n"
"        GALSF_SFR_MOLECULAR_CRITERION\n"
"        GALSF_FB_MECHANICAL\n"
"        METALS\n"
"        TURB_DIFF_METALS\n"
"        TURB_DIFF_METALS_LOWORDER\n"
"        COOLING\n"
"        COOL_METAL_LINES_BY_SPECIES\n"
"        COOL_LOW_TEMPERATURES\n"
"        OUTPUT_POWERSPEC\n"
"        OUTPUT_POWERSPEC_EACH_TYPE\n"
"        MULTIPLEDOMAINS=16\n"
"        OUTPUT_POSITIONS_IN_DOUBLE\n"
"        IO_COMPRESS_HDF5\n"
"        BOX_PERIODIC\n"
"        PMGRID=512\n"
"        PM_PLACEHIGHRESREGION=1+2+16\n"
"        PM_HIRES_REGION_CLIPPING=1000\n"
"        STOP_WHEN_BELOW_MINTIMESTEP\n"
"        PROTECT_FROZEN_FIRE\n"
"        FIRE_PHYSICS_DEFAULTS=2\n"
"\n");
}
