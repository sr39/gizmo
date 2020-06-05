#!/bin/bash            # this line only there to enable syntax highlighting in this file

####################################################################################################
#  Enable/Disable compile-time options as needed: this is where you determine how the code will act
#  From the list below, please activate/deactivate the
#       options that apply to your run. If you modify any of these options,
#       make sure that you recompile the whole code by typing "make clean; make".
#
#  Consult the User Guide before enabling any option. Some modules are proprietary -- access to them
#    must be granted separately by the code authors (just having the code does NOT grant permission).
#    Even public modules have citations which must be included if the module is used for published work,
#    these are all given in the User Guide.
#
# This file was originally part of the GADGET3 code developed by Volker Springel. It has been modified
#   substantially by Phil Hopkins (phopkins@caltech.edu) for GIZMO (to add new modules, change
#   naming conventions, restructure, add documention, and match GIZMO conventions)
#
####################################################################################################



####################################################################################################
# --------------------------------------- Boundary Conditions & Dimensions
####################################################################################################
#BOX_SPATIAL_DIMENSION=3    # sets number of spatial dimensions evolved (default=3). Switch for 1D/2D test problems: if =1, code only follows the x-line (all y=z=0), if =2, only xy-plane (all z=0). requires SELFGRAVITY_OFF
#BOX_PERIODIC               # Use this if periodic/finite boundaries are needed (otherwise an infinite box [no boundary] is assumed)
#BOX_BND_PARTICLES          # particles with ID=0 are forced in place (their accelerations are set =0): use for special boundary conditions where these particles represent fixed "walls"
#BOX_SHEARING=1             # shearing box boundaries: 1=r-z sheet (r,z,phi coordinates), 2=r-phi sheet (r,phi,z), 3=r-phi-z box, 4=as 3, with vertical gravity
#BOX_SHEARING_Q=(3./2.)     # shearing box q=-dlnOmega/dlnr; will default to 3/2 (Keplerian) if not set
#BOX_LONG_X=140             # modify box dimensions (non-square finite box): multiply X (not compatible with periodic gravity: if BOX_PERIODIC or PMGRID is active, make sure SELFGRAVITY_OFF or GRAVITY_NOT_PERIODIC is on)
#BOX_LONG_Y=1               # modify box dimensions (non-square finite box): multiply Y
#BOX_LONG_Z=1               # modify box dimensions (non-square finite box): multiply Z
#BOX_REFLECT_X=0            # make the x-boundary reflecting (assumes a box 0<x<BoxSize_X, where BoxSize_X=BoxSize*BOX_LONG_X, if BOX_LONG_X is set); if no value set or =0, both x-boundaries reflect, if =-1, only lower-x (x=0) boundary reflects, if =+1, only upper-x (x=BoxSize) boundary reflects
#BOX_REFLECT_Y              # make the y-boundary reflecting (assumes a box 0<y<BoxSize_Y); if no value set or =0, both y-boundaries reflect, if =-1, only lower-y (y=0) boundary reflects, if =+1, only upper-y (y=BoxSize) boundary reflects
#BOX_REFLECT_Z              # make the z-boundary reflecting (assumes a box 0<z<BoxSize_Z); if no value set or =0, both z-boundaries reflect, if =-1, only lower-z (z=0) boundary reflects, if =+1, only upper-z (z=BoxSize) boundary reflects
#BOX_OUTFLOW_X=0            # make the x-boundary outflowing (assumes a box 0<x<BoxSize_X, where BoxSize_X=BoxSize*BOX_LONG_X, if BOX_LONG_X is set); if no value set or =0, both x-boundaries outflow, if =-1, only lower-x (x=0) boundary outflows, if =+1, only upper-x (x=BoxSize) boundary outflows
#BOX_OUTFLOW_Y              # make the y-boundary outflowing (rules follow BOX_OUTFLOW_X, for the y-axis here). note that outflow boundaries are usually not needed, with Lagrangian methods, but may be useful in special cases.
#BOX_OUTFLOW_Z              # make the z-boundary outflowing (rules follow BOX_OUTFLOW_X, for the z-axis here)
####################################################################################################



####################################################################################################
# --------------------------------------- Hydro solver method
####################################################################################################
# --------------------------------------- Finite-volume Godunov methods (choose one, or SPH)
#HYDRO_MESHLESS_FINITE_MASS     # solve hydro using the mesh-free Lagrangian (fixed-mass) finite-volume Godunov method
#HYDRO_MESHLESS_FINITE_VOLUME   # solve hydro using the mesh-free (quasi-Lagrangian) finite-volume Godunov method (control mesh motion with HYDRO_FIX_MESH_MOTION)
#HYDRO_REGULAR_GRID             # solve hydro equations on a regular (recti-linear) Cartesian mesh (grid) with a finite-volume Godunov method
## -----------------------------------------------------------------------------------------------------
# --------------------------------------- Options to explicitly control the mesh motion (for use with the MFV or grid solvers): only set for non-standard behavior
#HYDRO_FIX_MESH_MOTION=0        # mesh with arbitrarily-defined mesh-generating velocities: (0=non-moving, 1=fixed-v [set in ICs] cartesian, 2=fixed-v [ICs] cylindrical, 3=fixed-v [ICs] spherical, 4=analytic function, 5=smoothed-Lagrangian, 6=glass-generating, 7=fully-Lagrangian)
#HYDRO_GENERATE_TARGET_MESH     # use for IC generation (can be used with -any- hydro method: MFM/MFV/SPH/grid): this allows you to specify in the functions 'return_user_desired_target_density' and 'return_user_desired_target_pressure' (in eos.c) the desired initial density/pressure profile, and the code will try to evolve towards this.
## -----------------------------------------------------------------------------------------------------
# --------------------------------------- SPH methods (enable one of these flags to use SPH):
#HYDRO_PRESSURE_SPH             # solve hydro using SPH with the 'pressure-sph' formulation ('P-SPH')
#HYDRO_DENSITY_SPH              # solve hydro using SPH with the 'density-sph' formulation (GADGET-2 & GASOLINE SPH)
# --------------------------------------- SPH artificial diffusion options (use with SPH; not relevant for Godunov/Mesh modes)
#SPH_DISABLE_CD10_ARTVISC       # for SPH only: Disable Cullen & Dehnen 2010 'inviscid sph' (viscosity suppression outside shocks); just use Balsara switch
#SPH_DISABLE_PM_CONDUCTIVITY    # for SPH only: Disable mixing entropy (J.Read's improved Price-Monaghan conductivity with Cullen-Dehnen switches)
## -----------------------------------------------------------------------------------------------------
# --------------------------------------- Kernel Options
#KERNEL_FUNCTION=3              # Choose the kernel function (2=quadratic peak, 3=cubic spline [default], 4=quartic spline, 5=quintic spline, 6=Wendland C2, 7=Wendland C4, 8=2-part quadratic)
#KERNEL_CRK_FACES               # Use the consistent reproducing kernel [higher-order tensor corrections to kernel above, compared to our usual matrix formalism] from Frontiere, Raskin, and Owen to define the faces in MFM/MFV methods. can give more accurate closure, potentially improved accuracy in MHD problems. remains experimental for now.
####################################################################################################



####################################################################################################
# --------------------------------------- Additional Fluid Physics
####################################################################################################
## ----------------------------------------------------------------------------------------------------
# --------------------------------------- Gas (or Material) Equations-of-State
#EOS_GAMMA=(5.0/3.0)            # Polytropic Index of Gas (for an ideal gas law): if not set and no other (more complex) EOS set, defaults to GAMMA=5/3
#EOS_HELMHOLTZ                  # Use Timmes & Swesty 2000 EOS (for e.g. stellar or degenerate equations of state); if additional tables needed, download at http://www.tapir.caltech.edu/~phopkins/public/helm_table.dat (or the BitBucket site)
#EOS_TILLOTSON                  # Use Tillotson (1962) EOS (for solid/liquid+vapor bodies, impacts); custom EOS params can be specified or pre-computed materials used. see User Guide and Deng et al., arXiv:1711.04589
#EOS_ELASTIC                    # treat fluid as elastic or plastic (or visco-elastic) material, obeying Hooke's law with full stress terms and von Mises yield model. custom EOS params can be specified or pre-computed materials used.
## -----------------------------------------------------------------------------------------------------
# --------------------------------- Magneto-Hydrodynamics
# ---------------------------------  these modules are public, but if used, the user should also cite the MHD-specific GIZMO methods paper
# ---------------------------------  (Hopkins 2015: 'Accurate, Meshless Methods for Magneto-Hydrodynamics') as well as the standard GIZMO paper
#MAGNETIC                       # master switch for MHD, regardless of which Hydro solver is used
#MHD_B_SET_IN_PARAMS            # set initial fields (Bx,By,Bz) in parameter file
#MHD_NON_IDEAL                  # enable non-ideal MHD terms: Ohmic resistivity, Hall effect, and ambipolar diffusion (solved explicitly); Users should cite Hopkins 2017, MNRAS, 466, 3387, in addition to the MHD paper
#MHD_CONSTRAINED_GRADIENT=1     # use CG method (in addition to cleaning, optional!) to maintain low divB: set this value to control how aggressive the div-reduction is:
                                # 0=minimal (safest), 1=intermediate (recommended), 2=aggressive (less stable), 3+=very aggressive (less stable+more expensive). [Please cite Hopkins, MNRAS, 2016, 462, 576]
