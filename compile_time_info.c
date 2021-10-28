#include <stdio.h>
void output_compile_time_options(void)
{
printf(
"        USE_FFTW3\n"
"        ALLOW_IMBALANCED_GASPARTICLELOAD\n"
"        OUTPUT_POSITIONS_IN_DOUBLE\n"
"        GALSF\n"
"        ADAPTIVE_GRAVSOFT_FORGAS\n"
"        ADM\n"
"        EOS_GAMMA=(5.0/3.0)\n"
"        HYDRO_MESHLESS_FINITE_MASS\n"
"        GALSF_SFR_VIRIAL_SF_CRITERION=0\n"
"        GALSF_SFR_MOLECULAR_CRITERION\n"
"        GALSF_SFR_VIRIAL_SF_CRITERION_ADM=1\n"
"        GALSF_FB_MECHANICAL\n"
"        METALS\n"
"        TURB_DIFF_METALS\n"
"        TURB_DIFF_METALS_LOWORDER\n"
"        GALSF_FB_FIRE_AGE_TRACERS=16\n"
"        COOLING\n"
"        COOL_METAL_LINES_BY_SPECIES\n"
"        COOL_LOW_TEMPERATURES\n"
"        MULTIPLEDOMAINS=64\n"
"        GALSF_FB_FIRE_STELLAREVOLUTION=2\n"
"        GALSF_FB_FIRE_RT_HIIHEATING\n"
"        GALSF_FB_FIRE_RT_LOCALRP\n"
"        GALSF_FB_FIRE_RT_LONGRANGE\n"
"        GALSF_FB_FIRE_RT_UVHEATING\n"
"\n");
}
