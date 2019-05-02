
/*! \file allvars.h
 *  \brief declares global variables.
 *
 *  This file declares all global variables. Further variables should be added here, and declared as
 *  'extern'. The actual existence of these variables is provided by the file 'allvars.c'. To produce
 *  'allvars.c' from 'allvars.h', do the following:
 *
 *     - Erase all #define statements
 *     - add #include "allvars.h"
 *     - delete all keywords 'extern'
 *     - delete all struct definitions enclosed in {...}, e.g.
 *        "extern struct global_data_all_processes {....} All;"
 *        becomes "struct global_data_all_processes All;"
 */

/*
 * This file was originally part of the GADGET3 code developed by
 * Volker Springel (volker.springel@h-its.org). The code has been modified
 * in part by Phil Hopkins (phopkins@caltech.edu) for GIZMO (new variables, 
 * and different naming conventions for some old variables)
 */


#ifndef ALLVARS_H
#define ALLVARS_H

#include <mpi.h>
#include <stdio.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_integration.h>
#include <gsl/gsl_spline.h>
#include <gsl/gsl_errno.h>
#ifdef _OPENMP
#include <omp.h>
#endif

#include "GIZMO_config.h"
/*------- Things that are always recommended (this must follow loading GIZMO_config.h!) -------*/
#define DOUBLEPRECISION         /* using double (not floating-point) precision */
#define PEANOHILBERT            /* sort particles on a Peano-Hilbert curve (huge optimization) */
#define WALLCLOCK               /* track timing of different routines */
#define MYSORT                  /* use our custom sort (as opposed to C default, which is compiler-dependent) */
#define ALLOWEXTRAPARAMS        /* don't crash (just warn) if there are extra lines in the input parameterfile */
#define INHOMOG_GASDISTR_HINT   /* if the gas is distributed very different from collisionless particles, this can helps to avoid problems in the domain decomposition */
#ifndef OUTPUT_ADDITIONAL_RUNINFO
#define IO_REDUCED_MODE
#endif
#ifndef IO_DISABLE_HDF5
#define HAVE_HDF5
#include <hdf5.h>
#endif



#define DO_PREPROCESSOR_EXPAND_(VAL)  VAL ## 1
#define EXPAND_PREPROCESSOR_(VAL)     DO_PREPROCESSOR_EXPAND_(VAL) /* checks for a NON-ZERO value of this parameter */


#if !defined(SLOPE_LIMITER_TOLERANCE)
#if defined(AGGRESSIVE_SLOPE_LIMITERS)
#define SLOPE_LIMITER_TOLERANCE 2
#else
#define SLOPE_LIMITER_TOLERANCE 1
#endif
#endif

#ifndef DISABLE_SPH_PARTICLE_WAKEUP
#if (SLOPE_LIMITER_TOLERANCE > 0)
#define WAKEUP   4.1            /* allows 2 timestep bins within kernel */
#else 
#define WAKEUP   2.1            /* allows only 1-separated timestep bins within kernel */
#endif
#endif

/* lock the 'default' hydro mode */
#if (defined(HYDRO_FIX_MESH_MOTION) || defined(HYDRO_REGULAR_GRID)) && !defined(HYDRO_MESHLESS_FINITE_VOLUME)
#define HYDRO_MESHLESS_FINITE_VOLUME /* only makes sense to use this modules with this 'backbone' of MFV here */
#elif !(defined(HYDRO_MESHLESS_FINITE_MASS) || defined(HYDRO_MESHLESS_FINITE_VOLUME) || defined(HYDRO_DENSITY_SPH) || defined(HYDRO_PRESSURE_SPH))
#define HYDRO_MESHLESS_FINITE_MASS   /* otherwise default to MFM if nothing is specified */
#endif

/* define the default mesh-motion assumption, if this is not provided by the user */
#if !defined(HYDRO_FIX_MESH_MOTION)
#if defined(HYDRO_REGULAR_GRID)
#define HYDRO_FIX_MESH_MOTION 0     /* default to non-moving for regular grids */
#else
#define HYDRO_FIX_MESH_MOTION 5     /* otherwise default to smoothed motion, only relevant for MFV (MFM/SPH will always move with flow) */
#endif
#endif

/* determine whether the mesh is adaptive via splitting/merging (refinement) or 'frozen' to the initial number of elements */
#if !defined(PREVENT_PARTICLE_MERGE_SPLIT) && (HYDRO_FIX_MESH_MOTION<5)
#define PREVENT_PARTICLE_MERGE_SPLIT  /* particle merging/splitting doesn't make sense with frozen grids */
#endif



#ifdef PMGRID
#define PM_ENLARGEREGION 1.1    /* enlarges PMGRID region as the simulation evolves */
/*
#     - PM_ENLARGEREGION: The spatial region covered by the high-res zone has a fixed
#                   size during the simulation, which initially is set to the
#                   smallest region that encompasses all high-res particles. Normally, the
#                   simulation will be interrupted, if high-res particles leave this
#                   region in the course of the run. However, by setting this parameter
#                   to a value larger than one, the high-res region can be expanded.
#                   For example, setting it to 1.4 will enlarge its side-length by
#                   40% (it remains centered on the high-res particles). Hence, with
#                   such a setting, the high-res region may expand or move by a
#                   limited amount. If in addition SYNCHRONIZATION is activated, then
#                   the code will be able to continue even if high-res particles
#                   leave the initial high-res grid. In this case, the code will
#                   update the size and position of the grid that is placed onto
#                   the high-resolution region automatically. To prevent that this
#                   potentially happens every single PM step, one should nevertheless
#                   assign a value slightly larger than 1 to PM_ENLARGEREGION.
*/
#endif





#if (defined(HYDRO_DENSITY_SPH) || defined(HYDRO_PRESSURE_SPH)) && !defined(HYDRO_SPH)
#define HYDRO_SPH               /* master flag for SPH: must be enabled if any SPH method is used */
#endif
#ifdef HYDRO_SPH
#if !defined(SPH_DISABLE_CD10_ARTVISC) && !(defined(EOS_TILLOTSON) || defined(EOS_ELASTIC)) // fancy viscosity switches assume positive pressures //
#define SPHAV_CD10_VISCOSITY_SWITCH 0.05   /* Enables Cullen & Dehnen 2010 'inviscid sph' (viscosity suppression outside shocks) */
#endif
#ifndef SPH_DISABLE_PM_CONDUCTIVITY
#define SPHAV_ARTIFICIAL_CONDUCTIVITY      /* Enables mixing entropy (J.Read's improved Price-Monaghan conductivity with Cullen-Dehnen switches) */
#endif
#endif

#ifdef PERIODIC
#define BOX_PERIODIC
#endif
#ifdef BND_PARTICLES
#define BOX_BND_PARTICLES
#endif
#ifdef LONG_X
#define BOX_LONG_X LONG_X
#endif
#ifdef LONG_Y
#define BOX_LONG_Y LONG_Y
#endif
#ifdef LONG_Z
#define BOX_LONG_Z LONG_Z
#endif
#ifdef REFLECT_BND_X
#define BOX_REFLECT_X
#endif
#ifdef REFLECT_BND_Y
#define BOX_REFLECT_Y
#endif
#ifdef REFLECT_BND_Z
#define BOX_REFLECT_Z
#endif
#ifdef SHEARING_BOX
#define BOX_SHEARING SHEARING_BOX
#endif
#ifdef SHEARING_BOX_Q
#define BOX_SHEARING_Q SHEARING_BOX_Q
#endif
#ifdef ANALYTIC_GRAVITY
#if !(EXPAND_PREPROCESSOR_(ANALYTIC_GRAVITY) == 1)
#define GRAVITY_ANALYTIC 1
#else
#define GRAVITY_ANALYTIC
#endif
#endif
#ifdef NOGRAVITY
#define SELFGRAVITY_OFF
#endif


#include "eos/eos.h"


#if defined(CBE_INTEGRATOR) || defined(DM_FUZZY) || defined(DM_SIDM)
#ifndef ADAPTIVE_GRAVSOFT_FORALL
#define ADAPTIVE_GRAVSOFT_FORALL 100000
#endif
#endif

#if defined(CBE_INTEGRATOR) || defined(DM_FUZZY)
#define AGS_FACE_CALCULATION_IS_ACTIVE
#endif

#if defined(CBE_INTEGRATOR)
#define CBE_INTEGRATOR_NBASIS CBE_INTEGRATOR
#ifdef CBE_INTEGRATOR_SECONDMOMENT
#define CBE_INTEGRATOR_NMOMENTS 10
#else
#define CBE_INTEGRATOR_NMOMENTS 4
#endif
#endif



#ifdef FIRE_PHYSICS_DEFAULTS
#define COOLING                             /*! master switch for cooling */
#define COOL_LOW_TEMPERATURES               /*! include low-temperature (<1e4 K) cooling */
#define COOL_METAL_LINES_BY_SPECIES         /*! include high-temperature metal-line cooling, species-by-species */
#define GALSF                               /*! master switch for star formation */
#define METALS                              /*! follow metals as passive scalars, use in cooling, etc */
#define TURB_DIFF_METALS                    /*! explicit sub-grid diffusivity for metals/passive scalars */
#define TURB_DIFF_METALS_LOWORDER           /*! memory-saving custom mod */
#define GALSF_SFR_MOLECULAR_CRITERION       /*! molecular criterion for star formation */
#define GALSF_SFR_VIRIAL_SF_CRITERION 0     /*! sink-particle like self-gravity requirement for star formation: original implementation */
#define GALSF_FB_MECHANICAL                 /*! master switch for mechanical feedback modules */
#define GALSF_FB_FIRE_STELLAREVOLUTION      /*! turns on default FIRE processes+lookup tables including gas return, SNe, R-process, etc. */
#define GALSF_FB_FIRE_RT_HIIHEATING         /*! gas within HII regions around young stars is photo-heated to 10^4 K - local stromgren approximation */
#define GALSF_FB_FIRE_RT_LOCALRP            /*! turn on local radiation pressure coupling to gas - account for local multiple-scattering and isotropic local absorption */
#define GALSF_FB_FIRE_RT_LONGRANGE          /*! continuous acceleration from starlight (uses luminosity tree) to propagate FIRE RT */
#define GALSF_FB_FIRE_RT_UVHEATING          /*! use estimate of local spectral information from FIRE RT for photoionization and photoelectric heating */
#define GALSF_FB_FIRE_RPROCESS 4      /*! tracks a set of 'dummy' species from neutron-star mergers (set to number: 4=extended model) */
//#define GALSF_SFR_IMF_VARIATION           /*! track [do not change] properties of gas from which stars form, for IMF models in post-processing */
#define PROTECT_FROZEN_FIRE                 /*! protect code so FIRE runs are not modified by various code updates, etc -- default FIRE-2 code locked */
#else
#if (defined(COOLING) && defined(GALSF) && defined(GALSF_FB_MECHANICAL)) && !defined(FIRE_UNPROTECT_FROZEN)
#define PROTECT_FROZEN_FIRE
#endif
#endif

#ifdef PROTECT_FROZEN_FIRE
#define GALSF_USE_SNE_ONELOOP_SCHEME // set to use the 'base' FIRE-2 SNe coupling. if commented out, will user newer version that more accurately manages the injected energy with neighbors moving to inject a specific target
#endif



#ifdef COSMIC_RAYS
#define GAMMA_COSMICRAY (4.0/3.0)
#define GAMMA_COSMICRAY_MINUS1 (GAMMA_COSMICRAY-1)
#ifdef COSMIC_RAYS_ALFVEN
#define GAMMA_ALFVEN_CRS (3.0/2.0)
#define COSMIC_RAYS_M1 (COSMIC_RAYS_ALFVEN)
#endif
#endif

#if defined(COOL_GRACKLE) 
#if !defined(COOLING)
#define COOLING
#endif
#include <grackle.h>
#endif


#ifdef GRAVITY_IMPROVED_INTEGRATION
#define GRAVITY_HYBRID_OPENING_CRIT // use both Barnes-Hut + relative tree opening criterion
#define STOP_WHEN_BELOW_MINTIMESTEP // stop when below min timestep to prevent bad timestepping
#define TIDAL_TIMESTEP_CRITERION // use tidal tensor timestep criterion
#endif


#ifdef SINGLE_STAR_FORMATION
#define DEVELOPER_MODE
#define GRAVITY_HYBRID_OPENING_CRIT // use both Barnes-Hut + relative tree opening criterion
#define STOP_WHEN_BELOW_MINTIMESTEP // stop when below min timestep to prevent bad timestepping
#define TIDAL_TIMESTEP_CRITERION // use tidal tensor timestep criterion
#define SINGLE_STAR_TIMESTEPPING // use additional timestep criteria for sink particles to ensure they don't evolve out-of-binary in close encounters
//#define SINGLE_STAR_SUPERTIMESTEPPING // Turns on sub-grid evolution of sink binaries to avoid slowdown of very short timesteps
#define SINGLE_STAR_HILL_CRITERION // use Hill-type tidal-tensor star formation criterion
#define SINGLE_STAR_STRICT_ACCRETION // use Bate 1995 angular momentum criterion for star formation, along with dynamically-evolved sink radius for apocentric distance threshold
#define GALSF // master switch needed to enable various frameworks
#define GALSF_SFR_VIRIAL_SF_CRITERION 2 // only allow star formation in virialized sub-regions meeting Jeans threshold + converging all 3 axes
#define METALS  // metals should be active for stellar return
#define BLACK_HOLES // need to have black holes active since these are our sink particles
//#define GALSF_SFR_IMF_VARIATION // save extra information about sinks when they form - MYG: switching this off for the sake of memory, but if it's useful for a project we can always bring it back
#ifdef MAGNETIC
#define MHD_CONSTRAINED_GRADIENT 1  // particularly worried about div-B errors around sink particles, so we'll switch this on to help scrub away the shame - MYG
#endif
#ifdef RADTRANSFER
#define RT_DISABLE_R15_GRADIENTFIX
#endif
#ifdef SINGLE_STAR_ACCRETION
#define BH_SWALLOWGAS // need to swallow gas [part of sink model]
#define BH_GRAVCAPTURE_GAS // use gravitational capture swallow criterion for resolved gravitational capture
#if (SINGLE_STAR_ACCRETION > 0)
#define BH_ALPHADISK_ACCRETION // swallowed gas goes to disk
#endif
#if (SINGLE_STAR_ACCRETION > 1)
#define BH_BONDI 0 // use Bondi accretion for diffuse gas
#endif
#if (SINGLE_STAR_ACCRETION > 2)
#define BH_SUBGRIDBHVARIABILITY // model sub-grid [unresolved] variability in accretion rates for Bondi
#endif
#endif
#define BH_CALC_DISTANCES // calculate distance to nearest sink in gravity tree
#ifdef SINGLE_STAR_STRICT_ACCRETION
#define READ_SINKRADIUS // read the sinkradius for sink partciles in IC files
#endif
#ifdef SINGLE_STAR_SUPERTIMESTEPPING
#define SUPERTIMESTEPPING_ERRCONST 0.01 // If sqrt(|nabla g| / (GM/a^3)) < SUPERTIMESTEPPING_ERRCONST, then we use super timestepping for the binary
#endif
#ifdef NEWSINK
#define NEWSINK_BONDI
#define NEWSINK_J_FEEDBACK //turns on angular momentum feedback in NEWSINK
#ifdef MAGNETIC
#define NEWSINK_B_FEEDBACK // turns on magnetic flux feedback, redistributing the magnetic flux of accreted particles similarly to how angular momentum is done
#endif
#define NEWSINK_EAT_SMALL_DT // particles with very small timesteps will be accreted to prevent extreme slowdowns, controlled by DT_MIN_TOLERANCE_FACTOR
#define NEWSINK_STOCHASTIC_ACCRETION //with this turned on NEWSINK will not leech of parts of a gas particle when accreting (default behavior), instead it will accrete the entire particle with p=dm/m_particle probability, where dm is the mass it would need to accrete
//#define NEWSINK_RELOCATE_KICKED_PARTICLE //will relocate the particle to the poles uniformly distributed along a spherical surface with opening angle NEWSINK_JET_OPENING_ANGLE at a distance of the interaction radius
#define NEWSINK_JET_OPENING_ANGLE 30.0 //Opening angle (in degrees) for jets for NEWSINK when using BH_WIND_KICK -1
#define ALPHASS 0.01 //alpha disk factor for disc timescale, should be between 0.01-1.0
#define DT_MIN_TOLERANCE_FACTOR 0.001 //tolerance factor for dt_min, defined in part (ii) of 2.3.5 in Hubber 2013.
#define INT_ZONE_TO_HSML 1.0 //how many times larger should the sink interaction zone be compared to the the particle's adaptive smoothing length, 1 should be good
#define NEWSINK_NEIGHBORMAX 200 //maximum number of neighbors anticipated, using BlackHoleNgbFactor=5 and DesNumNgb=32  value of 200 should be safe
#define MDOT_AVG_WINDOWS_SIZE 20 //for radiation mdot is smoothed over this number of timesteps if the total time over these steps is larger than t_dyn of the sink
#endif
//#GALSF_SFR_IMF_VARIATION         # determines the stellar IMF for each particle from the Guszejnov/Hopkins/Hennebelle/Chabrier/Padoan theory
#ifdef SINGLE_STAR_FB_HEATING
#define GALSF_FB_FIRE_RT_LONGRANGE  // turn on FIRE RT approximation: no Type-4 particles so don't worry about its approximations
#define BH_PHOTONMOMENTUM // enable BHs within the FIRE-RT framework. make sure BH_FluxMomentumFactor=0 to avoid launching winds this way!!!
#define BH_COMPTON_HEATING // turn on the heating term: this just calculates incident BH-particle flux, to be used in the cooling routine
#endif
#ifdef SINGLE_STAR_FB_JETS
//#define BH_WIND_CONTINUOUS 0
#define BH_WIND_KICK -1 // use kinetic feedback module for protostellar jets (for this, use the simple kicking module, it's not worth the expense of the other)
#endif
#ifdef SINGLE_STAR_PROMOTION
#define GALSF_FB_MECHANICAL // allow SNe + winds in promoted stars [at end of main sequence lifetimes]
#define GALSF_FB_FIRE_STELLAREVOLUTION // mass return and other properties for stellar winds [scaled appropriately for particle masses]
#define GALSF_FB_FIRE_RT_HIIHEATING // FIRE approximate photo-ionization [for particle masses; could also use real-RT]
#define GALSF_FB_FIRE_RT_LOCALRP // local radiation pressure [scaled with mass, single-scattering term here]
#define GALSF_FB_FIRE_RT_CONTINUOUSRP // force the local rad-pressure term to be continuous instead of small impulses
#endif
// if not using grackle modules, need to make sure appropriate cooling is enabled
#if defined(COOLING) && !defined(COOL_GRACKLE)
#ifndef COOL_LOW_TEMPERATURES
#define COOL_LOW_TEMPERATURES // make sure low-temperature cooling is enabled!
#endif
#ifndef COOL_METAL_LINES_BY_SPECIES
#define COOL_METAL_LINES_BY_SPECIES // metal-based cooling enabled
#endif
#endif
#endif // SINGLE_STAR_FORMATION