## ----------------------------------------------------------------------------------------------------
# -------------------------------------- Conduction
# ----------------------------------------- [Please cite and read the methods paper Hopkins 2017, MNRAS, 466, 3387]
#CONDUCTION                     # Thermal conduction solved *explicitly*: isotropic if MAGNETIC off, otherwise anisotropic
#CONDUCTION_SPITZER             # Spitzer conductivity accounting for saturation: otherwise conduction coefficient is constant  [cite Su et al., 2017, MNRAS, 471, 144, in addition to the conduction methods paper above].  Requires COOLING to calculate local thermal state of gas.
## ----------------------------------------------------------------------------------------------------
# -------------------------------------- Viscosity
# ----------------------------------------- [Please cite and read the methods paper Hopkins 2017, MNRAS, 466, 3387]
#VISCOSITY                      # Navier-stokes equations solved *explicitly*: isotropic coefficients if MAGNETIC off, otherwise anisotropic
#VISCOSITY_BRAGINSKII           # Braginskii viscosity tensor for ideal MHD [cite Su et al., 2017, MNRAS, 471, 144, in addition to the viscosity methods paper above]. Requires COOLING to calculate local thermal state of gas.
## ----------------------------------------------------------------------------------------------------
# -------------------------------------- Radiative Cooling physics (mostly geared towards galactic/extragalactic cooling)
# -------------------------- These modules were originally developed for a combination of proprietary physics modules. However they are now written in
# --------------------------   a form which allows them to be modular (and public). Users are free to use the Grackle modules and standard 'COOLING' flags,
# --------------------------   provided proper credit/citations are provided to the relevant methods papers given in the Users Guide ---
# --------------------------   but all users should cite Hopkins et al. 2017 (arXiv:1702.06148), where Appendix B details the cooling physics
#COOLING                        # enables radiative cooling and heating: if GALSF, also external UV background read from file "TREECOOL" (included in the cooling folder; be sure to cite its source as well, given in the TREECOOL file)
#COOL_LOW_TEMPERATURES          # allow fine-structure and molecular cooling to ~10 K; account for optical thickness and line-trapping effects with proper opacities [requires METALS]
#COOL_METAL_LINES_BY_SPECIES    # use full multi-species-dependent cooling tables ( http://www.tapir.caltech.edu/~phopkins/public/spcool_tables.tgz, or the Bitbucket site); requires METALS on; cite Wiersma et al. 2009 (MNRAS, 393, 99) in addition to Hopkins et al. 2017 (arXiv:1702.06148)
#COOL_GRACKLE                   # enable Grackle: cooling+chemistry package (requires COOLING above; https://grackle.readthedocs.org/en/latest ); see Grackle code for their required citations
#COOL_GRACKLE_CHEMISTRY=1       # choose Grackle cooling chemistry: (0)=tabular, (1)=Atomic, (2)=(1)+H2+H2I+H2II, (3)=(2)+DI+DII+HD
#METALS                         # enable metallicities (with multiple species optional) for gas and stars [must be included in ICs or injected via dynamical feedback; needed for some routines]
## ----------------------------------------------------------------------------------------------------
# -------------------------------------- Smagorinsky Turbulent Eddy Diffusion Model
# --------------------------------------- Users of these modules should cite Hopkins et al. 2017 (arXiv:1702.06148) and Colbrook et al. (arXiv:1610.06590)
#TURB_DIFF_METALS               # turbulent diffusion of metals (passive scalars); requires METALS
#TURB_DIFF_ENERGY               # turbulent diffusion of internal energy (conduction with effective turbulent coefficients)
#TURB_DIFF_VELOCITY             # turbulent diffusion of momentum (viscosity with effective turbulent coefficients)
#TURB_DIFF_DYNAMIC              # replace Smagorinsky-style eddy diffusion with the 'dynamic localized Smagorinsky' model from Rennehan et al. (arXiv:1807.11509): cite that paper for all methods. more accurate but more complex and expensive.
## ----------------------------------------------------------------------------------------------------
# --------------------------------------- Aerodynamic Particles
# ----------------------------- This is developed by P. Hopkins, who requests that you inform him of planned projects with these modules
# ------------------------------  because he is supervising several students using them as well, and there are some components still in active development.
# ------------------------------  Users should cite: Hopkins & Lee 2016, MNRAS, 456, 4174, and Lee, Hopkins, & Squire 2017, MNRAS, 469, 3532, for the numerical methods (plus other papers cited below)
#GRAIN_FLUID                    # aerodynamically-coupled grains (particle type 3 are grains); default is Epstein drag
#GRAIN_EPSTEIN_STOKES=1         # uses the cross section for molecular hydrogen (times this number) to calculate Epstein-Stokes drag; need to set GrainType=1 (will use calculate which applies and use appropriate value); if used with GRAIN_LORENTZFORCE and GrainType=2, will also compute Coulomb drag
#GRAIN_BACKREACTION             # account for momentum of grains pushing back on gas (from drag terms); users should cite Moseley et al., 2018, arXiv:1810.08214.
#GRAIN_LORENTZFORCE             # charged grains feel Lorentz forces (requires MAGNETIC); if used with GRAIN_EPSTEIN_STOKES flag, will also compute Coulomb drag (grain charges self-consistently computed from gas properties). Need to set GrainType=2.
#GRAIN_COLLISIONS               # model collisions between grains (super-particles; so this is stochastic). Default = hard-sphere scattering, with options for inelastic or velocity-dependent terms. Approved users please cite papers above and Rocha et al., MNRAS 2013, 430, 81
## ----------------------------------------------------------------------------------------------------
#---------------------------------------- Cosmic Rays
#---------------------------------------- (this is developed by P. Hopkins as part of the FIRE package: the same FIRE authorship & approval policies apply, see below)
#COSMIC_RAYS                    # two-fluid medium with CRs as an ultrarelativistic fluid: heating/cooling, anisotropic diffusion, streaming, injection by SNe
#COSMIC_RAYS_ALFVEN=(500.)      # solve CR transport based on Alfven-limited scattering from Thomas+Pfrommer 18, evolves CRs+resonant Alfven population; value here is maximum free-streaming speed in code units. requires MAGNETIC and COOLING for detailed MHD and ionization+thermal states.
#COSMIC_RAYS_M1=(500.)          # solve the CR transport in the M1 limit [second-order expansion of the collisionless boltzmann eqn]; value here is the streaming speed in code units
#COSMIC_RAYS_DIFFUSION_MODEL=0  # determine how coefficients for CR transport scale. 0=constant diffusivity, -1=no diffusion(still stream), values >=1 correspond to different literature scalings for the coefficients (see user guide)
#COSMIC_RAYS_ION_ALFVEN_SPEED   # assume the relevant Alfven speed governing CR transport is not the ideal-MHD Alfven speed, but the Alfven speed for -just- the ions (applicable in the weak-coupling limit for the resonant Alfven waves at CR gyro-resonance)
#COSMIC_RAYS_DISABLE_STREAMING  # turn off CR streaming (propagation is purely advective+diffusion; warning: this can severely under-estimate CR losses to Alfven waves)
#COSMIC_RAYS_DISABLE_COOLING    # turn off CR heating/cooling interactions with gas (catastrophic losses, hadronic interactions, etc; only adiabatic PdV work terms remain)
## ----------------------------------------------------------------------------------------------------
####################################################################################################



