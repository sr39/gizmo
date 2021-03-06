USE_FFTW3
#OUTPUT_POTENTIAL
#DM_SIDM=2
ALLOW_IMBALANCED_GASPARTICLELOAD
OUTPUT_POSITIONS_IN_DOUBLE

GALSF
ADAPTIVE_GRAVSOFT_FORGAS
#SELFGRAVITY_OFF
#GRAVITY_ANALYTIC
#ADM
EOS_GAMMA=(5.0/3.0)
HYDRO_MESHLESS_FINITE_MASS
GALSF_SFR_VIRIAL_SF_CRITERION=0
GALSF_SFR_MOLECULAR_CRITERION
#GALSF_SFR_VIRIAL_SF_CRITERION_ADM=1
GALSF_FB_MECHANICAL
METALS
TURB_DIFF_METALS
TURB_DIFF_METALS_LOWORDER
#GALSF_FB_FIRE_AGE_TRACERS=16
COOLING
COOL_METAL_LINES_BY_SPECIES
COOL_LOW_TEMPERATURES
#MULTIPLEDOMAINS=64
#GALSF_FB_FIRE_STELLAREVOLUTION=2
#GALSF_FB_FIRE_RT_HIIHEATING
#GALSF_FB_FIRE_RT_LOCALRP
#GALSF_FB_FIRE_RT_LONGRANGE
#GALSF_FB_FIRE_RT_UVHEATING

# Transfer Function Output
OUTPUT_POWERSPEC
OUTPUT_POWERSPEC_EACH_TYPE

# COSMOLOGICAL FLAGS
MULTIPLEDOMAINS=16
OUTPUT_POSITIONS_IN_DOUBLE 
IO_COMPRESS_HDF5 
BOX_PERIODIC 
PMGRID=512
PM_PLACEHIGHRESREGION=1+2+16
PM_HIRES_REGION_CLIPPING=1000
STOP_WHEN_BELOW_MINTIMESTEP

PROTECT_FROZEN_FIRE
FIRE_PHYSICS_DEFAULTS=2