#ifdef MHD_CONSTRAINED_GRADIENT
/* make sure mid-point gradient calculation for cleaning terms is enabled */
#ifndef MHD_CONSTRAINED_GRADIENT_MIDPOINT
#define MHD_CONSTRAINED_GRADIENT_MIDPOINT
#endif
#endif
/* these are tolerances for the slope-limiters. we define them here, because the gradient constraint routine needs to
    be sure to use the -same- values in both the gradients and reimann solver routines */
#if MHD_CONSTRAINED_GRADIENT
#if (MHD_CONSTRAINED_GRADIENT > 1)
#define MHD_CONSTRAINED_GRADIENT_FAC_MINMAX 7.5
#define MHD_CONSTRAINED_GRADIENT_FAC_MEDDEV 5.0
#define MHD_CONSTRAINED_GRADIENT_FAC_MED_PM 0.25
#define MHD_CONSTRAINED_GRADIENT_FAC_MAX_PM 0.25
#else
#define MHD_CONSTRAINED_GRADIENT_FAC_MINMAX 7.5
#define MHD_CONSTRAINED_GRADIENT_FAC_MEDDEV 1.5
#define MHD_CONSTRAINED_GRADIENT_FAC_MED_PM 0.2
#define MHD_CONSTRAINED_GRADIENT_FAC_MAX_PM 0.2
#endif
#else
#define MHD_CONSTRAINED_GRADIENT_FAC_MINMAX 2.0
#define MHD_CONSTRAINED_GRADIENT_FAC_MEDDEV 1.0
#define MHD_CONSTRAINED_GRADIENT_FAC_MED_PM 0.20
#define MHD_CONSTRAINED_GRADIENT_FAC_MAX_PM 0.125
#endif


/* force 'master' flags to be enabled for the appropriate methods, if we have enabled something using those methods */

/* options for FIRE RT method */
#if defined(GALSF_FB_FIRE_RT_LONGRANGE)
#ifndef RT_LEBRON
#define RT_LEBRON
#endif
#endif
#if defined(RT_LEBRON)
// use gravity tree for flux propagation
#define RT_USE_GRAVTREE
#endif

#ifdef RT_FLUXLIMITEDDIFFUSION
#define RT_OTVET /* for FLD, we use the OTVET architecture, but then just set the tensor to isotropic */
#endif

/* options for OTVET module */
#if defined(RT_OTVET)
// RADTRANSFER is ON, obviously
#ifndef RADTRANSFER
#define RADTRANSFER
#endif
// need to solve a diffusion equation
#ifndef RT_DIFFUSION
#define RT_DIFFUSION
#endif
// use gravity tree for Eddington tensor
#define RT_USE_GRAVTREE
// and be sure to track luminosity locations 
#ifndef RT_SEPARATELY_TRACK_LUMPOS
#define RT_SEPARATELY_TRACK_LUMPOS
#endif
// need source injection enabled to define emissivity
#define RT_SOURCE_INJECTION
#if !defined(RT_DIFFUSION_IMPLICIT) && !defined(RT_DIFFUSION_EXPLICIT)
#define RT_DIFFUSION_EXPLICIT // default to explicit (more accurate) solver //
#endif
#endif

/* options for M1 module */
#if defined(RT_M1)
// RADTRANSFER is ON, obviously
#ifndef RADTRANSFER
#define RADTRANSFER
#endif
// need to solve a diffusion equation
#ifndef RT_DIFFUSION
#define RT_DIFFUSION
#endif
// need source injection enabled to define emissivity
#define RT_SOURCE_INJECTION
// and need to evolve fluxes
#define RT_EVOLVE_FLUX
// at the moment, this only works for explicit solutions, so set this on
#ifndef RT_DIFFUSION_EXPLICIT
#define RT_DIFFUSION_EXPLICIT
#endif
#endif


/* options for direct/exact Jiang et al. method for direct evolution on an intensity grid */
#if defined(RT_LOCALRAYGRID)
#ifndef RADTRANSFER
#define RADTRANSFER
#endif
#define RT_EVOLVE_INTENSITIES
#define N_RT_INTENSITY_BINS (4*(RT_LOCALRAYGRID)*((RT_LOCALRAYGRID)+1))
#define RT_INTENSITY_BINS_DOMEGA (4.*M_PI/((double)N_RT_INTENSITY_BINS))
#define RT_SOURCE_INJECTION
#endif



/* decide which diffusion method to use (for any diffusion-based method) */
#if defined(RT_DIFFUSION) && !defined(RT_DIFFUSION_EXPLICIT)
#define RT_DIFFUSION_CG
#endif
/* check if flux-limiting is disabled: it should be on by default with diffusion-based methods */
#if (defined(RT_DIFFUSION)) && !defined(RT_DISABLE_FLUXLIMITER)
#define RT_FLUXLIMITER
#endif
/* check if we are -explicitly- evolving the radiation field, in which case we need to carry time-derivatives of the field */
#if defined(RT_DIFFUSION_EXPLICIT)
#define RT_EVOLVE_NGAMMA
#endif

/* enable radiation pressure forces unless they have been explicitly disabled */
#if defined(RADTRANSFER) && !defined(RT_DISABLE_RAD_PRESSURE)
#define RT_RAD_PRESSURE_FORCES
#endif

#if ((defined(RT_FLUXLIMITER) || defined(RT_RAD_PRESSURE_FORCES) || defined(RT_DIFFUSION_EXPLICIT)) && !defined(RT_EVOLVE_FLUX)) && !defined(RT_EVOLVE_EDDINGTON_TENSOR)
#define RT_EVOLVE_EDDINGTON_TENSOR
#endif

/* enable appropriate chemistry flags if we are using the photoionization modules */
#if defined(RT_CHEM_PHOTOION)
#if (RT_CHEM_PHOTOION > 1)
/* enables multi-frequency radiation transport for ionizing photons. Integration variable is the ionising intensity J_nu */
#define RT_CHEM_PHOTOION_HE
#define RT_PHOTOION_MULTIFREQUENCY // if using He-ionization, default to multi-frequency RT [otherwise doesn't make sense] //
#endif
#endif


#if defined(RT_XRAY)
#if (RT_XRAY == 1)
#define RT_SOFT_XRAY
#endif
#if (RT_XRAY == 2)
#define RT_HARD_XRAY
#endif
#if (RT_XRAY == 3)
#define RT_SOFT_XRAY
#define RT_HARD_XRAY
#endif
#endif


/* default to speed-of-light equal to actual speed-of-light, and stars as photo-ionizing sources */
#ifndef RT_SPEEDOFLIGHT_REDUCTION
#define RT_SPEEDOFLIGHT_REDUCTION 1.0
#endif
#ifndef RT_SOURCES
#define RT_SOURCES 16
#endif

/* cooling must be enabled for RT cooling to function */
#if defined(RT_COOLING_PHOTOHEATING_OLDFORMAT) && !defined(COOLING)
#define COOLING
#endif

#if !defined(RT_USE_GRAVTREE) && defined(RT_SELFGRAVITY_OFF) && !defined(SELFGRAVITY_OFF)
#define SELFGRAVITY_OFF // safely define SELFGRAVITY_OFF in this case, otherwise we act like there is gravity except in the final setting of accelerations
#endif



#if defined(GALSF) || defined(BLACK_HOLES) || defined(RADTRANSFER)
#define DO_DENSITY_AROUND_STAR_PARTICLES
#if !defined(ALLOW_IMBALANCED_GASPARTICLELOAD)
#define ALLOW_IMBALANCED_GASPARTICLELOAD
#endif
#endif


//#define ENERGY_ENTROPY_SWITCH_IS_ACTIVE
/* this is a ryu+jones type energy/entropy switch. it can help with some problems, but can also generate significant 
 errors in other types of problems. in general, even for pure hydro, this isn't recommended; use it for special problems if you know what you are doing. */




#ifdef MAGNETIC
/* recommended MHD switches -- only turn these off for de-bugging */
#define DIVBCLEANING_DEDNER         /* hyperbolic/parabolic div-cleaing (Dedner 2002), with TP improvements */
/* MHD switches specific to SPH MHD */
#ifdef HYDRO_SPH
#define SPH_TP12_ARTIFICIAL_RESISTIVITY   /* turns on magnetic dissipation ('artificial resistivity'): uses tricco switch =h*|gradB|/|B| */
#endif
#endif


#if defined(TURB_DIFF_ENERGY) || defined(TURB_DIFF_VELOCITY) || defined(TURB_DIFF_MASS) || defined(TURB_DIFF_METALS)
#define TURB_DIFFUSION /* master switch to calculate properties needed for scalar turbulent diffusion/mixing: must enable with any specific version */
#if defined(TURB_DIFF_VELOCITY) && !defined(VISCOSITY)
#define VISCOSITY
#endif
#if defined(TURB_DIFF_ENERGY) && !defined(CONDUCTION)
#define CONDUCTION
#endif
#endif


#if defined(BLACK_HOLES) && (defined(BH_REPOSITION_ON_POTMIN) || defined(BH_SEED_FROM_FOF))
#ifndef EVALPOTENTIAL
#define EVALPOTENTIAL
#endif
#if !defined(BH_DYNFRICTION) && (BH_REPOSITION_ON_POTMIN == 2)
#define BH_DYNFRICTION 1 // use for local damping of anomalous velocities wrt background medium //
#endif
#endif


#ifdef EVALPOTENTIAL
#ifndef COMPUTE_POTENTIAL_ENERGY
#define COMPUTE_POTENTIAL_ENERGY
#endif
#endif



#ifdef BOX_SHEARING
/* set default compile-time flags for the shearing-box (or shearing-sheet) boundaries */
/* shearing box boundaries: 1=r-z sheet (coordinates [0,1,2] = [r,z,phi]), 2=r-phi sheet [r,phi,z], 3=[r-phi-z] box */
#if (BOX_SHEARING==1)
#define BOX_SHEARING_PHI_COORDINATE 2
#else
#define BOX_SHEARING_PHI_COORDINATE 1
#endif
/* if the r-z or r-phi sheet is set, the code must be compiled in 2D mode */
#if (BOX_SHEARING==1) || (BOX_SHEARING==2)
#ifndef BOX_SPATIAL_DIMENSION
#define BOX_SPATIAL_DIMENSION 2
#endif
#endif
/* box must be periodic in this approximation */
#ifndef BOX_PERIODIC
#define BOX_PERIODIC
#endif
/* if not set, default to q=3/2 (q==-dlnOmega/dlnr, used for boundary and velocity corrections) */
#ifndef BOX_SHEARING_Q
#define BOX_SHEARING_Q (3.0/2.0)
#endif
/* set omega - usually we will default to always using time coordinates such that Omega = 1 at the box center */
#define BOX_SHEARING_OMEGA_BOX_CENTER 1.0
/* need analytic gravity on so we can add the appropriate source terms to the EOM */
#ifndef GRAVITY_ANALYTIC
#define GRAVITY_ANALYTIC
#endif
/* if self-gravity is on, we need to make sure the gravitational forces are not periodic. this is going to cause some errors at the x/y 'edges', 
    but for now at least, the periodic gravity routines (particularly the FFT's involved) require a regular periodic map, they cannot handle the 
    non-standard map that the shearing box represents. */
#ifndef GRAVITY_NOT_PERIODIC
#define GRAVITY_NOT_PERIODIC
#endif
#endif // BOX_SHEARING



#if defined(GRAVITY_ANALYTIC)
#if !(EXPAND_PREPROCESSOR_(GRAVITY_ANALYTIC) == 1)
#if (GRAVITY_ANALYTIC > 0)
#define GRAVITY_ANALYTIC_ANCHOR_TO_PARTICLE /* ok, analytic gravity is defined with a numerical value > 0, indicating we should use this flag */
#ifndef BH_CALC_DISTANCES
#define BH_CALC_DISTANCES
#endif
#endif
#endif
#endif


#if defined(CONDUCTION) || defined(EOS_GENERAL)
#define DOGRAD_INTERNAL_ENERGY 1
#endif

#if defined(EOS_GENERAL)
#define DOGRAD_SOUNDSPEED 1
#endif

#if defined(CONDUCTION) || defined(VISCOSITY) || defined(TURB_DIFFUSION) || defined(MHD_NON_IDEAL) || (defined(COSMIC_RAYS) && !defined(COSMIC_RAYS_DISABLE_DIFFUSION)) || (defined(RT_DIFFUSION_EXPLICIT) && !defined(RT_EVOLVE_FLUX))
#ifndef DISABLE_SUPER_TIMESTEPPING
//#define SUPER_TIMESTEP_DIFFUSION
#endif
#endif



/*------- Things that are always recommended -------*/


#ifdef MPISENDRECV_CHECKSUM
#define MPI_Sendrecv MPI_Check_Sendrecv
#endif

#ifdef MPISENDRECV_SIZELIMIT
#define MPI_Sendrecv MPI_Sizelimited_Sendrecv
#endif

#include "tags.h"
#include <assert.h>


#ifdef NUCLEAR_NETWORK
#include "nuclear/nuclear_network.h"
void network_normalize(double *x, double *e, const struct network_data *nd, struct network_workspace *nw);
int network_integrate( double temp, double rho, const double *x, double *dx, double dt, double *dedt, double *drhodt, const struct network_data *nd, struct network_workspace *nw );
#endif


#ifdef MYSORT
#define MYSORT_DATAINDEX mysort_dataindex
#else // MYSORT
#define MYSORT_DATAINDEX qsort
#endif

// compiler specific data alignment hints
// XLC compiler
#if defined(__xlC__)
#define ALIGN(n) __attribute__((__aligned__(n)))
// GNU compiler 
#elif defined(__GNUC__)
#define ALIGN(n) __attribute__((__aligned__(n)))
// Intel Compiler
#elif defined(__INTEL_COMPILER)
// GNU Intel Compiler
#define ALIGN(n) __declspec(align(n))
// Unknown Compiler
#else
#define ALIGN(n) 
#endif


#define ASSIGN_ADD(x,y,mode) (mode == 0 ? (x=y) : (x+=y))



#define  GIZMO_VERSION   "2017"	/*!< code version string */

#ifndef  GALSF_GENERATIONS
#define  GALSF_GENERATIONS     1	/*!< Number of star particles that may be created per gas particle */
#endif


typedef  int integertime;
#define  TIMEBINS        29
#define  TIMEBASE        (1<<TIMEBINS)  /*!< The simulated timespan is mapped onto the integer interval [0,TIMESPAN],
                                         *   where TIMESPAN needs to be a power of 2. Note that (1<<28) corresponds
                                         *   to 2^29
                                         */

#ifdef ADAPTIVE_GRAVSOFT_FORALL
#define AGS_OUTPUTGRAVSOFT 1  /*! output softening to snapshots */
//#define AGS_OUTPUTZETA 1 /*! output correction zeta term to snapshots */
#endif


#if defined(RADTRANSFER) || defined(RT_USE_GRAVTREE)
#define RT_BIN0 (-1)

#ifndef RT_CHEM_PHOTOION
#define RT_FREQ_BIN_H0 (RT_BIN0+0)
#else
#define RT_FREQ_BIN_H0 (RT_BIN0+1)
#endif

#ifndef RT_PHOTOION_MULTIFREQUENCY
#define RT_FREQ_BIN_He0 (RT_FREQ_BIN_H0+0)
#define RT_FREQ_BIN_He1 (RT_FREQ_BIN_He0+0)
#define RT_FREQ_BIN_He2 (RT_FREQ_BIN_He1+0)
#else
#define RT_FREQ_BIN_He0 (RT_FREQ_BIN_H0+1)
#define RT_FREQ_BIN_He1 (RT_FREQ_BIN_He0+1)
#define RT_FREQ_BIN_He2 (RT_FREQ_BIN_He1+1)
#endif

#ifndef GALSF_FB_FIRE_RT_LONGRANGE
#define RT_FREQ_BIN_FIRE_UV (RT_FREQ_BIN_He2+0)
#define RT_FREQ_BIN_FIRE_OPT (RT_FREQ_BIN_FIRE_UV+0)
#define RT_FREQ_BIN_FIRE_IR (RT_FREQ_BIN_FIRE_OPT+0)
#else
#define RT_FREQ_BIN_FIRE_UV (RT_FREQ_BIN_He2+1)
#define RT_FREQ_BIN_FIRE_OPT (RT_FREQ_BIN_FIRE_UV+1)
#define RT_FREQ_BIN_FIRE_IR (RT_FREQ_BIN_FIRE_OPT+1)
#endif