####################################################################################################
# ------------------------------------- Driven turbulence (for turbulence tests, large-eddy sims)
# ------------------------------- users of these routines should cite Bauer & Springel 2012, MNRAS, 423, 3102. Thanks to A. Bauer for providing the core algorithms
####################################################################################################
#TURB_DRIVING                   # turns on turbulent driving/stirring. see begrun for parameters that must be set
#TURB_DRIVING_SPECTRUMGRID=128  # activates on-the-fly calculation of the turbulent velocity, vorticity, and smoothed-velocity power spectra, evaluated on a grid of linear-size TURB_DRIVING_SPECTRUMGRID elements. Requires BOX_PERIODIC
####################################################################################################



####################################################################################################
## ------------------------ Gravity & Cosmological Integration Options ---------------------------------
####################################################################################################
# --------------------------------------- TreePM Options (recommended for cosmological sims)
#PMGRID=512                     # adds Particle-Mesh grid for faster (but less accurate) long-range gravitational forces: value sets resolution (e.g. a PMGRID^3 grid will overlay the box, as the 'top level' grid)
#PM_PLACEHIGHRESREGION=1+2+16   # adds a second-level (nested) PM grid before the tree: value denotes particle types (via bit-mask) to place high-res PMGRID around. Requires PMGRID.
#PM_HIRES_REGION_CLIPPING=1000  # optional additional criterion for boundaries in 'zoom-in' type simulations: clips gas particles that escape the hires region in zoom/isolated sims, specifically those whose nearest-neighbor distance exceeds this value (in code units)
#PM_HIRES_REGION_CLIPDM         # split low-res DM particles that enter high-res region (completely surrounded by high-res)
## -----------------------------------------------------------------------------------------------------
# ---------------------------------------- Adaptive Grav. Softening (including Lagrangian conservation terms!)
#ADAPTIVE_GRAVSOFT_FORGAS       # allows variable softening length for gas particles (scaled with local inter-element separation), so gravity traces same density field seen by hydro
#ADAPTIVE_GRAVSOFT_FORALL=1+2   # enable adaptive gravitational softening lengths for designated particle types (ADAPTIVE_GRAVSOFT_FORGAS should be disabled). the softening is set to the distance
                                # enclosing a neighbor number set in the parameter file. flag value = bitflag like PM_PLACEHIGHRESREGION, which determines which particle types are adaptive (others use fixed softening). cite Hopkins et al., arXiv:1702.06148
## -----------------------------------------------------------------------------------------------------
#SELFGRAVITY_OFF                # turn off self-gravity (compatible with GRAVITY_ANALYTIC); setting NOGRAVITY gives identical functionality
#GRAVITY_NOT_PERIODIC           # self-gravity is not periodic, even though the rest of the box is periodic
## -----------------------------------------------------------------------------------------------------
#GRAVITY_ANALYTIC               # specific analytic gravitational force to use instead of or with self-gravity. If set to a numerical value
                                #  > 0 (e.g. =1), then BH_CALC_DISTANCES will be enabled, and it will use the nearest BH particle as the center for analytic gravity computations
                                #  (edit "gravity/analytic_gravity.h" to actually assign the analytic gravitational forces). 'ANALYTIC_GRAVITY' gives same functionality
## ----------------------------------------------------------------------------------------------------
# -------------------------------------- Self-Interacting DM (Rocha et al. 2012) and Scalar-field DM and Fuzzy DM
# -------------------------------    use of these routines (if not in the public GIZMO code) requires explicit pre-approval by developers J. Bullock or M. Boylan-Kolchin (acting for M. Rocha); approved users please cite Rocha et al., MNRAS 2013, 430, 81 and Robles et al, 2017 (arXiv:1706.07514)
#DM_SIDM=2                      # self-interacting particle types (specify the particle types which are self-interacting DM with a bit mask, as for PM_PLACEHIGHRESREGION above (see description); previous "DMDISK_INTERACTIONS" is identical to setting DM_SIDM=2+4  [cite Rocha et al., MNRAS 2013, 430, 81 and Robles et al, 2017 (arXiv:1706.07514)]
#DM_SCALARFIELD_SCREENING       # gravity is mediated by a long-range scalar field, with dynamical screening (primarily alternative DE models) [cite Rocha et al., MNRAS 2013, 430, 81 and Robles et al, 2017 (arXiv:1706.07514)]
#DM_FUZZY=0                     #- DM particles (Type=1) are described by Bose-Einstein Condensate: within gravity kernel (adaptive), solves quantum pressure tensor for non-linear terms arising from Schroedinger equation for a given particle mass. IN TESTING PLEASE DO NOT USE!!! 0=fully-conservative Madelung method. 1=mass-conserving direct SPE integration. 2=direct SPE (non mass-conserving)
## -----------------------------------------------------------------------------------------------------
# --------------------------------------- Pure-Tree Options for Direct N-body of small-N groups (recommended for hard binaries, etc)
#GRAVITY_ACCURATE_FEWBODY_INTEGRATION # enables a suite: GRAVITY_HYBRID_OPENING_CRIT, TIDAL_TIMESTEP_CRITERION, LONG_INTEGER_TIME, to more accurately follow few-body point-like dynamics in the tree. currently compatible only with pure-tree gravity.
## ----------------------------------------------------------------------------------------------------
# -------------------------------------- arbitrary time-dependent dark energy equations-of-state, expansion histories, or gravitational constants
#GR_TABULATED_COSMOLOGY         # enable reading tabulated cosmological/gravitational parameters (master switch)
#GR_TABULATED_COSMOLOGY_W       # read pre-tabulated dark energy equation-of-state w(z)
#GR_TABULATED_COSMOLOGY_H       # read pre-tabulated hubble function (expansion history) H(z)
#GR_TABULATED_COSMOLOGY_G       # read pre-tabulated gravitational constant G(z) [also rescales H(z) appropriately]
## ----------------------------------------------------------------------------------------------------
#EOS_TRUELOVE_PRESSURE          # adds artificial pressure floor force Jeans length above resolution scale (means you can get the wrong answer, but things will look smooth).  cite Robertson & Kravtsov 2008, ApJ, 680, 1083
####################################################################################################



####################################################################################################
# --------------------------------------- On the fly FOF groupfinder
# ----------------- This is originally developed as part of GADGET-3 by V. Springel
# ----------------- Users of any of these modules should cite Springel et al., MNRAS, 2001, 328, 726 for the numerical methods.
####################################################################################################
## ----------------------------------------------------------------------------------------------------
# ------------------------------------- Friends-of-friends on-the-fly finder options (source in fof.c)
# -----------------------------------------------------------------------------------------------------
#FOF                                # master switch: enable FoF searching on-the-fly and outputs (set parameter LINKLENGTH=x to control LinkingLength; default=0.2)
#FOF_PRIMARY_LINK_TYPES=2           # bitflag: sum of 2^type for the primary type used to define initial FOF groups (use a common type to ensure 'start' in reasonable locations)
#FOF_SECONDARY_LINK_TYPES=1+16+32   # bitflag: sum of 2^type for the seconary types which can be linked to nearest primaries (will be 'seen' when calculating group properties)
#FOF_DENSITY_SPLIT_TYPES=1+2+16+32  # bitflag: sum of 2^type for which the densities should be calculated seperately (i.e. if 1+2+16+32, fof densities are separately calculated for types 0,1,4,5, and shared for types 2,3)
#FOF_GROUP_MIN_SIZE=32              # minimum number of identified members required to qualify as a 'group': default is 32
## ----------------------------------------------------------------------------------------------------
# -------------------------------------  Subhalo on-the-fly finder options (uses "subfind" source code).
## ----------------------------------------------------------------------------------------------------
#SUBFIND                            # master switch to enable substructure-finding with the SubFind algorithm
#SUBFIND_ADDIO_NUMOVERDEN=1         # for M200,R200-type properties, compute values within in this number of different overdensities (default=1=)
#SUBFIND_ADDIO_VELDISP              # add the mass-weighted 1D velocity dispersions to properties computed in parent group[s], within the chosen overdensities
#SUBFIND_ADDIO_BARYONS              # add gas mass, mass-weighted temperature, and x-ray luminosity (assuming ionized primoridal gas), and stellar masses, to properties computed in parent group[s], within the chosen overdensities
## ----------------------------------------------------------------------------------------------------
#SUBFIND_REMOVE_GAS_STRUCTURES      # delete (do not save) any structures which are entirely gas (or have fewer than target number of elements which are non-gas, with the rest in gas)
#SUBFIND_SAVE_PARTICLEDATA          # save all particle positions,velocity,type,mass in subhalo file (in addition to IDs: this is highly redundant with snapshots, so makes subhalo info more like a snapshot)
####################################################################################################



