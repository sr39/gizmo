#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <mpi.h>
#include <gsl/gsl_sf_gamma.h>

#include "allvars.h"
#include "proto.h"


/*! \file init.c
 *  \brief code for initialisation of a simulation from initial conditions
 */
/*
 * This file was originally part of the GADGET3 code developed by
 * Volker Springel (volker.springel@h-its.org). The code has been modified
 * in part by Phil Hopkins (phopkins@caltech.edu) for GIZMO (mostly initializing 
 * new/modified variables, as needed)
 */

/*! This function reads the initial conditions, and allocates storage for the
 *  tree(s). Various variables of the particle data are initialised and An
 *  intial domain decomposition is performed. If SPH particles are present,
 *  the initial gas kernel lengths are determined.
 */
void init(void)
{
    int i, j;
    double a3, atime;
    
#ifdef MAGNETIC
    double a2_fac;
    double gauss2gizmo = All.UnitMagneticField_in_gauss / sqrt(4.*M_PI*All.UnitPressure_in_cgs);
    /* NOTE: we will always work -internally- in code units where MU_0 = 1; hence the 4pi here;
        [much simpler, but be sure of your conversions!] */
#endif
    
#ifdef BLACK_HOLES
    int count_holes = 0;
#endif
    
#ifdef DISTORTIONTENSORPS
    int i1, i2;
#endif
    
    All.Time = All.TimeBegin;
    set_cosmo_factors_for_current_time();
    
    
    if(RestartFlag == 3 && RestartSnapNum < 0)
    {
        if(ThisTask == 0)
            printf("Need to give the snapshot number if FOF/SUBFIND is selected for output\n");
        endrun(0);
    }
    
    if(RestartFlag == 4 && RestartSnapNum < 0)
    {
        if(ThisTask == 0)
            printf("Need to give the snapshot number if snapshot should be converted\n");
        endrun(0);
    }
    
    if(RestartFlag == 5 && RestartSnapNum < 0)
    {
        if(ThisTask == 0)
            printf
            ("Need to give the snapshot number if power spectrum and two-point correlation function should be calculated\n");
        endrun(0);
    }
    
    if(RestartFlag == 6 && RestartSnapNum < 0)
    {
        if(ThisTask == 0)
            printf
            ("Need to give the snapshot number if velocity power spectrum for the gas cells should be calculated\n");
        endrun(0);
    }
    
    
    switch (All.ICFormat)
    {
        case 1:
        case 2:
        case 3:
        case 4:
            if(RestartFlag >= 2 && RestartSnapNum >= 0)
            {
                char fname[1000];
                
                if(All.NumFilesPerSnapshot > 1)
                    sprintf(fname, "%s/snapdir_%03d/%s_%03d", All.OutputDir, RestartSnapNum, All.SnapshotFileBase,
                            RestartSnapNum);
                else
                    sprintf(fname, "%s%s_%03d", All.OutputDir, All.SnapshotFileBase, RestartSnapNum);
                
                read_ic(fname);
                
            }
            else
            {
                read_ic(All.InitCondFile);
            }
            break;
            
        default:
            if(ThisTask == 0)
                printf("ICFormat=%d not supported.\n", All.ICFormat);
            endrun(0);
    }
    
    All.Time = All.TimeBegin;
    set_cosmo_factors_for_current_time();
    
#ifdef SCFPOTENTIAL
    if(ThisTask == 0)
    {
        printf("Init SCF...\n");
        fflush(stdout);
    }
    SCF_init();
    if(ThisTask == 0)
    {
        printf("Initial random seed = %ld\n", scf_seed);
        printf("done.\n");
        fflush(stdout);
    }
#endif
    
    
#ifdef COOLING
    IonizeParams();
#endif
    
    if(All.ComovingIntegrationOn)
    {
        All.Timebase_interval = (log(All.TimeMax) - log(All.TimeBegin)) / TIMEBASE;
        All.Ti_Current = 0;
        a3 = All.Time * All.Time * All.Time;
        atime = All.Time;
#ifdef MAGNETIC
        a2_fac = (All.Time * All.Time);
#endif
    }
    else
    {
        All.Timebase_interval = (All.TimeMax - All.TimeBegin) / TIMEBASE;
        All.Ti_Current = 0;
        a3 = 1;
        atime = 1;
#ifdef MAGNETIC
        a2_fac = 1;
#endif
    }
    
#ifdef RADTRANSFER
    All.Radiation_Ti_begstep = 0;
#endif
    
    set_softenings();
    
    All.NumCurrentTiStep = 0;	/* setup some counters */
    All.SnapshotFileCount = 0;
    if(RestartFlag == 2)
    {
        if(RestartSnapNum < 0)
        {
            char *underscore = strrchr(All.InitCondFile, '_');
            if(!underscore)
            {
                char buf[1000];
                sprintf(buf, "Your input file '%s' lacks an underscore. Cannot infer next snapshot number.\n",
                        All.InitCondFile);
                terminate(buf);
            }
            else
                All.SnapshotFileCount = atoi(underscore + 1) + 1;
        }
        else
            All.SnapshotFileCount = RestartSnapNum + 1;
    }
    
#ifdef OUTPUTLINEOFSIGHT
    All.Ti_nextlineofsight = (int) (log(All.TimeFirstLineOfSight / All.TimeBegin) / All.Timebase_interval);
    if(RestartFlag == 2)
        endrun(78787);
#endif
    
    All.TotNumOfForces = 0;
    All.TopNodeAllocFactor = 0.008; /* this will start from a low value and be iteratively increased until it is well-behaved */
    All.TreeAllocFactor = 0.45; /* this will also iteratively increase to fit the particle distribution */
    /* To construct the BH-tree for N particles, somewhat less than N
     internal tree-nodes are necessary for ‘normal’ particle distributions. 
     TreeAllocFactor sets the number of internal tree-nodes allocated in units of the particle number. 
     By experience, space for ≃ 0.65N internal nodes is usually fully sufficient for typical clustered 
     particle distributions, so a value of 0.7 should put you on the safe side. If the employed particle 
     number per processor is very small (less than a thousand or so), or if there are many particle pairs 
     with identical or nearly identical coordinates, a higher value may be required. Since the number of 
     particles on a given processor may be higher by a factor PartAllocFactor than the average particle 
     number, the total amount of memory requested for the BH tree on a single processor scales proportional 
     to PartAllocFactor*TreeAllocFactor. */
    
    
    
    
    if(All.ComovingIntegrationOn)
        if(All.PeriodicBoundariesOn == 1)
            check_omega();
    
    All.TimeLastStatistics = All.TimeBegin - All.TimeBetStatistics;
#if defined(BLACK_HOLES) || defined(GALSF_SUBGRID_VARIABLEVELOCITY)
    All.TimeNextOnTheFlyFoF = All.TimeBegin;
#endif
    
    for(i = 0; i < GRAVCOSTLEVELS; i++)
        All.LevelToTimeBin[i] = 0;
    
    for(i = 0; i < NumPart; i++)
        for(j = 0; j < GRAVCOSTLEVELS; j++)
            P[i].GravCost[j] = 0;
    
#ifdef BUBBLES
    if(All.ComovingIntegrationOn)
        All.TimeOfNextBubble = 1. / (1. + All.FirstBubbleRedshift);
    else
        All.TimeOfNextBubble = All.TimeBegin + All.BubbleTimeInterval / All.UnitTime_in_Megayears;
    if(ThisTask == 0)
        printf("Initial time: %g and first bubble time %g \n", All.TimeBegin, All.TimeOfNextBubble);
    
    if(RestartFlag == 2 && All.TimeBegin > All.TimeOfNextBubble)
    {
        printf("Restarting from the snapshot file with the wrong FirstBubbleRedshift! \n");
        endrun(0);
    }
#endif
    
#ifdef MULTI_BUBBLES
    if(All.ComovingIntegrationOn)
        All.TimeOfNextBubble = 1. / (1. + All.FirstBubbleRedshift);
    else
        All.TimeOfNextBubble = All.TimeBegin + All.BubbleTimeInterval / All.UnitTime_in_Megayears;
    if(ThisTask == 0)
        printf("Initial time: %g and time of the first bubbles %g \n", All.TimeBegin, All.TimeOfNextBubble);
    if(RestartFlag == 2 && All.TimeBegin > All.TimeOfNextBubble)
    {
        printf("Restarting from the snapshot file with the wrong FirstBubbleRedshift! \n");
        endrun(0);
    }
#endif
    
    if(All.ComovingIntegrationOn)	/*  change to new velocity variable */
    {
        for(i = 0; i < NumPart; i++)
            for(j = 0; j < 3; j++)
	    {
                P[i].Vel[j] *= sqrt(All.Time) * All.Time;
	    }
    }
    
#ifdef SIDM
    init_self_interactions();
#endif
    
    for(i = 0; i < NumPart; i++)	/*  start-up initialization */
    {
        for(j = 0; j < 3; j++)
            P[i].GravAccel[j] = 0;
        
        /* DISTORTION PARTICLE SETUP */
#ifdef DISTORTIONTENSORPS
        /*init tidal tensor for first output (not used for calculation) */
        for(i1 = 0; i1 < 3; i1++)
            for(i2 = 0; i2 < 3; i2++)
                P[i].tidal_tensorps[i1][i2] = 0.0;
        
        /* find caustics by sign analysis of configuration space distortion */
        P[i].last_determinant = 1.0;
        
#ifdef OUTPUT_LAST_CAUSTIC
        /* all entries zero -> no caustic yet */
        P[i].lc_Time = 0.0;
        P[i].lc_Pos[0] = 0.0;
        P[i].lc_Pos[1] = 0.0;
        P[i].lc_Pos[2] = 0.0;
        P[i].lc_Vel[0] = 0.0;
        P[i].lc_Vel[1] = 0.0;
        P[i].lc_Vel[2] = 0.0;
        P[i].lc_rho_normed_cutoff = 0.0;
        
        P[i].lc_Dir_x[0] = 0.0;
        P[i].lc_Dir_x[1] = 0.0;
        P[i].lc_Dir_x[2] = 0.0;
        P[i].lc_Dir_y[0] = 0.0;
        P[i].lc_Dir_y[1] = 0.0;
        P[i].lc_Dir_y[2] = 0.0;
        P[i].lc_Dir_z[0] = 0.0;
        P[i].lc_Dir_z[1] = 0.0;
        P[i].lc_Dir_z[2] = 0.0;
        
        P[i].lc_smear_x = 0.0;
        P[i].lc_smear_y = 0.0;
        P[i].lc_smear_z = 0.0;
#endif
        
        
#ifdef PMGRID
        /* long range tidal field init */
        P[i].tidal_tensorpsPM[0][0] = 0;
        P[i].tidal_tensorpsPM[0][1] = 0;
        P[i].tidal_tensorpsPM[0][2] = 0;
        P[i].tidal_tensorpsPM[1][0] = 0;
        P[i].tidal_tensorpsPM[1][1] = 0;
        P[i].tidal_tensorpsPM[1][2] = 0;
        P[i].tidal_tensorpsPM[2][0] = 0;
        P[i].tidal_tensorpsPM[2][1] = 0;
        P[i].tidal_tensorpsPM[2][2] = 0;
#endif
        
        for(i1 = 0; i1 < 6; i1++)
            for(i2 = 0; i2 < 6; i2++)
            {
                if((i1 == i2))
                    P[i].distortion_tensorps[i1][i2] = 1.0;
                else
                    P[i].distortion_tensorps[i1][i2] = 0.0;
            }
        
        /* for cosmological simulations we do init here, not read from ICs */
        if(All.ComovingIntegrationOn)
        {
#ifndef GDE_READIC
            /* no caustic passages in the beginning */
            P[i].caustic_counter = 0.0;
#ifndef GDE_LEAN
            /* Lagrange time of particle */
            P[i].a0 = All.TimeBegin;
            /* approximation: perfect Hubble Flow -> peculiar sheet orientation is exactly zero */
            for(i1 = 0; i1 < 3; i1++)
                for(i2 = 0; i2 < 3; i2++)
                    GDE_VMATRIX(i,i1,i2) = 0.0;
            /* approximation: initial sream density equals background density */
            P[i].init_density = All.Omega0 * 3 * All.Hubble * All.Hubble / (8 * M_PI * All.G);
#else
            All.GDEInitStreamDensity = All.Omega0 * 3 * All.Hubble * All.Hubble / (8 * M_PI * All.G);
#endif
#endif
        }
        
#ifndef GDE_LEAN
        /* annihilation stuff */
        P[i].s_1_last = 1.0;
        P[i].s_2_last = 1.0;
        P[i].s_3_last = 1.0;
        P[i].second_deriv_last = 0.0;
        P[i].rho_normed_cutoff_last = 1.0;
        
        P[i].s_1_current = 1.0;
        P[i].s_2_current = 1.0;
        P[i].s_3_current = 1.0;
        P[i].second_deriv_current = 0.0;
        P[i].rho_normed_cutoff_current = 1.0;
        
        P[i].annihilation = 0.0;
        P[i].analytic_caustics = 0.0;
        P[i].analytic_annihilation = 0.0;
#endif
        
        if(All.ComovingIntegrationOn)
            P[i].stream_density = GDE_INITDENSITY(i) / (All.TimeBegin * All.TimeBegin * All.TimeBegin);
        else
            P[i].stream_density = GDE_INITDENSITY(i);
        
#endif /* DISTORTIONTENSORPS */
        
#ifdef KEEP_DM_HSML_AS_GUESS
        if(RestartFlag != 1)
            P[i].DM_Hsml = -1;
#endif
        
#ifdef PMGRID
        for(j = 0; j < 3; j++)
            P[i].GravPM[j] = 0;
#endif
        P[i].Ti_begstep = 0;
        P[i].Ti_current = 0;
        P[i].TimeBin = 0;
        
        if(header.flag_ic_info != FLAG_SECOND_ORDER_ICS)
            P[i].OldAcc = 0;	/* Do not zero in 2lpt case as masses are stored here */
        
#if defined(EVALPOTENTIAL) || defined(COMPUTE_POTENTIAL_ENERGY)
        P[i].Potential = 0;
#endif
#ifdef GALSF
        if(RestartFlag == 0)
        {
            P[i].StellarAge = 0;
#ifdef GALSF_SFR_IMF_VARIATION
            P[i].IMF_Mturnover = 2.0;
#endif
        }
#endif
        
#if defined(GALSF_FB_GASRETURN) || defined(GALSF_FB_HII_HEATING) || defined(GALSF_FB_SNE_HEATING) || defined(GALSF_FB_RT_PHOTON_LOCALATTEN )
        if(RestartFlag != 1)
        {
            P[i].DensAroundStar = 0;
#ifdef GALSF_FB_RT_PHOTON_LOCALATTEN
            P[i].GradRho[0]=0;
            P[i].GradRho[1]=0;
            P[i].GradRho[2]=1;
#endif
#ifdef GALSF_FB_SNE_HEATING
            P[i].SNe_ThisTimeStep = 0;
            P[i].Area_weighted_sum = 0;
#endif
#ifdef GALSF_FB_GASRETURN
            P[i].MassReturn_ThisTimeStep = 0;
#endif
#ifdef GALSF_FB_RPROCESS_ENRICHMENT
            P[i].RProcessEvent_ThisTimeStep = 0;
#endif
        }
#endif
        
#if defined(GALSF_FB_GASRETURN) || defined(GALSF_FB_RPWIND_LOCAL) || defined(GALSF_FB_HII_HEATING) || defined(GALSF_FB_SNE_HEATING) || defined(GALSF_FB_RT_PHOTONMOMENTUM)
        if(RestartFlag == 0)
        {
            P[i].StellarAge = -2.0 * All.InitStellarAgeinGyr / (All.UnitTime_in_Megayears*0.001) * get_random_number(P[i].ID + 3);
        }
#endif
        
#ifdef GRAIN_FLUID
        if(RestartFlag == 0)
        {
            P[i].Grain_Size = All.Grain_Size_Min * exp( gsl_rng_uniform(random_generator) * log(All.Grain_Size_Max/All.Grain_Size_Min) );
            P[i].Gas_Density = 0;
            P[i].Gas_InternalEnergy = 0;
            P[i].Gas_Velocity[0]=P[i].Gas_Velocity[1]=P[i].Gas_Velocity[2]=0;
#ifdef GRAIN_COLLISIONS
            P[i].Grain_Density = 0;
            P[i].Grain_Velocity[0]=P[i].Grain_Velocity[1]=P[i].Grain_Velocity[2]=0;
#endif
#ifdef GRAIN_LORENTZFORCE
            P[i].Gas_B[0]=P[i].Gas_B[1]=P[i].Gas_B[2];
#endif
        }
#endif
        
        
        
#ifdef METALS
        All.SolarAbundances[0]=0.02;        // all metals (by mass); present photospheric abundances from Asplund et al. 2009 (Z=0.0134, proto-solar=0.0142) in notes;
                                            //   also Anders+Grevesse 1989 (older, but hugely-cited compilation; their Z=0.0201, proto-solar=0.0213)
#ifdef COOL_METAL_LINES_BY_SPECIES
        if (NUM_METAL_SPECIES>=10) {
            All.SolarAbundances[1]=0.28;    // He  (10.93 in units where log[H]=12, so photospheric mass fraction -> Y=0.2485 [Hydrogen X=0.7381]; Anders+Grevesse Y=0.2485, X=0.7314)
            All.SolarAbundances[2]=3.26e-3; // C   (8.43 -> 2.38e-3, AG=3.18e-3)
            All.SolarAbundances[3]=1.32e-3; // N   (7.83 -> 0.70e-3, AG=1.15e-3)
            All.SolarAbundances[4]=8.65e-3; // O   (8.69 -> 5.79e-3, AG=9.97e-3)
            All.SolarAbundances[5]=2.22e-3; // Ne  (7.93 -> 1.26e-3, AG=1.72e-3)
            All.SolarAbundances[6]=9.31e-4; // Mg  (7.60 -> 7.14e-4, AG=6.75e-4)
            All.SolarAbundances[7]=1.08e-3; // Si  (7.51 -> 6.71e-4, AG=7.30e-4)
            All.SolarAbundances[8]=6.44e-4; // S   (7.12 -> 3.12e-4, AG=3.80e-4)
            All.SolarAbundances[9]=1.01e-4; // Ca  (6.34 -> 0.65e-4, AG=0.67e-4)
            All.SolarAbundances[10]=1.73e-3; // Fe (7.50 -> 1.31e-3, AG=1.92e-3)
        }
#endif // COOL_METAL_LINES_BY_SPECIES
#ifdef GALSF_FB_RPROCESS_ENRICHMENT
        //All.SolarAbundances[NUM_METAL_SPECIES-1]=0.0; // R-process tracer
        for(j=1;j<=NUM_RPROCESS_SPECIES;j++) All.SolarAbundances[NUM_METAL_SPECIES-j]=0.0; // R-process tracer
#endif
        
        if(RestartFlag == 0) {
#if defined(GALSF_FB_GASRETURN) || defined(GALSF_FB_RPWIND_LOCAL) || defined(GALSF_FB_HII_HEATING) || defined(GALSF_FB_SNE_HEATING) || defined(GALSF_FB_RT_PHOTONMOMENTUM)
            P[i].Metallicity[0] = All.InitMetallicityinSolar*All.SolarAbundances[0];
#else
            P[i].Metallicity[0] = 0;
#endif
            /* initialize abundance ratios. for now, assume solar */
            for(j=0;j<NUM_METAL_SPECIES;j++) P[i].Metallicity[j]=All.SolarAbundances[j]*P[i].Metallicity[0]/All.SolarAbundances[0];
            /* need to allow for a primordial He abundance */
            if(NUM_METAL_SPECIES>=10) P[i].Metallicity[1]=0.25+(All.SolarAbundances[1]-0.25)*P[i].Metallicity[0]/All.SolarAbundances[0];
        } // if(RestartFlag == 0)
#endif // METALS
        
        
        
        
#ifdef BLACK_HOLES
        if(P[i].Type == 5)
        {
            count_holes++;
            
            if(RestartFlag == 0)
                BPP(i).BH_Mass = All.SeedBlackHoleMass;
#ifdef BH_ALPHADISK_ACCRETION
            BPP(i).BH_Mass_AlphaDisk = 0;
#endif
#ifdef BH_COUNTPROGS
            BPP(i).BH_CountProgs = 1;
#endif
#ifdef BH_BUBBLES
            if(RestartFlag == 0)
            {
                BPP(i).BH_Mass_bubbles = All.SeedBlackHoleMass;
                BPP(i).BH_Mass_ini = All.SeedBlackHoleMass;
#ifdef UNIFIED_FEEDBACK
                BPP(i).BH_Mass_radio = All.SeedBlackHoleMass;
#endif
            }
#endif
        }
#endif
    }
    
#ifdef BLACK_HOLES
    MPI_Allreduce(&count_holes, &All.TotBHs, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
#endif
    
    for(i = 0; i < TIMEBINS; i++)
        TimeBinActive[i] = 1;
    
    reconstruct_timebins();
    
#ifdef PMGRID
    All.PM_Ti_endstep = All.PM_Ti_begstep = 0;
#endif
        
    for(i = 0; i < N_gas; i++)	/* initialize sph_properties */
    {
        SphP[i].InternalEnergyPred = SphP[i].InternalEnergy;
        
        for(j = 0; j < 3; j++)
        {
            SphP[i].VelPred[j] = P[i].Vel[j];
            SphP[i].HydroAccel[j] = 0;
            //SphP[i].dMomentum[j] = 0;//manifest-indiv-timestep-debug//
        }
        
        //SphP[i].dInternalEnergy = 0;//manifest-indiv-timestep-debug//
        P[i].Particle_DivVel = 0;
        SphP[i].ConditionNumber = 1;
        SphP[i].DtInternalEnergy = 0;
#ifdef ENERGY_ENTROPY_SWITCH_IS_ACTIVE
        SphP[i].MaxKineticEnergyNgb = 0;
#endif
#ifdef HYDRO_MESHLESS_FINITE_VOLUME
        SphP[i].dMass = 0;
        SphP[i].DtMass = 0;
        SphP[i].MassTrue = P[i].Mass;
        for(j=0;j<3;j++) SphP[i].GravWorkTerm[j] = 0;
#endif
        
#if defined(ADAPTIVE_GRAVSOFT_FORGAS) || defined(ADAPTIVE_GRAVSOFT_FORALL)
        PPPZ[i].AGS_zeta = 0;
#endif
#ifdef EOS_DEGENERATE
        for(j = 0; j < 3; j++)
            SphP[i].xnucPred[j] = SphP[i].xnuc[j];
#endif
        
#ifdef CONDUCTION
        SphP[i].Kappa_Conduction = 0;
#endif
#ifdef VISCOSITY
        SphP[i].Eta_ShearViscosity = 0;
        SphP[i].Zeta_BulkViscosity = 0;
#endif
        
        
#ifdef TURB_DIFFUSION
        SphP[i].TD_DiffCoeff = 0;
#endif
        
        if(RestartFlag == 0)
        {
#ifndef READ_HSML
            PPP[i].Hsml = 0;
#endif
            SphP[i].Density = -1;
#ifdef COOLING
            SphP[i].Ne = 1.0;
#endif
#ifdef GALSF_FB_LOCAL_UV_HEATING
            SphP[i].RadFluxUV = 0;
            SphP[i].RadFluxEUV = 0;
#endif
#ifdef BH_COMPTON_HEATING
            SphP[i].RadFluxAGN = 0;
#endif
        }
#ifdef GALSF_SUBGRID_WINDS
        if(RestartFlag == 0)
            SphP[i].DelayTime = 0;
#ifdef GALSF_SUBGRID_VARIABLEVELOCITY
        SphP[i].HostHaloMass = 0;
#endif
#endif // GALSF_SUBGRID_WINDS //
#ifdef GALSF_FB_HII_HEATING
        SphP[i].DelayTimeHII = 0;
#endif
#ifdef GALSF_TURNOFF_COOLING_WINDS
        SphP[i].DelayTimeCoolingSNe = 0;
#endif
#ifdef GALSF
        SphP[i].Sfr = 0;
#endif
#ifdef COSMIC_RAYS
        SphP[i].CosmicRayEnergyPred = SphP[i].CosmicRayEnergy = 0;
        SphP[i].CosmicRayDiffusionCoeff = SphP[i].DtCosmicRayEnergy = 0;
#endif
#ifdef MAGNETIC
#if defined B_SET_IN_PARAMS
        if(RestartFlag == 0)
        {			/* Set only when starting from ICs */
            SphP[i].B[0]=SphP[i].BPred[0] = All.BiniX;
            SphP[i].B[1]=SphP[i].BPred[1] = All.BiniY;
            SphP[i].B[2]=SphP[i].BPred[2] = All.BiniZ;
        }
#endif /*B_SET_IN_PARAMS*/
        for(j = 0; j < 3; j++)
        {
            SphP[i].BPred[j] *= a2_fac * gauss2gizmo;
            SphP[i].B[j] = SphP[i].BPred[j];
        }
#if defined(TRICCO_RESISTIVITY_SWITCH)
        SphP[i].Balpha = 0.0;
#endif
#ifdef DIVBCLEANING_DEDNER
        SphP[i].Phi = SphP[i].PhiPred = SphP[i].DtPhi = 0;
#endif
#endif
#ifdef SPHAV_CD10_VISCOSITY_SWITCH
        SphP[i].alpha = 0.0;
#endif
#if defined(BH_THERMALFEEDBACK)
        SphP[i].Injected_BH_Energy = 0;
#endif
    }
    
#ifndef SHEARING_BOX
#ifdef TWODIMS
    for(i = 0; i < NumPart; i++)
    {
        P[i].Pos[2] = 0;
        P[i].Vel[2] = 0;
        
        P[i].GravAccel[2] = 0;
        
        if(P[i].Type == 0)
        {
            SphP[i].VelPred[2] = 0;
            SphP[i].HydroAccel[2] = 0;
        }
    }
#endif
#endif
    
#ifdef ONEDIM
    for(i = 0; i < NumPart; i++)
    {
        P[i].Pos[1] = P[i].Pos[2] = 0;
        P[i].Vel[1] = P[i].Vel[2] = 0;
        
        P[i].GravAccel[1] = P[i].GravAccel[2] = 0;
        
        if(P[i].Type == 0)
        {
            SphP[i].VelPred[1] = SphP[i].VelPred[2] = 0;
            SphP[i].HydroAccel[1] = SphP[i].HydroAccel[2] = 0;
        }
    }
#endif
    
#ifdef ASSIGN_NEW_IDS
    assign_unique_ids();
#endif
    
#ifdef TEST_FOR_IDUNIQUENESS
    test_id_uniqueness();
#endif
    
    Flag_FullStep = 1;		/* to ensure that Peano-Hilber order is done */
    
    TreeReconstructFlag = 1;
    
    
#ifdef SHIFT_BY_HALF_BOX
    for(i = 0; i < NumPart; i++)
        for(j = 0; j < 3; j++)
            P[i].Pos[j] += 0.5 * All.BoxSize;
#endif
    
    
    Gas_split = 0;
#ifdef GALSF
    Stars_converted = 0;
#endif
    domain_Decomposition(0, 0, 0);	/* do initial domain decomposition (gives equal numbers of particles) */
    
    set_softenings();
    
    /* will build tree */
    ngb_treebuild();
    
    All.Ti_Current = 0;
    
    if(RestartFlag != 3 && RestartFlag != 5)
        setup_smoothinglengths();
    
#ifdef ADAPTIVE_GRAVSOFT_FORALL
    if(RestartFlag != 3 && RestartFlag != 5)
        ags_setup_smoothinglengths();
#endif
    
#ifdef GALSF_SUBGRID_DMDISPERSION
    if(RestartFlag != 3 && RestartFlag != 5)
        disp_setup_smoothinglengths();
#endif
    

#if defined(TURB_DRIVING)
    {
        double mass = 0, glob_mass;
        int i;
        for(i=0; i< N_gas; i++)
            mass += P[i].Mass;
        MPI_Allreduce(&mass, &glob_mass, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
        All.RefDensity = glob_mass / pow(All.BoxSize, 3);
        All.RefInternalEnergy = All.IsoSoundSpeed*All.IsoSoundSpeed / (GAMMA*GAMMA_MINUS1);
    }
#endif
    
    /* HELLO! This here is where you should insert custom code for hard-wiring the ICs of various test problems */
    
    
    
    density();
    for(i = 0; i < N_gas; i++)	/* initialize sph_properties */
    {
#ifndef EOS_DEGENERATE
        SphP[i].InternalEnergyPred = SphP[i].InternalEnergy;
#else
        for(j = 0; j < EOS_NSPECIES; j++)
        {
            SphP[i].dxnuc[j] = 0;
        }
        SphP[i].InternalEnergy *= All.UnitEnergy_in_cgs;
        SphP[i].InternalEnergyPred *= All.UnitEnergy_in_cgs;
        /* call eos with physical units, energy and entropy are always stored in physical units */
        SphP[i].temp = -1.0;
        
        struct eos_result res;
        eos_calc_egiven(SphP[i].Density * All.UnitDensity_in_cgs, SphP[i].xnuc, SphP[i].InternalEnergy, &SphP[i].temp, &res);
        SphP[i].Pressure = res.p.v / All.UnitPressure_in_cgs;
        // Warning: dp_drho is in physical units ...
        SphP[i].dp_drho = res.p.drho + res.temp * gsl_pow_2(res.p.dtemp / (SphP[i].Density * All.UnitDensity_in_cgs)) / res.e.dtemp;
#endif
        
#if defined(TURB_DRIVING)
        SphP[i].InternalEnergy = All.RefInternalEnergy;
        SphP[i].InternalEnergyPred = All.RefInternalEnergy;
#endif
        // re-match the predicted and initial velocities and B-field values, just to be sure //
        for(j=0;j<3;j++) SphP[i].VelPred[j]=P[i].Vel[j];
#ifdef MAGNETIC
        for(j=0;j<3;j++) {SphP[i].B[j] = SphP[i].BPred[j] * P[i].Mass / SphP[i].Density;} // convert to the conserved unit V*B //
        for(j=0;j<3;j++) {SphP[i].BPred[j]=SphP[i].B[j]; SphP[i].DtB[j]=0;}
#endif
        //SphP[i].dInternalEnergy = 0;//manifest-indiv-timestep-debug//
        SphP[i].DtInternalEnergy = 0;
#ifdef HYDRO_MESHLESS_FINITE_VOLUME
        SphP[i].dMass = 0;
        SphP[i].DtMass = 0;
        SphP[i].MassTrue = P[i].Mass;
        for(j=0;j<3;j++) SphP[i].GravWorkTerm[j] = 0;
#endif
#if defined(ADAPTIVE_GRAVSOFT_FORGAS) || defined(ADAPTIVE_GRAVSOFT_FORALL)
        PPPZ[i].AGS_zeta = 0;
#endif
#ifdef GALSF_FB_LOCAL_UV_HEATING
        SphP[i].RadFluxUV = 0;
        SphP[i].RadFluxEUV = 0;
#endif
#ifdef BH_COMPTON_HEATING
        SphP[i].RadFluxAGN = 0;
#endif
        
        
#ifdef GRACKLE
        if(RestartFlag == 0)
        {
#if (GRACKLE_CHEMISTRY >= 1)
            SphP[i].grHI    = HYDROGEN_MASSFRAC;
            SphP[i].grHII   = 1.0e-20;
            SphP[i].grHM    = 1.0e-20;
            SphP[i].grHeI   = 1.0 - HYDROGEN_MASSFRAC;
            SphP[i].grHeII  = 1.0e-20;
            SphP[i].grHeIII = 1.0e-20;
#endif
#if (GRACKLE_CHEMISTRY >= 2)
            SphP[i].grH2I   = 1.0e-20;
            SphP[i].grH2II  = 1.0e-20;
#endif
#if (GRACKLE_CHEMISTRY >= 3)
            SphP[i].grDI    = 2.0 * 3.4e-5;
            SphP[i].grDII   = 1.0e-20;
            SphP[i].grHDI   = 1.0e-20;
#endif
        }
#endif
        
    }
    
    
    /* we should define the maximum and minimum particle masses 
        below/above which particles are merged/split */
    if(RestartFlag != 1)
    {
        double mass_min = MAX_REAL_NUMBER;
        double mass_max = -MAX_REAL_NUMBER;
        for(i = 0; i < N_gas; i++)	/* initialize sph_properties */
        {
            if(P[i].Mass > mass_max) mass_max = P[i].Mass;
            if(P[i].Mass < mass_min) mass_min = P[i].Mass;
        }
        /* broadcast this and get the min and max values over all processors */
        double mpi_mass_min,mpi_mass_max;
        MPI_Allreduce(&mass_min, &mpi_mass_min, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
        MPI_Allreduce(&mass_max, &mpi_mass_max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
        All.MinMassForParticleMerger = 0.50 * mpi_mass_min;
        All.MaxMassForParticleSplit  = 5.00 * mpi_mass_max;
    }
    
    
    if(RestartFlag == 3)
    {
#ifdef SUBFIND_RESHUFFLE_AND_POTENTIAL
        if(ThisTask == 0)
            printf("SUBFIND_RESHUFFLE_AND_POTENTIAL: Calculating potential energy before reshuffling...\n");
#ifdef PMGRID
        long_range_init_regionsize();
#endif
        compute_potential();
        if(ThisTask == 0)
            printf("potential energy done.\n");
        
#endif
        
#ifdef ADAPTIVE_GRAVSOFT_FORALL
        if(ThisTask == 0)
            printf("*ADAPTIVE_GRAVSOFT_FORALL* Computation of softening lengths... \n");
        ags_setup_smoothinglengths();
        if(ThisTask == 0)
            printf("*ADAPTIVE_GRAVSOFT_FORALL* Computation of softening lengths done. \n");
#endif
        
#ifdef FOF
        fof_fof(RestartSnapNum);
#endif
        endrun(0);
    }
    
    if(RestartFlag == 5)
    {
        /* calculating powerspec and twopoint function */
#ifdef PMGRID
        long_range_init_regionsize();
#ifdef PERIODIC
        int n, n_type[6];
        long long ntot_type_all[6];
        /* determine global and local particle numbers */
        for(n = 0; n < 6; n++)
            n_type[n] = 0;
        for(n = 0; n < NumPart; n++)
            n_type[P[n].Type]++;
        sumup_large_ints(6, n_type, ntot_type_all);
        
        calculate_power_spectra(RestartSnapNum, ntot_type_all);
#endif
#endif
        force_treebuild(NumPart, NULL);
        twopoint();
        endrun(0);
    }
    
    
    if(RestartFlag == 6)
    {
#if defined(PERIODIC) && defined(ADJ_BOX_POWERSPEC)
        adj_box_powerspec();
#endif
        endrun(0);
    }
    
    
    if(RestartFlag == 4)
    {
        All.Time = All.TimeBegin = header.time;
        sprintf(All.SnapshotFileBase, "%s_converted", All.SnapshotFileBase);
        if(ThisTask == 0)
            printf("Start writing file %s\n", All.SnapshotFileBase);
        printf("RestartSnapNum %d\n", RestartSnapNum);
        
        All.TopNodeAllocFactor = 0.008;
        
        savepositions(RestartSnapNum);
        endrun(0);
    }
}


/*! This routine computes the mass content of the box and compares it to the
 * specified value of Omega-matter.  If discrepant, the run is terminated.
 */
void check_omega(void)
{
    double mass = 0, masstot, omega;
    int i;
    
    for(i = 0; i < NumPart; i++)
        mass += P[i].Mass;
    
    MPI_Allreduce(&mass, &masstot, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    
    omega = masstot / (All.BoxSize * All.BoxSize * All.BoxSize) / (3 * All.Hubble * All.Hubble / (8 * M_PI * All.G));
#ifdef TIMEDEPGRAV
    omega *= All.Gini / All.G;
#endif
    
    //if(fabs(omega - All.Omega0) > 1.0e-3)
    // because of how we set up these ICs, allow a little more generous tolerance
    if(fabs(omega - All.Omega0) > 1.0e-2)
    {
        if(ThisTask == 0)
        {
            printf("\n\nI've found something odd!\n");
            printf
            ("The mass content accounts only for Omega=%g,\nbut you specified Omega=%g in the parameterfile.\n",
             omega, All.Omega0);
            printf("\nI better stop.\n");
            
            fflush(stdout);
        }
        endrun(1);
    }
}



/*! This function is used to find an initial kernel length (what used to be called the 
 *  'smoothing length' for SPH, but is just the kernel size for the mesh-free methods) for each gas
 *  particle. It guarantees that the number of neighbours will be between
 *  desired_ngb-MAXDEV and desired_ngb+MAXDEV. For simplicity, a first guess
 *  of the kernel length is provided to the function density(), which will
 *  then iterate if needed to find the right kernel length.
 */
void setup_smoothinglengths(void)
{
    int i, no, p;
    if((RestartFlag == 0)||(RestartFlag==2)) // best for stability if we re-calc Hsml for snapshot restarts //
    {
#if defined(DO_DENSITY_AROUND_STAR_PARTICLES) || defined(GRAIN_FLUID)
        for(i = 0; i < NumPart; i++)
#else
            for(i = 0; i < N_gas; i++)
#endif
            {
                no = Father[i];
                
                while(10 * All.DesNumNgb * P[i].Mass > Nodes[no].u.d.mass)
                {
                    p = Nodes[no].u.d.father;
                    
                    if(p < 0)
                        break;
                    
                    no = p;
                }
                
                if((RestartFlag == 0)||(P[i].Type != 0)) // if Restartflag==2, use the saved Hsml of the gas as initial guess //
                {
                    
#ifndef READ_HSML
#if NUMDIMS == 3
                    PPP[i].Hsml = pow(3.0 / (4 * M_PI) * All.DesNumNgb * P[i].Mass / Nodes[no].u.d.mass, 0.333333) * Nodes[no].len;
#endif
#if NUMDIMS == 2
                    PPP[i].Hsml = pow(1.0 / (M_PI) * All.DesNumNgb * P[i].Mass / Nodes[no].u.d.mass, 0.5) * Nodes[no].len;
#endif
#if NUMDIMS == 1
                    PPP[i].Hsml = All.DesNumNgb * (P[i].Mass / Nodes[no].u.d.mass) * Nodes[no].len;
#endif
#ifndef NOGRAVITY
                    if(All.SofteningTable[0] != 0)
                    {
                        if((PPP[i].Hsml>100.*All.SofteningTable[0])||(PPP[i].Hsml<=0.01*All.SofteningTable[0])||(Nodes[no].u.d.mass<=0)||(Nodes[no].len<=0))
                            PPP[i].Hsml = All.SofteningTable[0];
                    }
#else
                    if((Nodes[no].u.d.mass<=0)||(Nodes[no].len<=0)) PPP[i].Hsml = 1.0;
#endif
#endif // READ_HSML
                } // closes if((RestartFlag == 0)||(P[i].Type != 0))
            }
    }
    
    
#ifdef BLACK_HOLES
    if(RestartFlag == 0 || RestartFlag == 2)
    {
        for(i = 0; i < NumPart; i++)
            if(P[i].Type == 5)
                PPP[i].Hsml = All.SofteningTable[5];
    }
#endif
    
#ifdef GRAIN_FLUID
    if(RestartFlag == 0 || RestartFlag == 2)
    {
        for(i = 0; i < NumPart; i++)
            if(P[i].Type > 0)
                PPP[i].Hsml = All.SofteningTable[P[i].Type];
    }
#endif
    
#if defined(RADTRANSFER)
    if(RestartFlag == 0 || RestartFlag == 2)
    {
        for(i = 0; i < NumPart; i++)
            if(P[i].Type == 4)
                PPP[i].Hsml = All.SofteningTable[4];
    }
#endif
    
    density();    
}


void assign_unique_ids(void)
{
    int i, *numpartlist;
    MyIDType idfirst;
    
    numpartlist = (int *) mymalloc("numpartlist", NTask * sizeof(int));
    
    MPI_Allgather(&NumPart, 1, MPI_INT, numpartlist, 1, MPI_INT, MPI_COMM_WORLD);
    
    idfirst = 1;
    
    for(i = 0; i < ThisTask; i++)
        idfirst += numpartlist[i];
    
    for(i = 0; i < NumPart; i++)
    {
        P[i].ID = idfirst;
        idfirst++;
    }
    
    myfree(numpartlist);
}


#ifdef ADAPTIVE_GRAVSOFT_FORALL
void ags_setup_smoothinglengths(void)
{
    int i, no, p;
    if(RestartFlag == 0 || RestartFlag == 2)
    {
        for(i = 0; i < NumPart; i++)
        {
            P[i].Particle_DivVel = 0;
            PPPZ[i].AGS_zeta = 0;
            if(P[i].Type > 0)
            {
                no = Father[i];
                while(10 * All.AGS_DesNumNgb * P[i].Mass > Nodes[no].u.d.mass)
                {
                    p = Nodes[no].u.d.father;
                    if(p < 0)
                        break;
                    no = p;
                }
                PPP[i].Hsml = pow(1.0/NORM_COEFF * All.AGS_DesNumNgb * P[i].Mass / Nodes[no].u.d.mass, 1.0/NUMDIMS) * Nodes[no].len;
                if(All.SofteningTable[P[i].Type] != 0)
                {
                    if((PPP[i].Hsml>1000.*All.SofteningTable[P[i].Type])||(PPP[i].Hsml<=0.01*All.SofteningTable[P[i].Type])||(Nodes[no].u.d.mass<=0)||(Nodes[no].len<=0))
                        PPP[i].Hsml = All.SofteningTable[P[i].Type];
                }
            }
        }
    }
    ags_density();
}
#endif // ADAPTIVE_GRAVSOFT_FORALL


#ifdef GALSF_SUBGRID_DMDISPERSION
void disp_setup_smoothinglengths(void)
{
    int i, no, p;
    if(RestartFlag == 0 || RestartFlag == 2)
    {
        for(i = 0; i < NumPart; i++)
        {
            if(P[i].Type == 0)
            {
                no = Father[i];
                while(10 * 2.0 * 64 * P[i].Mass > Nodes[no].u.d.mass)
                {
                    p = Nodes[no].u.d.father;
                    if(p < 0)
                        break;
                    no = p;
                }
                SphP[i].HsmlDM = pow(1.0/NORM_COEFF * 2.0 * 64 * P[i].Mass / Nodes[no].u.d.mass, 1.0/NUMDIMS) * Nodes[no].len;
                if(All.SofteningTable[P[i].Type] != 0)
                {
                    if((SphP[i].HsmlDM >1000.*All.SofteningTable[P[i].Type])||(PPP[i].Hsml<=0.01*All.SofteningTable[P[i].Type])||(Nodes[no].u.d.mass<=0)||(Nodes[no].len<=0))
                        SphP[i].HsmlDM = All.SofteningTable[P[i].Type];
                }
            }
        }
    }
    
    if(ThisTask == 0)
    {
        printf("computing DM Vel_disp around gas particles.\n");
        fflush(stdout);
    }
    disp_density();
}
#endif


void test_id_uniqueness(void)
{
    double t0, t1;
#ifndef BND_PARTICLES
    int i;
    MyIDType *ids, *ids_first;
#endif
    
    if(ThisTask == 0)
    {
        printf("Testing ID uniqueness...\n");
        fflush(stdout);
    }
    
    if(NumPart == 0)
    {
        printf("need at least one particle per cpu\n");
        endrun(8);
    }
    
    t0 = my_second();
    
#ifndef BND_PARTICLES
    ids = (MyIDType *) mymalloc("ids", NumPart * sizeof(MyIDType));
    ids_first = (MyIDType *) mymalloc("ids_first", NTask * sizeof(MyIDType));
    
    for(i = 0; i < NumPart; i++)
        ids[i] = P[i].ID;
    
#ifdef ALTERNATIVE_PSORT
    init_sort_ID(ids, NumPart);
#else
    parallel_sort(ids, NumPart, sizeof(MyIDType), compare_IDs);
#endif
    
    for(i = 1; i < NumPart; i++)
        if(ids[i] == ids[i - 1])
        {
#ifdef LONGIDS
            printf("non-unique ID=%d%09d found on task=%d (i=%d NumPart=%d)\n",
                   (int) (ids[i] / 1000000000), (int) (ids[i] % 1000000000), ThisTask, i, NumPart);
            
#else
            printf("non-unique ID=%d found on task=%d   (i=%d NumPart=%d)\n", (int) ids[i], ThisTask, i, NumPart);
#endif
            endrun(12);
        }
    
    MPI_Allgather(&ids[0], sizeof(MyIDType), MPI_BYTE, ids_first, sizeof(MyIDType), MPI_BYTE, MPI_COMM_WORLD);
    
    if(ThisTask < NTask - 1)
        if(ids[NumPart - 1] == ids_first[ThisTask + 1])
        {
            printf("non-unique ID=%d found on task=%d\n", (int) ids[NumPart - 1], ThisTask);
            endrun(13);
        }
    
    myfree(ids_first);
    myfree(ids);
#endif
    
    t1 = my_second();
    
    if(ThisTask == 0)
    {
        printf("success.  took=%g sec\n", timediff(t0, t1));
        fflush(stdout);
    }
}

int compare_IDs(const void *a, const void *b)
{
    if(*((MyIDType *) a) < *((MyIDType *) b))
        return -1;
    
    if(*((MyIDType *) a) > *((MyIDType *) b))
        return +1;
    
    return 0;
}