#ifndef RT_SOFT_XRAY
#define RT_FREQ_BIN_SOFT_XRAY (RT_FREQ_BIN_FIRE_IR+0)
#else
#define RT_FREQ_BIN_SOFT_XRAY (RT_FREQ_BIN_FIRE_IR+1)
#endif

#ifndef RT_HARD_XRAY
#define RT_FREQ_BIN_HARD_XRAY (RT_FREQ_BIN_SOFT_XRAY+0)
#else
#define RT_FREQ_BIN_HARD_XRAY (RT_FREQ_BIN_SOFT_XRAY+1)
#endif

#ifndef RT_PHOTOELECTRIC
#define RT_FREQ_BIN_PHOTOELECTRIC (RT_FREQ_BIN_HARD_XRAY+0)
#else
#define RT_FREQ_BIN_PHOTOELECTRIC (RT_FREQ_BIN_HARD_XRAY+1)
#endif

#ifndef RT_LYMAN_WERNER
#define RT_FREQ_BIN_LYMAN_WERNER (RT_FREQ_BIN_PHOTOELECTRIC+0)
#else
#define RT_FREQ_BIN_LYMAN_WERNER (RT_FREQ_BIN_PHOTOELECTRIC+1)
#endif

#ifndef RT_NUV
#define RT_FREQ_BIN_NUV (RT_FREQ_BIN_LYMAN_WERNER+0)
#else
#define RT_FREQ_BIN_NUV (RT_FREQ_BIN_LYMAN_WERNER+1)
#endif

#ifndef RT_OPTICAL_NIR
#define RT_FREQ_BIN_OPTICAL_NIR (RT_FREQ_BIN_NUV+0)
#else
#define RT_FREQ_BIN_OPTICAL_NIR (RT_FREQ_BIN_NUV+1)
#endif


/* be sure to add all new wavebands to these lists, or else we will run into problems */
/* ALSO, the IR bin here should be the last bin: add additional bins ABOVE this line */
#ifndef RT_INFRARED
#define RT_FREQ_BIN_INFRARED (RT_FREQ_BIN_OPTICAL_NIR+0)
#else
#define RT_FREQ_BIN_INFRARED (RT_FREQ_BIN_OPTICAL_NIR+1)
#endif

#define N_RT_FREQ_BINS (RT_FREQ_BIN_INFRARED+1)

#endif // #if defined(RADTRANSFER) || defined(RT_USE_GRAVTREE)


#ifndef  MULTIPLEDOMAINS
#define  MULTIPLEDOMAINS     8
#endif

#ifndef  TOPNODEFACTOR
#define  TOPNODEFACTOR       4.0
#endif

#ifndef  GRAVCOSTLEVELS
#define  GRAVCOSTLEVELS      6
#endif

#define  NUMBER_OF_MEASUREMENTS_TO_RECORD  6  /* this is the number of past executions of a timebin that the reported average CPU-times average over */

#define  NODELISTLENGTH      8


#define EPSILON_FOR_TREERND_SUBNODE_SPLITTING (1.0e-3) /* define some number << 1; particles with less than this separation will trigger randomized sub-node splitting in the tree.
                                                            we set it to a global value here so that other sub-routines will know not to force particle separations below this */

#ifdef GALSF_SFR_IMF_VARIATION
#define N_IMF_FORMPROPS  13  /*!< formation properties of star particles to record for output */
#endif


typedef unsigned long long peanokey;


#define  BITS_PER_DIMENSION 21	/* for Peano-Hilbert order. Note: Maximum is 10 to fit in 32-bit integer ! */
#define  PEANOCELLS (((peanokey)1)<<(3*BITS_PER_DIMENSION))

#define  BITS_PER_DIMENSION_SAVE_KEYS 10
#define  PEANOCELLS_SAVE_KEYS (((peanokey)1)<<(3*BITS_PER_DIMENSION_SAVE_KEYS))



#define  terminate(x) {char termbuf[2000]; sprintf(termbuf, "code termination on task=%d, function '%s()', file '%s', line %d: '%s'\n", ThisTask, __FUNCTION__, __FILE__, __LINE__, x); printf("%s", termbuf); fflush(stdout); MPI_Abort(MPI_COMM_WORLD, 1); exit(0);}

#ifndef DISABLE_MEMORY_MANAGER
#define  mymalloc(x, y)            mymalloc_fullinfo(x, y, __FUNCTION__, __FILE__, __LINE__)
#define  mymalloc_movable(x, y, z) mymalloc_movable_fullinfo(x, y, z, __FUNCTION__, __FILE__, __LINE__)

#define  myrealloc(x, y)           myrealloc_fullinfo(x, y, __FUNCTION__, __FILE__, __LINE__)
#define  myrealloc_movable(x, y)   myrealloc_movable_fullinfo(x, y, __FUNCTION__, __FILE__, __LINE__)

#define  myfree(x)                 myfree_fullinfo(x, __FUNCTION__, __FILE__, __LINE__)
#define  myfree_movable(x)         myfree_movable_fullinfo(x, __FUNCTION__, __FILE__, __LINE__)

#define  report_memory_usage(x, y) report_detailed_memory_usage_of_largest_task(x, y, __FUNCTION__, __FILE__, __LINE__)
#else
#define  mymalloc(x, y)            malloc(y)
#define  mymalloc_movable(x, y, z) malloc(z)

#define  myrealloc(x, y)           realloc(x, y)
#define  myrealloc_movable(x, y)   realloc(x, y)

#define  myfree(x)                 free(x)
#define  myfree_movable(x)         free(x)

#define  report_memory_usage(x, y) printf("Memory manager disabled.\n")
#endif


#ifdef GAMMA_ENFORCE_ADIABAT
#define EOS_ENFORCE_ADIABAT (GAMMA_ENFORCE_ADIABAT) /* this allows for either term to be defined, for backwards-compatibility */
#endif

#if !defined(GAMMA) /* this allows for either term to be defined, for backwards-compatibility */
#ifndef EOS_GAMMA
#define  GAMMA         (5.0/3.0)	/*!< adiabatic index of simulated gas */
#else
#define  GAMMA         (EOS_GAMMA)
#endif
#endif

#define  GAMMA_MINUS1  (GAMMA-1)
#define  GAMMA_MINUS1_INV  (1./(GAMMA-1))

#if !defined(RT_HYDROGEN_GAS_ONLY) || defined(RT_CHEM_PHOTOION_HE)
#define  HYDROGEN_MASSFRAC 0.76 /*!< mass fraction of hydrogen, relevant only for radiative cooling */
#else
#define  HYDROGEN_MASSFRAC 1.0  /*!< mass fraction of hydrogen, relevant only for radiative cooling */
#endif

#define  MAX_REAL_NUMBER  1e37
#define  MIN_REAL_NUMBER  1e-37

#if (defined(MAGNETIC) && !defined(COOLING))
#define  CONDITION_NUMBER_DANGER  1.0e7 /*!< condition number above which we will not trust matrix-based gradients */
#else
#define  CONDITION_NUMBER_DANGER  1.0e3 /*!< condition number above which we will not trust matrix-based gradients */
#endif

#ifdef USE_PREGENERATED_RANDOM_NUMBER_TABLE
#define  RNDTABLE 16384 /*!< this is arbitrary, but some power of 2 makes much easier */
#endif

/* ... often used physical constants (cgs units) */

#define  GRAVITY     6.672e-8
#define  SOLAR_MASS  1.989e33
#define  SOLAR_LUM   3.826e33
#define  RAD_CONST   7.565e-15
#define  AVOGADRO    6.0222e23
#define  BOLTZMANN   1.38066e-16
#define  GAS_CONST   8.31425e7
#define  C           2.9979e10
#define  PLANCK      6.6262e-27
#define  CM_PER_MPC  3.085678e24
#define  PROTONMASS  1.6726e-24
#define  ELECTRONMASS 9.10953e-28
#define  THOMPSON     6.65245e-25
#define  ELECTRONCHARGE  4.8032e-10
#define  HUBBLE          3.2407789e-18	/* in h/sec */
#define  LYMAN_ALPHA      1215.6e-8	/* 1215.6 Angstroem */
#define  LYMAN_ALPHA_HeII  303.8e-8	/* 303.8 Angstroem */
#define  OSCILLATOR_STRENGTH       0.41615
#define  OSCILLATOR_STRENGTH_HeII  0.41615
#define  ELECTRONVOLT_IN_ERGS      1.60217733e-12

#define KAPPA_IR 10.0   /* in cm^2/g for solar abundances */
#define KAPPA_OP 180.0
#define KAPPA_UV 1800.0

#ifdef GALSF_FB_FIRE_RT_HIIHEATING
#define HIIRegion_Temp 1.0e4 /* temperature (in K) of heated gas */
#endif


#ifdef METALS

#ifdef GALSF_FB_FIRE_RPROCESS
#define NUM_RPROCESS_SPECIES (GALSF_FB_FIRE_RPROCESS)
#else
#define NUM_RPROCESS_SPECIES 0
#endif

#ifdef COOL_METAL_LINES_BY_SPECIES
#define NUM_METAL_SPECIES (11+NUM_RPROCESS_SPECIES)
#else
#define NUM_METAL_SPECIES (1+NUM_RPROCESS_SPECIES)
#endif

#endif // METALS //



#define  SEC_PER_MEGAYEAR   3.155e13
#define  SEC_PER_YEAR       3.155e7


#ifndef FOF_PRIMARY_LINK_TYPES
#define FOF_PRIMARY_LINK_TYPES 2
#endif

#ifndef FOF_SECONDARY_LINK_TYPES
#define FOF_SECONDARY_LINK_TYPES 0
#endif


/* some flags for the field "flag_ic_info" in the file header */
#define FLAG_ZELDOVICH_ICS     1
#define FLAG_SECOND_ORDER_ICS  2
#define FLAG_EVOLVED_ZELDOVICH 3
#define FLAG_EVOLVED_2LPT      4
#define FLAG_NORMALICS_2LPT    5


#ifndef ASMTH
/*! ASMTH gives the scale of the short-range/long-range force split in units of FFT-mesh cells */
#define ASMTH 1.25
#endif
#ifndef RCUT
/*! RCUT gives the maximum distance (in units of the scale used for the force split) out to which short-range
 * forces are evaluated in the short-range tree walk.
 */
#define RCUT  4.5
#endif

#define COND_TIMESTEP_PARAMETER 0.25
#define VISC_TIMESTEP_PARAMETER 0.25

#define MAXLEN_OUTPUTLIST 1200	/*!< maxmimum number of entries in output list */

#define DRIFT_TABLE_LENGTH  1000	/*!< length of the lookup table used to hold the drift and kick factors */


#define MAXITER 150

#ifndef LINKLENGTH
#define LINKLENGTH 0.2
#endif

#ifndef FOF_GROUP_MIN_LEN
#define FOF_GROUP_MIN_LEN 32
#endif

#define MINRESTFAC 0.05


#ifdef SUBFIND_DENSITY_AND_POTENTIAL  /*!< activate needed options */
 #define ONLY_PRODUCE_HSML_FILES
 #define COMPUTE_POTENTIAL_ENERGY
 #define SUBFIND_RESHUFFLE_AND_POTENTIAL
 #define SUBFIND_RESHUFFLE_CATALOGUE
#endif


#ifndef GDE_TYPES 
#define GDE_TYPES 2
#endif

#ifndef LONGIDS
typedef unsigned int MyIDType;
#else
typedef unsigned long long MyIDType;
#endif


#ifndef DOUBLEPRECISION     /* default is single-precision */
typedef float  MyFloat;
typedef float  MyDouble;
#else                        /* everything double-precision */
typedef double  MyFloat;
typedef double  MyDouble;
#endif

#ifdef OUTPUT_IN_DOUBLEPRECISION
typedef double MyOutputFloat;
#else
typedef float MyOutputFloat;
#endif

#ifdef INPUT_IN_DOUBLEPRECISION
typedef double MyInputFloat;
#else
typedef float MyInputFloat;
#endif

#ifdef OUTPUT_POSITIONS_IN_DOUBLE
typedef double MyOutputPosFloat;
#else
typedef MyOutputFloat MyOutputPosFloat;
#endif
#ifdef INPUT_POSITIONS_IN_DOUBLE
typedef double MyInputPosFloat;
#else
typedef MyInputFloat MyInputPosFloat;
#endif


struct unbind_data
{
  int index;
};


#ifdef FIX_PATHSCALE_MPI_STATUS_IGNORE_BUG
extern MPI_Status mpistat;
#undef MPI_STATUS_IGNORE
#define MPI_STATUS_IGNORE &mpistat
#endif

#define FLT(x) (x)
typedef MyFloat MyLongDouble;
typedef MyDouble MyBigFloat;

#define GDE_ABS(x) (fabs(x))
#define GDE_SQRT(x) (sqrt(x))
#define GDE_LOG(x) (log(x))


#define CPU_ALL            0
#define CPU_TREEWALK1      1
#define CPU_TREEWALK2      2
#define CPU_TREEWAIT1      3
#define CPU_TREEWAIT2      4
#define CPU_TREESEND       5
#define CPU_TREERECV       6
#define CPU_TREEMISC       7
#define CPU_TREEBUILD      8
#define CPU_TREEUPDATE     9
#define CPU_TREEHMAXUPDATE 10
#define CPU_DOMAIN         11
#define CPU_DENSCOMPUTE    12
#define CPU_DENSWAIT       13
#define CPU_DENSCOMM       14
#define CPU_DENSMISC       15
#define CPU_HYDCOMPUTE     16
#define CPU_HYDWAIT        17
#define CPU_HYDCOMM        18
#define CPU_HYDMISC        19
#define CPU_DRIFT          20
#define CPU_TIMELINE       21
#define CPU_POTENTIAL      22
#define CPU_MESH           23
#define CPU_PEANO          24
#define CPU_COOLINGSFR     25
#define CPU_SNAPSHOT       26
#define CPU_FOF            27
#define CPU_BLACKHOLES     28
#define CPU_MISC           29
#define CPU_DRAGFORCE      30
#define CPU_SNIIHEATING    31
#define CPU_HIIHEATING     32
#define CPU_LOCALWIND      33
#define CPU_HYDNETWORK     34
#define CPU_AGSDENSCOMPUTE 35
#define CPU_AGSDENSWAIT    36
#define CPU_AGSDENSCOMM    37
#define CPU_AGSDENSMISC    38
#define CPU_SIDMSCATTER    39
#define CPU_PARTS          40  /* this gives the number of parts above (must be last) */

#define CPU_STRING_LEN 120

#if (BOX_SPATIAL_DIMENSION==1) || defined(ONEDIM)
#define NUMDIMS 1           /* define number of dimensions and volume normalization */
#define NORM_COEFF 2.0
#elif (BOX_SPATIAL_DIMENSION==2) || defined(TWODIMS)
#define NUMDIMS 2
#define NORM_COEFF M_PI
#else
#define NORM_COEFF 4.188790204786  /* 4pi/3 */
#define NUMDIMS 3
#endif


#define PPP P
#if defined(ADAPTIVE_GRAVSOFT_FORALL)
#define PPPZ P
#else
#define PPPZ SphP
#endif

#ifdef BOX_PERIODIC
extern MyDouble boxSize, boxHalf, inverse_boxSize;
#ifdef BOX_LONG_X
extern MyDouble boxSize_X, boxHalf_X, inverse_boxSize_X;
#else
#define boxSize_X boxSize
#define boxHalf_X boxHalf
#define inverse_boxSize_X inverse_boxSize
#endif
#ifdef BOX_LONG_Y
extern MyDouble boxSize_Y, boxHalf_Y, inverse_boxSize_Y;
#else
#define boxSize_Y boxSize
#define boxHalf_Y boxHalf
#define inverse_boxSize_Y inverse_boxSize
#endif
#ifdef BOX_LONG_Z
extern MyDouble boxSize_Z, boxHalf_Z, inverse_boxSize_Z;
#else
#define boxSize_Z boxSize
#define boxHalf_Z boxHalf
#define inverse_boxSize_Z inverse_boxSize
#endif
#endif


#ifdef BOX_SHEARING
extern MyDouble Shearing_Box_Vel_Offset;
extern MyDouble Shearing_Box_Pos_Offset;
#endif


/****************************************************************************************************************************/
/* Here we define the box-wrapping macros NEAREST_XYZ and NGB_PERIODIC_BOX_LONG_X,NGB_PERIODIC_BOX_LONG_Y,NGB_PERIODIC_BOX_LONG_Z. 
 *   The inputs to these functions are (dx_position, dy_position, dz_position, sign), where 
 *     'sign' = -1 if dx_position = x_test_point - x_reference (reference = particle from which we are doing a calculation), 
 *     'sign' = +1 if dx_position = x_reference - x_test_point
 *
 *   For non-periodic cases these functions are trivial (do nothing, or just take absolute values).
 *
 *   For standard periodic cases it will wrap in each dimension, allowing for a different box length in X/Y/Z.
 *      here the "sign" term is irrelevant. Also NGB_PERIODIC_BOX_LONG_X, NGB_PERIODIC_BOX_LONG_Y, NGB_PERIODIC_BOX_LONG_Z will each 
 *      compile to only use the x,y, or z information, but all four inputs are required for the sake of completeness 
 *      and consistency.
 *
 *   The reason for the added complexity is for shearing boxes. In this case, the Y(phi)-coordinate for particles being
 *      wrapped in the X(r)-direction must be modified by a time-dependent term. It also matters for the sign of that 
 *      term "which side" of the box we are wrapping across (i.e. does the 'virtual particle' -- the test point which is
 *      not the particle for which we are currently calculating forces, etc -- lie on the '-x' side or the '+x' side)
 *      (note after all that: if very careful, sign -cancels- within the respective convention, for the type of wrapping below)
 */
/****************************************************************************************************************************/