####################################################################################################
# ----------------- Galaxy formation & Galactic Star formation
####################################################################################################
## ---------------------------------------------------------------------------------------------------
#GALSF                           # master switch for galactic star formation model: enables SF, stellar ages, generations, etc. [cite Springel+Hernquist 2003, MNRAS, 339, 289]
## ----------------------------------------------------------------------------------------------------
# --- star formation law/particle spawning (additional options: otherwise all star particles will reflect IMF-averaged populations and form strictly based on a density criterion) ---- #
## ----------------------------------------------------------------------------------------------------
#GALSF_SFR_CRITERION=(0+1+2)     # mix-and-match SF criteria with a bitflag: 0=density threshold, 1=virial criterion, 2=convergent flow, 4=local extremum, 8=no sink in kernel, 16=not falling into sink, 32=hill (tidal) criterion, 64=Jeans criterion, 128=converging flow along all principle axes, 256=self-shielding/molecular, 512=multi-free-fall (smooth dependence on virial), 1024=adds a 'catch' which weakens some kinematic criteria when forces become strongly non-Newtonian (when approach minimum force-softening) 
#GALSF_SFR_MOLECULAR_CRITERION   # [if not using GALSF_SFR_CRITERION]: estimates molecular/self-shielded fraction in SF-ing gas, only SF from that is allowed. Cite Krumholz & Gnedin (ApJ 2011 729 36) and Hopkins et al., 2017a, arXiv:1702.06148. requires METALS and COOLING.
#GALSF_SFR_VIRIAL_SF_CRITERION=0 # [if not using GALSF_SFR_CRITERION]: only allow star formation in virialized sub-regions (alpha<1) (0/no value='default'; 1='strict' (zero sf if not bound)); 2=1+time-smoothed estimator; 3=2+Jeans criterion; 4=3+check if converging along all-3 principle axes. 5=4+Tidal Hill criterion (tidal tensor converging in all dimensions). Cite Hopkins, Narayanan, & Murray 2013 (MNRAS, 432, 2647) and Hopkins et al., 2017a, arXiv:1702.06148; (or Grudic et al. arXiv:1708.09065 for option=3,4,5)
#GALSF_SFR_IMF_VARIATION         # determines the stellar IMF for each particle from the Guszejnov/Hopkins/Hennebelle/Chabrier/Padoan theory. Cite Guszejnov, Hopkins, & Ma 2017, MNRAS, 472, 2107
#GALSF_SFR_IMF_SAMPLING          # discretely sample the IMF: simplified model with quantized number of massive stars. Cite Kung-Yi Su, Hopkins, et al., Hayward, et al., 2017, "Discrete Effects in Stellar Feedback: Individual Supernovae, Hypernovae, and IMF Sampling in Dwarf Galaxies". 
#GALSF_GENERATIONS=1             # the number of star particles a gas particle may spawn (defaults to 1, set otherwise)
## ----------------------------------------------------------------------------------------------------------------------------
# ---- sub-grid models (for large-volume simulations or modest/low resolution galaxy simulations) -----------------------------
# -------- the SUBGRID_WINDS models are variations of the Springel & Hernquist 2005 sub-grid models for the ISM, star formation, and winds.
# -------- Volker has granted permissions for their use, provided users properly cite the sources for the relevant models and scalings (described below)
#GALSF_EFFECTIVE_EQS            # Springel-Hernquist 'effective equation of state' model for the ISM and star formation [cite Springel & Hernquist, MNRAS, 2003, 339, 289]
#GALSF_SUBGRID_WINDS            # sub-grid winds ('kicks' as in Oppenheimer+Dave,Springel+Hernquist,Boothe+Schaye,etc): enable this master switch for basic functionality [cite Springel & Hernquist, MNRAS, 2003, 339, 289]
#GALSF_SUBGRID_WIND_SCALING=0   # set wind velocity scaling: 0 (default)=constant v [and mass-loading]; 1=velocity scales with halo mass (cite Oppenheimer & Dave, 2006, MNRAS, 373, 1265), requires FOF modules; 2=scale with local DM dispersion as Vogelsberger 13 (cite Zhu & Li, ApJ, 2016, 831, 52)
#GALSF_WINDS_ORIENTATION=0      # directs wind orientation [0=isotropic/random, 1=polar, 2=along density gradient]
#GALSF_FB_TURNOFF_COOLING       # turn off cooling for SNe-heated particles (as Stinson+ 2006 GASOLINE model, cite it); requires GALSF_FB_THERMAL
## ----------------------------------------------------------------------------------------------------------------------------
# ---- explicit thermal/kinetic stellar models: i.e. models which track individual 'events' (SNe, stellar mass loss, etc) and inject energy/mass/metals/momentum directly from star particles into neighboring gas
# -------- these modules explicitly evolve individual stars+stellar populations. Event rates (SNe rates, mass-loss rates) and associated yields, etc, are all specified in 'stellar_evolution.c'. the code will then handle the actual injection and events.
# -------- users are encouraged to explore their own stellar evolution models and include various types of feedback (e.g. SNe, stellar mass-loss, NS mergers, etc)
#GALSF_FB_MECHANICAL            # explicit algorithm including thermal+kinetic/momentum terms from Hopkins+ 2018 (MNRAS, 477, 1578): manifestly conservative+isotropic, and accounts properly for un-resolved PdV work+cooling during blastwave expansion. cite Hopkins et al. 2018, MNRAS, 477, 1578, and Hopkins+ 2014 (MNRAS 445, 581)
#GALSF_FB_THERMAL               # simple 'pure thermal energy dump' feedback: mass, metals, and thermal energy are injected locally in simple kernel-weighted fashion around young stars. tends to severely over-cool owing to lack of mechanical/kinetic treatment at finite resolution (better algorithm is mechanical)
## ----------------------------------------------------------------------------------------------------
#------ FIRE simulation modules for mechanical+radiative FB with full evolution+yield tracks (Hopkins et al. 2014, Hopkins et al., 2017a, arXiv:1702.06148) ------ ##
##-------- [FIRE_PHYSICS_DEFAULTS, GALSF_FB_FIRE_STELLAREVOLUTION, GALSF_FB_FIRE_RPROCESS, GALSF_FB_FIRE_RT_LONGRANGE, GALSF_FB_FIRE_RT_UVHEATING, GALSF_FB_FIRE_RT_LOCALRP, GALSF_FB_FIRE_RT_HIIHEATING, GALSF_FB_FIRE_RT_CONTINUOUSRP, FIRE_UNPROTECT_FROZEN]
#--------- Use of these follows the FIRE authorship policy. Modules are NOT to be used without authors permission (including P. Hopkins, E. Quataert, D. Keres, and C.A. Faucher-Giguere), even if you are already using the development GIZMO code. (PFH does not have sole authority to grant permission for the modules)
#--------- New projects using these modules must FIRST be PRE-APPROVED by the collaboration (after permission to use the modules has been explicitly granted), and subsequently are required to follow the collaboration's paper approval and submission policies
#FIRE_PHYSICS_DEFAULTS           # enable standard set of FIRE physics packages; note use policy above
##-----------------------------------------------------------------------------------------------------
############################################################################################################################



############################################################################################################################
## ----------------------------------------------------------------------------------------------------
# --------------- Star+Planet+Compact Object Formation (Sink Particle + Explicit/Keplerian N-Body Dynamics)
# -------------------- (unlike GALSF options, these sinks are individual accretors, not populations). Much in common with Black Hole modules below.
# -------------------- These are proprietary right now, in development with Mike Grudic, David Guszejnov, and PFH: modules not to be used without authors permission, though basic modules may be ok there is a lot of development happening here]
## ----------------------------------------------------------------------------------------------------
#SINGLE_STAR_SINK_DYNAMICS      # master switch to enable any other modules in this section
## ----------------------------------------------------------------------------------------------------
# ----- time integration, regularization, and explicit small-N-body dynamical treatments (for e.g. hard binaries, etc)
## ----------------------------------------------------------------------------------------------------
#SINGLE_STAR_TIMESTEPPING=1     # use additional timestep criteria to ensure resolved binaries/multiples dont dissolve in close encounters. 0=most conservative. 1=super-timestep hard binaries by operator-splitting the binary orbit. 2=more aggressive super-timestep.
#HERMITE_INTEGRATION=32         # Instead of the usual 2nd order DKD Leapfrog timestep, do 4th order Hermite integration for particles matching the bitflag. Allows longer timesteps and higher accuracy collisional dynamics
## ----------------------------------------------------------------------------------------------------
# ----- sink creation and accretion/growth/merger modules
## ----------------------------------------------------------------------------------------------------
#SINGLE_STAR_SINK_FORMATION=(0+1+2+4+8+16+32+64) # form new sinks on the fly, criteria from bitflag: 0=density threshold, 1=virial criterion, 2=convergent flow, 4=local extremum, 8=no sink in kernel, 16=not falling into sink, 32=hill (tidal) criterion, 64=Jeans criterion, 128=converging flow along all principle axes, 256=self-shielding/molecular, 512=multi-free-fall (smooth dependence on virial)
#SINGLE_STAR_ACCRETION=7        # sink accretion [details in BH info below]: 0-8: use BH_GRAVACCRETION=X, 9: BH_BONDI=0, 10:BH_BONDI=1, 11: BH_GRAVCAPTURE_GAS, 12: BH_GRAVCAPTURE_GAS modified with Bate-style FIXEDSINKRADIUS
## ----------------------------------------------------------------------------------------------------
#------ star (+planet) formation-specific modules (feedback, jets, radiation, protostellar evolution, etc)
##-----------------------------------------------------------------------------------------------------
#SINGLE_STAR_STARFORGE_PROTOSTELLAR_EVOLUTION=1 # sinks are assumed to be proto-stars and follow protostellar evolution tracks as they accrete to evolve radii+luminosities, determines proto-stellar feedback properties. 1=simple model [PFH], 2=fancy model [DG+MG]
#SINGLE_STAR_FB_RT_HEATING      # proto-stellar heating: luminosity determined by BlackHoleRadiativeEfficiency (typical ~5e-7)
#SINGLE_STAR_FB_JETS            # kinematic jets from sinks: outflow rate+velocity set by BAL_f_accretion+BAL_v_outflow. for now cite Angles-Alcazar et al., 2017, MNRAS, 464, 2840 (for algorithm, developed for black hole jets), though now using SPAWN algorithm developed by KY Su
## ----------------------------------------------------------------------------------------------------
# ----- optional and de-bugging modules (intended for specific behaviors)
## ----------------------------------------------------------------------------------------------------
#BH_ACCRETE_NEARESTFIRST        # place all weight for sink/BH 'swallowing' in continuous/stochastic accretion models on single nearest gas element, instead of spreading over same kernel used to calculate mdot
#BH_RETURN_ANGMOM_TO_GAS        # BH/sink particles return accreted angular momentum to surrounding gas (per Hubber+13) to represent AM transfer (loss in accreting material)
#BH_SWALLOW_SMALLTIMESTEPS      # particles with very small timesteps will be accreted to prevent extreme slowdowns, controlled by DT_MIN_TOLERANCE_FACTOR~0.001
#BH_DEBUG_DISABLE_MERGERS       # disable BH-BH (sink-sink) mergers in all the various sink routines
############################################################################################################################



####################################################################################################
# ---------------- Black Holes (Sink-Particles with Accretion and Feedback)
####################################################################################################
#BLACK_HOLES                    # master switch to enable BHs
## ----------------------------------------------------------------------------------------------------
# ----- seeding / BH-particle spawning
## ----------------------------------------------------------------------------------------------------
#BH_SEED_FROM_FOF=0             # use FOF on-the-fly to seed BHs in massive FOF halos; =0 uses DM groups, =1 uses stellar [type=4] groups; requires FOF with linking type including relevant particles (cite Angles-Alcazar et al., MNRAS, 2017, arXiv:1707.03832)
#BH_SEED_FROM_LOCALGAS          # BHs seeded on-the-fly from dense, low-metallicity gas (no FOF), like star formation; criteria define-able in modular fashion in sfr_eff.c (function return_probability_of_this_forming_bh_from_seed_model). cite Grudic et al. (arXiv:1612.05635) and Lamberts et al. (MNRAS, 2016, 463, L31). Requires GALSF and METALS
#BH_INCREASE_DYNAMIC_MASS=100   # increase the particle dynamical mass by this factor at the time of BH seeding
## ----------------------------------------------------------------------------------------------------
# ----- dynamics (when BH mass is not >> other particle masses, it will artificially get kicked and not sink via dynamical friction; these 're-anchor' the BH for low-res sims)
## ----------------------------------------------------------------------------------------------------
#BH_DYNFRICTION=0               # apply explicit dynamical friction force to the BHs when m_bh not >> other particle mass: 0=[DM+stars+gas]; 1=[DM+stars]; =2[stars]; >2 simply multiplies the DF force by this number (cite Tremmel, Governato, Volonteri, & Quinn,2015, MNRAS, 451, 1868)
#BH_DRAG=1                      # drag force on BH due to accretion; =1 uses actual mdot, =2 boost as if BH is accreting at eddington. cite Springel, Di Matteo, and Hernquist, 2005, MNRAS, 361, 776
#BH_REPOSITION_ON_POTMIN=2      # reposition black hole on potential minimum (requires EVALPOTENTIAL). [=0 'jumps', =1 to "jump" onto STARS only, =2 moves smoothly with damped velocity to most-bound particle]
## ----------------------------------------------------------------------------------------------------
# ----- accretion models (modules for gas or other particle accretion)
## ----------------------------------------------------------------------------------------------------
#BH_SWALLOWGAS                  # 'master switch' for accretion (should always be enabled if accretion is on). enables BH to actually eliminate gas particles and take their mass.
#BH_ALPHADISK_ACCRETION         # gas accreted goes into a 'virtual' alpha-disk (mass reservoir), which then accretes onto the BH at the viscous rate (determining luminosity, etc). cite GIZMO methods (or PFH private communication)
#BH_SUBGRIDBHVARIABILITY        # model variability below resolved dynamical time for BH (convolve accretion rate with a uniform power spectrum of fluctuations on timescales below the minimum resolved dynamical time). cite Hopkins & Quataert 2011, MNRAS, 415, 1027. Requires GALSF.
#BH_GRAVCAPTURE_NONGAS          # accretion determined only by resolved gravitational capture by the BH, for non-gas particles (can be enabled with other accretion models for gas). cite Hopkins et al., 2016, MNRAS, 458, 816
## ----
#BH_GRAVCAPTURE_GAS             # accretion determined only by resolved gravitational capture by the BH (for gas particles). cite Hopkins et al., 2016, MNRAS, 458, 816
#BH_GRAVACCRETION=1             # family of gravitational/torque/angular-momentum-driven accretion models from Hopkins & Quataert (2011): cite Hopkins & Quataert 2011, MNRAS, 415, 1027 and Angles-Alcazar et al. 2017, MNRAS, 464, 2840. see `notes_blackholes` for details:
#                               # [=0] evaluate at density kernel radius, [=1] evaluate at fixed physical radius, [=2] fixed efficiency per FF time at physical radius, [=3] gravity-turbulent scaling, [=4] fixed per FF at BH radius of influence, [=5] hybrid scaling (switch to Bondi if circularization radius small),
#                               # [=6] modified bondi-hoyle/fixed accretion in sonic point for rho~r^-1 profile, [=7] shu+pressure+turbulence solution for isothermal sphere (self-similar isothermal sphere solution with these terms), [=8] hubber+13 estimator of local inflow (limited by 'external alpha-disk' and 'internal bondi' estimates)
#BH_BONDI=0                     # Bondi-Hoyle style accretion model: 0=default (with velocity); 1=dont use gas velocity with sound speed; 2=variable-alpha tweak (Booth & Schaye 2009; requires GALSF). cite Springel, Di Matteo, and Hernquist, 2005, MNRAS, 361, 776
## ----------------------------------------------------------------------------------------------------
# ----- feedback models/options
## ----------------------------------------------------------------------------------------------------
#BH_FB_COLLIMATED               #-BH feedback is narrowly collimated along the axis defined by the angular momentum accreted thus far in the simulation
# -- thermal (pure thermal energy injection around BH particle, proportional to BH accretion rate)
#BH_THERMALFEEDBACK             # constant fraction of luminosity coupled in kernel around BH. cite Springel, Di Matteo, and Hernquist, 2005, MNRAS, 361, 776
# -- mechanical (wind from accretion disk/BH with specified mass/momentum/energy-loading relative to accretion rate)
#BH_WIND_CONTINUOUS             # gas in kernel around BH given continuous wind flux (energy/momentum/etc). cite Hopkins et al., 2016, MNRAS, 458, 816
#BH_WIND_KICK=1                 # gas in kernel given stochastic 'kicks' at fixed velocity. (>0=isotropic, <0=collimated, absolute value sets momentum-loading in L/c units). cite Angles-Alcazar et al., 2017, MNRAS, 464, 2840
#BH_WIND_SPAWN=2                #-spawn virtual 'wind' particles to carry BH winds out [in development by Paul Torrey]. use requires permissions from P. Torrey and PFH (cite Torrey et al. 2019 if used: -strongly- recommend contacting P. Torrey and PFH before use, as this is not fully-debugged). value=min number spawned per spawn-step
#--- radiative: [FIRE] these currently are built on the architecture of the FIRE stellar FB modules, and require some of those be active. their use therefore follows FIRE policies (see details above).
#BH_COMPTON_HEATING             # enable Compton heating/cooling from BHs in cooling function (needs BH_PHOTONMOMENTUM). cite Hopkins et al., 2016, MNRAS, 458, 816
#BH_HII_HEATING                 # photo-ionization feedback from BH (needs GALSF_FB_FIRE_RT_HIIHEATING). cite Hopkins et al., arXiv:1702.06148
#BH_PHOTONMOMENTUM              # continuous long-range IR radiation pressure acceleration from BH (needs GALSF_FB_FIRE_RT_LONGRANGE). cite Hopkins et al., arXiv:1702.06148
#--- cosmic ray: these currently build on the architecture of the BH_WIND modules, one of those must be enabled, along with the usual cosmic-ray physics set of modules for CR transport. the same restrictions (e.g. FIRE) apply to this as CR modules. developed by P. Hopkins
#BH_COSMIC_RAYS                 # explicitly inject and transport CRs from BH. set injection energy efficiency. injected alongside mechanical energy (params file sets ratios of energy in different mechanisms)
## ----------------------------------------------------------------------------------------------------
# ----- output options
## ----------------------------------------------------------------------------------------------------
#BH_OUTPUT_MOREINFO             # output additional info to "blackhole_details" on timestep-level, following Angles-Alcazar et al. 2017, MNRAS 472, 109 (use caution: files can get very large if many BHs exist)
#BH_CALC_DISTANCES              # calculate distances for all particles to closest BH for, e.g., refinement, external potentials, etc. cite Garrison-Kimmel et al., MNRAS, 2017, 471, 1709
## ----------------------------------------------------------------------------------------------------
# ----------- deprecated or de-bugging options (most have been combined or optimized into the functions above, here for legacy)
##---------------------BH_SEED_GROWTH_TESTS             #- Currently testing options for BH seeding
####################################################################################################