#ifdef BOX_PERIODIC
#define NGB_PERIODIC_BOX_LONG_X(x,y,z,sign) (xtmp=fabs(x),(xtmp>boxHalf_X)?(boxSize_X-xtmp):xtmp) // normal periodic wrap //
#define NGB_PERIODIC_BOX_LONG_Z(x,y,z,sign) (xtmp=fabs(z),(xtmp>boxHalf_Z)?(boxSize_Z-xtmp):xtmp) // normal periodic wrap //

#if (BOX_SHEARING > 1)
/* Shearing Periodic Box::
    in this case, we have a shearing box with the '1' coordinate being phi, so there is a periodic extra wrap */
#define NEAREST_XYZ(x,y,z,sign) (\
y += Shearing_Box_Pos_Offset * (((x)>boxHalf_X)?(1):(((x)<-boxHalf_X)?(-1):(0))),\
y = ((y)>boxSize_Y)?((y)-boxSize_Y):(((y)<-boxSize_Y)?((y)+boxSize_Y):(y)),\
y=((y)>boxHalf_Y)?((y)-boxSize_Y):(((y)<-boxHalf_Y)?((y)+boxSize_Y):(y)),\
x=((x)>boxHalf_X)?((x)-boxSize_X):(((x)<-boxHalf_X)?((x)+boxSize_X):(x)),\
z=((z)>boxHalf_Z)?((z)-boxSize_Z):(((z)<-boxHalf_Z)?((z)+boxSize_Z):(z)))

#define NGB_PERIODIC_BOX_LONG_Y(x,y,z,sign) (\
xtmp = y + Shearing_Box_Pos_Offset * (((x)>boxHalf_X)?(1):(((x)<-boxHalf_X)?(-1):(0))),\
xtmp = fabs(((xtmp)>boxSize_Y)?((xtmp)-boxSize_Y):(((xtmp)<-boxSize_Y)?((xtmp)+boxSize_Y):(xtmp))),\
(xtmp>boxHalf_Y)?(boxSize_Y-xtmp):xtmp)

#else
/* Standard Periodic Box:: 
    this box-wraps all three (x,y,z) separation variables when taking position differences */
#define NEAREST_XYZ(x,y,z,sign) (\
x=((x)>boxHalf_X)?((x)-boxSize_X):(((x)<-boxHalf_X)?((x)+boxSize_X):(x)),\
y=((y)>boxHalf_Y)?((y)-boxSize_Y):(((y)<-boxHalf_Y)?((y)+boxSize_Y):(y)),\
z=((z)>boxHalf_Z)?((z)-boxSize_Z):(((z)<-boxHalf_Z)?((z)+boxSize_Z):(z)))
#define NGB_PERIODIC_BOX_LONG_Y(x,y,z,sign) (xtmp=fabs(y),(xtmp>boxHalf_Y)?(boxSize_Y-xtmp):xtmp) // normal periodic wrap //

#endif

#else
/* Non-periodic box:: */
#define NEAREST_XYZ(x,y,z,sign) /* this is an empty macro: nothing will happen to the variables input here */
#define NGB_PERIODIC_BOX_LONG_X(x,y,z,sign) (fabs(x))
#define NGB_PERIODIC_BOX_LONG_Y(x,y,z,sign) (fabs(y))
#define NGB_PERIODIC_BOX_LONG_Z(x,y,z,sign) (fabs(z))
#endif

#define FACT1 0.366025403785	/* FACT1 = 0.5 * (sqrt(3)-1) */
#define FACT2 0.86602540        /* FACT2 = 0.5 * sqrt(3) */



/*********************************************************/
/*  Global variables                                     */
/*********************************************************/


extern int FirstActiveParticle;
extern int *NextActiveParticle;
extern unsigned char *ProcessedFlag;

extern int TimeBinCount[TIMEBINS];
extern int TimeBinCountSph[TIMEBINS];
extern int TimeBinActive[TIMEBINS];

extern int FirstInTimeBin[TIMEBINS];
extern int LastInTimeBin[TIMEBINS];
extern int *NextInTimeBin;
extern int *PrevInTimeBin;

#ifdef GALSF
extern double TimeBinSfr[TIMEBINS];
#endif

#ifdef BLACK_HOLES
#define BH_COUNTPROGS
/* carries a counter for each BH that gives the total number of seeds that merged into it */
#define BH_ENFORCE_EDDINGTON_LIMIT
/* put a hard limit on the maximum accretion rate (set BlackHoleEddingtonFactor>>1 to allow super-eddington) */
extern double TimeBin_BH_mass[TIMEBINS];
extern double TimeBin_BH_dynamicalmass[TIMEBINS];
extern double TimeBin_BH_Mdot[TIMEBINS];
extern double TimeBin_BH_Medd[TIMEBINS];
#if defined(BH_GRAVCAPTURE_GAS) || defined(BH_GRAVACCRETION) || defined(BH_GRAVCAPTURE_NONGAS) || defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS) || defined(BH_DYNFRICTION)
#define BH_NEIGHBOR_BITFLAG 63 /* allow all particle types in the BH search: 63=2^0+2^1+2^2+2^3+2^4+2^5 */
#else
#define BH_NEIGHBOR_BITFLAG 33 /* only search for particles of types 0 and 5 (gas and black holes) around a primary BH particle */
#endif
#endif

extern int ThisTask;		/*!< the number of the local processor  */
extern int NTask;		/*!< number of processors */
extern int PTask;		/*!< note: NTask = 2^PTask */

extern double CPUThisRun;	/*!< Sums CPU time of current process */

extern int NumForceUpdate;	/*!< number of active particles on local processor in current timestep  */
extern long long GlobNumForceUpdate;

extern int NumSphUpdate;	/*!< number of active SPH particles on local processor in current timestep  */

extern int MaxTopNodes;	        /*!< Maximum number of nodes in the top-level tree used for domain decomposition */

extern int RestartFlag;		/*!< taken from command line used to start code. 0 is normal start-up from
				   initial conditions, 1 is resuming a run from a set of restart files, while 2
				   marks a restart from a snapshot file. */
extern int RestartSnapNum;
extern int SelRnd;

extern int TakeLevel;

extern int *Exportflag;	        /*!< Buffer used for flagging whether a particle needs to be exported to another process */
extern int *Exportnodecount;
extern int *Exportindex;

extern int *Send_offset, *Send_count, *Recv_count, *Recv_offset;

extern size_t AllocatedBytes;
extern size_t HighMarkBytes;
extern size_t FreeBytes;

extern double CPU_Step[CPU_PARTS];
extern char CPU_Symbol[CPU_PARTS];
extern char CPU_SymbolImbalance[CPU_PARTS];
extern char CPU_String[CPU_STRING_LEN + 1];

extern double WallclockTime;    /*!< This holds the last wallclock time measurement for timings measurements */

extern int Flag_FullStep;	/*!< Flag used to signal that the current step involves all particles */

extern size_t HighMark_run,  HighMark_domain, HighMark_gravtree, HighMark_pmperiodic,
  HighMark_pmnonperiodic,  HighMark_sphdensity, HighMark_sphhydro, HighMark_GasGrad;

#ifdef TURB_DRIVING
extern size_t HighMark_turbpower;
#endif

extern int TreeReconstructFlag;
extern int GlobFlag;

extern char DumpFlag;

extern int NumPart;		/*!< number of particles on the LOCAL processor */
extern int N_gas;		/*!< number of gas particles on the LOCAL processor  */
#ifdef SEPARATE_STELLARDOMAINDECOMP
extern int N_stars;
#endif

extern long long Ntype[6];	/*!< total number of particles of each type */
extern int NtypeLocal[6];	/*!< local number of particles of each type */

extern gsl_rng *random_generator;	/*!< the random number generator used */


extern int Gas_split;           /*!< current number of newly-spawned gas particles outside block */
#ifdef GALSF
extern int Stars_converted;	/*!< current number of star particles in gas particle block */
#endif

extern double TimeOfLastTreeConstruction;	/*!< holds what it says */

extern int *Ngblist;		/*!< Buffer to hold indices of neighbours retrieved by the neighbour search
				   routines */

extern double *R2ngblist;

extern double DomainCorner[3], DomainCenter[3], DomainLen, DomainFac;
extern int *DomainStartList, *DomainEndList;

extern double *DomainWork;
extern int *DomainCount;
extern int *DomainCountSph;
extern int *DomainTask;
extern int *DomainNodeIndex;
extern int *DomainList, DomainNumChanged;

extern peanokey *Key, *KeySorted;


#ifdef RT_CHEM_PHOTOION
double rt_nu_eff_eV[N_RT_FREQ_BINS];
double precalc_stellar_luminosity_fraction[N_RT_FREQ_BINS];
double nu[N_RT_FREQ_BINS];
double rt_sigma_HI[N_RT_FREQ_BINS];
double rt_sigma_HeI[N_RT_FREQ_BINS];
double rt_sigma_HeII[N_RT_FREQ_BINS];
double G_HI[N_RT_FREQ_BINS];
double G_HeI[N_RT_FREQ_BINS];
double G_HeII[N_RT_FREQ_BINS];
#endif

extern struct topnode_data
{
  peanokey Size;
  peanokey StartKey;
  long long Count;
  MyFloat GravCost;
  int Daughter;
  int Pstart;
  int Blocks;
  int Leaf;
} *TopNodes;

extern int NTopnodes, NTopleaves;

#ifdef USE_PREGENERATED_RANDOM_NUMBER_TABLE
extern double RndTable[RNDTABLE];
#endif


#ifdef SUBFIND
extern int GrNr;
extern int NumPartGroup;
#endif



/* variables for input/output , usually only used on process 0 */


extern char ParameterFile[100];	/*!< file name of parameterfile used for starting the simulation */

extern FILE
#ifndef IO_REDUCED_MODE
 *FdTimebin,    /*!< file handle for timebin.txt log-file. */
 *FdInfo,       /*!< file handle for info.txt log-file. */
 *FdEnergy,     /*!< file handle for energy.txt log-file. */
 *FdTimings,    /*!< file handle for timings.txt log-file. */
 *FdBalance,    /*!< file handle for balance.txt log-file. */
#ifdef RT_CHEM_PHOTOION
 *FdRad,		/*!< file handle for radtransfer.txt log-file. */
#endif
#ifdef TURB_DRIVING
 *FdTurb,       /*!< file handle for turb.txt log-file */
#endif
#ifdef GR_TABULATED_COSMOLOGY
 *FdDE,         /*!< file handle for darkenergy.txt log-file. */
#endif
#endif
 *FdCPU;        /*!< file handle for cpu.txt log-file. */

#ifdef SCF_POTENTIAL
extern FILE *FdSCF;
#endif

#ifdef GALSF
extern FILE *FdSfr;		/*!< file handle for sfr.txt log-file. */
#endif
#ifdef GALSF_FB_FIRE_RT_LOCALRP
extern FILE *FdMomWinds;	/*!< file handle for MomWinds.txt log-file */
#endif
#ifdef GALSF_FB_FIRE_RT_HIIHEATING
extern FILE *FdHIIHeating;	/*!< file handle for HIIheating.txt log-file */
#endif
#ifdef GALSF_FB_MECHANICAL
extern FILE *FdSneIIHeating;	/*!< file handle for SNIIheating.txt log-file */
#endif

#ifdef GDE_DISTORTIONTENSOR
#ifdef PMGRID
extern FILE *FdTidaltensor;     /*!< file handle for tidaltensor.txt log-file. */
#endif
#endif

#ifdef BLACK_HOLES
extern FILE *FdBlackHoles;	/*!< file handle for blackholes.txt log-file. */
//#ifndef IO_REDUCED_MODE   DAA-IO: BH_OUTPUT_MOREINFO overrides IO_REDUCED_MODE
#if !defined(IO_REDUCED_MODE) || defined(BH_OUTPUT_MOREINFO)
extern FILE *FdBlackHolesDetails;
#ifdef BH_OUTPUT_MOREINFO
extern FILE *FdBhMergerDetails;
#ifdef BH_WIND_KICK
extern FILE *FdBhWindDetails;
#endif
#endif
#endif
#endif



#if defined(COOLING) && defined(GALSF_EFFECTIVE_EQS)
#ifndef COOLING_OPERATOR_SPLIT
#define COOLING_OPERATOR_SPLIT /*!< the Springel-Hernquist EOS depends explicitly on the cooling time in a way that requires de-coupled hydro cooling */
#endif
#endif





/*! table for the cosmological drift factors */
extern double DriftTable[DRIFT_TABLE_LENGTH];

/*! table for the cosmological kick factor for gravitational forces */
extern double GravKickTable[DRIFT_TABLE_LENGTH];

extern void *CommBuffer;	/*!< points to communication buffer, which is used at a few places */

/*! This structure contains data which is the SAME for all tasks (mostly code parameters read from the
 * parameter file).  Holding this data in a structure is convenient for writing/reading the restart file, and
 * it allows the introduction of new global variables in a simple way. The only thing to do is to introduce
 * them into this structure.
 */