############################################################################################################################
# -------------------------------------- Radiative Transfer & Radiation Hydrodynamics:
# -------------------------------------------- modules developed by PFH with David Khatami, Mike Grudic, and Nathan Butcher (special  thanks to Alessandro Lupi)
# --------------------------------------------  these are now public, but if used, cite the appropriate paper[s] for their methods/implementation in GIZMO
############################################################################################################################
# -------------------- methods for calculating photon propagation (one, and only one, of these MUST be on for RT). whatever method is used, you must cite the appropriate methods paper.
#RT_FLUXLIMITEDDIFFUSION                # RT solved using moments-based 0th-order flux-limited diffusion approximation (constant, always-isotropic Eddington tensor). cite Hopkins & Grudic, 2018, arXiv:1803.07573
#RT_M1                                  # RT solved using moments-based 1st-order M1 approximation (solve fluxes and tensors with M1 closure; gives better shadowing; currently only compatible with explicit diffusion solver). cite Hopkins & Grudic, 2018, arXiv:1803.07573
#RT_OTVET                               # RT solved using moments-based 0th-order OTVET approximation (optically thin Eddington tensor, but interpolated to thick when appropriate). cite Hopkins & Grudic, 2018, arXiv:1803.07573
#RT_LOCALRAYGRID=1                      # RT solved using exact method of Jiang et al. (each cell carries a mesh in phase space of the intensity directions, rays directly solved over the 6+1D direction-space-frequency-time mesh [value=number of polar angles per octant: N_rays=4*value*(value+1)]. this is still in development, DO NOT USE without contacting PFH
#RT_LEBRON                              # RT solved using ray-based LEBRON approximation (locally-extincted background radiation in optically-thin networks; default in the FIRE simulations). cite Hopkins et al. 2012, MNRAS, 421, 3488 and Hopkins et al. 2018, MNRAS, 480, 800 [former developed methods and presented tests, latter details all algorithmic aspects explicitly]
# -------------------- solvers (numerical) --------------------------------------------------------
#RT_SPEEDOFLIGHT_REDUCTION=1            # set to a number <1 to use the 'reduced speed of light' approximation for photon propagation (C_eff=C_true*RT_SPEEDOFLIGHT_REDUCTION)
#RT_DIFFUSION_IMPLICIT                  # solve the diffusion part of the RT equations (if needed) implicitly with Conjugate Gradient iteration (Petkova+Springel): less accurate and only works with some methods, but allows larger timesteps [otherwise more accurate explicit used]
# -------------------- physics: wavelengths+coupled RT-chemistry networks (if any of these is used, cite Hopkins et al. 2018, MNRAS, 480, 800) -----------------------------------
#RT_SOURCES=1+16+32                     # source types for radiation given by bitflag (1=2^0=gas,16=2^4=new stars,32=2^5=BH)
#RT_XRAY=3                              # x-rays: 1=soft (0.5-2 keV), 2=hard (>2 keV), 3=soft+hard; used for Compton-heating
#RT_CHEM_PHOTOION=2                     # ionizing photons: 1=H-only [single-band], 2=H+He [four-band]
#RT_LYMAN_WERNER                        # lyman-werner [narrow H2 dissociating] band
#RT_PHOTOELECTRIC                       # far-uv (8-13.6eV): track photo-electric heating photons + their dust interactions
#RT_NUV                                 # near-UV: 1550-3600 Angstrom (where direct stellar emission dominates)
#RT_OPTICAL_NIR                         # optical+near-ir: 3600 Angstrom-3 micron (where direct stellar emission dominates)
#RT_FREEFREE                            # scattering from Thompson, absorption+emission from free-free, appropriate for fully-ionized plasma
#RT_INFRARED                            # infrared: photons absorbed in other bands are down-graded to IR: IR radiation + dust + gas temperatures evolved independently. Requires METALS and COOLING.
#RT_GENERIC_USER_FREQ                   # example of an easily-customizable, grey or narrow band: modify this to add your own custom wavebands easily!
# -------------------- radiation pressure options -------------------------------------------------
#RT_DISABLE_RAD_PRESSURE                # turn off radiation pressure forces (included by default)
#RT_RAD_PRESSURE_OUTPUT                 # print radiation pressure to file (requires some extra variables to save it)
#RT_ENABLE_R15_GRADIENTFIX              # for moments [FLD/OTVET/M1]: enable the Rosdahl+ 2015 approximate 'fix' (off by default) for gradients under-estimating flux when under-resolved by replacing it with E_nu*c
## ----------------------------------------------------------------------------------------------------
# ----------- alternative, test-problem, deprecated, or de-bugging functions
## ----------------------------------------------------------------------------------------------------
#RT_SELFGRAVITY_OFF                     # turn off gravity: if using an RT method that needs the gravity tree (FIRE, OTVET), use this -instead- of SELFGRAVITY_OFF to safely turn off gravitational forces
#RT_USE_TREECOL_FOR_NH=6                # uses the TreeCol method to estimate effective optical depth using non-local information from the gravity tree; cite Clark, Glover & Klessen 2012 MNRAS 420 754. Value specifies the number of angular bins on the sky for ray-tracing column density.
#RT_DIFFUSION_CG_MODIFY_EDDINGTON_TENSOR # when RT_DIFFUSION_IMPLICIT is enabled, modifies the Eddington tensor to the fully anisotropic version (less stable CG iteration)
#RT_SEPARATELY_TRACK_LUMPOS             # keep luminosity vs. mass positions separate in tree. not compatible with Tree-PM mode, but it can be slightly more accurate and useful for debugging in tree-only mode with LEBRON or OTVET algorithms.
#RT_DISABLE_FLUXLIMITER                 # removes the flux-limiter from the diffusion operations (default is to include it when using the relevant approximations)
#RT_HYDROGEN_GAS_ONLY                   # sets hydrogen fraction to 1.0 (used for certain idealized chemistry calculations)
#RT_COOLING_PHOTOHEATING_OLDFORMAT      # includes photoheating and cooling (using RT information), doing just the photo-heating [for more general cooling physics, enable COOLING]
#RT_FIRE_FIX_SPECTRAL_SHAPE             # enable with GALSF_FB_FIRE_RT_LONGRANGE to use a fixed SED shape set in parameterfile for all incident fluxes
#RT_DISABLE_UV_BACKGROUND               # disable extenal UV background in cooling functions (to isolate pure effects of local RT, or if simulating the background directly)
#RT_INJECT_PHOTONS_DISCRETELY           # do photon injection in discrete packets, instead of sharing a continuous source function. works better with adaptive timestepping (default with GALSF)
####################################################################################################



####################################################################################################
# --------------------------------------- Multi-Threading and Parallelization options
####################################################################################################
#OPENMP=2                       # Masterswitch for explicit OpenMP implementation
#PTHREADS_NUM_THREADS=4         # custom PTHREADs implementation (don't enable with OPENMP)
#MULTIPLEDOMAINS=16             # Multi-Domain option for the top-tree level (alters load-balancing)
####################################################################################################



####################################################################################################
# --------------------------------------- Input/Output options
####################################################################################################
#OUTPUT_ADDITIONAL_RUNINFO      # enables extended simulation output data (can slow down machines significantly in massively-parallel runs)
#OUTPUT_IN_DOUBLEPRECISION      # snapshot files will be written in double precision
#INPUT_IN_DOUBLEPRECISION       # input files assumed to be in double precision (otherwise float is assumed)
#OUTPUT_POSITIONS_IN_DOUBLE     # input/output files in single, but positions in double (used in hires, hi-dynamic range sims when positions differ by < float accuracy)
#INPUT_POSITIONS_IN_DOUBLE      # as above, but specific to the ICs file
#OUTPUT_POTENTIAL               # forces code to compute+output potentials in snapshots
#OUTPUT_TIDAL_TENSOR            # writes tidal tensor (computed in gravity) to snapshots
#OUTPUT_ACCELERATION            # output physical acceleration of each particle in snapshots
#OUTPUT_CHANGEOFENERGY          # outputs rate-of-change of internal energy of gas particles in snapshots
#OUTPUT_VORTICITY               # outputs the vorticity vector
#OUTPUT_TIMESTEP                # outputs timesteps for each particle
#OUTPUT_COOLRATE                # outputs cooling rate, and conduction rate if enabled
#OUTPUT_COOLRATE_DETAIL         # outputs cooling rate term by term [saves all individually to snapshot]
#OUTPUT_LINEOFSIGHT				# enables on-the-fly output of Ly-alpha absorption spectra. requires METALS and COOLING.
#OUTPUT_LINEOFSIGHT_SPECTRUM    # computes power spectrum of these (requires additional code integration)
#OUTPUT_LINEOFSIGHT_PARTICLES   # computes power spectrum of these (requires additional code integration)
#OUTPUT_POWERSPEC               # compute and output cosmological power spectra. requires BOX_PERIODIC and PMGRID.
#OUTPUT_RECOMPUTE_POTENTIAL     # update potential every output even it EVALPOTENTIAL is set
#OUTPUT_DENS_AROUND_STAR        # output gas density in neighborhood of stars [collisionless particle types], not just gas
#OUTPUT_DELAY_TIME_HII          # output DelayTimeHII. Requires CHIMES_HII_REGIONS or GALSF_FB_FIRE_RT_HIIHEATING (and corresponding flags/permissions set)
#OUTPUT_MOLECULAR_FRACTION      # output the code-estimated molecular mass fraction [needs COOLING], for e.g. approximate molecular fraction estimators (as opposed to detailed chemistry modules, which already output this)
#INPUT_READ_HSML                # force reading hsml from IC file (instead of re-computing them; in general this is redundant but useful if special guesses needed)
#OUTPUT_TWOPOINT_ENABLED        # allows user to calculate mass 2-point function by enabling and setting restartflag=5
#IO_DISABLE_HDF5                # disable HDF5 I/O support (for both reading/writing; use only if HDF5 not install-able)
#IO_COMPRESS_HDF5     		    # write HDF5 in compressed form (will slow down snapshot I/O and may cause issues on old machines, but reduce snapshots 2x)
#IO_SUPPRESS_TIMEBIN_STDOUT=10  # only prints timebin-list to log file if highest active timebin index is within N (value set) of the highest timebin (dt_bin=2^(-N)*dt_bin,max)
#IO_SUBFIND_IN_OLD_ASCII_FORMAT # write sub-find outputs in the old massive ascii-table format (unweildy and can cause lots of filesystem issues, but here for backwards compatibility)
#IO_SUBFIND_READFOF_FROMIC      # try read already existing FOF files associated with a run instead of recomputing them: not de-bugged
#IO_TURB_DIFF_DYNAMIC_ERROR     # save error terms from localized dynamic Smagorinsky model to snapshots
####################################################################################################



####################################################################################################
# -------------------------------------------- De-Bugging & special (usually test-problem only) behaviors
####################################################################################################
# --------------------
# ----- General De-Bugging and Special Behaviors
#DEVELOPER_MODE                 # allows you to modify various numerical parameters (courant factor, etc) at run-time
#LONG_INTEGER_TIME              # total number of integer time step = 1<<39
#FORCE_EQUAL_TIMESTEPS          # force the code to use a single universal timestep (can change in time, but all particles advance together). chosen as minimum of any particle that step.
#STOP_WHEN_BELOW_MINTIMESTEP    # forces code to quit when stepsize wants to go below MinSizeTimestep specified in the parameterfile
#DEBUG                          # enables core-dumps and FPU exceptions
# --------------------
# ----- Hydrodynamics
#FREEZE_HYDRO                   # zeros all fluxes from RP and doesn't let particles move (for testing additional physics layers)
#EOS_ENFORCE_ADIABAT=(1.0)      # if set, this forces gas to lie -exactly- along the adiabat P=EOS_ENFORCE_ADIABAT*(rho^GAMMA)
#HYDRO_REPLACE_RIEMANN_KT       # replaces the hydro Riemann solver (HLLC) with a Kurganov-Tadmor flux derived in Panuelos, Wadsley, and Kevlahan, 2019. works with MFM/MFV/fixed-grid methods [-without- MHD active, but other modules are fine]. more diffusive, but smoother, and more stable convergence results
#SLOPE_LIMITER_TOLERANCE=1      # sets the slope-limiters used. higher=more aggressive (less diffusive, but less stable). 1=default. 0=conservative. use on problems where sharp density contrasts in poor particle arrangement may cause errors. 2=same as AGGRESSIVE_SLOPE_LIMITERS below
#AGGRESSIVE_SLOPE_LIMITERS      # use the original GIZMO paper (more aggressive) slope-limiters. more accurate for smooth problems, but
                                # these can introduce numerical instability in problems with poorly-resolved large noise or density contrasts (e.g. multi-phase, self-gravitating flows)
#HYDRO_RIEMANN_KT_UNLIMITED     # removes the limiter otherwise used to reduce dissipation in the Kurganov-Tadmor flux : more diffusive but smoother solutions
#ENERGY_ENTROPY_SWITCH_IS_ACTIVE # enable energy-entropy switch as described in GIZMO methods paper. This can greatly improve performance on some problems where the
                                # the flow is very cold and highly super-sonic. it can cause problems in multi-phase flows with strong cooling, though, and is not compatible with non-barytropic equations of state