extern struct global_data_all_processes
{
  long long TotNumPart;		/*!<  total particle numbers (global value) */
  long long TotN_gas;		/*!<  total gas particle number (global value) */

#ifdef BLACK_HOLES
  int TotBHs;
#endif

    
#ifdef DM_SIDM
    MyDouble InteractionCrossSection;  /*!< self-interaction cross-section in [cm^2/g]*/
#endif
    
  int MaxPart;			/*!< This gives the maxmimum number of particles that can be stored on one processor. */
  int MaxPartSph;		/*!< This gives the maxmimum number of SPH particles that can be stored on one processor. */
  int ICFormat;			/*!< selects different versions of IC file-format */
  int SnapFormat;		/*!< selects different versions of snapshot file-formats */
  int DoDynamicUpdate;
  int NumFilesPerSnapshot;	/*!< number of files in multi-file snapshot dumps */
  int NumFilesWrittenInParallel;	/*!< maximum number of files that may be written simultaneously when
                                     writing/reading restart-files, or when writing snapshot files */
  double BufferSize;		/*!< size of communication buffer in MB */
  int BunchSize;     	        /*!< number of particles fitting into the buffer in the parallel tree algorithm  */

  double PartAllocFactor;	/*!< in order to maintain work-load balance, the particle load will usually
				   NOT be balanced.  Each processor allocates memory for PartAllocFactor times
				   the average number of particles to allow for that */

  double TreeAllocFactor;	/*!< Each processor allocates a number of nodes which is TreeAllocFactor times
				   the maximum(!) number of particles.  Note: A typical local tree for N
				   particles needs usually about ~0.65*N nodes. */

  double TopNodeAllocFactor;	/*!< Each processor allocates a number of nodes which is TreeAllocFactor times
				   the maximum(!) number of particles.  Note: A typical local tree for N
				   particles needs usually about ~0.65*N nodes. */

#ifdef DM_SCALARFIELD_SCREENING
  double ScalarBeta;
  double ScalarScreeningLength;
#endif

  /* some SPH parameters */

  double DesNumNgb;		/*!< Desired number of SPH neighbours */
#ifdef SUBFIND
  int DesLinkNgb;
  double ErrTolThetaSubfind;
#endif

  double MaxNumNgbDeviation;	/*!< Maximum allowed deviation neighbour number */

  double ArtBulkViscConst;	/*!< Sets the parameter \f$\alpha\f$ of the artificial viscosity */
  double InitGasTemp;		/*!< may be used to set the temperature in the IC's */
  double InitGasU;		/*!< the same, but converted to thermal energy per unit mass */
  double MinGasTemp;		/*!< may be used to set a floor for the gas temperature */
  double MinEgySpec;		/*!< the minimum allowed temperature expressed as energy per unit mass */
#ifdef SPHAV_ARTIFICIAL_CONDUCTIVITY
  double ArtCondConstant;
#endif
    
#ifdef PM_HIRES_REGION_CLIPDM
    double MassOfClippedDMParticles; /*!< the mass of high-res DM particles which the low-res particles will target if they enter the highres region */
#endif
    
    double MinMassForParticleMerger; /*!< the minimum mass of a gas particle below which it will be merged into a neighbor */
    double MaxMassForParticleSplit; /*!< the maximum mass of a gas particle above which it will be split into a pair */

  /* some force counters  */

  long long TotNumOfForces;	/*!< counts total number of force computations  */

  long long NumForcesSinceLastDomainDecomp;	/*!< count particle updates since last domain decomposition */

  /* some variable for dynamic work-load adjustment based on CPU measurements */

  double cf_atime, cf_a2inv, cf_a3inv, cf_afac1, cf_afac2, cf_afac3, cf_hubble_a, cf_hubble_a2;   /* various cosmological factors that are only a function of the current scale factor, and in Newtonian runs are set to 1 */

  /* system of units  */

  double UnitTime_in_s,		/*!< factor to convert internal time unit to seconds/h */
    UnitMass_in_g,		/*!< factor to convert internal mass unit to grams/h */
    UnitVelocity_in_cm_per_s,	/*!< factor to convert intqernal velocity unit to cm/sec */
    UnitLength_in_cm,		/*!< factor to convert internal length unit to cm/h */
    UnitPressure_in_cgs,	/*!< factor to convert internal pressure unit to cgs units (little 'h' still around!) */
    UnitDensity_in_cgs,		/*!< factor to convert internal length unit to g/cm^3*h^2 */
    UnitEnergy_in_cgs,		/*!< factor to convert internal energy to cgs units */
    UnitTime_in_Megayears,	/*!< factor to convert internal time to megayears/h */
    GravityConstantInternal,	/*!< If set to zero in the parameterfile, the internal value of the
				   gravitational constant is set to the Newtonian value based on the system of
				   units specified. Otherwise the value provided is taken as internal gravity
				   constant G. */
    G;				/*!< Gravity-constant in internal units */
#ifdef GDE_DISTORTIONTENSOR
  double UnitDensity_in_Gev_per_cm3; /*!< factor to convert internal density unit to GeV/c^2 / cm^3 */
#endif
    /* Cosmology */

#ifdef MAGNETIC
  double UnitMagneticField_in_gauss; /*!< factor to convert internal magnetic field (B) unit to gauss (cgs) units */
#endif
    
  double Hubble_H0_CodeUnits;		/*!< Hubble-constant (unit-ed version: 100 km/s/Mpc) in internal units */
  double Omega0,		/*!< matter density in units of the critical density (at z=0) */
    OmegaLambda,		/*!< vaccum energy density relative to crictical density (at z=0) */
    OmegaBaryon,		/*!< baryon density in units of the critical density (at z=0) */
    HubbleParam;		/*!< little `h', i.e. Hubble constant in units of 100 km/s/Mpc.  Only needed to get absolute
				 * physical values for cooling physics
				 */

  double BoxSize;		/*!< Boxsize in case periodic boundary conditions are used */

    
  /* Code options */

  int ComovingIntegrationOn;	/*!< flags that comoving integration is enabled */
  int ResubmitOn;		/*!< flags that automatic resubmission of job to queue system is enabled */
  int TypeOfOpeningCriterion;	/*!< determines tree cell-opening criterion: 0 for Barnes-Hut, 1 for relative criterion */
  int OutputListOn;		/*!< flags that output times are listed in a specified file */

  int HighestActiveTimeBin;
  int HighestOccupiedTimeBin;

  /* parameters determining output frequency */

  int SnapshotFileCount;	/*!< number of snapshot that is written next */
  double TimeBetSnapshot,	/*!< simulation time interval between snapshot files */
    TimeOfFirstSnapshot,	/*!< simulation time of first snapshot files */
    CpuTimeBetRestartFile,	/*!< cpu-time between regularly generated restart files */
    TimeLastRestartFile,	/*!< cpu-time when last restart-file was written */
    TimeBetStatistics,		/*!< simulation time interval between computations of energy statistics */
    TimeLastStatistics;		/*!< simulation time when the energy statistics was computed the last time */
  int NumCurrentTiStep;		/*!< counts the number of system steps taken up to this point */

  /* Current time of the simulation, global step, and end of simulation */

  double Time,			/*!< current time of the simulation */
    TimeBegin,			/*!< time of initial conditions of the simulation */
    TimeStep,			/*!< difference between current times of previous and current timestep */
    TimeMax;			/*!< marks the point of time until the simulation is to be evolved */

  /* variables for organizing discrete timeline */

  double Timebase_interval;	/*!< factor to convert from floating point time interval to integer timeline */
  integertime Ti_Current;		/*!< current time on integer timeline */
  integertime Previous_Ti_Current;
  integertime Ti_nextoutput;		/*!< next output time on integer timeline */
  integertime Ti_lastoutput;

#ifdef PMGRID
  integertime PM_Ti_endstep, PM_Ti_begstep;
  double Asmth[2], Rcut[2];
  double Corner[2][3], UpperCorner[2][3], Xmintot[2][3], Xmaxtot[2][3];
  double TotalMeshSize[2];
#endif


  integertime Ti_nextlineofsight;
#ifdef OUTPUT_LINEOFSIGHT
  double TimeFirstLineOfSight;
#endif

  int    CPU_TimeBinCountMeasurements[TIMEBINS];
  double CPU_TimeBinMeasurements[TIMEBINS][NUMBER_OF_MEASUREMENTS_TO_RECORD];

  int LevelToTimeBin[GRAVCOSTLEVELS];

  /* variables that keep track of cumulative CPU consumption */

  double TimeLimitCPU;
  double CPU_Sum[CPU_PARTS];    /*!< sums wallclock time/CPU consumption in whole run */

  /* tree code opening criterion */

  double ErrTolTheta;		/*!< BH tree opening angle */
  double ErrTolForceAcc;	/*!< parameter for relative opening criterion in tree walk */


  /* adjusts accuracy of time-integration */

  double ErrTolIntAccuracy;	/*!< accuracy tolerance parameter \f$ \eta \f$ for timestep criterion. The
				   timesteps is \f$ \Delta t = \sqrt{\frac{2 \eta eps}{a}} \f$ */

  double MinSizeTimestep,	/*!< minimum allowed timestep. Normally, the simulation terminates if the
				   timestep determined by the timestep criteria falls below this limit. */
    MaxSizeTimestep;		/*!< maximum allowed timestep */

  double MaxRMSDisplacementFac;	/*!< this determines a global timestep criterion for cosmological simulations
				   in comoving coordinates.  To this end, the code computes the rms velocity
				   of all particles, and limits the timestep such that the rms displacement
				   is a fraction of the mean particle separation (determined from the
				   particle mass and the cosmological parameters). This parameter specifies
				   this fraction. */

  int MaxMemSize;

  double CourantFac;		/*!< SPH-Courant factor */


  /* frequency of tree reconstruction/domain decomposition */


  double TreeDomainUpdateFrequency;	/*!< controls frequency of domain decompositions  */


  /* gravitational and hydrodynamical softening lengths (given in terms of an `equivalent' Plummer softening length)
   *
   * five groups of particles are supported 0=gas,1=halo,2=disk,3=bulge,4=stars
   */
    double MinGasHsmlFractional; /*!< minimim allowed gas kernel length relative to force softening (what you actually set) */
    double MinHsml;			/*!< minimum allowed gas kernel length */
    double MaxHsml;           /*!< minimum allowed gas kernel length */
    
#ifdef TURB_DRIVING
  double RefDensity;
  double RefInternalEnergy;
  double IsoSoundSpeed;
  double TurbInjectedEnergy;
  double TurbDissipatedEnergy;
  double TimeBetTurbSpectrum;
  double TimeNextTurbSpectrum;
  int FileNumberTurbSpectrum;
  double SetLastTime;
#endif

  double SofteningGas,		/*!< for type 0 */
    SofteningHalo,		/*!< for type 1 */
    SofteningDisk,		/*!< for type 2 */
    SofteningBulge,		/*!< for type 3 */
    SofteningStars,		/*!< for type 4 */
    SofteningBndry;		/*!< for type 5 */

  double SofteningGasMaxPhys,	/*!< for type 0 */
    SofteningHaloMaxPhys,	/*!< for type 1 */
    SofteningDiskMaxPhys,	/*!< for type 2 */
    SofteningBulgeMaxPhys,	/*!< for type 3 */
    SofteningStarsMaxPhys,	/*!< for type 4 */
    SofteningBndryMaxPhys;	/*!< for type 5 */

  double SofteningTable[6];	/*!< current (comoving) gravitational softening lengths for each particle type */
  double ForceSoftening[6];	/*!< the same, but multiplied by a factor 2.8 - at that scale the force is Newtonian */


  /*! If particle masses are all equal for one type, the corresponding entry in MassTable is set to this
   *  value, * allowing the size of the snapshot files to be reduced
   */
  double MassTable[6];


  /* some filenames */
  char InitCondFile[100],
    OutputDir[100],
    SnapshotFileBase[100],
    RestartFile[100], ResubmitCommand[100], OutputListFilename[100];
    /* EnergyFile[100], CpuFile[100], InfoFile[100], TimingsFile[100], TimebinFile[100], */

#ifdef COOL_GRACKLE
    char GrackleDataFile[100];
#endif
    
  /*! table with desired output times */
  double OutputListTimes[MAXLEN_OUTPUTLIST];
  char OutputListFlag[MAXLEN_OUTPUTLIST];
  int OutputListLength;		/*!< number of times stored in table of desired output times */



#ifdef RADTRANSFER
  integertime Radiation_Ti_begstep;
  integertime Radiation_Ti_endstep;
#endif
    
#ifdef RT_EVOLVE_INTENSITIES
    double RT_Intensity_Direction[N_RT_INTENSITY_BINS][3];
#endif

#if defined(RT_CHEM_PHOTOION) && !(defined(GALSF_FB_FIRE_RT_HIIHEATING) || defined(GALSF))
    double IonizingLuminosityPerSolarMass_cgs;
    double star_Teff;
#endif
    
    
#ifdef RT_LEBRON
    double PhotonMomentum_Coupled_Fraction;
#ifdef GALSF_FB_FIRE_RT_LONGRANGE
    double PhotonMomentum_fUV;
    double PhotonMomentum_fOPT;
#endif
#ifdef BH_PHOTONMOMENTUM
    double BH_FluxMomentumFactor;
#endif
#endif
    

#ifdef GRAIN_FLUID
#ifdef GRAIN_RDI_TESTPROBLEM
#if(NUMDIMS==3)
#define GRAV_DIRECTION_RDI 2
#else
#define GRAV_DIRECTION_RDI 1
#endif
    double Grain_Charge_Parameter;
    double Dust_to_Gas_Mass_Ratio;
    double Vertical_Gravity_Strength;
    double Vertical_Grain_Accel;
    double Vertical_Grain_Accel_Angle;
#endif
    double Grain_Internal_Density;
    double Grain_Size_Min;
    double Grain_Size_Max;
    double Grain_Size_Spectrum_Powerlaw;
#endif
    
#ifdef COSMIC_RAYS
    double CosmicRayDiffusionCoeff;
#endif
    

    
#ifdef GDE_DISTORTIONTENSOR
  /* present day velocity dispersion of DM particle in cm/s (e.g. Neutralino = 0.03 cm/s) */
  double DM_velocity_dispersion;
  double TidalCorrection;
#ifdef GDE_LEAN
  double GDEInitStreamDensity;
#endif
#endif

#ifdef GALSF		/* star formation and feedback sector */
  double CritOverDensity;
  double CritPhysDensity;
  double OverDensThresh;
  double PhysDensThresh;
  double MaxSfrTimescale;

#ifdef GALSF_EFFECTIVE_EQS
  double EgySpecSN;
  double FactorSN;
  double EgySpecCold;
  double FactorEVP;
  double FeedbackEnergy;
  double TempSupernova;
  double TempClouds;
  double FactorForSofterEQS;
#endif
    
#ifdef GALSF_FB_FIRE_RT_LOCALRP
  double WindMomentumLoading;
#endif
    
#ifdef GALSF_SUBGRID_WINDS
#ifndef GALSF_SUBGRID_WIND_SCALING
#define GALSF_SUBGRID_WIND_SCALING 0 // default to constant-velocity winds //
#endif
  double WindEfficiency;
  double WindEnergyFraction;
  double WindFreeTravelMaxTimeFactor;  /* maximum free travel time in units of the Hubble time at the current simulation redshift */
  double WindFreeTravelDensFac;
#if (GALSF_SUBGRID_WIND_SCALING>0)
  double VariableWindVelFactor;  /* wind velocity in units of the halo escape velocity */
  double VariableWindSpecMomentum;  /* momentum available for wind per unit mass of stars formed, in internal velocity units */
#endif
#endif // GALSF_SUBGRID_WINDS //

#ifdef GALSF_FB_FIRE_STELLAREVOLUTION
    double SNeIIEnergyFrac;
    double GasReturnFraction;
    double AGBGasEnergy;
#ifdef COSMIC_RAYS
    double CosmicRay_SNeFraction;
#endif
#endif

#ifdef GALSF_FB_FIRE_RT_HIIHEATING
  double HIIRegion_fLum_Coupled;
#endif

#endif // GALSF

#if defined(COOL_METAL_LINES_BY_SPECIES) || defined(GALSF_FB_FIRE_RT_LOCALRP) || defined(GALSF_FB_FIRE_RT_HIIHEATING) || defined(GALSF_FB_MECHANICAL) || defined(GALSF_FB_FIRE_RT_LONGRANGE) || defined(GALSF_FB_THERMAL)
  double InitMetallicityinSolar;
  double InitStellarAgeinGyr;
#endif

#if defined(BH_WIND_CONTINUOUS) || defined(BH_WIND_KICK) || defined(BH_WIND_SPAWN)
    double BAL_f_accretion;
    double BAL_v_outflow;
#endif
    
#if defined(BH_COSMIC_RAYS)
    double BH_CosmicRay_Injection_Efficiency;
#endif
    
#ifdef METALS
    double SolarAbundances[NUM_METAL_SPECIES];
#ifdef COOL_METAL_LINES_BY_SPECIES
    int SpeciesTableInUse;
#endif
#endif

    
#ifdef GR_TABULATED_COSMOLOGY
  double DarkEnergyConstantW;	/*!< fixed w for equation of state */
#if defined(GR_TABULATED_COSMOLOGY_W) || defined(GR_TABULATED_COSMOLOGY_G) || defined(GR_TABULATED_COSMOLOGY_H)
#ifndef GR_TABULATED_COSMOLOGY_W
#define GR_TABULATED_COSMOLOGY_W
#endif
  char TabulatedCosmologyFile[100];	/*!< tabulated parameters for expansion and/or gravity */
#ifdef GR_TABULATED_COSMOLOGY_G
  double Gini;
#endif
#endif
#endif

#ifdef RESCALEVINI
  double VelIniScale;		/*!< Scale the initial velocities by this amount */
#endif

#ifdef SPHAV_CD10_VISCOSITY_SWITCH
  double ViscosityAMin;
  double ViscosityAMax;
#endif
#ifdef TURB_DIFFUSION
  double TurbDiffusion_Coefficient;
#endif
  
#if defined(CONDUCTION)
   double ConductionCoeff;	/*!< Thermal Conductivity */
#endif
    
#if defined(VISCOSITY)
   double ShearViscosityCoeff;
   double BulkViscosityCoeff;
#endif

#if defined(CONDUCTION_SPITZER) || defined(VISCOSITY_BRAGINSKII)
    double ElectronFreePathFactor;	/*!< Factor to get electron mean free path */
#endif

    
#ifdef MAGNETIC
#ifdef MHD_B_SET_IN_PARAMS
  double BiniX, BiniY, BiniZ;	/*!< Initial values for B */
#endif
#ifdef SPH_TP12_ARTIFICIAL_RESISTIVITY
  double ArtMagDispConst;	/*!< Sets the parameter \f$\alpha\f$ of the artificial magnetic disipation */
#endif
#ifdef DIVBCLEANING_DEDNER
  double FastestWaveSpeed;
  double FastestWaveDecay;
  double DivBcleanParabolicSigma;
  double DivBcleanHyperbolicSigma;
#endif
#endif /* MAGNETIC */
    
#if defined(BLACK_HOLES) || defined(GALSF_SUBGRID_WINDS)
  double TimeNextOnTheFlyFoF;
  double TimeBetOnTheFlyFoF;
#endif

#ifdef BLACK_HOLES
  double BlackHoleAccretionFactor;	/*!< Fraction of BH bondi accretion rate */
  double BlackHoleFeedbackFactor;	/*!< Fraction of the black luminosity feed into thermal feedback */
  double SeedBlackHoleMass;         /*!< Seed black hole mass */
#if defined(BH_SEED_FROM_FOF) || defined(BH_SEED_FROM_LOCALGAS)
  double SeedBlackHoleMassSigma;    /*!< Standard deviation of init black hole masses */
  double SeedBlackHoleMinRedshift;  /*!< Minimum redshift where BH seeds are allowed */
#ifdef BH_SEED_FROM_LOCALGAS
  double SeedBlackHolePerUnitMass;  /*!< Defines probability per unit mass of seed BH forming */
#endif
#endif
#ifdef BH_ALPHADISK_ACCRETION
  double SeedAlphaDiskMass;         /*!< Seed alpha disk mass */
#endif
#ifdef BH_WIND_SPAWN
  double BAL_wind_particle_mass;        /*!< target mass for feedback particles to be spawned */
  double BAL_internal_temperature;
  MyIDType AGNWindID;
#endif
#ifdef BH_SEED_FROM_FOF
  double MinFoFMassForNewSeed;      /*!< Halo mass required before new seed is put in */
#endif
  double BlackHoleNgbFactor;        /*!< Factor by which the SPH neighbour should be increased/decreased */
  double BlackHoleMaxAccretionRadius;
  double BlackHoleEddingtonFactor;	/*!< Factor above Eddington */
  double BlackHoleRadiativeEfficiency;  /**< Radiative efficiency determined by the spin value, default value is 0.1 */
#ifdef MODIFIEDBONDI
  double BlackHoleRefDensity;
  double BlackHoleRefSoundspeed;
#endif
#endif

#if defined(EOS_TILLOTSON) || defined(EOS_ELASTIC)
  double Tillotson_EOS_params[7][12]; /*! < holds parameters for Tillotson EOS for solids */
#endif

#ifdef EOS_TABULATED
    char EosTable[100];
#endif

#ifdef NUCLEAR_NETWORK
  char NetworkRates[100];
  char NetworkPartFunc[100];
  char NetworkMasses[100];
  char NetworkWeakrates[100];
  struct network_data nd;
  struct network_workspace nw;
  double NetworkTempThreshold;
#endif

#ifdef ADAPTIVE_GRAVSOFT_FORALL
  double AGS_DesNumNgb;
  double AGS_MaxNumNgbDeviation;
#endif
    
#ifdef DM_FUZZY
  double FuzzyDM_Mass_in_eV;
#endif

#ifdef TURB_DRIVING
  double StDecay;
  double StEnergy;
  double StDtFreq;
  double StKmin;
  double StKmax;
  double StSolWeight;
  double StAmplFac;

  int StSpectForm;
  int StSeed;
#endif

#if defined(COOLING) && defined(COOL_GRACKLE)
    code_units GrackleUnits;
#endif
}
All;



/*! This structure holds all the information that is
 * stored for each particle of the simulation.
 */
extern ALIGN(32) struct particle_data
{
    short int Type;                 /*!< flags particle type.  0=gas, 1=halo, 2=disk, 3=bulge, 4=stars, 5=bndry */
    short int TimeBin;
    MyIDType ID;                    /*! < unique ID of particle (assigned at beginning of the simulation) */
    MyIDType ID_child_number;       /*! < child number for particles 'split' from main (retain ID, get new child number) */
    int ID_generation;              /*! < generation (need to track for particle-splitting to ensure each 'child' gets a unique child number */
    
    integertime Ti_begstep;         /*!< marks start of current timestep of particle on integer timeline */
    integertime Ti_current;         /*!< current time of the particle */
    
    ALIGN(32) MyDouble Pos[3];      /*!< particle position at its current time */
    MyDouble Mass;                  /*!< particle mass */
    
    MyDouble Vel[3];                /*!< particle velocity at its current time */
    MyDouble dp[3];
    MyFloat Particle_DivVel;        /*!< velocity divergence of neighbors (for predict step) */
    
    MyDouble GravAccel[3];          /*!< particle acceleration due to gravity */
#ifdef PMGRID
    MyFloat GravPM[3];		/*!< particle acceleration due to long-range PM gravity force */
#endif
    MyFloat OldAcc;			/*!< magnitude of old gravitational force. Used in relative opening criterion */
#if defined(EVALPOTENTIAL) || defined(COMPUTE_POTENTIAL_ENERGY) || defined(OUTPUT_POTENTIAL)
    MyFloat Potential;		/*!< gravitational potential */
#if defined(EVALPOTENTIAL) && defined(PMGRID)
    MyFloat PM_Potential;
#endif
#endif
#if defined(SINGLE_STAR_HILL_CRITERION) || defined(TIDAL_TIMESTEP_CRITERION) || defined(GDE_DISTORTIONTENSOR)
#define COMPUTE_TIDAL_TENSOR_IN_GRAVTREE
    double tidal_tensorps[3][3];                        /*!< tidal tensor (=second derivatives of grav. potential) */
#endif
#ifdef GDE_DISTORTIONTENSOR
    MyBigFloat distortion_tensorps[6][6];               /*!< phase space distortion tensor */
    MyBigFloat last_determinant;                        /*!< last real space distortion tensor determinant */
    MyBigFloat stream_density;                          /*!< physical stream density that is going to be integrated */
    float caustic_counter;                              /*!< caustic counter */
#ifndef GDE_LEAN
    MyBigFloat annihilation;                            /*!< integrated annihilation rate */
    MyBigFloat analytic_annihilation;                   /*!< analytically integrated annihilation rate */
    MyBigFloat rho_normed_cutoff_current;               /*!< current and last normed_cutoff density in rho_max/rho_init * sqrt(sigma) */
    MyBigFloat rho_normed_cutoff_last;
    MyBigFloat s_1_current, s_2_current, s_3_current;   /*! < current and last stretching factor */
    MyBigFloat s_1_last, s_2_last, s_3_last;
    MyBigFloat second_deriv_current;                    /*! < current and last second derivative */
    MyBigFloat second_deriv_last;
    double V_matrix[3][3];                              /*!< initial orientation of CDM sheet the particle is embedded in */
    float init_density;                                 /*!< initial stream density */
    float analytic_caustics;                            /*!< number of caustics that were integrated analytically */
    float a0;
#endif
#ifdef OUTPUT_GDE_LASTCAUSTIC
    MyFloat lc_Time;                                  /*!< time of caustic passage */
    MyFloat lc_Pos[3];                                /*!< position of caustic */
    MyFloat lc_Vel[3];                                /*!< particle velocity when passing through caustic */
    MyFloat lc_rho_normed_cutoff;                     /*!< normed_cutoff density at caustic */
    MyFloat lc_Dir_x[3];                              /*!< principal axis frame of smear out */
    MyFloat lc_Dir_y[3];
    MyFloat lc_Dir_z[3];
    MyFloat lc_smear_x;                               /*!< smear out length */
    MyFloat lc_smear_y;
    MyFloat lc_smear_z;
#endif
#ifdef PMGRID
    double tidal_tensorpsPM[3][3];	            /*!< for TreePM simulations, long range tidal field */
#endif
#endif // GDE_DISTORTIONTENSOR // 
    
    
#ifdef GALSF
    MyFloat StellarAge;		/*!< formation time of star particle */
#endif
#ifdef METALS
    MyFloat Metallicity[NUM_METAL_SPECIES]; /*!< metallicity (species-by-species) of gas or star particle */
#endif
#ifdef GALSF_SFR_IMF_VARIATION
    MyFloat IMF_Mturnover; /*!< IMF turnover mass [in solar] (or any other parameter which conveniently describes the IMF) */
    MyFloat IMF_FormProps[N_IMF_FORMPROPS]; /*!< formation properties of star particles to record for output */
#endif
#ifdef GALSF_SFR_IMF_SAMPLING
    MyFloat IMF_NumMassiveStars; /*!< number of massive stars to associate with this star particle (for feedback) */
#endif
    
    MyFloat Hsml;                   /*!< search radius around particle for neighbors/interactions */
    MyFloat NumNgb;                 /*!< neighbor number around particle */
    MyFloat DhsmlNgbFactor;        /*!< correction factor needed for varying kernel lengths */
#ifdef DO_DENSITY_AROUND_STAR_PARTICLES
    MyFloat DensAroundStar;         /*!< gas density in the neighborhood of the collisionless particle (evaluated from neighbors) */
    MyFloat GradRho[3];             /*!< gas density gradient evaluated simply from the neighboring particles, for collisionless centers */
#endif
#if defined(RT_SOURCE_INJECTION)
    MyFloat KernelSum_Around_RT_Source; /*!< kernel summation around sources for radiation injection (save so can be different from 'density') */
#endif

#if defined(GALSF_FB_MECHANICAL) || defined(GALSF_FB_THERMAL)
    MyFloat SNe_ThisTimeStep; /* flag that indicated number of SNe for the particle in the timestep */
#ifdef GALSF_FB_FIRE_STELLAREVOLUTION
    MyFloat MassReturn_ThisTimeStep; /* gas return from stellar winds */
    MyFloat RProcessEvent_ThisTimeStep; /* R-process event tracker */
#endif
#endif
#ifdef GALSF_FB_MECHANICAL
#define AREA_WEIGHTED_SUM_ELEMENTS 11 /* number of weights needed for full momentum-and-energy conserving system */
    MyFloat Area_weighted_sum[AREA_WEIGHTED_SUM_ELEMENTS]; /* normalized weights for particles in kernel weighted by area, not mass */
#endif
    
#if defined(GRAIN_FLUID)
    MyFloat Grain_Size;
    MyFloat Gas_Density;
    MyFloat Gas_InternalEnergy;
    MyFloat Gas_Velocity[3];
#ifdef GRAIN_LORENTZFORCE
    MyFloat Gas_B[3];
#endif
#ifdef GRAIN_COLLISIONS
    MyFloat Grain_Density;
    MyFloat Grain_Velocity[3];
#endif
#endif
    
#if defined(BLACK_HOLES)
    MyIDType SwallowID;
    int IndexMapToTempStruc;   /*!< allows for mapping to BlackholeTempInfo struc */
#ifdef BH_WIND_SPAWN
    MyFloat unspawned_wind_mass;    /*!< tabulates the wind mass which has not yet been spawned */
#endif
#ifdef BH_COUNTPROGS
    int BH_CountProgs;
#endif
    MyFloat BH_Mass;
#ifdef SINGLE_STAR_STRICT_ACCRETION
    MyFloat SinkRadius;
#endif
#ifdef SINGLE_STAR_FORMATION  
    MyFloat SwallowEnergy; /* specific energy of a particle w.r.t. a sink particle  */
#endif 
#ifdef BH_ALPHADISK_ACCRETION
    MyFloat BH_Mass_AlphaDisk;
#endif
#ifdef BH_WAKEUP_GAS /* force all gas within the interaction radius of a sink to timestep at the same rate */
    int LowestBHTimeBin;
#endif
#ifdef NEWSINK    
    MyFloat init_mass_in_intzone; /*initial mass in interaction zone, used for scaling mdot*/
    MyFloat BH_Mdot_Avg; /*Mdot averaged over MDOT_AVG_WINDOWS_SIZE timesteps*/
#if !defined(BH_ALPHADISK_ACCRETION)
    MyFloat Mdotvals[MDOT_AVG_WINDOWS_SIZE]; /*value of mdot for the last MDOT_AVG_WINDOWS_SIZE timesteps*/
    MyFloat dtvals[MDOT_AVG_WINDOWS_SIZE]; /*value of particle dt for the last MDOT_AVG_WINDOWS_SIZE timesteps*/
    MyFloat BH_Mdot_Avg_tdyn; /*Mdot averaged over the dynamical time of the sink, last timestep taken into account is MDOT_AVG_WINDOWS_SIZE timesteps before the current time */
#else
    MyFloat BH_Mdot_AlphaDisk; /*Mdot for the alpha disk*/
#endif
#endif
#ifdef NEWSINK_J_FEEDBACK
    MyFloat Jsink[3];
    MyFloat t_disc;
#endif
    MyFloat BH_Mdot;
    int BH_TimeBinGasNeighbor;
#ifdef SINGLE_STAR_FORMATION
    MyFloat BH_NearestGasNeighbor;
#endif
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
    MyFloat BH_disk_hr;
#endif
#ifdef BH_REPOSITION_ON_POTMIN
    MyFloat BH_MinPotPos[3];
    MyFloat BH_MinPot;
#endif
#endif  /* if defined(BLACK_HOLES) */
    
#ifdef BH_CALC_DISTANCES
    MyFloat min_dist_to_bh;
    MyFloat min_xyz_to_bh[3];
#ifdef SINGLE_STAR_TIMESTEPPING
    MyFloat min_bh_freefall_time;
    MyFloat min_bh_periastron;
    MyFloat min_bh_approach_time;
#ifdef SINGLE_STAR_SUPERTIMESTEPPING
    int SuperTimestepFlag; // 2 if allowed to super-timestep, 1 if a candidate for super-timestepping, 0 otherwise
    MyFloat min_bh_t_orbital; //orbital time for binary
    MyDouble comp_dx[3]; //position offset of binary companion - this will be evolved in the Kepler solution while we use the Pos attribute to track the binary COM
    MyDouble comp_dv[3]; //velocity offset of binary companion - this will be evolved in the Kepler solution while we use the Vel attribute to track the binary COM velocity
    MyDouble comp_Mass; //mass of binary companion
    //MyIDType comp_ID; //ID of binary companion
    MyLongDouble COM_tidal_tensorps[3][3]; //tidal tensor evaluated at the center of mass without contribution from the companion
    MyLongDouble COM_GravAccel[3]; //gravitational acceleration evaluated at the center of mass without contribution from the companion
#endif
#endif  
#endif

#if defined(SINGLE_STAR_PROMOTION) || defined(SINGLE_STAR_FB_HEATING) || defined(SINGLE_STAR_FB_JETS)
#define SINGLE_STAR_PROTOSTELLAR_EVOLUTION
#endif    
#ifdef SINGLE_STAR_PROTOSTELLAR_EVOLUTION    
    MyFloat ProtoStellarAge; /*!< record the proto-stellar age instead of age */
    //MyFloat PreMainSeq_Tracker; /*!< track evolution from protostar to ZAMS star */
    MyFloat ProtoStellar_Radius; /*!< protostellar radius (also tracks evolution from protostar to ZAMS star) */
#endif
    
#ifdef DM_SIDM
    int dt_step_sidm; /*!< timestep used if self-interaction probabilities greater than 0.2 are found */
    long unsigned int NInteractions; /*!< Total number of interactions */
#endif

#if defined(SUBFIND)
    int GrNr;
    int SubNr;
    int DM_NumNgb;
    unsigned short targettask, origintask2;
    int origintask, submark, origindex;
    MyFloat DM_Hsml;
    union
    {
        MyFloat DM_Density;
        MyFloat DM_Potential;
    } u;
    union
    {
        MyFloat DM_VelDisp;
        MyFloat DM_BindingEnergy;
    } v;
#ifdef FOF_DENSITY_SPLIT_TYPES
    union
    {
        MyFloat int_energy;
        MyFloat density_sum;
    } w;
#endif
#ifdef SAVE_HSML_IN_IC_ORDER
    MyIDType ID_ic_order;
#endif
#ifdef SUBFIND_ALTERNATIVE_COLLECTIVE
    peanokey Key;
#endif
#endif
    
    float GravCost[GRAVCOSTLEVELS];   /*!< weight factor used for balancing the work-load */
    
#ifdef WAKEUP
    int dt_step;
#endif
    
#ifdef SCF_HYBRID
    MyDouble GravAccelSum[3];
    MyFloat MassBackup;
#endif
    
#ifdef ADAPTIVE_GRAVSOFT_FORALL
    MyDouble AGS_Hsml;          /*!< smoothing length (for gravitational forces) */
    MyFloat AGS_zeta;           /*!< factor in the correction term */
    MyDouble AGS_vsig;          /*!< signal velocity of particle approach, to properly time-step */
#if defined(WAKEUP)
    short int wakeup;                     /*!< flag to wake up particle */
#endif
#endif
    
#ifdef DM_FUZZY
    MyFloat AGS_Density;                /*!< density calculated corresponding to AGS routine (over interacting DM neighbors) */
    MyFloat AGS_Gradients_Density[3];   /*!< density gradient calculated corresponding to AGS routine (over interacting DM neighbors) */
    MyFloat AGS_Gradients2_Density[3][3];   /*!< density gradient calculated corresponding to AGS routine (over interacting DM neighbors) */
    MyFloat AGS_Numerical_QuantumPotential; /*!< additional potential terms 'generated' by un-resolved compression [numerical diffusivity] */
    MyFloat AGS_Dt_Numerical_QuantumPotential; /*!< time derivative of the above */
#endif
#if defined(AGS_FACE_CALCULATION_IS_ACTIVE)
    MyFloat NV_T[3][3];                                           /*!< holds the tensor used for gradient estimation */
#endif
#ifdef CBE_INTEGRATOR
    double CBE_basis_moments[CBE_INTEGRATOR_NBASIS][CBE_INTEGRATOR_NMOMENTS];         /* moments per basis function */
    double CBE_basis_moments_dt[CBE_INTEGRATOR_NBASIS][CBE_INTEGRATOR_NMOMENTS];      /* time-derivative of moments per basis function */
#endif
}
 *P,				/*!< holds particle data on local processor */
 *DomainPartBuf;		/*!< buffer for particle data used in domain decomposition */


#ifndef GDE_LEAN
#define GDE_TIMEBEGIN(i) (P[i].a0)
#define GDE_VMATRIX(i, a, b) (P[i].V_matrix[a][b])
#define GDE_INITDENSITY(i) (P[i].init_density)
#else
#define GDE_TIMEBEGIN(i) (All.TimeBegin)
#define GDE_VMATRIX(i, a, b) (0.0)
#define GDE_INITDENSITY(i) (All.GDEInitStreamDensity)
#endif

#if defined(BLACK_HOLES)
#define BPP(i) P[(i)]
#endif


/* the following struture holds data that is stored for each SPH particle in addition to the collisionless
 * variables.
 */