#FORCE_ENTROPIC_EOS_BELOW=(0.01) # set (manually) the alternative energy-entropy switch which is enabled by default in MFM/MFV: if relative velocities are below this threshold, it uses the entropic EOS
#EOS_GMC_BAROTROPIC             # Barotropic EOS calibratied to Masunaga & Inutsuka 2000; useful for test problems in small-scale star formation such as cloud collapse, jet launching. See Federrath et al. 2014ApJ...790..128F. Can also set to a numerical value =1 to instead use EOS used in Bate Bonnell & Bromm 2003
#DISABLE_SPH_PARTICLE_WAKEUP    # don't let gas particles move to lower timesteps based on neighbor activity (use for debugging)
#DO_UPWIND_TIME_CENTERING       # this (and DO_HALFSTEP_FOR_MESHLESS_METHODS) use alternative methods for up-winding the fluxes in the MFM/MFV schemes. this up-weighting can be more accurate in hydrostatic problems with a large sound-speed discontinuity -if- the pressure gradient is steady-state, but if they are moving or unstable, it is less accurate (and can suppress mixing)
# --------------------
# ----- Additional Fluid Physics and Gravity
#COOLING_OPERATOR_SPLIT         # do the hydro heating/cooling in operator-split fashion from chemical/radiative. slightly more accurate when tcool >> tdyn, but much noisier when tcool << tdyn
#COOL_LOWTEMP_THIN_ONLY         # in the COOL_LOW_TEMPERATURES module, treat low-temperature cooling as optically-thin instead of interpolating between optically-thin and -thick regimes
#MHD_ALTERNATIVE_LEAPFROG_SCHEME # use alternative leapfrog where magnetic fields are treated like potential/positions (per Federico Stasyszyn's suggestion): still testing
#SUPER_TIMESTEP_DIFFUSION       # use super-timestepping to accelerate integration of diffusion operators [for testing or if there are stability concerns]
#EVALPOTENTIAL                  # computes gravitational potential
#GRAVITY_HYBRID_OPENING_CRIT    # use -both- Barnes-Hut + relative angle opening criterion for the gravity tree (normally choose one or the other)
#TIDAL_TIMESTEP_CRITERION       # replace standard acceleration-based timestep criterion with one based on the tidal tensor norm, which is more accurate and adaptive (testing, but may be promoted to default code)
#BH_WAKEUP_GAS                  # force all gas within the interaction radius of a BH/sink particle to timestep at the same rate (set to lowest timebin of any of the interacting neighbors)
# --------------------
# ----- Particle IDs
#TEST_FOR_IDUNIQUENESS          # explicitly check if particles have unique id numbers (only use for special behaviors)
#LONGIDS                        # use long ints for IDs (needed for super-large simulations)
#ASSIGN_NEW_IDS                 # assign IDs on startup instead of reading from ICs
#NO_CHILD_IDS_IN_ICS            # IC file does not have child IDs: do not read them (used for compatibility with snapshot restarts from old versions of the code)
# --------------------
# ----- Particle Merging/Splitting/Deletion/Boundaries
#PREVENT_PARTICLE_MERGE_SPLIT   # don't allow gas particle splitting/merging operations
#PARTICLE_EXCISION              # enable dynamical excision (remove particles within some radius)
#MERGESPLIT_HARDCODE_MAX_MASS=(1.0e-6)   # manually set maximum mass for particle merge-split operations (in code units): useful for snapshot restarts and other special circumstances
#MERGESPLIT_HARDCODE_MIN_MASS=(1.0e-7)   # manually set minimum mass for particle merge-split operations (in code units): useful for snapshot restarts and other special circumstances
#BH_DEBUG_FIX_MASS              # does not allow BH/sink [type=5] particles to change their mass during run, from accretion/merging/swallowing
# --------------------
# ----- MPI & Parallel-FFTW De-Bugging
#USE_MPI_IN_PLACE               # MPI debugging: makes AllGatherV compatible with MPI_IN_PLACE definitions in some MPI libraries
#NO_ISEND_IRECV_IN_DOMAIN       # MPI debugging: slower, but fixes memory errors during exchange in the domain decomposition (ANY RUN with >2e9 particles MUST SET THIS OR FAIL!)
#FIX_PATHSCALE_MPI_STATUS_IGNORE_BUG # MPI debugging
#MPISENDRECV_SIZELIMIT=100      # MPI debugging
#MPISENDRECV_CHECKSUM           # MPI debugging
#DONOTUSENODELIST               # MPI debugging
#NOTYPEPREFIX_FFTW              # FFTW debugging (fftw-header/libraries accessed without type prefix, adopting whatever was
                                #   chosen as default at compile of fftw). Otherwise, the type prefix 'd' for double is used.
#USE_FFTW3                      # enables FFTW3 (can be used with DOUBLEPRECISION_FFTW). Thanks to Takashi Okamoto.
#DOUBLEPRECISION_FFTW           # FFTW in double precision to match libraries
#DISABLE_ALIGNED_ALLOC          # disable calls to 'aligned_alloc', needed for older C99-only versions of GCC compilers [everything C11+ -should- be compatible and not need this]
# --------------------
# ----- Load-Balancing
#ALLOW_IMBALANCED_GASPARTICLELOAD # increases All.MaxPartSph to All.MaxPart: can allow better load-balancing in some cases, but uses more memory. But use me if you run into errors where it can't fit the domain (where you would increase PartAllocFac, but can't for some reason)
#SEPARATE_STELLARDOMAINDECOMP   # separate stars (ptype=4) and other non-gas particles in domain decomposition (may help load-balancing)
####################################################################################################





####################################################################################################-
####################################################################################################-
##-
##- LEGACY CODE & PARTIALLY-IMPLEMENTED FEATURES: BEWARE EVERYTHING BELOW THIS LINE !!!
##-
####################################################################################################-
####################################################################################################-



####################################################################################################-
##----------------------------------------------------------------------------------------------------
#-------------------------------------- Non-Equilibrium Chemical Networks (includes novel cooling modules but also chemical networks and solvers)
#-------------------------- This is the CHIMES network developed by A. Richings. The module is proprietary at the moment and users should request permission from A. Richings
####################################################################################################-
#CHIMES                         #- enable CHIMES: cooling & chemistry package. Requires COOLING above. Also, requires COOL_METAL_LINES_BY_SPECIES to include metals.
#CHIMES_HYDROGEN_ONLY           #- Hydrogen-only. This is ignored if METALS are also set.
#CHIMES_SOBOLEV_SHIELDING       #- Enables local self-shielding over a Sobolev-like length scale
#CHIMES_HII_REGIONS             #- Disables shielding withing HII region (requires FIRE modules for radiation transport/coupling: uses GALSF_FB_FIRE_RT_HIIHEATING, and permissions follow those modules)
#CHIMES_STELLAR_FLUXES          #- Couple UV fluxes from the luminosity tree to CHIMES (requires FIRE modules for radiation transport/coupling: use permissions follow those modules)
#CHIMES_REDUCED_OUTPUT          #- Full CHIMES abundance array only output in some snapshots
#CHIMES_NH_OUTPUT               #- Write out column densities of gas particles to snapshots
#CHIMES_INITIALISE_IN_EQM       #- Initialise CHIMES abundances in equilibrium at the start of the simulation
#CHIMES_TURB_DIFF_IONS          #- Turbulent diffusions of CHIMES abundances. Requires TURB_DIFF_METALS and TURB_DIFF_METALS_LOWORDER (see modules for metal diffusion above: use/citation policy follows those)
#CHIMES_METAL_DEPLETION         #- Uses density-dependent metal depletion factors (Jenkins 2009, De Cia et al. 2016)
####################################################################################################-



####################################################################################################-
#--------------------------------------- nuclear network
#-------------------------------- (these are currently non-functional and should not be used)
####################################################################################################-
#NUCLEAR_NETWORK
#EOS_NSPECIES                   #- must be set BY HAND to match the number in the species tables being read by the subroutines
#NUCLEARNET_NEGLECT_DTDY_TERMS
#NUCLEARNET_OUTPUT_TIMEEVOLUTION
####################################################################################################-

####################################################################################################-
#BH_DEBUG_SPAWN_JET_TEST        #- BH outflow/particle spawn in jet (currently testing/early-dev, doesn't work for general problems! units hardcoded!)
####################################################################################################-

####################################################################################################-
#PIC_MHD                        #- hybrid MHD-PIC simulations for cosmic rays (particle type=3). need to set 'subtype'. in early testing.
#PIC_SPEEDOFLIGHT_REDUCTION=1   #- factor to reduce the speed-of-light for mhd-pic simulations.
####################################################################################################-


####################################################################################################-
##----------------------------------------------------------------------------------------------------
#--------------------------------------- on the fly analysis of phase structure and potentials in DM simulations
#---------------------------------------  (this code works, but requires additional modules not part of GIZMO but from GADGET-3,
#---------------------------------------   where they were developed. users who wish to incorporate a new version/fix this are welcome)
##----------------------------------------------------------------------------------------------------
#------------------------------- Fine-grained phase space structure analysis (M. Vogelsberger)
#-------------------------------- use of these routines requires explicit pre-approval by developer M. Vogelsberger
#GDE_DISTORTIONTENSOR           #- main switch: integrate phase-space distortion tensor
#GDE_TYPES=2+4+8+16+32          #- track GDE for these types
#GDE_READIC                     #- read initial sheet orientation/initial density/initial caustic count from ICs
#GDE_LEAN                       #- lean version of GDE
#OUTPUT_GDE_DISTORTIONTENSOR    #- write phase-space distortion tensor to snapshot
#OUTPUT_GDE_LASTCAUSTIC         #- write info on last passed caustic to snapshot
####################################################################################################-