extern struct sph_particle_data
{
    /* the PRIMITIVE and CONSERVED hydro variables used in STATE reconstruction */
    MyDouble Density;               /*!< current baryonic mass density of particle */
#ifdef HYDRO_MESHLESS_FINITE_VOLUME
    MyDouble MassTrue;              /*!< true particle mass ('mass' now is -predicted- mass */
    MyDouble dMass;                 /*!< change in particle masses from hydro step (conserved variable) */
    MyDouble DtMass;                /*!< rate-of-change of particle masses (for drifting) */
    MyDouble GravWorkTerm[3];       /*!< correction term needed for hydro mass flux in gravity */
    MyDouble ParticleVel[3];        /*!< actual velocity of the mesh-generating points */
#endif

    MyDouble Pressure;              /*!< current pressure */
    MyDouble InternalEnergy;        /*!< internal energy of particle */
    MyDouble InternalEnergyPred;    /*!< predicted value of the internal energy at the current time */
    //MyDouble dInternalEnergy;     /*!< change in internal energy from hydro step */ //manifest-indiv-timestep-debug//
    MyDouble DtInternalEnergy;      /*!< rate of change of internal energy */

    MyDouble VelPred[3];            /*!< predicted SPH particle velocity at the current time */
    //MyDouble dMomentum[3];        /*!< change in momentum from hydro step (conserved variable) */ //manifest-indiv-timestep-debug//
    MyDouble HydroAccel[3];         /*!< acceleration due to hydrodynamical force (for drifting) */
    
#ifdef MAGNETIC
    MyDouble Face_Area[3];          /*!< vector sum of effective areas of 'faces'; this is used to check closure for meshless methods */
    MyDouble BPred[3];              /*!< current magnetic field strength */
    MyDouble B[3];                  /*!< actual B (conserved variable used for integration; can be B*V for flux schemes) */
    MyDouble DtB[3];                /*!< time derivative of B-field (of -conserved- B-field) */
    MyFloat divB;                   /*!< storage for the 'effective' divB used in div-cleaning procedure */
#ifdef DIVBCLEANING_DEDNER
    MyDouble DtB_PhiCorr[3];        /*!< correction forces for mid-face update to phi-field */
    MyDouble PhiPred;               /*!< current value of Phi */
    MyDouble Phi;                   /*!< scalar field for Dedner divergence cleaning */
    MyDouble DtPhi;                 /*!< time derivative of Phi-field */
#endif
#ifdef MHD_CONSTRAINED_GRADIENT
    int FlagForConstrainedGradients;/*!< flag indicating whether the B-field gradient is a 'standard' one or the constrained-divB version */
#endif
#if defined(SPH_TP12_ARTIFICIAL_RESISTIVITY)
    MyFloat Balpha;                 /*!< effective resistivity coefficient */
#endif
#endif /* MAGNETIC */

#if defined(KERNEL_CRK_FACES)
    MyFloat Tensor_CRK_Face_Corrections[16]; /*!< tensor set for face-area correction terms for the CRK formulation of SPH or MFM/V areas */
#endif

#ifdef COSMIC_RAYS
    MyFloat CosmicRayEnergy;        /*!< total energy of cosmic ray fluid (the conserved variable) */
    MyFloat CosmicRayEnergyPred;    /*!< total energy of cosmic ray fluid (the conserved variable) */
    MyFloat DtCosmicRayEnergy;      /*!< time derivative of cosmic ray energy */
    MyFloat CosmicRayDiffusionCoeff;/*!< diffusion coefficient kappa for cosmic ray fluid */
#ifdef COSMIC_RAYS_M1
    MyFloat CosmicRayFlux[3];       /*!< CR flux vector [explicitly evolved] - conserved-variable */
    MyFloat CosmicRayFluxPred[3];   /*!< CR flux vector [explicitly evolved] - conserved-variable */
#endif
#ifdef COSMIC_RAYS_ALFVEN
    MyFloat CosmicRayAlfvenEnergy[2];       /*!< forward and backward-traveling Alfven wave-packet energies */
    MyFloat CosmicRayAlfvenEnergyPred[2];   /*!< drifted forward and backward-traveling Alfven wave-packet energies */
    MyFloat DtCosmicRayAlfvenEnergy[2];     /*!< time derivative fof forward and backward-traveling Alfven wave-packet energies */
#endif
#endif
    
#ifdef SUPER_TIMESTEP_DIFFUSION
    MyDouble Super_Timestep_Dt_Explicit; /*!< records the explicit step being used to scale the sub-steps for the super-stepping */
    int Super_Timestep_j; /*!< records which sub-step if the super-stepping cycle the particle is in [needed for adaptive steps] */
#endif
    
#ifdef SINGLE_STAR_FORMATION
    MyFloat Density_Relative_Maximum_in_Kernel; /*!< hold density_max-density_i, for particle i, so we know if its a local maximum */
#endif
    
    /* matrix of the primitive variable gradients: rho, P, vx, vy, vz, B, phi */
    struct
    {
        MyDouble Density[3];
        MyDouble Pressure[3];
#ifdef SINGLE_STAR_FORMATION
        MyDouble PressureMagnitude;
#endif      
        MyDouble Velocity[3][3];
#ifdef MAGNETIC
        MyDouble B[3][3];
#ifdef DIVBCLEANING_DEDNER
        MyDouble Phi[3];
#endif
#endif
#ifdef DOGRAD_SOUNDSPEED
        MyDouble SoundSpeed[3];
#endif
#ifdef DOGRAD_INTERNAL_ENERGY
        MyDouble InternalEnergy[3];
#endif
#if defined(TURB_DIFF_METALS) && !defined(TURB_DIFF_METALS_LOWORDER)
        MyDouble Metallicity[NUM_METAL_SPECIES][3];
#endif
#ifdef COSMIC_RAYS
        MyDouble CosmicRayPressure[3];
#endif
#ifdef RT_EVOLVE_EDDINGTON_TENSOR
        MyDouble E_gamma_ET[N_RT_FREQ_BINS][3];
#endif
    } Gradients;
    MyFloat NV_T[3][3];             /*!< holds the tensor used for gradient estimation */
    MyDouble ConditionNumber;       /*!< condition number of the gradient matrix: needed to ensure stability */
#ifdef ENERGY_ENTROPY_SWITCH_IS_ACTIVE
    MyDouble MaxKineticEnergyNgb;   /*!< maximum kinetic energy (with respect to neighbors): use for entropy 'switch' */
#endif

    
#ifdef HYDRO_SPH
    MyDouble DhsmlHydroSumFactor;   /* for 'traditional' SPH, we need the SPH hydro-element volume estimator */
#endif
    
#ifdef HYDRO_PRESSURE_SPH
    MyDouble EgyWtDensity;          /*!< 'effective' rho to use in hydro equations */
#endif
    
#if defined(ADAPTIVE_GRAVSOFT_FORGAS) && !defined(ADAPTIVE_GRAVSOFT_FORALL)
    MyFloat AGS_zeta;               /*!< correction term for adaptive gravitational softening lengths */
#endif
    
    MyFloat MaxSignalVel;           /*!< maximum signal velocity (needed for time-stepping) */
    
#ifdef GALSF_FB_FIRE_RT_UVHEATING
    MyFloat RadFluxUV;              /*!< local UV field strength */
    MyFloat RadFluxEUV;             /*!< local (ionizing/hard) UV field strength */
#endif
#ifdef BH_COMPTON_HEATING
    MyFloat RadFluxAGN;             /*!< local AGN flux */
#endif
    
#if defined(TURB_DRIVING) || defined(OUTPUT_VORTICITY)
   MyFloat Vorticity[3];
   MyFloat SmoothedVel[3];
#endif

#if defined(BH_THERMALFEEDBACK)
    MyDouble Injected_BH_Energy;
#endif

#ifdef COOLING
  MyFloat Ne;  /*!< electron fraction, expressed as local electron number
		    density normalized to the hydrogen number density. Gives
		    indirectly ionization state and mean molecular weight. */
#endif
#ifdef GALSF
  MyFloat Sfr;                      /*!< particle star formation rate */
#if (GALSF_SFR_VIRIAL_SF_CRITERION>=3)
  MyFloat AlphaVirial_SF_TimeSmoothed;  /*!< dimensionless number > 0.5 if self-gravitating for smoothed virial criterion */
#endif
#endif
#ifdef GALSF_SUBGRID_WINDS
  MyFloat DelayTime;                /*!< remaining maximum decoupling time of wind particle */
#if (GALSF_SUBGRID_WIND_SCALING==1)
  MyFloat HostHaloMass;             /*!< host halo mass estimator for wind launching velocity */
#endif
#if (GALSF_SUBGRID_WIND_SCALING==2)
  MyFloat HsmlDM;                   /*!< smoothing length to find neighboring dark matter particles */
  MyDouble NumNgbDM;                /*!< number of neighbor dark matter particles */
  MyDouble DM_Vx, DM_Vy, DM_Vz, DM_VelDisp; /*!< surrounding DM velocity and velocity dispersion */
#endif
#endif
    
#ifdef GALSF_FB_FIRE_RT_HIIHEATING
  MyFloat DelayTimeHII;             /*!< flag indicating particle is ionized by nearby star */
#endif
#ifdef GALSF_FB_TURNOFF_COOLING
  MyFloat DelayTimeCoolingSNe;      /*!< flag indicating cooling is suppressed b/c heated by SNe */
#endif
    
#ifdef TURB_DRIVING
  MyDouble DuDt_diss;               /*!< quantities specific to turbulent driving routines */
  MyDouble DuDt_drive;
  MyDouble EgyDiss;
  MyDouble EgyDrive;
  MyDouble TurbAccel[3];
#endif

#ifdef TURB_DIFFUSION
  MyFloat TD_DiffCoeff;             /*!< effective diffusion coefficient for sub-grid turbulent diffusion */
#endif
#if defined(SPHAV_CD10_VISCOSITY_SWITCH)
  MyFloat NV_DivVel;                /*!< quantities specific to the Cullen & Dehnen viscosity switch */
  MyFloat NV_dt_DivVel;
  MyFloat NV_A[3][3];
  MyFloat NV_D[3][3];
  MyFloat NV_trSSt;
  MyFloat alpha;
#endif

#ifdef HYDRO_SPH
  MyFloat alpha_limiter;                /*!< artificial viscosity limiter (Balsara-like) */
#endif

#ifdef CONDUCTION
    MyFloat Kappa_Conduction;           /*!< conduction coefficient */
#endif

    
#ifdef MHD_NON_IDEAL
    MyFloat Eta_MHD_OhmicResistivity_Coeff;     /*!< Ohmic resistivity coefficient [physical units of L^2/t] */
    MyFloat Eta_MHD_HallEffect_Coeff;           /*!< Hall effect coefficient [physical units of L^2/t] */
    MyFloat Eta_MHD_AmbiPolarDiffusion_Coeff;   /*!< Hall effect coefficient [physical units of L^2/t] */
#endif
    
    
#if defined(VISCOSITY)
    MyFloat Eta_ShearViscosity;         /*!< shear viscosity coefficient */
    MyFloat Zeta_BulkViscosity;         /*!< bulk viscosity coefficient */
#endif

    
#if defined(RADTRANSFER)
    MyFloat ET[N_RT_FREQ_BINS][6];      /*!< eddington tensor - symmetric -> only 6 elements needed: this is dimensionless by our definition */
    MyFloat Je[N_RT_FREQ_BINS];         /*!< emissivity (includes sources like stars, as well as gas): units=E_gamma/time  */
    MyFloat E_gamma[N_RT_FREQ_BINS];    /*!< photon energy (integral of dE_gamma/dvol*dVol) associated with particle [for simple frequency bins, equivalent to photon number] */
    MyFloat Kappa_RT[N_RT_FREQ_BINS];   /*!< opacity [physical units ~ length^2 / mass]  */
    MyFloat Lambda_FluxLim[N_RT_FREQ_BINS]; /*!< dimensionless flux-limiter (0<lambda<1) */
#ifdef RT_EVOLVE_INTENSITIES
    MyFloat Intensity[N_RT_FREQ_BINS][N_RT_INTENSITY_BINS]; /*!< intensity values along different directions, for each frequency */
    MyFloat Intensity_Pred[N_RT_FREQ_BINS][N_RT_INTENSITY_BINS]; /*!< predicted [drifted] values of intensities */
    MyFloat Dt_Intensity[N_RT_FREQ_BINS][N_RT_INTENSITY_BINS]; /*!< time derivative of intensities */
#endif
#ifdef RT_EVOLVE_FLUX
    MyFloat Flux[N_RT_FREQ_BINS][3];    /*!< photon energy flux density (energy/time/area), for methods which track this explicitly (e.g. M1) */
    MyFloat Flux_Pred[N_RT_FREQ_BINS][3];/*!< predicted photon energy flux density for drift operations (needed for adaptive timestepping) */
    MyFloat Dt_Flux[N_RT_FREQ_BINS][3]; /*!< time derivative of photon energy flux density */
#endif
#ifdef RT_EVOLVE_NGAMMA
    MyFloat E_gamma_Pred[N_RT_FREQ_BINS]; /*!< predicted E_gamma for drift operations (needed for adaptive timestepping) */
    MyFloat Dt_E_gamma[N_RT_FREQ_BINS]; /*!< time derivative of photon number in particle (used only with explicit solvers) */
#endif
#ifdef RT_RAD_PRESSURE_OUTPUT
    MyFloat RadAccel[3];
#endif
#ifdef RT_INFRARED
    MyFloat Radiation_Temperature; /* IR radiation field temperature (evolved variable ^4 power, for convenience) */
    MyFloat Dt_E_gamma_T_weighted_IR; /* IR radiation temperature-weighted time derivative of photon energy (evolved variable ^4 power, for convenience) */
    MyFloat Dust_Temperature; /* Dust temperature (evolved variable ^4 power, for convenience) */
#endif
#ifdef RT_CHEM_PHOTOION
    MyFloat HI;                  /* HI fraction */
    MyFloat HII;                 /* HII fraction */
#ifndef COOLING
    MyFloat Ne;               /* electron fraction */
#endif
#ifdef RT_CHEM_PHOTOION_HE
    MyFloat HeI;                 /* HeI fraction */
    MyFloat HeII;                 /* HeII fraction */
    MyFloat HeIII;                 /* HeIII fraction */
#endif
#endif
#endif
    
    
    
    
#ifdef EOS_GENERAL
    MyFloat SoundSpeed;                   /* Sound speed */
#ifdef EOS_CARRIES_TEMPERATURE
    MyFloat Temperature;                  /* Temperature */
#endif
#ifdef EOS_CARRIES_YE
    MyFloat Ye;                           /* Electron fraction */
#endif
#ifdef EOS_CARRIES_ABAR
    MyFloat Abar;                         /* Average atomic weight (in atomic mass units) */
#endif
#if defined(EOS_TILLOTSON) || defined(EOS_ELASTIC)
    int CompositionType;                  /* define the composition of the material */
#endif
#ifdef EOS_ELASTIC
    MyDouble Elastic_Stress_Tensor[3][3]; /* deviatoric stress tensor */
    MyDouble Elastic_Stress_Tensor_Pred[3][3];
    MyDouble Dt_Elastic_Stress_Tensor[3][3];
#endif
#endif
    
    
#if defined(WAKEUP) && !defined(ADAPTIVE_GRAVSOFT_FORALL)
    short int wakeup;                     /*!< flag to wake up particle */
#endif
    
#if defined(COOLING) && defined(COOL_GRACKLE)
#if (COOL_GRACKLE_CHEMISTRY >= 1)
    gr_float grHI;
    gr_float grHII;
    gr_float grHM;
    gr_float grHeI;
    gr_float grHeII;
    gr_float grHeIII;
#endif
#if (COOL_GRACKLE_CHEMISTRY >= 2)
    gr_float grH2I;
    gr_float grH2II;
#endif
#if (COOL_GRACKLE_CHEMISTRY >= 3)
    gr_float grDI;
    gr_float grDII;
    gr_float grHDI;
#endif
#endif
    
}
  *SphP,				/*!< holds SPH particle data on local processor */
  *DomainSphBuf;			/*!< buffer for SPH particle data in domain decomposition */


extern peanokey *DomainKeyBuf;

/* global state of system
*/
extern struct state_of_system
{
  double Mass,
    EnergyKin,
    EnergyPot,
    EnergyInt,
    EnergyTot,
    Momentum[4],
    AngMomentum[4],
    CenterOfMass[4],
    MassComp[6],
    EnergyKinComp[6],
    EnergyPotComp[6],
    EnergyIntComp[6],
    EnergyTotComp[6],
    MomentumComp[6][4],
    AngMomentumComp[6][4],
    CenterOfMassComp[6][4];
}
SysState, SysStateAtStart, SysStateAtEnd;


/* Various structures for communication during the gravity computation.
 */

extern struct data_index
{
  int Task;
  int Index;
  int IndexGet;
}
 *DataIndexTable;		/*!< the particles to be exported are grouped
                                  by task-number. This table allows the
                                  results to be disentangled again and to be
                                  assigned to the correct particle */

extern struct data_nodelist
{
  int NodeList[NODELISTLENGTH];
}
*DataNodeList;

extern struct gravdata_in
{
    MyFloat Pos[3];
#if defined(RT_USE_GRAVTREE) || defined(ADAPTIVE_GRAVSOFT_FORALL) || defined(ADAPTIVE_GRAVSOFT_FORGAS)
    MyFloat Mass;
#endif
#ifdef SINGLE_STAR_TIMESTEPPING
    MyFloat Vel[3];
#endif  
    int Type;
#if defined(RT_USE_GRAVTREE) || defined(ADAPTIVE_GRAVSOFT_FORALL) || defined(ADAPTIVE_GRAVSOFT_FORGAS)
    MyFloat Soft;
#if defined(ADAPTIVE_GRAVSOFT_FORGAS) || defined(ADAPTIVE_GRAVSOFT_FORALL)
    MyFloat AGS_zeta;
#endif
#endif
#ifdef SINGLE_STAR_SUPERTIMESTEPPING
    MyFloat min_bh_t_orbital; //orbital time for binary
    MyDouble comp_dx[3]; //position of binary companion
    MyDouble comp_dv[3]; //velocity of binary companion
    MyDouble comp_Mass; //mass of binary companion
    int SuperTimestepFlag; // 2 if allowed to super-timestep, 1 if a candidate for super-timestepping, 0 otherwise
#endif
    MyFloat OldAcc;
    int NodeList[NODELISTLENGTH];
}
 *GravDataIn,			/*!< holds particle data to be exported to other processors */
 *GravDataGet;			/*!< holds particle data imported from other processors */


extern struct gravdata_out
{
    MyLongDouble Acc[3];
#ifdef RT_OTVET
    MyLongDouble ET[N_RT_FREQ_BINS][6];
#endif
#ifdef GALSF_FB_FIRE_RT_UVHEATING
    MyLongDouble RadFluxUV;
    MyLongDouble RadFluxEUV;
#endif
#ifdef BH_COMPTON_HEATING
    MyLongDouble RadFluxAGN;
#endif
#ifdef EVALPOTENTIAL
    MyLongDouble Potential;
#endif
#ifdef COMPUTE_TIDAL_TENSOR_IN_GRAVTREE
    MyLongDouble tidal_tensorps[3][3];
#endif
#ifdef BH_CALC_DISTANCES
    MyFloat min_dist_to_bh;
    MyFloat min_xyz_to_bh[3];
#ifdef SINGLE_STAR_TIMESTEPPING
    MyFloat min_bh_freefall_time;    // minimum value of sqrt(R^3 / G(M_BH + M_particle)) as calculated from the tree-walk
    MyFloat min_bh_approach_time; // smallest approach time t_a = |v_radial|/r
    MyFloat min_bh_periastron; // closest anticipated periastron passage
#ifdef SINGLE_STAR_SUPERTIMESTEPPING
    MyFloat min_bh_t_orbital; //orbital time for binary
    MyDouble comp_dx[3]; //position of binary companion
    MyDouble comp_dv[3]; //velocity of binary companion
    MyDouble comp_Mass; //mass of binary companion
    MyLongDouble COM_tidal_tensorps[3][3]; //tidal tensor evaluated at the center of mass without contribution from the companion
    MyLongDouble COM_GravAccel[3]; //gravitational acceleration evaluated at the center of mass without contribution from the companion
    int COM_calc_flag; //flag that tells whether this was only a rerun to get the acceleration ad the tidal tenor at the center of mass of a binary
    int SuperTimestepFlag; // 2 if allowed to super-timestep, 1 if a candidate for super-timestepping, 0 otherwise
#endif
#endif  
#endif
}
 *GravDataResult,		/*!< holds the partial results computed for imported particles. Note: We use GravDataResult = GravDataGet, such that the result replaces the imported data */
 *GravDataOut;			/*!< holds partial results received from other processors. This will overwrite the GravDataIn array */


extern struct potdata_out
{
  MyLongDouble Potential;
}
 *PotDataResult,		/*!< holds the partial results computed for imported particles. Note: We use GravDataResult = GravDataGet, such that the result replaces the imported data */
 *PotDataOut;			/*!< holds partial results received from other processors. This will overwrite the GravDataIn array */


extern struct info_block
{
  char label[4];
  char type[8];
  int ndim;
  int is_present[6];
}
*InfoBlock;


#ifdef BLACK_HOLES
#define BHPOTVALUEINIT 1.0e30

extern int N_active_loc_BHs;    /*!< number of active black holes on the LOCAL processor */


extern struct blackhole_temp_particle_data       // blackholedata_topass
{
    MyIDType index;
    MyFloat BH_InternalEnergy;
    MyLongDouble accreted_Mass;
    MyLongDouble accreted_BH_Mass;
    MyLongDouble accreted_momentum[3];
    MyLongDouble Mgas_in_Kernel;                 // mass/angular momentum for GAS/STAR/TOTAL components computed always now
    MyLongDouble Mstar_in_Kernel;
    MyLongDouble Malt_in_Kernel;
    MyLongDouble Sfr_in_Kernel;
    MyLongDouble Jgas_in_Kernel[3];
    MyLongDouble Jstar_in_Kernel[3];
    MyLongDouble Jalt_in_Kernel[3];
#ifdef BH_GRAVACCRETION
    MyLongDouble MgasBulge_in_Kernel;
    MyLongDouble MstarBulge_in_Kernel;
#endif
#if defined(BH_PHOTONMOMENTUM) || defined(BH_WIND_CONTINUOUS)
    MyLongDouble GradRho_in_Kernel[3];
    MyFloat BH_angle_weighted_kernel_sum;
#endif
#ifdef BH_DYNFRICTION
    MyFloat DF_mean_vel[3];
    MyFloat DF_rms_vel;
    MyFloat DF_mmax_particles;
#endif
#if defined(BH_BONDI) || defined(BH_DRAG) || (BH_GRAVACCRETION == 5)
    MyFloat BH_SurroundingGasVel[3];
#endif
    
#if defined(BH_ALPHADISK_ACCRETION)
    MyFloat mdot_alphadisk;             /*!< gives mdot of mass going into alpha disk */
#endif

#if defined(BH_GRAVCAPTURE_GAS)
    MyFloat mass_to_swallow_edd;        /*!< gives the mass we want to swallow that contributes to eddington */
#endif
#ifdef SINGLE_STAR_STRICT_ACCRETION
    MyLongDouble accreted_moment[3]; /* first mass moment of accreted gas, for preserving centre of mass */
#endif
#if defined(NEWSINK)
    /* Timescales for the implementation of the NEWSINK algorithm from Hubber 2013 */
    MyFloat t_disc;        /* Disc timescale */
    MyFloat t_rad;        /* Timescale of radial infall */
    MyFloat t_acc;        /* Accretion timescale */
    /* Further gas properties */
    MyFloat gas_Erot_in_intzone;        /* Rotational energy in kernel */
    MyFloat gas_Egrav_in_intzone;        /* gravitational energy in kernel */
    MyFloat t_rad_denom_sum;        /* sum in denominator of Eq 8 in Hbber 2013 */
    MyFloat t_disc_num_sum;        /* sum in Eq 10 in Hubber 2013 without kernel weight*/
    MyFloat intzone_massweight_all;        /* sum in denominator of Eq 8 in Hubber 2013 */
    MyFloat intzone_gasmass;        /* sum of gas mass in Sink Radius */
    /* properties of neighboring particles, used for preferential feeding */
    int n_neighbor; //number of neighbors currently stored in the arrays below
    MyFloat rgas[NEWSINK_NEIGHBORMAX]; /* Distance of gas from sink */
    MyFloat xgas[NEWSINK_NEIGHBORMAX]; /* x coordinate of gas from sink */
    MyFloat ygas[NEWSINK_NEIGHBORMAX]; /* y coordinate of gas from sink */
    MyFloat zgas[NEWSINK_NEIGHBORMAX]; /* z coordinate of gas from sink */
    MyFloat Hsmlgas[NEWSINK_NEIGHBORMAX]; /* gas smoothing length */
    MyFloat mgas[NEWSINK_NEIGHBORMAX]; /* Mass of gas particle */
    MyIDType gasID[NEWSINK_NEIGHBORMAX]; /* ID of gas particle */
    int isbound[NEWSINK_NEIGHBORMAX]; /* is it bound to the sink */
    MyFloat f_acc[NEWSINK_NEIGHBORMAX]; /* How much of the gas particle should be accreted */
#ifdef NEWSINK_BONDI
    MyFloat min_bondi_mdot;
    MyFloat gasmass_within_softening;
#endif     
#if defined(NEWSINK_J_FEEDBACK)
    MyLongDouble accreted_J[3]; /* Accreted angular momentum */
    MyDouble dv_ang_kick_norm[NEWSINK_NEIGHBORMAX]; /*Normalization term for angular momentum feedback kicks, see denominator of Eq 22 of Hubber 2013*/
#endif
#endif

}
*BlackholeTempInfo, *BlackholeDataPasserResult, *BlackholeDataPasserOut;
#endif




/*! Header for the standard file format.
 */
extern struct io_header
{
  int npart[6];			/*!< number of particles of each type in this file */
  double mass[6];		/*!< mass of particles of each type. If 0, then the masses are explicitly
                                stored in the mass-block of the snapshot file, otherwise they are omitted */
  double time;			/*!< time of snapshot file */
  double redshift;		/*!< redshift of snapshot file */
  int flag_sfr;			/*!< flags whether the simulation was including star formation */
  int flag_feedback;		/*!< flags whether feedback was included (obsolete) */
  unsigned int npartTotal[6];	/*!< total number of particles of each type in this snapshot. This can be
				   different from npart if one is dealing with a multi-file snapshot. */
  int flag_cooling;		/*!< flags whether cooling was included  */
  int num_files;		/*!< number of files in multi-file snapshot */
  double BoxSize;		/*!< box-size of simulation in case periodic boundaries were used */
  double Omega0;		/*!< matter density in units of critical density */
  double OmegaLambda;		/*!< cosmological constant parameter */
  double HubbleParam;		/*!< Hubble parameter in units of 100 km/sec/Mpc */
  int flag_stellarage;		/*!< flags whether the file contains formation times of star particles */
  int flag_metals;		/*!< flags whether the file contains metallicity values for gas and star particles */
  unsigned int npartTotalHighWord[6];	/*!< High word of the total number of particles of each type (needed to combine with npartTotal to allow >2^31 particles of a given type) */
  int flag_doubleprecision;	/*!< flags that snapshot contains double-precision instead of single precision */

  int flag_ic_info;             /*!< flag to inform whether IC files are generated with ordinary Zeldovich approximation,
                                     or whether they ocontains 2nd order lagrangian perturbation theory initial conditions.
                                     For snapshots files, the value informs whether the simulation was evolved from
                                     Zeldoch or 2lpt ICs. Encoding is as follows:
                                        FLAG_ZELDOVICH_ICS     (1)   - IC file based on Zeldovich
                                        FLAG_SECOND_ORDER_ICS  (2)   - Special IC-file containing 2lpt masses
                                        FLAG_EVOLVED_ZELDOVICH (3)   - snapshot evolved from Zeldovich ICs
                                        FLAG_EVOLVED_2LPT      (4)   - snapshot evolved from 2lpt ICs
                                        FLAG_NORMALICS_2LPT    (5)   - standard gadget file format with 2lpt ICs
                                     All other values, including 0 are interpreted as "don't know" for backwards compatability.
                                 */
  float lpt_scalingfactor;      /*!< scaling factor for 2lpt initial conditions */

  char fill[18];		/*!< fills to 256 Bytes */
  char names[15][2];
}
header;				/*!< holds header for snapshot files */







enum iofields
{ IO_POS,
  IO_VEL,
  IO_ID,
  IO_CHILD_ID,
  IO_GENERATION_ID,
  IO_MASS,
  IO_SECONDORDERMASS,
  IO_U,
  IO_RHO,
  IO_NE,
  IO_NH,
  IO_HSML,
  IO_SFR,
  IO_AGE,
  IO_GRAINSIZE,
  IO_HSMS,
  IO_Z,
  IO_BHMASS,
  IO_BHMASSALPHA,
  IO_BHMDOT,
  IO_BHPROGS,
  IO_BH_DIST,
  IO_ACRB,
  IO_SINKRAD,
  IO_JSINK,
  IO_BHMASSINIT,
  IO_POT,
  IO_ACCEL,
  IO_HII,
  IO_HeI,
  IO_HeII,
  IO_HeIII,
  IO_H2I,
  IO_H2II,
  IO_HM,
  IO_HD,
  IO_DI,
  IO_DII,
  IO_HeHII,
  IO_DTENTR,
  IO_STRESSDIAG,
  IO_STRESSOFFDIAG,
  IO_STRESSBULK,
  IO_SHEARCOEFF,
  IO_TSTP,
  IO_BFLD,
  IO_DBDT,
  IO_IMF,
  IO_COSMICRAY_ENERGY,
  IO_COSMICRAY_KAPPA,
  IO_COSMICRAY_ALFVEN,
  IO_DIVB,
  IO_ABVC,
  IO_AMDC,
  IO_PHI,
  IO_GRADPHI,
  IO_ROTB,
  IO_COOLRATE,
  IO_CONDRATE,
  IO_DENN,
  IO_TIDALTENSORPS,
  IO_GDE_DISTORTIONTENSOR,
  IO_FLOW_DETERMINANT,
  IO_PHASE_SPACE_DETERMINANT,
  IO_ANNIHILATION_RADIATION,
  IO_STREAM_DENSITY,
  IO_EOSTEMP,
  IO_EOSABAR,
  IO_EOSYE,
  IO_PRESSURE,
  IO_EOSCS,
  IO_EOS_STRESS_TENSOR,
  IO_CBE_MOMENTS,
  IO_EOSCOMP,
  IO_PARTVEL,
  IO_RADGAMMA,
  IO_RAD_ACCEL,
  IO_EDDINGTON_TENSOR,
  IO_LAST_CAUSTIC,
  IO_SHEET_ORIENTATION,
  IO_INIT_DENSITY,
  IO_CAUSTIC_COUNTER,
  IO_DMHSML,                    /* for 'SUBFIND_RESHUFFLE_CATALOGUE' option */
  IO_DMDENSITY,
  IO_DMVELDISP,
  IO_DMHSML_V,                
  IO_DMDENSITY_V,
  IO_VRMS,
  IO_VBULK,
  IO_VRAD,
  IO_VTAN,
  IO_TRUENGB,
  IO_VDIV,
  IO_VROT,
  IO_VORT,
  IO_CHEM,
  IO_DELAYTIME,
  IO_AGS_SOFT,
  IO_AGS_RHO,
  IO_AGS_QPT,
  IO_AGS_ZETA,
  IO_AGS_OMEGA,
  IO_AGS_CORR,
  IO_AGS_NGBS,
  IO_VSTURB_DISS,
  IO_VSTURB_DRIVE,
  IO_MG_PHI,
  IO_MG_ACCEL,
  IO_grHI,
  IO_grHII,
  IO_grHM,
  IO_grHeI,
  IO_grHeII,
  IO_grHeIII,
  IO_grH2I,
  IO_grH2II,
  IO_grDI,
  IO_grDII,
  IO_grHDI,
  IO_OSTAR,  
  IO_LASTENTRY			/* This should be kept - it signals the end of the list */
};

enum siofields
{ SIO_GLEN,
  SIO_GOFF,
  SIO_MTOT,
  SIO_GPOS,
  SIO_MMEA,
  SIO_RMEA,
  SIO_MCRI,
  SIO_RCRI,
  SIO_MTOP,
  SIO_RTOP,
  SIO_DMEA,
  SIO_DCRI,
  SIO_DTOP,
  SIO_MGAS,
  SIO_MSTR,
  SIO_TGAS,
  SIO_LGAS,
  SIO_NCON,
  SIO_MCON,
  SIO_BGPOS,
  SIO_BGMTOP,
  SIO_BGRTOP,
  SIO_NSUB,
  SIO_FSUB,
  SIO_SLEN,
  SIO_SOFF,
  SIO_PFOF,
  SIO_MSUB,
  SIO_SPOS,
  SIO_SVEL,
  SIO_SCM,
  SIO_SPIN,
  SIO_DSUB,
  SIO_VMAX,
  SIO_RVMAX,
  SIO_RHMS,
  SIO_MBID,
  SIO_GRNR,
  SIO_SMST,
  SIO_SLUM,
  SIO_SLATT,
  SIO_SLOBS,
  SIO_DUST,
  SIO_SAGE,
  SIO_SZ,
  SIO_SSFR,
  SIO_PPOS,
  SIO_PVEL,
  SIO_PTYP,
  SIO_PMAS,
  SIO_PAGE,
  SIO_PID,

  SIO_LASTENTRY
};

/*
 * Variables for Tree
 * ------------------
 */

extern long Nexport, Nimport;
extern int BufferFullFlag;
extern int NextParticle;
extern int NextJ;
extern int TimerFlag;

// note, the ALIGN(32) directive will effectively pad the structure size
// to a multiple of 32 bytes
extern ALIGN(32) struct NODE
{
  MyFloat center[3];		/*!< geometrical center of node */
  MyFloat len;			/*!< sidelength of treenode */

  union
  {
    int suns[8];		/*!< temporary pointers to daughter nodes */
    struct
    {
      MyFloat s[3];		/*!< center of mass of node */
      MyFloat mass;		/*!< mass of node */
      unsigned int bitflags;	/*!< flags certain node properties */
      int sibling;		/*!< this gives the next node in the walk in case the current node can be used */
      int nextnode;		/*!< this gives the next node in case the current node needs to be opened */
      int father;		/*!< this gives the parent node of each node (or -1 if we have the root node) */
    }
    d;
  }
  u;

  double GravCost;
  integertime Ti_current;

#ifdef RT_USE_GRAVTREE
  MyFloat stellar_lum[N_RT_FREQ_BINS]; /*!< luminosity in the node*/
#endif

#ifdef BH_PHOTONMOMENTUM
    MyFloat bh_lum;		/*!< luminosity of BHs in the node */
    MyFloat bh_lum_hR;		/*!< local h/R for gas around BH (gives angular dependence) */
    MyFloat bh_lum_grad[3];	/*!< gradient vector for gas around BH (for angular dependence) */
#endif    

#ifdef BH_CALC_DISTANCES
  MyFloat bh_mass;      /*!< holds the BH mass in the node.  Used for calculating tree based dist to closest bh */
  MyFloat bh_pos[3];    /*!< holds the mass-weighted position of the the actual black holes within the node */
#if defined(SINGLE_STAR_TIMESTEPPING) || defined(SINGLE_STAR_SUPERTIMESTPEPING)
  MyFloat bh_vel[3];    /*!< holds the mass-weighted avg. velocity of black holes in the node */
  int N_BH;             /*!< holds the number of BH particles in the node. Used for refinement/search criteria */
#endif  
#endif
    
#ifdef RT_SEPARATELY_TRACK_LUMPOS
    MyFloat rt_source_lum_s[3];     /*!< center of luminosity for sources in the node*/
#endif
    
  MyFloat maxsoft;		/*!< hold the maximum gravitational softening of particle in the node */
  
#ifdef DM_SCALARFIELD_SCREENING
  MyFloat s_dm[3];
  MyFloat mass_dm;
#endif
}
 *Nodes_base,			/*!< points to the actual memory allocted for the nodes */
 *Nodes;			/*!< this is a pointer used to access the nodes which is shifted such that Nodes[All.MaxPart]
				   gives the first allocated node */


extern struct extNODE
{
  MyLongDouble dp[3];
#ifdef RT_SEPARATELY_TRACK_LUMPOS
    MyLongDouble rt_source_lum_dp[3];
    MyFloat rt_source_lum_vs[3];
#endif
#ifdef DM_SCALARFIELD_SCREENING
  MyLongDouble dp_dm[3];
  MyFloat vs_dm[3];
#endif
  MyFloat vs[3];
  MyFloat vmax;
  MyFloat hmax;			/*!< maximum gas kernel length in node. Only used for gas particles */
  MyFloat divVmax;
  integertime Ti_lastkicked;
  int Flag;
}
 *Extnodes, *Extnodes_base;


extern int MaxNodes;		/*!< maximum allowed number of internal nodes */
extern int Numnodestree;	/*!< number of (internal) nodes in each tree */


extern int *Nextnode;		/*!< gives next node in tree walk  (nodes array) */
extern int *Father;		/*!< gives parent node in tree (Prenodes array) */

extern int maxThreads;

#ifdef TURB_DRIVING
//parameters
extern double StDecay;
extern double StEnergy;
extern double StDtFreq;
extern double StKmin;
extern double StKmax;
extern double StSolWeight;
extern double StAmplFac;
 
//Ornstein-Uhlenbeck variables
extern int StSeed;
extern double StOUVar;
extern double* StOUPhases;


//forcing field in fourie space
extern double* StAmpl;
extern double* StAka; //phases (real part)
extern double* StAkb; //phases (imag part)
extern double* StMode;
extern int StNModes;

extern integertime StTPrev;
extern double StSolWeightNorm;

extern int StSpectForm;

extern gsl_rng* StRng;
extern int FB_Seed;
#endif



#ifdef DM_SIDM
#define GEOFACTOR_TABLE_LENGTH 1000    /*!< length of the table used for the geometric factor spline */
extern MyDouble GeoFactorTable[GEOFACTOR_TABLE_LENGTH];
#endif

#endif  /* ALLVARS_H  - please do not put anything below this line */








