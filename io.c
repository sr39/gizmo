#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_HDF5
#include <hdf5.h>
#endif

#include "allvars.h"
#include "proto.h"

#ifdef JD_DPP
#include "cr_electrons.h"
#endif

/*! \file io.c
 *  \brief Output of a snapshot file to disk.
 */
/*
 * This file was originally part of the GADGET3 code developed by
 * Volker Springel (volker.springel@h-its.org). The code has been modified
 * in part by Phil Hopkins (phopkins@caltech.edu) for GIZMO (mostly to 
 * write out new/modified quantities, as needed)
 */

static int n_type[6];
static long long ntot_type_all[6];

static int n_info;

/*! This function writes a snapshot of the particle distribution to one or
 * several files using Gadget's default file format.  If
 * NumFilesPerSnapshot>1, the snapshot is distributed into several files,
 * which are written simultaneously. Each file contains data from a group of
 * processors of size roughly NTask/NumFilesPerSnapshot.
 */
void savepositions(int num)
{
  size_t bytes;
  char buf[500];
  int n, filenr, gr, ngroups, masterTask, lastTask;

  CPU_Step[CPU_MISC] += measure_time();

  rearrange_particle_sequence();
  /* ensures that new tree will be constructed */
  All.NumForcesSinceLastDomainDecomp = (long long) (1 + All.TreeDomainUpdateFrequency * All.TotNumPart);

#if defined(JD_DPP) && defined(JD_DPPONSNAPSHOTONLY)
  compute_Dpp(0);		/* Particle loop already inside, just add water */
#endif

  if(DumpFlag == 1)
    {
      if(ThisTask == 0)
	printf("\nwriting snapshot file #%d... \n", num);

#ifdef ORDER_SNAPSHOTS_BY_ID
      double t0, t1;

      if(All.TotN_gas > 0)
	{
	  if(ThisTask == 0)
	    printf
	      ("\nThe option ORDER_SNAPSHOTS_BY_ID does not work yet with gas particles, only simulations with collisionless particles are allowed.\n\n");
	  endrun(0);
	}

      t0 = my_second();
      for(n = 0; n < NumPart; n++)
	{
	  P[n].GrNr = ThisTask;
	  P[n].SubNr = n;
	}
      parallel_sort(P, NumPart, sizeof(struct particle_data), io_compare_P_ID);
      t1 = my_second();
      if(ThisTask == 0)
	printf("Reordering of particle-data in ID-sequence took = %g sec\n", timediff(t0, t1));
#endif

      if(!(CommBuffer = mymalloc("CommBuffer", bytes = All.BufferSize * 1024 * 1024)))
	{
	  printf("failed to allocate memory for `CommBuffer' (%g MB).\n", bytes / (1024.0 * 1024.0));
	  endrun(2);
	}


      if(NTask < All.NumFilesPerSnapshot)
	{
	  if(ThisTask == 0)
	    printf
	      ("Fatal error.\nNumber of processors must be larger or equal than All.NumFilesPerSnapshot.\n");
	  endrun(0);
	}
      if(All.SnapFormat < 1 || All.SnapFormat > 3)
	{
	  if(ThisTask == 0)
	    printf("Unsupported File-Format\n");
	  endrun(0);
	}
#ifndef  HAVE_HDF5
      if(All.SnapFormat == 3)
	{
	  if(ThisTask == 0)
	    printf("Code wasn't compiled with HDF5 support enabled!\n");
	  endrun(0);
	}
#endif


      /* determine global and local particle numbers */
      for(n = 0; n < 6; n++)
	n_type[n] = 0;

      for(n = 0; n < NumPart; n++)
	n_type[P[n].Type]++;

      sumup_large_ints(6, n_type, ntot_type_all);
      
      /* assign processors to output files */
      distribute_file(All.NumFilesPerSnapshot, 0, 0, NTask - 1, &filenr, &masterTask, &lastTask);

      if(All.NumFilesPerSnapshot > 1)
	{
	  if(ThisTask == 0)
	    {
	      sprintf(buf, "%s/snapdir_%03d", All.OutputDir, num);
	      mkdir(buf, 02755);
	    }
	  MPI_Barrier(MPI_COMM_WORLD);
	}

      if(All.NumFilesPerSnapshot > 1)
	sprintf(buf, "%s/snapdir_%03d/%s_%03d.%d", All.OutputDir, num, All.SnapshotFileBase, num, filenr);
      else
	sprintf(buf, "%s%s_%03d", All.OutputDir, All.SnapshotFileBase, num);


      ngroups = All.NumFilesPerSnapshot / All.NumFilesWrittenInParallel;
      if((All.NumFilesPerSnapshot % All.NumFilesWrittenInParallel))
	ngroups++;

      for(gr = 0; gr < ngroups; gr++)
	{
	  if((filenr / All.NumFilesWrittenInParallel) == gr)	/* ok, it's this processor's turn */
	    {
	      write_file(buf, masterTask, lastTask);
	    }
	  MPI_Barrier(MPI_COMM_WORLD);
	}

      myfree(CommBuffer);

#ifdef ORDER_SNAPSHOTS_BY_ID
      t0 = my_second();
      parallel_sort(P, NumPart, sizeof(struct particle_data), io_compare_P_GrNr_SubNr);
      t1 = my_second();
      if(ThisTask == 0)
	printf("Restoring order of particle-data took = %g sec\n", timediff(t0, t1));
#endif

      if(ThisTask == 0)
	printf("done with snapshot.\n");

      All.Ti_lastoutput = All.Ti_Current;

      CPU_Step[CPU_SNAPSHOT] += measure_time();

#ifdef SUBFIND_RESHUFFLE_CATALOGUE
      endrun(0);
#endif
    }

#ifdef FOF
  if(RestartFlag != 4)
    {
      if(ThisTask == 0)
	printf("\ncomputing group catalogue...\n");

      fof_fof(num);

      if(ThisTask == 0)
	printf("done with group catalogue.\n");

      CPU_Step[CPU_FOF] += measure_time();
    }
#endif

#ifdef POWERSPEC_ON_OUTPUT
  if(RestartFlag != 4)
    {
      if(ThisTask == 0)
	printf("\ncomputing power spectra...\n");

      calculate_power_spectra(num, &ntot_type_all[0]);

      if(ThisTask == 0)
	printf("done with power spectra.\n");

      CPU_Step[CPU_MISC] += measure_time();
    }
#endif

}



/*! This function fills the write buffer with particle data. New output blocks can in
 *  principle be added here.
 */
void fill_write_buffer(enum iofields blocknr, int *startindex, int pc, int type)
{
  int n, k, pindex;
  MyOutputFloat *fp;
  MyIDType *ip;
  int *ip_int;
  float *fp_single;
  integertime dt_step;

#ifdef PERIODIC
  MyFloat boxSize;
#endif
#ifdef PMGRID
  double dt_gravkick_pm = 0;
#endif
  double dt_gravkick, dt_hydrokick, fac1, fac2;

#ifdef OUTPUTCOOLRATE
  double tcool, u;
#endif

#ifdef COSMIC_RAYS
  int CRpop;
#endif

#ifdef BP_REAL_CRs
  int Nbin;
#endif

#if (defined(OUTPUT_DISTORTIONTENSORPS) || defined(OUTPUT_TIDALTENSORPS))
  MyBigFloat half_kick_add[6][6];
  int l;
#endif

    
    fac1 = All.cf_a2inv;
    fac2 = All.cf_afac2;
#ifdef MAGNETIC
    double a2_inv = All.cf_a2inv;
    double gizmo2gauss = sqrt(4.*M_PI*All.UnitPressure_in_cgs) / All.UnitMagneticField_in_gauss;
    /* NOTE: we will always work -internally- in code units where MU_0 = 1; hence the 4pi here;
        [much simpler, but be sure of your conversions!] */
#endif

    
#ifdef PMGRID
  if(All.ComovingIntegrationOn)
    dt_gravkick_pm =
      get_gravkick_factor(All.PM_Ti_begstep,
			  All.Ti_Current) -
      get_gravkick_factor(All.PM_Ti_begstep, (All.PM_Ti_begstep + All.PM_Ti_endstep) / 2);
  else
    dt_gravkick_pm = (All.Ti_Current - (All.PM_Ti_begstep + All.PM_Ti_endstep) / 2) * All.Timebase_interval;
#endif

#ifdef DISTORTIONTENSORPS
  MyBigFloat flde, psde;
#endif

  fp = (MyOutputFloat *) CommBuffer;
  fp_single = (float *) CommBuffer;
  ip = (MyIDType *) CommBuffer;
  ip_int = (int *) CommBuffer;

  pindex = *startindex;

  switch (blocknr)
    {
    case IO_POS:		/* positions */
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    for(k = 0; k < 3; k++)
	      {
		fp[k] = P[pindex].Pos[k];
#ifdef PERIODIC
		boxSize = All.BoxSize;
#ifdef LONG_X
		if(k == 0)
		  boxSize = All.BoxSize * LONG_X;
#endif
#ifdef LONG_Y
		if(k == 1)
		  boxSize = All.BoxSize * LONG_Y;
#endif
#ifdef LONG_Z
		if(k == 2)
		  boxSize = All.BoxSize * LONG_Z;
#endif
		while(fp[k] < 0)
		  fp[k] += boxSize;
		while(fp[k] >= boxSize)
		  fp[k] -= boxSize;
#endif
	      }
	    n++;
	    fp += 3;
	  }
      break;

    case IO_VEL:		/* velocities */
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    dt_step = (P[pindex].TimeBin ? (((integertime) 1) << P[pindex].TimeBin) : 0);

	    if(All.ComovingIntegrationOn)
	      {
		dt_gravkick =
		  get_gravkick_factor(P[pindex].Ti_begstep, All.Ti_Current) -
		  get_gravkick_factor(P[pindex].Ti_begstep, P[pindex].Ti_begstep + dt_step / 2);
        dt_hydrokick =
          (All.Ti_Current - (P[pindex].Ti_begstep + dt_step / 2)) * All.Timebase_interval;
	      }
	    else
	      dt_gravkick = dt_hydrokick =
		(All.Ti_Current - (P[pindex].Ti_begstep + dt_step / 2)) * All.Timebase_interval;

	    for(k = 0; k < 3; k++)
	      {
		fp[k] = P[pindex].Vel[k] + P[pindex].GravAccel[k] * dt_gravkick;
		if(P[pindex].Type == 0)
		  fp[k] += SphP[pindex].HydroAccel[k] * dt_hydrokick * All.cf_atime;
	      }
#ifdef PMGRID
	    for(k = 0; k < 3; k++)
	      fp[k] += P[pindex].GravPM[k] * dt_gravkick_pm;
#endif
	    for(k = 0; k < 3; k++)
	      fp[k] *= sqrt(All.cf_a3inv);

	    n++;
	    fp += 3;
	  }
      break;

    case IO_ID:		/* particle ID */
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *ip++ = P[pindex].ID;
	    n++;
	  }
      break;

    case IO_MASS:		/* particle mass */
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = P[pindex].Mass;
	    n++;
	  }
      break;

    case IO_U:			/* internal energy */
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
        *fp++ = DMAX(All.MinEgySpec, Particle_Internal_energy_i(pindex));
	    n++;
	  }
      break;

    case IO_RHO:		/* density */
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
            *fp++ = SphP[pindex].Density;
	    n++;
	  }
      break;

    case IO_NE:		/* electron abundance */
#if defined(COOLING)
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].Ne;
	    n++;
	  }
#endif
      break;

    case IO_NH:		/* neutral hydrogen fraction */
#if defined(COOLING) || defined(RADTRANSFER)
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
#if defined(RADTRANSFER)
	    *fp++ = SphP[pindex].HI;
#else
	    double u, ne, nh0 = 0;
	    double nHeII;
	    ne = SphP[pindex].Ne; 
        u = DMAX(All.MinEgySpec, SphP[pindex].InternalEnergy); // needs to be in code units
	    AbundanceRatios(u, SphP[pindex].Density * All.cf_a3inv, &ne, &nh0, &nHeII, pindex);
#ifdef GALSF_FB_HII_HEATING
        if(SphP[pindex].DelayTimeHII>0 || SphP[pindex].Ne>=1.15789) nh0=0;
#endif
	    *fp++ = (MyOutputFloat) nh0;
#endif // #if defined(RADTRANSFER)
	    n++;
	  }
#endif // #if defined(COOLING) || defined(RADTRANSFER)
      break;

    case IO_HII:		/* ionized hydrogen abundance */
#if defined(RADTRANSFER)
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].HII;
	    n++;
	  }
#endif
      break;

    case IO_HeI:		/* neutral Helium */
#if (defined(RADTRANSFER) && defined(RT_INCLUDE_HE))
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].HeI;
	    n++;
	  }
#endif
      break;

    case IO_HeII:		/* ionized Heluum */
#if (defined(RADTRANSFER) && defined(RT_INCLUDE_HE))
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].HeII;
	    n++;
	  }
#endif
      break;

            
    case IO_HeIII:
        break;
    case IO_H2I:
        break;
    case IO_H2II:
        break;
    case IO_HM:
        break;
    case IO_HD:
        break;
    case IO_DI:
        break;
    case IO_DII:
        break;
    case IO_HeHII:
        break;
            

    case IO_HSML:		/* gas kernel length */
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = PPP[pindex].Hsml;
	    n++;
	  }
      break;

    case IO_SFR:		/* star formation rate */
#ifdef GALSF
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
        /* units convert to solar masses per yr */
        *fp++ = get_starformation_rate(pindex) * ((All.UnitMass_in_g / SOLAR_MASS) / (All.UnitTime_in_s / SEC_PER_YEAR));
	    n++;
	  }
#endif
      break;

    case IO_AGE:		/* stellar formation time */
#ifdef GALSF
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = P[pindex].StellarAge;
	    n++;
	  }
#endif
      break;

    case IO_VSTURB_DISS:
#if defined(TURB_DRIVING)
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].DuDt_diss;
	    n++;
	  }
#endif
      break;

    case IO_VSTURB_DRIVE:
#if defined(TURB_DRIVING)
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].DuDt_drive;
	    n++;
	  }
#endif
      break;
 
    case IO_HSMS:		/* kernel length for star particles */
#ifdef SUBFIND
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = P[pindex].DM_Hsml;
	    n++;
	  }
#endif
      break;

    case IO_Z:			/* gas and star metallicity */
#ifdef METALS
    for(n = 0; n < pc; pindex++)
    if(P[pindex].Type == type)
    {
        for(k = 0; k < NUM_METAL_SPECIES; k++)
            fp[k] = P[pindex].Metallicity[k];
        fp += NUM_METAL_SPECIES;
    n++;
    }
#endif
      break;

    case IO_POT:		/* gravitational potential */
#if defined(OUTPUTPOTENTIAL)  || defined(SUBFIND_RESHUFFLE_AND_POTENTIAL)
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = P[pindex].Potential;
	    n++;
	  }
#endif
      break;

    case IO_ACCEL:		/* acceleration */
#ifdef OUTPUTACCELERATION
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    for(k = 0; k < 3; k++)
	      fp[k] = fac1 * P[pindex].GravAccel[k];
#ifdef PMGRID
	    for(k = 0; k < 3; k++)
	      fp[k] += fac1 * P[pindex].GravPM[k];
#endif
	    if(P[pindex].Type == 0)
	      for(k = 0; k < 3; k++)
		fp[k] += SphP[pindex].HydroAccel[k];
          /* used to multiply by fac2 to get to physical, but now code convention is HydroAccel in physical */
	    fp += 3;
	    n++;
	  }
#endif
      break;

    case IO_DTENTR:		/* rate of change of internal energy */
#ifdef OUTPUTCHANGEOFENERGY
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].DtInternalEnergy;
	    n++;
	  }
#endif
      break;

    case IO_STRESSDIAG:	/* Diagonal components of viscous shear tensor */
      break;

    case IO_STRESSOFFDIAG:	/* Offdiagonal components of viscous shear tensor */
      break;

    case IO_STRESSBULK:	/* Viscous bulk tensor */
      break;

    case IO_DELAYTIME:
#ifdef GALSF_SUBGRID_WINDS
      for(n = 0; n < pc; pindex++)
    	if(P[pindex].Type == type)
    	  {
    		*fp++ = SphP[pindex].DelayTime;
    		n++;
    	  }
#endif
      break;

    case IO_SHEARCOEFF:	/* Shear viscosity coefficient */
      break;

    case IO_TSTP:		/* timestep  */
#ifdef OUTPUTTIMESTEP

      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = (P[pindex].TimeBin ? (((integertime) 1) << P[pindex].TimeBin) : 0) * All.Timebase_interval;
	    n++;
	  }
#endif
      break;

    case IO_BFLD:		/* magnetic field  */
#ifdef MAGNETIC
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    for(k = 0; k < 3; k++)
	      *fp++ = (Get_Particle_BField(pindex,k) * a2_inv * gizmo2gauss);
	    n++;
	  }
#endif
      break;

    case IO_DBDT:		/* rate of change of magnetic field  */
      break;

    case IO_VRMS:		/* turbulent velocity around mean */
      break;

    case IO_VBULK:		/* mean velocity in kernel */
      break;

    case IO_VTAN:		/* turbulent velocity around mean, tangential part */
      break;

    case IO_VRAD:		/* turbulent velocity around mean, radial part */
      break;


    case IO_TRUENGB:		/* True Number of Neighbours  */
      break;

    case IO_DPP:		/* Reacceleration Coefficient */
#ifdef JD_DPP
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].Dpp;
	    n++;
	  }
#endif
      break;

    case IO_VDIV:		/* Divergence of Vel */
      break;

    case IO_VROT:		/* Velocity Curl */
      break;

    case IO_VORT:		/* Vorticity */
#if defined(TURB_DRIVING) || defined(OUTPUT_VORTICITY)
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].Vorticity[0];
	    *fp++ = SphP[pindex].Vorticity[1];
	    *fp++ = SphP[pindex].Vorticity[2];
	    n++;
	  }
#endif
      break;


    case IO_IMF:		/* parameters describing the IMF  */
#ifdef GALSF_SFR_IMF_VARIATION
    for(n = 0; n < pc; pindex++)
        if(P[pindex].Type == type)
            {
                *fp++ = SphP[pindex].IMF_Mturnover;
                n++;
            }
        break;
#endif
            
            
    case IO_DIVB:		/* divergence of magnetic field  */
#ifdef MAGNETIC
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
        /* divB is saved in physical units */
	    *fp++ = (SphP[pindex].divB*gizmo2gauss * (SphP[pindex].Density/P[pindex].Mass*All.cf_a3inv));
	    n++;
	  }
#endif
      break;

    case IO_ABVC:		/* artificial viscosity of particle  */
#if defined(SPHAV_CD10_VISCOSITY_SWITCH)
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].alpha * SphP[pindex].alpha_limiter;
	    n++;
	  }
#endif
      break;


    case IO_AMDC:		/* artificial magnetic dissipation of particle  */
#if defined(TRICCO_RESISTIVITY_SWITCH)
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].Balpha;
	    n++;
	  }
#endif
      break;

    case IO_PHI:		/* divBcleaning fuction of particle  */
#ifdef DIVBCLEANING_DEDNER
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = (Get_Particle_PhiField(pindex) * All.cf_a3inv * gizmo2gauss);
	    n++;
	  }
#endif
      break;

    case IO_GRADPHI:		/* divBcleaning fuction of particle  */
#ifdef DIVBCLEANING_DEDNER
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    for(k = 0; k < 3; k++)
	      *fp++ = (SphP[pindex].Gradients.Phi[k] * a2_inv*a2_inv * gizmo2gauss);
	    n++;
	  }
#endif
      break;

    case IO_ROTB:		/* rot of magnetic field  */
      break;

    case IO_COOLRATE:		/* current cooling rate of particle  */
#ifdef OUTPUTCOOLRATE
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    double ne = SphP[pindex].Ne;
	    /* get cooling time */
        u = Particle_Internal_energy_i(pindex);
        tcool = GetCoolingTime(u, SphP[pindex].Density * All.cf_a3inv, &ne, i);
	    /* convert cooling time with current thermal energy to du/dt */
	    if(tcool != 0)
	      *fp++ = u / tcool;
	    else
	      *fp++ = 0;
	    n++;
	  }
#endif // OUTPUTCOOLRATE
      break;

    case IO_CONDRATE:		/* current heating/cooling due to thermal conduction  */
      break;

    case IO_DENN:		/* density normalization factor */
      break;


    case IO_CR_C0:
#ifdef COSMIC_RAYS
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    for(CRpop = 0; CRpop < NUMCRPOP; CRpop++)
	      fp[CRpop] = SphP[pindex].CR_C0[CRpop];
	    n++;
	    fp += NUMCRPOP;
	  }
#endif
      break;

    case IO_CR_Q0:
#ifdef COSMIC_RAYS
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    for(CRpop = 0; CRpop < NUMCRPOP; CRpop++)
	      fp[CRpop] = SphP[pindex].CR_q0[CRpop];
	    n++;
	    fp += NUMCRPOP;
	  }
#endif
      break;

    case IO_CR_P0:
#if defined(COSMIC_RAYS) && defined(CR_OUTPUT_THERMO_VARIABLES)
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    for(CRpop = 0; CRpop < NUMCRPOP; CRpop++)
	      fp[CRpop] = CR_Physical_Pressure(&SphP[pindex], CRpop);
	    n++;
	    fp += NUMCRPOP;
	  }
#endif
      break;

    case IO_CR_E0:
#if defined(COSMIC_RAYS) && defined(CR_OUTPUT_THERMO_VARIABLES)
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    for(CRpop = 0; CRpop < NUMCRPOP; CRpop++)
	      fp[CRpop] = SphP[pindex].CR_E0[CRpop];
	    n++;
	    fp += NUMCRPOP;
	  }
#endif
      break;

    case IO_CR_n0:
#if defined(COSMIC_RAYS) && defined(CR_OUTPUT_THERMO_VARIABLES)
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    for(CRpop = 0; CRpop < NUMCRPOP; CRpop++)
	      fp[CRpop] = SphP[pindex].CR_n0[CRpop];
	    n++;
	    fp += NUMCRPOP;
	  }
#endif
      break;

    case IO_CR_ThermalizationTime:
#if defined(COSMIC_RAYS) && defined(CR_OUTPUT_TIMESCALES)
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    for(CRpop = 0; CRpop < NUMCRPOP; CRpop++)
	      fp[CRpop] = CR_Tab_GetThermalizationTimescale(SphP[pindex].CR_q0[CRpop] *
							    pow(SphP[pindex].Density * All.cf_a3inv, 0.333333),
							    SphP[pindex].Density * All.cf_a3inv, CRpop);
	    n++;
	    fp += NUMCRPOP;
	  }
#endif
      break;

    case IO_CR_DissipationTime:
#if defined(COSMIC_RAYS) && defined(CR_OUTPUT_TIMESCALES)
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    for(CRpop = 0; CRpop < NUMCRPOP; CRpop++)
	      fp[CRpop] = CR_Tab_GetDissipationTimescale(SphP[pindex].CR_q0[CRpop] *
							 pow(SphP[pindex].Density * All.cf_a3inv, 0.333333),
							 SphP[pindex].Density * All.cf_a3inv, CRpop);
	    n++;
	    fp += NUMCRPOP;
	  }
#endif
      break;

    case IO_BPCR_pNORM:
#if defined BP_REAL_CRs
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    for(Nbin = 0; Nbin < BP_REAL_CRs; Nbin++)
	      fp[Nbin] = SphP[pindex].CRpNorm[Nbin];
	    n++;
	    fp += BP_REAL_CRs;
	  }
#endif
      break;

    case IO_BPCR_eNORM:
#if defined BP_REAL_CRs
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    for(Nbin = 0; Nbin < BP_REAL_CRs; Nbin++)
	      fp[Nbin] = SphP[pindex].CReNorm[Nbin];
	    n++;
	    fp += BP_REAL_CRs;
	  }
#endif
      break;

    case IO_BPCR_pSLOPE:
#if defined BP_REAL_CRs
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    for(Nbin = 0; Nbin < BP_REAL_CRs; Nbin++)
	      fp[Nbin] = SphP[pindex].CRpSlope[Nbin];
	    n++;
	    fp += BP_REAL_CRs;
	  }
#endif
      break;

    case IO_BPCR_eSLOPE:
#if defined BP_REAL_CRs
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    for(Nbin = 0; Nbin < BP_REAL_CRs; Nbin++)
	      fp[Nbin] = SphP[pindex].CReSlope[Nbin];
	    n++;
	    fp += BP_REAL_CRs;
	  }
#endif
      break;

    case IO_BPCR_pE:
#if defined BP_REAL_CRs
      for(n = 0; n < pc; pindex++)
        if(P[pindex].Type == type)
	  {
	    for(Nbin = 0; Nbin < BP_REAL_CRs; Nbin++)
	      fp[Nbin] = SphP[pindex].CRpE[Nbin];
	    n++;
	    fp += BP_REAL_CRs;
	  }
#endif
      break;

    case IO_BPCR_pN:
#if defined BP_REAL_CRs
      for(n = 0; n < pc; pindex++)
        if(P[pindex].Type == type)
	  {
	    for(Nbin = 0; Nbin < BP_REAL_CRs; Nbin++)
	      fp[Nbin] = SphP[pindex].CRpN[Nbin];
	    n++;
	    fp += BP_REAL_CRs;
	  }
#endif

    case IO_BPCR_pPRESSURE:
#if defined BP_REAL_CRs
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].CRpPressure;
	    n++;
	  }
#endif
      break;

    case IO_BPCR_ePRESSURE:
#if defined BP_REAL_CRs
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].CRePressure;
	    n++;
	  }
#endif
      break;

    case IO_BHMASS:
#ifdef BLACK_HOLES
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = BPP(pindex).BH_Mass;
	    n++;
	  }
#endif
      break;

    case IO_BHMASSALPHA:
#ifdef BH_ALPHADISK_ACCRETION
        for(n = 0; n < pc; pindex++)
        if(P[pindex].Type == type)
        {
            *fp++ = BPP(pindex).BH_Mass_AlphaDisk;
            n++;
        }
#endif
      break;
            
    case IO_BHMDOT:
#ifdef BLACK_HOLES
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = BPP(pindex).BH_Mdot;
	    n++;
	  }
#endif
      break;

    case IO_BHPROGS:
#ifdef BH_COUNTPROGS
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *ip_int++ = BPP(pindex).BH_CountProgs;
	    n++;
	  }
#endif
      break;

    case IO_BHMBUB:
#ifdef BH_BUBBLES
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = P[pindex].BH_Mass_bubbles;
	    n++;
	  }
#endif
      break;

    case IO_BHMINI:
#ifdef BH_BUBBLES
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = P[pindex].BH_Mass_ini;
	    n++;
	  }
#endif
      break;

    case IO_BHMRAD:
#ifdef UNIFIED_FEEDBACK
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = P[pindex].BH_Mass_radio;
	    n++;
	  }
#endif
      break;

    case IO_ACRB:
#ifdef BLACK_HOLES
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = P[pindex].Hsml;
	    n++;
	  }
#endif
      break;

    case IO_MACH:
#if defined(MACHNUM)
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].Shock_MachNumber;
	    n++;
	  }
#endif
      break;

    case IO_DTENERGY:
#ifdef MACHSTATISTIC
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].Shock_DtEnergy;
	    n++;
	  }
#endif
      break;

    case IO_PRESHOCK_CSND:
#ifdef OUTPUT_PRESHOCK_CSND
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].PreShock_PhysicalSoundSpeed;
	    n++;
	  }
#endif
      break;

    case IO_PRESHOCK_DENSITY:
#if defined(CR_OUTPUT_JUMP_CONDITIONS) || defined(OUTPUT_PRESHOCK_CSND)
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].PreShock_PhysicalDensity;
	    n++;
	  }
#endif
      break;

    case IO_PRESHOCK_ENERGY:
#ifdef CR_OUTPUT_JUMP_CONDITIONS
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].PreShock_PhysicalEnergy;
	    n++;
	  }
#endif
      break;

    case IO_PRESHOCK_XCR:
#ifdef CR_OUTPUT_JUMP_CONDITIONS
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].PreShock_XCR;
	    n++;
	  }
#endif
      break;

    case IO_DENSITY_JUMP:
#ifdef CR_OUTPUT_JUMP_CONDITIONS
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].Shock_DensityJump;
	    n++;
	  }
#endif
      break;

    case IO_ENERGY_JUMP:
#ifdef CR_OUTPUT_JUMP_CONDITIONS
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].Shock_EnergyJump;
	    n++;
	  }
#endif
      break;

    case IO_CRINJECT:
#if defined( COSMIC_RAYS ) && defined( CR_OUTPUT_INJECTION )
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].CR_Specific_SupernovaHeatingRate;
	    n++;
	  }
#endif
      break;

    case IO_TIDALTENSORPS:
      /* 3x3 configuration-space tidal tensor that is driving the GDE */
#ifdef OUTPUT_TIDALTENSORPS
      for(n = 0; n < pc; pindex++)

	if(P[pindex].Type == type)
	  {
	    for(k = 0; k < 3; k++)
	      {
		for(l = 0; l < 3; l++)
		  {
		    fp[k * 3 + l] = (MyOutputFloat) P[pindex].tidal_tensorps[k][l];
#if defined(PMGRID)
		    fp[k * 3 + l] += (MyOutputFloat) P[pindex].tidal_tensorpsPM[k][l];
#endif

		  }
	      }

	    fflush(stderr);
	    n++;
	    fp += 9;
	  }
#endif
      break;


    case IO_DISTORTIONTENSORPS:
      /* full 6D phase-space distortion tensor from GDE integration */
#ifdef OUTPUT_DISTORTIONTENSORPS
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    get_half_kick_distortion(pindex, half_kick_add);
	    for(k = 0; k < 6; k++)
	      {
		for(l = 0; l < 6; l++)
		  { 
                    fp[k * 6 + l] = (MyOutputFloat) (P[pindex].distortion_tensorps[k][l] + half_kick_add[k][l]);
		  }
	      }
	    n++;
	    fp += 36;

	  }
#endif
      break;

    case IO_CAUSTIC_COUNTER:
      /* caustic counter */
#ifdef DISTORTIONTENSORPS
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = (MyOutputFloat) P[pindex].caustic_counter;
	    n++;
	  }
#endif
      break;

    case IO_FLOW_DETERMINANT:
      /* physical NON-CUTOFF corrected stream determinant = 1.0/normed stream density * 1.0/initial stream density */
#if defined(DISTORTIONTENSORPS) && !defined(GDE_LEAN)
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    get_current_ps_info(pindex, &flde, &psde);
	    *fp++ = (MyOutputFloat) flde;
	    n++;
	  }
#endif
      break;

    case IO_STREAM_DENSITY:
      /* physical CUTOFF corrected stream density = normed stream density * initial stream density */
#ifdef DISTORTIONTENSORPS
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
            *fp++ = (MyOutputFloat) (P[pindex].stream_density);
	    n++;
	  }
#endif
      break;

    case IO_PHASE_SPACE_DETERMINANT:
      /* determinant of phase-space distortion tensor -> should be 1 due to Liouville theorem */
#ifdef DISTORTIONTENSORPS
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    get_current_ps_info(pindex, &flde, &psde);
	    *fp++ = (MyOutputFloat) psde;
	    n++;
	  }
#endif
      break;

    case IO_ANNIHILATION_RADIATION:
      /* time integrated stream density in physical units */
#if defined(DISTORTIONTENSORPS) && !defined(GDE_LEAN)
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = (MyOutputFloat) (P[pindex].annihilation * GDE_INITDENSITY(pindex));
	    *fp++ = (MyOutputFloat) (P[pindex].analytic_caustics);
	    *fp++ = (MyOutputFloat) (P[pindex].analytic_annihilation * GDE_INITDENSITY(pindex));
	    n++;
	  }
#endif
      break;

    case IO_LAST_CAUSTIC:
      /* extensive information on the last caustic the particle has passed */
#ifdef OUTPUT_LAST_CAUSTIC
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = (MyOutputFloat) P[pindex].lc_Time;
	    *fp++ = (MyOutputFloat) P[pindex].lc_Pos[0];
	    *fp++ = (MyOutputFloat) P[pindex].lc_Pos[1];
	    *fp++ = (MyOutputFloat) P[pindex].lc_Pos[2];
	    *fp++ = (MyOutputFloat) P[pindex].lc_Vel[0];
	    *fp++ = (MyOutputFloat) P[pindex].lc_Vel[1];
	    *fp++ = (MyOutputFloat) P[pindex].lc_Vel[2];
	    *fp++ = (MyOutputFloat) P[pindex].lc_rho_normed_cutoff;

	    *fp++ = (MyOutputFloat) P[pindex].lc_Dir_x[0];
	    *fp++ = (MyOutputFloat) P[pindex].lc_Dir_x[1];
	    *fp++ = (MyOutputFloat) P[pindex].lc_Dir_x[2];
	    *fp++ = (MyOutputFloat) P[pindex].lc_Dir_y[0];
	    *fp++ = (MyOutputFloat) P[pindex].lc_Dir_y[1];
	    *fp++ = (MyOutputFloat) P[pindex].lc_Dir_y[2];
	    *fp++ = (MyOutputFloat) P[pindex].lc_Dir_z[0];
	    *fp++ = (MyOutputFloat) P[pindex].lc_Dir_z[1];
	    *fp++ = (MyOutputFloat) P[pindex].lc_Dir_z[2];

	    *fp++ = (MyOutputFloat) P[pindex].lc_smear_x;
	    *fp++ = (MyOutputFloat) P[pindex].lc_smear_y;
	    *fp++ = (MyOutputFloat) P[pindex].lc_smear_z;
	    n++;
	  }
#endif
      break;

    case IO_SHEET_ORIENTATION:
      /* initial orientation of the CDM sheet where the particle started */
#if defined(DISTORTIONTENSORPS) && (!defined(GDE_LEAN) || defined(GDE_READIC))
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = (MyOutputFloat) GDE_VMATRIX(pindex,0,0);
            *fp++ = (MyOutputFloat) GDE_VMATRIX(pindex,0,1);
            *fp++ = (MyOutputFloat) GDE_VMATRIX(pindex,0,2);
            *fp++ = (MyOutputFloat) GDE_VMATRIX(pindex,1,0);
            *fp++ = (MyOutputFloat) GDE_VMATRIX(pindex,1,1);
            *fp++ = (MyOutputFloat) GDE_VMATRIX(pindex,1,2);
            *fp++ = (MyOutputFloat) GDE_VMATRIX(pindex,2,0);
            *fp++ = (MyOutputFloat) GDE_VMATRIX(pindex,2,1);
            *fp++ = (MyOutputFloat) GDE_VMATRIX(pindex,2,2);
	    n++;
	  }
#endif
      break;

    case IO_INIT_DENSITY:
      /* initial stream density in physical units  */
#if defined(DISTORTIONTENSORPS) && (!defined(GDE_LEAN) || defined(GDE_READIC))
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
            if(All.ComovingIntegrationOn)
       	      *fp++ = GDE_INITDENSITY(pindex) / (GDE_TIMEBEGIN(pindex) * GDE_TIMEBEGIN(pindex) * GDE_TIMEBEGIN(pindex));
            else
 	      *fp++ = GDE_INITDENSITY(pindex);
	    n++;
	  }
#endif
      break;

    case IO_SECONDORDERMASS:
      break;

    case IO_EOSTEMP:
#ifdef EOS_DEGENERATE
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].temp;
	    n++;
	  }
#endif
      break;

    case IO_EOSXNUC:
#ifdef EOS_DEGENERATE
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    for(k = 0; k < EOS_NSPECIES; k++)
	      {
		*fp++ = SphP[pindex].xnuc[k];
	      }
	    n++;
	  }
#endif
      break;

    case IO_PRESSURE:
#if defined(EOS_DEGENERATE)
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = SphP[pindex].Pressure;
	    n++;
	  }
#endif
      break;

    case IO_RADGAMMA:
#ifdef RADTRANSFER
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    for(k = 0; k < N_BINS; k++)
	      fp[k] = SphP[pindex].n_gamma[k] / 1e53;
	    
	    n++;
	    fp += N_BINS;
	  }
#endif
      break;

    case IO_RAD_ACCEL:
#if defined(RADTRANSFER) && defined(RT_OUTPUT_RAD_ACCEL)
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    for(k = 0; k < 3; k++)
	      fp[k] = SphP[pindex].RadAccel[k];
	    
	    n++;
	    fp += 3;
	  }
#endif
      break;

    case IO_EDDINGTON_TENSOR:
#ifdef RADTRANSFER
#ifdef RT_OUTPUT_ET
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    for(k = 0; k < 6; k++)
	      {
		fp[k] = SphP[pindex].ET[k];
	      }
	    n++;
	    fp += 6;
	  }
#endif
#endif
      break;

    case IO_DMHSML:
#if defined(SUBFIND_RESHUFFLE_CATALOGUE) && defined(SUBFIND) 
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp_single++ = P[pindex].DM_Hsml;
	    n++;
	  }
#endif
      break;

    case IO_DMDENSITY:
#if defined(SUBFIND_RESHUFFLE_CATALOGUE) && defined(SUBFIND) 
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp_single++ = P[pindex].u.DM_Density;
	    n++;
	  }
#endif
      break;

    case IO_DMVELDISP:
#if defined(SUBFIND_RESHUFFLE_CATALOGUE) && defined(SUBFIND) 
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp_single++ = P[pindex].v.DM_VelDisp;
	    n++;
	  }
#endif
      break;

    case IO_DMHSML_V:
      break;

    case IO_DMDENSITY_V:
      break;

    case IO_CHEM:
      break;
    case IO_AGS_SOFT:		/* Adaptive Gravitational Softening: softening */
#if defined(ADAPTIVE_GRAVSOFT_FORALL) && defined(AGS_OUTPUTGRAVSOFT)
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = PPP[pindex].Hsml;
	    n++;
	  }
#endif
      break;
    case IO_AGS_ZETA:		/* Adaptive Gravitational Softening: zeta */
#if defined(ADAPTIVE_GRAVSOFT_FORALL) && defined(AGS_OUTPUTZETA)
      for(n = 0; n < pc; pindex++)
	if(P[pindex].Type == type)
	  {
	    *fp++ = PPPZ[pindex].AGS_zeta;
	    n++;
	  }
#endif
      break;
    case IO_AGS_OMEGA:		/* Adaptive Gravitational Softening: omega */
      break;
    case IO_AGS_NGBS:		/* Adaptive Gravitational Softening: neighbours */
      break;
    case IO_AGS_CORR:		/* Adaptive Gravitational Softening: correction */
      break;

case IO_MG_PHI:      /* scalar field fluctuations */
      break;
      
    case IO_MG_ACCEL:      /* modified gravity acceleration */
      break;
      
    case IO_LASTENTRY:
      endrun(213);
      break;
    }

  *startindex = pindex;
}




/*! This function tells the size of one data entry in each of the blocks
 *  defined for the output file.
 */
int get_bytes_per_blockelement(enum iofields blocknr, int mode)
{
  int bytes_per_blockelement = 0;

  switch (blocknr)
    {
    case IO_POS:
    case IO_VEL:
    case IO_ACCEL:
    case IO_VBULK:
    case IO_BFLD:
    case IO_GRADPHI:
    case IO_DBDT:
    case IO_ROTB:
    case IO_STRESSDIAG:
    case IO_STRESSOFFDIAG:
    case IO_RAD_ACCEL:
    case IO_VORT:
    case IO_MG_ACCEL:
      if(mode)
	bytes_per_blockelement = 3 * sizeof(MyInputFloat);
      else
	bytes_per_blockelement = 3 * sizeof(MyOutputFloat);
      break;

    case IO_ID:
      bytes_per_blockelement = sizeof(MyIDType);
      break;

    case IO_BHPROGS:
    case IO_TRUENGB:
    case IO_AGS_NGBS:
      bytes_per_blockelement = sizeof(int);
      break;

    case IO_MASS:
    case IO_SECONDORDERMASS:
    case IO_U:
    case IO_RHO:
    case IO_NE:
    case IO_NH:
    case IO_HII:
    case IO_HeI:
    case IO_HeII:
    case IO_HeIII:
    case IO_H2I:
    case IO_H2II:
    case IO_HM:
    case IO_HD:
    case IO_DI:
    case IO_DII:
    case IO_HeHII:
    case IO_HSML:
    case IO_SFR:
    case IO_AGE:
    case IO_DELAYTIME:
    case IO_HSMS:
    case IO_POT:
    case IO_DTENTR:
    case IO_STRESSBULK:
    case IO_SHEARCOEFF:
    case IO_TSTP:
    case IO_IMF:
    case IO_DIVB:
    case IO_VRMS:
    case IO_VRAD:
    case IO_VTAN:
    case IO_VDIV:
    case IO_VROT:
    case IO_DPP:
    case IO_ABVC:
    case IO_AMDC:
    case IO_PHI:
    case IO_COOLRATE:
    case IO_CONDRATE:
    case IO_DENN:
    case IO_BHMASS:
    case IO_BHMASSALPHA:
    case IO_ACRB:
    case IO_BHMDOT:
    case IO_BHMBUB:
    case IO_BHMINI:
    case IO_BHMRAD:
    case IO_MACH:
    case IO_DTENERGY:
    case IO_PRESHOCK_CSND:
    case IO_PRESHOCK_DENSITY:
    case IO_PRESHOCK_ENERGY:
    case IO_PRESHOCK_XCR:
    case IO_DENSITY_JUMP:
    case IO_ENERGY_JUMP:
    case IO_CRINJECT:
    case IO_CAUSTIC_COUNTER:
    case IO_FLOW_DETERMINANT:
    case IO_STREAM_DENSITY:
    case IO_PHASE_SPACE_DETERMINANT:
    case IO_EOSTEMP:
    case IO_PRESSURE:
    case IO_INIT_DENSITY:
    case IO_BPCR_pPRESSURE:
    case IO_BPCR_ePRESSURE:
    case IO_AGS_SOFT:
    case IO_AGS_ZETA:
    case IO_AGS_OMEGA:
    case IO_AGS_CORR:
    case IO_VSTURB_DISS:
    case IO_VSTURB_DRIVE:
    case IO_MG_PHI:
      if(mode)
	bytes_per_blockelement = sizeof(MyInputFloat);
      else
	bytes_per_blockelement = sizeof(MyOutputFloat);
      break;

    case IO_CR_C0:
    case IO_CR_Q0:
    case IO_CR_P0:
    case IO_CR_E0:
    case IO_CR_n0:
    case IO_CR_ThermalizationTime:
    case IO_CR_DissipationTime:
      if(mode)
	bytes_per_blockelement = NUMCRPOP * sizeof(MyInputFloat);
      else
	bytes_per_blockelement = NUMCRPOP * sizeof(MyOutputFloat);
      break;

    case IO_BPCR_pNORM:
    case IO_BPCR_eNORM:
    case IO_BPCR_pSLOPE:
    case IO_BPCR_eSLOPE:
    case IO_BPCR_pE:
    case IO_BPCR_pN:
#ifndef BP_REAL_CRs
      if(mode)
	bytes_per_blockelement = sizeof(MyInputFloat);
      else
	bytes_per_blockelement = sizeof(MyOutputFloat);
      break;
#else
      if(mode)
	bytes_per_blockelement = BP_REAL_CRs * sizeof(MyInputFloat);
      else
	bytes_per_blockelement = BP_REAL_CRs * sizeof(MyOutputFloat);
      break;
#endif

    case IO_DMHSML:
    case IO_DMDENSITY:
    case IO_DMVELDISP:
    case IO_DMHSML_V:
    case IO_DMDENSITY_V:
      bytes_per_blockelement = sizeof(float);
      break;

    case IO_RADGAMMA:
#ifdef RADTRANSFER
      if(mode)
	bytes_per_blockelement = N_BINS * sizeof(MyInputFloat);
      else
	bytes_per_blockelement = N_BINS * sizeof(MyOutputFloat);
#endif
      break;

    case IO_EDDINGTON_TENSOR:
      if(mode)
	bytes_per_blockelement = 6 * sizeof(MyInputFloat);
      else
	bytes_per_blockelement = 6 * sizeof(MyOutputFloat);


    case IO_Z:
#ifdef METALS
      if(mode)
         bytes_per_blockelement = (NUM_METAL_SPECIES) * sizeof(MyInputFloat);
      else
         bytes_per_blockelement = (NUM_METAL_SPECIES) * sizeof(MyOutputFloat);
#endif
      break;

    case IO_TIDALTENSORPS:
      if(mode)
	bytes_per_blockelement = 9 * sizeof(MyInputFloat);
      else
	bytes_per_blockelement = 9 * sizeof(MyOutputFloat);
      break;

    case IO_DISTORTIONTENSORPS:
      if(mode)
	bytes_per_blockelement = 36 * sizeof(MyInputFloat);
      else
	bytes_per_blockelement = 36 * sizeof(MyOutputFloat);
      break;

    case IO_ANNIHILATION_RADIATION:
      if(mode)
	bytes_per_blockelement = 3 * sizeof(MyInputFloat);
      else
	bytes_per_blockelement = 3 * sizeof(MyOutputFloat);
      break;

    case IO_LAST_CAUSTIC:
      if(mode)
	bytes_per_blockelement = 20 * sizeof(MyInputFloat);
      else
	bytes_per_blockelement = 20 * sizeof(MyOutputFloat);
      break;

    case IO_SHEET_ORIENTATION:
      if(mode)
	bytes_per_blockelement = 9 * sizeof(MyInputFloat);
      else
	bytes_per_blockelement = 9 * sizeof(MyOutputFloat);
      break;

    case IO_EOSXNUC:
#ifdef EOS_DEGENERATE
      if(mode)
	bytes_per_blockelement = EOS_NSPECIES * sizeof(MyInputFloat);
      else
	bytes_per_blockelement = EOS_NSPECIES * sizeof(MyOutputFloat);
      break;
#else
      if(mode)
	bytes_per_blockelement = sizeof(MyInputFloat);
      else
	bytes_per_blockelement = sizeof(MyOutputFloat);
      break;
#endif

      
    case IO_CHEM:
      bytes_per_blockelement = 0;
      break;


    case IO_LASTENTRY:
      endrun(214);
      break;
    }

  return bytes_per_blockelement;
}

int get_datatype_in_block(enum iofields blocknr)
{
  int typekey;

  switch (blocknr)
    {
    case IO_ID:
#ifdef LONGIDS
      typekey = 2;		/* native long long */
#else
      typekey = 0;		/* native int */
#endif
      break;

    case IO_TRUENGB:
    case IO_BHPROGS:
      typekey = 0;		/* native int */
      break;

    default:
      typekey = 1;		/* native MyOutputFloat */
      break;
    }

  return typekey;
}



int get_values_per_blockelement(enum iofields blocknr)
{
  int values = 0;

  switch (blocknr)
    {
    case IO_POS:
    case IO_VEL:
    case IO_ACCEL:
    case IO_BFLD:
    case IO_GRADPHI:
    case IO_DBDT:
    case IO_ROTB:
    case IO_STRESSDIAG:
    case IO_STRESSOFFDIAG:
    case IO_VBULK:
    case IO_RAD_ACCEL:
    case IO_VORT:
    case IO_MG_ACCEL:
      values = 3;
      break;

    case IO_ID:
    case IO_MASS:
    case IO_SECONDORDERMASS:
    case IO_U:
    case IO_RHO:
    case IO_NE:
    case IO_NH:
    case IO_HII:
    case IO_HeI:
    case IO_HeII:
    case IO_HeIII:
    case IO_H2I:
    case IO_H2II:
    case IO_HM:
    case IO_HD:
    case IO_DI:
    case IO_DII:
    case IO_HeHII:
    case IO_HSML:
    case IO_SFR:
    case IO_AGE:
    case IO_DELAYTIME:
    case IO_HSMS:
    case IO_POT:
    case IO_DTENTR:
    case IO_STRESSBULK:
    case IO_SHEARCOEFF:
    case IO_TSTP:
    case IO_VRMS:
    case IO_TRUENGB:
    case IO_VTAN:
    case IO_VRAD:  
    case IO_VDIV:
    case IO_VROT:
    case IO_DPP:
    case IO_IMF:
    case IO_DIVB:
    case IO_ABVC:
    case IO_AMDC:
    case IO_PHI:
    case IO_COOLRATE:
    case IO_CONDRATE:
    case IO_DENN:
    case IO_BHMASS:
    case IO_BHMASSALPHA:
    case IO_ACRB:
    case IO_BHMDOT:
    case IO_BHPROGS:
    case IO_BHMBUB:
    case IO_BHMINI:
    case IO_BHMRAD:
    case IO_MACH:
    case IO_DTENERGY:
    case IO_PRESHOCK_CSND:
    case IO_PRESHOCK_DENSITY:
    case IO_PRESHOCK_ENERGY:
    case IO_PRESHOCK_XCR:
    case IO_DENSITY_JUMP:
    case IO_ENERGY_JUMP:
    case IO_CRINJECT:
    case IO_CAUSTIC_COUNTER:
    case IO_FLOW_DETERMINANT:
    case IO_STREAM_DENSITY:
    case IO_PHASE_SPACE_DETERMINANT:
    case IO_EOSTEMP:
    case IO_PRESSURE:
    case IO_INIT_DENSITY:
    case IO_DMHSML:
    case IO_DMDENSITY:
    case IO_DMVELDISP:
    case IO_DMHSML_V:
    case IO_DMDENSITY_V:
    case IO_BPCR_pPRESSURE:
    case IO_BPCR_ePRESSURE:
    case IO_AGS_SOFT:
    case IO_AGS_ZETA:
    case IO_AGS_OMEGA:
    case IO_AGS_CORR:
    case IO_AGS_NGBS:
    case IO_VSTURB_DISS:
    case IO_VSTURB_DRIVE:
    case IO_MG_PHI:
      values = 1;
      break;

    case IO_CR_C0:
    case IO_CR_Q0:
    case IO_CR_P0:
    case IO_CR_E0:
    case IO_CR_n0:
    case IO_CR_ThermalizationTime:
    case IO_CR_DissipationTime:
      values = NUMCRPOP;
      break;

    case IO_BPCR_pNORM:
    case IO_BPCR_eNORM:
    case IO_BPCR_pSLOPE:
    case IO_BPCR_eSLOPE:
    case IO_BPCR_pE:
    case IO_BPCR_pN:
#ifdef BP_REAL_CRs
      values = BP_REAL_CRs;
#endif
      break;

    case IO_EDDINGTON_TENSOR:
      values = 6;
      break;

    case IO_RADGAMMA:
#ifdef RADRANSFER
      values = N_BINS;
#else
      values = 0;
#endif
      break;

    case IO_Z:
#ifdef METALS
      values = NUM_METAL_SPECIES;
#else
      values = 0;
#endif
      break;

    case IO_TIDALTENSORPS:
      values = 9;
      break;
    case IO_DISTORTIONTENSORPS:
      values = 36;
      break;
    case IO_ANNIHILATION_RADIATION:
      values = 3;
      break;
    case IO_LAST_CAUSTIC:
      values = 20;
      break;
    case IO_SHEET_ORIENTATION:
      values = 9;
      break;


    case IO_EOSXNUC:
#ifndef EOS_DEGENERATE
      values = 1;
#else
      values = EOS_NSPECIES;
#endif
      break;


    case IO_CHEM:
      values = 0;
      break;


    case IO_LASTENTRY:
      endrun(215);
      break;
    }
  return values;
}




/*! This function determines how many particles there are in a given block,
 *  based on the information in the header-structure.  It also flags particle
 *  types that are present in the block in the typelist array.
 */
int get_particles_in_block(enum iofields blocknr, int *typelist)
{
    int i, nall, nsel, ntot_withmasses, ngas, ngasAlpha, nstars, nngb;
    
    nall = 0;
    nsel = 0;
    ntot_withmasses = 0;
    
    for(i = 0; i < 6; i++)
    {
        typelist[i] = 0;
        
        if(header.npart[i] > 0)
        {
            nall += header.npart[i];
            typelist[i] = 1;
        }
        
        if(All.MassTable[i] == 0)
            ntot_withmasses += header.npart[i];
    }
    
    ngas = header.npart[0];
    ngasAlpha = NUMCRPOP * header.npart[0];
    nstars = header.npart[4];


  switch (blocknr)
    {
    case IO_POS:
    case IO_VEL:
    case IO_ACCEL:
    case IO_TSTP:
    case IO_ID:
    case IO_POT:
    case IO_SECONDORDERMASS:
    case IO_AGS_SOFT:
//    case IO_AGS_DENS:
    case IO_AGS_ZETA:
    case IO_AGS_OMEGA:
    case IO_AGS_CORR:
    case IO_AGS_NGBS:
    case IO_MG_PHI:
      return nall;
      break;

    case IO_MASS:
            for(i = 0; i < 6; i++)
            {
                typelist[i] = 0;
                if(All.MassTable[i] == 0 && header.npart[i] > 0)
                    typelist[i] = 1;
            }
            return ntot_withmasses;
            break;

    case IO_RAD_ACCEL:
    case IO_RADGAMMA:
    case IO_EDDINGTON_TENSOR:
    case IO_U:
    case IO_RHO:
    case IO_NE:
    case IO_NH:
    case IO_HII:
    case IO_HeI:
    case IO_HeII:
    case IO_HeIII:
    case IO_H2I:
    case IO_H2II:
    case IO_HM:
    case IO_HD:
    case IO_DI:
    case IO_DII:
    case IO_HeHII:
    case IO_HSML:
    case IO_DELAYTIME:
    case IO_SFR:
    case IO_DTENTR:
    case IO_STRESSDIAG:
    case IO_STRESSOFFDIAG:
    case IO_STRESSBULK:
    case IO_SHEARCOEFF:
    case IO_BFLD:
    case IO_DBDT:
    case IO_VRMS:
    case IO_VBULK:
    case IO_VTAN:
    case IO_VRAD:  
    case IO_VDIV:
    case IO_VROT:
    case IO_VORT:
    case IO_DPP:
    case IO_IMF:
    case IO_DIVB:
    case IO_ABVC:
    case IO_AMDC:
    case IO_PHI:
    case IO_GRADPHI:
    case IO_ROTB:
    case IO_COOLRATE:
    case IO_CONDRATE:
    case IO_DENN:
    case IO_MACH:
    case IO_DTENERGY:
    case IO_PRESHOCK_CSND:
    case IO_PRESHOCK_DENSITY:
    case IO_PRESHOCK_ENERGY:
    case IO_PRESHOCK_XCR:
    case IO_DENSITY_JUMP:
    case IO_ENERGY_JUMP:
    case IO_CRINJECT:
    case IO_EOSTEMP:
    case IO_EOSXNUC:
    case IO_PRESSURE:
    case IO_CHEM:
    case IO_BPCR_pNORM:
    case IO_BPCR_eNORM:
    case IO_BPCR_pSLOPE:
    case IO_BPCR_eSLOPE:
    case IO_BPCR_pE:
    case IO_BPCR_pN:
    case IO_BPCR_pPRESSURE:
    case IO_BPCR_ePRESSURE:
    case IO_VSTURB_DISS:
    case IO_VSTURB_DRIVE:
      for(i = 1; i < 6; i++)
	typelist[i] = 0;
      return ngas;
      break;

    case IO_CR_C0:
    case IO_CR_Q0:
    case IO_CR_P0:
    case IO_CR_E0:
    case IO_CR_n0:
    case IO_CR_ThermalizationTime:
    case IO_CR_DissipationTime:
      for(i = 1; i < 6; i++)
	typelist[i] = 0;
      return ngasAlpha;
      break;

    case IO_AGE:
      for(i = 0; i < 6; i++)
#ifdef BLACK_HOLES
	if(i != 4 && i != 5)
	  typelist[i] = 0;
      return nstars + header.npart[5];
#else
	if(i != 4)
	  typelist[i] = 0;
      return nstars;
#endif
      break;

    case IO_TRUENGB:
      nngb = ngas;
      for(i = 1; i < 4; i++)
	typelist[i] = 0;
      typelist[4] = 0;
#ifdef BLACK_HOLES
      nngb += header.npart[5];
#else
      typelist[5] = 0;
#endif
      return nngb;
      break;

    case IO_HSMS:
      for(i = 0; i < 6; i++)
	if(i != 4)
	  typelist[i] = 0;
      return nstars;
      break;

    case IO_Z:
      for(i = 0; i < 6; i++)
	if(i != 0 && i != 4)
	  typelist[i] = 0;
      return ngas + nstars;
      break;

    case IO_BHMASS:
    case IO_BHMASSALPHA:
    case IO_ACRB:
    case IO_BHMDOT:
    case IO_BHMBUB:
    case IO_BHMINI:
    case IO_BHMRAD:
    case IO_BHPROGS:
      for(i = 0; i < 6; i++)
	if(i != 5)
	  typelist[i] = 0;
      return header.npart[5];
      break;

    case IO_TIDALTENSORPS:
    case IO_DISTORTIONTENSORPS:
    case IO_CAUSTIC_COUNTER:
    case IO_FLOW_DETERMINANT:
    case IO_STREAM_DENSITY:
    case IO_PHASE_SPACE_DETERMINANT:
    case IO_ANNIHILATION_RADIATION:
    case IO_LAST_CAUSTIC:
    case IO_SHEET_ORIENTATION:
    case IO_INIT_DENSITY:
      for(i = 0; i < 6; i++)
        if(((1 << i) & (GDE_TYPES)))
          nsel += header.npart[i];
        else
          typelist[i] = 0;
      return nsel;
      break;


    case IO_DMHSML:
    case IO_DMDENSITY:
    case IO_DMVELDISP:
    case IO_DMHSML_V:
    case IO_DMDENSITY_V:
      for(i = 0; i < 6; i++)
	if(((1 << i) & (FOF_PRIMARY_LINK_TYPES)))
	  nsel += header.npart[i];
	else
	  typelist[i] = 0;
      return nsel;
      break;

    case IO_MG_ACCEL:
      for(i = 2; i < 6; i++)
        typelist[i] = 0;
      return ngas + header.npart[1];
      break;
    
    case IO_LASTENTRY:
      endrun(216);
      break;
    }

  endrun(212);
  return 0;
}



/*! This function tells whether a block in the output file is present or not.
 */
int blockpresent(enum iofields blocknr)
{
  switch (blocknr)
    {
    case IO_POS:
    case IO_VEL:
    case IO_ID:
    case IO_MASS:
    case IO_U:
    case IO_RHO:
    case IO_HSML:
      return 1;			/* always present */

    case IO_NE:
    case IO_NH:
      if(All.CoolingOn == 0)
#if defined(RADTRANSFER)
	return 1;
#else
	return 0;
#endif
      else
	return 1;
      break;

    case IO_RADGAMMA:
#ifdef RADTRANSFER
      return N_BINS;
#else
      return 0;
#endif
      break;

    case IO_RAD_ACCEL:
#ifdef RT_OUTPUT_RAD_ACCEL
      return 3;
#else
      return 0;
#endif

    case IO_HSMS:
#if defined(SUBFIND) 
      return 1;
#else
      return 0;
#endif
      break;

    case IO_SFR:
    case IO_AGE:
    case IO_Z:
      if(All.StarformationOn == 0)
	return 0;
      else
	{
#ifdef GALSF
	  if(blocknr == IO_SFR)
	    return 1;
	  if(blocknr == IO_AGE)
	    return 1;
#endif
#ifdef METALS
	  if(blocknr == IO_Z)
	    return 1;
#endif
	}
      return 0;
      break;

    case IO_DELAYTIME:
#ifdef GALSF_SUBGRID_WINDS
    	return 1;
#else
    	return 0;
#endif
    	break;

    case IO_HeI:
    case IO_HeII:
#if defined(RADTRANSFER)
      return 1;
#else
      return 0;
#endif
      break;

    case IO_HII:
    case IO_HeIII:
      return 0;
      break;


    
    case IO_H2I:
    case IO_H2II:
    case IO_HM:
      return 0;
      break;

    case IO_HD:
    case IO_DI:
    case IO_DII:
    case IO_HeHII:
      return 0;
      break;

    case IO_POT:
#if defined(OUTPUTPOTENTIAL) || defined(SUBFIND_RESHUFFLE_AND_POTENTIAL)
      return 1;
#else
      return 0;
#endif


    case IO_VSTURB_DISS:
    case IO_VSTURB_DRIVE:
#if defined(TURB_DRIVING)
      return 1;
#else
      return 0;
#endif
      break;


    case IO_ACCEL:
#ifdef OUTPUTACCELERATION
      return 1;
#else
      return 0;
#endif
      break;


    case IO_DTENTR:
#ifdef OUTPUTCHANGEOFENERGY
      return 1;
#else
      return 0;
#endif
      break;

    case IO_STRESSDIAG:
      return 0;
      break;

    case IO_STRESSOFFDIAG:
      return 0;
      break;

    case IO_STRESSBULK:
      return 0;
      break;

    case IO_SHEARCOEFF:
      return 0;
      break;

    case IO_TSTP:
#ifdef OUTPUTTIMESTEP
      return 1;
#else
      return 0;
#endif


    case IO_BFLD:
#ifdef MAGNETIC
      return 1;
#else
      return 0;
#endif
      break;


    case IO_DBDT:
      return 0;
      break;

    case IO_VRMS:
    case IO_VBULK:
    case IO_TRUENGB:
      return 0;
      break;

    case IO_VRAD:
    case IO_VTAN:
      return 0;
      break;

    case IO_VDIV:
    case IO_VROT:
      return 0;
      break;

    case IO_VORT:
#if defined(TURB_DRIVING) || defined(OUTPUT_VORTICITY)
      return 1;
#else
      return 0;
#endif
      break;

    case IO_DPP:
#ifdef JD_DPP
      return 1;
#else
      return 0;
#endif
      break;

    case IO_IMF:
#ifdef GALSF_SFR_IMF_VARIATION
        return 1;
#else
        return 0;
#endif
        break;
            

    case IO_DIVB:
#ifdef MAGNETIC
      return 1;
#else
      return 0;
#endif
      break;

    case IO_ABVC:
#if defined(SPHAV_CD10_VISCOSITY_SWITCH)
      return 1;
#else
      return 0;
#endif
      break;


    case IO_AMDC:
#if defined(TRICCO_RESISTIVITY_SWITCH)
      return 1;
#else
      return 0;
#endif
      break;

    case IO_PHI:
#ifdef DIVBCLEANING_DEDNER
      return 1;
#else
      return 0;
#endif
      break;

    case IO_GRADPHI:
#ifdef DIVBCLEANING_DEDNER
      return 1;
#else
      return 0;
#endif
      break;


    case IO_ROTB:
      return 0;
      break;


    case IO_COOLRATE:
#ifdef OUTPUTCOOLRATE
      return 1;
#else
      return 0;
#endif
      break;


    case IO_CONDRATE:
#ifdef OUTPUTCONDRATE
      return 1;
#else
      return 0;
#endif
      break;


    case IO_DENN:
      return 0;
      break;


    case IO_ACRB:
    case IO_BHMASS:
    case IO_BHMASSALPHA:
    case IO_BHMDOT:
#ifdef BLACK_HOLES
      return 1;
#else
      return 0;
#endif
      break;

    case IO_BHPROGS:
#ifdef BH_COUNTPROGS
      return 1;
#else
      return 0;
#endif
      break;

    case IO_BHMBUB:
    case IO_BHMINI:
#ifdef BH_BUBBLES
      return 1;
#else
      return 0;
#endif
      break;

    case IO_BHMRAD:
#ifdef UNIFIED_FEEDBACK
      return 1;
#else
      return 0;
#endif
      break;

    case IO_MACH:
#if defined(MACHNUM)
      return 1;
#else
      return 0;
#endif
      break;

    case IO_DTENERGY:
#ifdef MACHSTATISTIC
      return 1;
#else
      return 0;
#endif
      break;

    case IO_PRESHOCK_CSND:
#ifdef OUTPUT_PRESHOCK_CSND


      return 1;
#else
      return 0;
#endif
      break;


    case IO_CR_C0:
    case IO_CR_Q0:
#ifdef COSMIC_RAYS
      return 1;
#else
      return 0;
#endif
      break;


    case IO_CR_P0:
    case IO_CR_E0:
    case IO_CR_n0:
#ifdef CR_OUTPUT_THERMO_VARIABLES
      return 1;
#else
      return 0;
#endif
      break;


    case IO_CR_ThermalizationTime:
    case IO_CR_DissipationTime:
#ifdef CR_OUTPUT_TIMESCALES
      return 1;
#else
      return 0;
#endif
      break;

    case IO_PRESHOCK_DENSITY:
#if defined(CR_OUTPUT_JUMP_CONDITIONS) || defined(OUTPUT_PRESHOCK_CSND)


      return 1;
#else
      return 0;
#endif
      break;

    case IO_PRESHOCK_ENERGY:
    case IO_PRESHOCK_XCR:
    case IO_DENSITY_JUMP:
    case IO_ENERGY_JUMP:
#ifdef CR_OUTPUT_JUMP_CONDITIONS
      return 1;
#else
      return 0;
#endif
      break;

    case IO_BPCR_pNORM:
    case IO_BPCR_eNORM:
    case IO_BPCR_pSLOPE:
    case IO_BPCR_eSLOPE:
    case IO_BPCR_pE:
    case IO_BPCR_pN:
    case IO_BPCR_pPRESSURE:
    case IO_BPCR_ePRESSURE:
#ifdef BP_REAL_CRs
      return 1;
#else
      return 0;
#endif
      break;

    case IO_CRINJECT:
#ifdef CR_OUTPUT_INJECTION
      return 1;
#else
      return 0;
#endif
      break;

    case IO_TIDALTENSORPS:
#ifdef OUTPUT_TIDALTENSORPS
      return 1;
#else
      return 0;
#endif
    case IO_DISTORTIONTENSORPS:
#ifdef OUTPUT_DISTORTIONTENSORPS
      return 1;
#else
      return 0;
#endif

    case IO_CAUSTIC_COUNTER:
#ifdef DISTORTIONTENSORPS
      return 1;
#else
      return 0;
#endif

    case IO_FLOW_DETERMINANT:
#if defined(DISTORTIONTENSORPS) && !defined(GDE_LEAN)
      return 1;
#else
      return 0;
#endif

    case IO_STREAM_DENSITY:
#ifdef DISTORTIONTENSORPS
      return 1;
#else
      return 0;
#endif

    case IO_PHASE_SPACE_DETERMINANT:
#ifdef DISTORTIONTENSORPS
      return 1;
#else
      return 0;
#endif

    case IO_ANNIHILATION_RADIATION:
#if defined(DISTORTIONTENSORPS) && !defined(GDE_LEAN)
      return 1;
#else
      return 0;
#endif

    case IO_LAST_CAUSTIC:
#ifdef OUTPUT_LAST_CAUSTIC
      return 1;
#else
      return 0;
#endif

    case IO_SHEET_ORIENTATION:
#if defined(DISTORTIONTENSORPS) && (!defined(GDE_LEAN) || defined(GDE_READIC)) 
      return 1;
#else
      return 0;
#endif

    case IO_INIT_DENSITY:
#if defined(DISTORTIONTENSORPS) && (!defined(GDE_LEAN) || defined(GDE_READIC)) 
      return 1;
#else
      return 0;
#endif


    case IO_SECONDORDERMASS:
      if(header.flag_ic_info == FLAG_SECOND_ORDER_ICS)
	return 1;
      else
	return 0;

    case IO_EOSTEMP:
    case IO_EOSXNUC:
    case IO_PRESSURE:
#ifdef EOS_DEGENERATE
      return 1;
#else
      return 0;
#endif

    case IO_EDDINGTON_TENSOR:
#if defined(RADTRANSFER) && defined(RT_OUTPUT_ET)
      return 1;
#else
      return 0;
#endif

    case IO_DMHSML:
    case IO_DMDENSITY:
    case IO_DMVELDISP:
#if defined(SUBFIND_RESHUFFLE_CATALOGUE) && defined(SUBFIND) 
      return 1;
#else
      return 0;
#endif
      break;

    case IO_DMHSML_V:
    case IO_DMDENSITY_V:
      return 0;
      break;

    case IO_CHEM:
      return 0;
      break;

    case IO_AGS_SOFT:
#if defined (ADAPTIVE_GRAVSOFT_FORALL) && defined(AGS_OUTPUTGRAVSOFT)
      return 1;
#else
      return 0;
#endif
      break;

/*
    case IO_AGS_DENS:
#if defined (ADAPTIVE_GRAVSOFT_FORALL) && defined(AGS_OUTPUTGRAVNUMDENS)
      return 1;
#else
      return 0;
#endif
      break;
*/
 
    case IO_AGS_ZETA:
#if defined (ADAPTIVE_GRAVSOFT_FORALL) && defined(AGS_OUTPUTZETA)
      return 1;
#else
      return 0;
#endif
      break;

    case IO_AGS_OMEGA:
      return 0;
      break;

    case IO_AGS_CORR:
      return 0;
      break;

    case IO_AGS_NGBS:
#if defined (ADAPTIVE_GRAVSOFT_FORALL) && defined(AGS_OUTPUTNGBS)
      return 1;
#else
      return 0;
#endif
      break;

    case IO_MG_PHI:
      return 0;
      break;
      
    case IO_MG_ACCEL:
      return 0;
      break;
      
    case IO_LASTENTRY:
      return 0;			/* will not occur */
    }


  return 0;			/* default: not present */
}




/*! This function associates a short 4-character block name with each block number.
 *  This is stored in front of each block for snapshot FileFormat=2.
 */
void get_Tab_IO_Label(enum iofields blocknr, char *label)
{
  switch (blocknr)
    {
    case IO_POS:
      strncpy(label, "POS ", 4);
      break;
    case IO_VEL:
      strncpy(label, "VEL ", 4);
      break;
    case IO_ID:
      strncpy(label, "ID  ", 4);
      break;
    case IO_MASS:
      strncpy(label, "MASS", 4);
      break;
    case IO_U:
      strncpy(label, "U   ", 4);
      break;
    case IO_RHO:
      strncpy(label, "RHO ", 4);
      break;
    case IO_NE:
      strncpy(label, "NE  ", 4);
      break;
    case IO_NH:
      strncpy(label, "NH  ", 4);
      break;
    case IO_HII:
      strncpy(label, "HII ", 4);
      break;
    case IO_HeI:
      strncpy(label, "HeI ", 4);
      break;
    case IO_HeII:
      strncpy(label, "HeII", 4);
      break;
    case IO_HeIII:
      strncpy(label, "He3 ", 4);
      break;
    case IO_H2I:
      strncpy(label, "H2I ", 4);
      break;
    case IO_H2II:
      strncpy(label, "H2II", 4);
      break;
    case IO_HM:
      strncpy(label, "HM  ", 4);
      break;
    case IO_HD:
      strncpy(label, "HD  ", 4);
      break;
    case IO_DI:
      strncpy(label, "DI  ", 4);
      break;
    case IO_DII:
      strncpy(label, "DII ", 4);
      break;
    case IO_HeHII:
      strncpy(label, "HeHp", 4);
      break;
    case IO_HSML:
      strncpy(label, "HSML", 4);
      break;
    case IO_SFR:
      strncpy(label, "SFR ", 4);
      break;
    case IO_AGE:
      strncpy(label, "AGE ", 4);
      break;
    case IO_DELAYTIME:
      strncpy(label, "DETI", 4);
      break;
    case IO_HSMS:
      strncpy(label, "HSMS", 4);
      break;
    case IO_Z:
      strncpy(label, "Z   ", 4);
      break;
    case IO_POT:
      strncpy(label, "POT ", 4);
      break;
    case IO_ACCEL:
      strncpy(label, "ACCE", 4);
      break;
    case IO_DTENTR:
      strncpy(label, "ENDT", 4);
      break;
    case IO_STRESSDIAG:
      strncpy(label, "STRD", 4);
      break;
    case IO_STRESSOFFDIAG:
      strncpy(label, "STRO", 4);
      break;
    case IO_STRESSBULK:
      strncpy(label, "STRB", 4);
      break;
    case IO_SHEARCOEFF:
      strncpy(label, "SHCO", 4);
      break;
    case IO_TSTP:
      strncpy(label, "TSTP", 4);
      break;
    case IO_BFLD:
      strncpy(label, "BFLD", 4);
      break;
    case IO_DBDT:
      strncpy(label, "DBDT", 4);
      break;
    case IO_VBULK:
      strncpy(label, "VBLK", 4);
      break;
    case IO_VRMS:
      strncpy(label, "VRMS", 4);
      break;
    case IO_VTAN:
      strncpy(label, "VTAN", 4);
      break;
    case IO_VRAD:
      strncpy(label, "VRAD", 4);
      break;  
    case IO_TRUENGB:
      strncpy(label, "TNGB", 4);
      break;
    case IO_DPP:
      strncpy(label, "DPP ", 4);
      break;
    case IO_VDIV:
      strncpy(label, "VDIV", 4);
      break;
    case IO_VROT:
      strncpy(label, "VROT", 4);
      break;
    case IO_VORT:
      strncpy(label, "VORT", 4);
      break;
    case IO_IMF:
      strncpy(label, "IMF ", 4);
      break;
    case IO_DIVB:
      strncpy(label, "DIVB", 4);
      break;
    case IO_ABVC:
      strncpy(label, "ABVC", 4);
      break;
    case IO_AMDC:
      strncpy(label, "AMDC", 4);
      break;
    case IO_PHI:
      strncpy(label, "PHI ", 4);
      break;
    case IO_GRADPHI:
      strncpy(label, "GPHI", 4);
      break;
    case IO_ROTB:
      strncpy(label, "ROTB", 4);
      break;
    case IO_COOLRATE:
      strncpy(label, "COOR", 4);
      break;
    case IO_CONDRATE:
      strncpy(label, "CONR", 4);
      break;
    case IO_DENN:
      strncpy(label, "DENN", 4);
      break;
    case IO_CR_C0:
      strncpy(label, "CRC0", 4);
      break;
    case IO_CR_Q0:
      strncpy(label, "CRQ0", 4);
      break;
    case IO_CR_P0:
      strncpy(label, "CRP0", 4);
      break;
    case IO_CR_E0:
      strncpy(label, "CRE0", 4);
      break;
    case IO_CR_n0:
      strncpy(label, "CRn0", 4);
      break;
    case IO_CR_ThermalizationTime:
      strncpy(label, "CRco", 4);
      break;
    case IO_CR_DissipationTime:
      strncpy(label, "CRdi", 4);
      break;
    case IO_BHMASS:
      strncpy(label, "BHMA", 4);
      break;
    case IO_BHMASSALPHA:
      strncpy(label, "BHMa", 4);
      break;
    case IO_ACRB:
      strncpy(label, "ACRB", 4);
      break;
    case IO_BHMDOT:
      strncpy(label, "BHMD", 4);
      break;
    case IO_BHPROGS:
      strncpy(label, "BHPC", 4);
      break;
    case IO_BHMBUB:
      strncpy(label, "BHMB", 4);
      break;
    case IO_BHMINI:
      strncpy(label, "BHMI", 4);
      break;
    case IO_BHMRAD:
      strncpy(label, "BHMR", 4);
      break;
    case IO_MACH:
      strncpy(label, "MACH", 4);
      break;
    case IO_DTENERGY:
      strncpy(label, "DTEG", 4);
      break;
    case IO_PRESHOCK_CSND:
      strncpy(label, "PSCS", 4);
      break;
    case IO_PRESHOCK_DENSITY:
      strncpy(label, "PSDE", 4);
      break;
    case IO_PRESHOCK_ENERGY:
      strncpy(label, "PSEN", 4);
      break;
    case IO_PRESHOCK_XCR:
      strncpy(label, "PSXC", 4);
      break;
    case IO_DENSITY_JUMP:
      strncpy(label, "DJMP", 4);
      break;
    case IO_ENERGY_JUMP:
      strncpy(label, "EJMP", 4);
      break;
    case IO_CRINJECT:
      strncpy(label, "CRDE", 4);
      break;
    case IO_TIDALTENSORPS:
      strncpy(label, "TIPS", 4);
      break;
    case IO_DISTORTIONTENSORPS:
      strncpy(label, "DIPS", 4);
      break;
    case IO_CAUSTIC_COUNTER:
      strncpy(label, "CACO", 4);
      break;
    case IO_FLOW_DETERMINANT:
      strncpy(label, "FLDE", 4);
      break;
    case IO_STREAM_DENSITY:
      strncpy(label, "STDE", 4);
      break;
    case IO_SECONDORDERMASS:
      strncpy(label, "SOMA", 4);
      break;
    case IO_PHASE_SPACE_DETERMINANT:
      strncpy(label, "PSDE", 4);
      break;
    case IO_ANNIHILATION_RADIATION:
      strncpy(label, "ANRA", 4);
      break;
    case IO_LAST_CAUSTIC:
      strncpy(label, "LACA", 4);
      break;
    case IO_SHEET_ORIENTATION:
      strncpy(label, "SHOR", 4);
      break;
    case IO_INIT_DENSITY:
      strncpy(label, "INDE", 4);
      break;
    case IO_EOSTEMP:
      strncpy(label, "TEMP", 4);
      break;
    case IO_EOSXNUC:
      strncpy(label, "XNUC", 4);
      break;
    case IO_PRESSURE:
      strncpy(label, "P   ", 4);
      break;
    case IO_RADGAMMA:
      strncpy(label, "RADG", 4);
      break;
    case IO_RAD_ACCEL:
      strncpy(label, "RADA", 4);
      break;
    case IO_EDDINGTON_TENSOR:
      strncpy(label, "ET", 4);
      break;
    case IO_DMHSML:
      strncpy(label, "DMHS", 4);
      break;
    case IO_DMDENSITY:
      strncpy(label, "DMDE", 4);
      break;
    case IO_DMVELDISP:
      strncpy(label, "DMVD", 4);
      break;
    case IO_DMHSML_V:
      strncpy(label, "VDMH", 4);
      break;
    case IO_DMDENSITY_V:
      strncpy(label, "VDMD", 4);
      break;
    case IO_CHEM:
      strncpy(label, "CHEM", 4);
      break;
    case IO_BPCR_pNORM:
      strncpy(label, "CRpN", 4);
      break;
    case IO_BPCR_eNORM:
      strncpy(label, "CReN", 4);
      break;
    case IO_BPCR_pSLOPE:
      strncpy(label, "CRpS", 4);
      break;
    case IO_BPCR_eSLOPE:
      strncpy(label, "CReS", 4);
      break;
    case IO_BPCR_pE:
      strncpy(label, "CRpE", 4);
      break;
    case IO_BPCR_pN:
      strncpy(label, "CRpD", 4);
      break;
    case IO_BPCR_pPRESSURE:
      strncpy(label, "CRpP", 4);
      break;
    case IO_BPCR_ePRESSURE:
      strncpy(label, "CReP", 4);
      break;
    case IO_AGS_SOFT:
      strncpy(label, "AGSH", 4);
      break;
    case IO_AGS_ZETA:
      strncpy(label, "AGSZ", 4);
      break;
    case IO_AGS_OMEGA:
      strncpy(label, "AGSO", 4);
      break;
    case IO_AGS_CORR:
      strncpy(label, "AGSC", 4);
      break;
    case IO_AGS_NGBS:
      strncpy(label, "AGSN", 4);
      break;
    case IO_VSTURB_DISS:
      strncpy(label, "VSDI", 4);
      break;
    case IO_VSTURB_DRIVE:
      strncpy(label, "VSDR", 4);
      break;
    case IO_MG_PHI:
      strncpy(label, "MGPH", 4);
      break;
    case IO_MG_ACCEL:
      strncpy(label, "MGAC", 4);
      break;  
      
    case IO_LASTENTRY:
      endrun(217);
      break;
    }
}


void get_dataset_name(enum iofields blocknr, char *buf)
{
  strcpy(buf, "default");

  switch (blocknr)
    {
    case IO_POS:
      strcpy(buf, "Coordinates");
      break;
    case IO_VEL:
      strcpy(buf, "Velocities");
      break;
    case IO_ID:
      strcpy(buf, "ParticleIDs");
      break;
    case IO_MASS:
      strcpy(buf, "Masses");
      break;
    case IO_U:
      strcpy(buf, "InternalEnergy");
      break;
    case IO_RHO:
      strcpy(buf, "Density");
      break;
    case IO_NE:
      strcpy(buf, "ElectronAbundance");
      break;
    case IO_NH:
      strcpy(buf, "NeutralHydrogenAbundance");
      break;
    case IO_RADGAMMA:
      strcpy(buf, "photon number density");
      break;
    case IO_RAD_ACCEL:
      strcpy(buf, "rad acceleration");
      break;
    case IO_HII:
      strcpy(buf, "HII");
      break;
    case IO_HeI:
      strcpy(buf, "HeI");
      break;
    case IO_HeII:
      strcpy(buf, "HeII");
      break;
    case IO_HeIII:
      strcpy(buf, "HeIII");
      break;
    case IO_H2I:
      strcpy(buf, "H2I");
      break;
    case IO_H2II:
      strcpy(buf, "H2II");
      break;
    case IO_HM:
      strcpy(buf, "HM");
      break;
    case IO_HD:
      strcpy(buf, "HD  ");
      break;
    case IO_DI:
      strcpy(buf, "DI  ");
      break;
    case IO_DII:
      strcpy(buf, "DII ");
      break;
    case IO_HeHII:
      strcpy(buf, "HeHp");
      break;
    case IO_DELAYTIME:
      strcpy(buf, "DelayTime");
      break;
    case IO_HSML:
      strcpy(buf, "SmoothingLength");
      break;
    case IO_SFR:
      strcpy(buf, "StarFormationRate");
      break;
    case IO_AGE:
      strcpy(buf, "StellarFormationTime");
      break;
    case IO_HSMS:
      strcpy(buf, "StellarSmoothingLength");
      break;
    case IO_Z:
      strcpy(buf, "Metallicity");
      break;
    case IO_POT:
      strcpy(buf, "Potential");
      break;
    case IO_ACCEL:
      strcpy(buf, "Acceleration");
      break;
    case IO_DTENTR:
      strcpy(buf, "RateOfChangeOfInternalEnergy");
      break;
    case IO_STRESSDIAG:
      strcpy(buf, "DiagonalStressTensor");
      break;
    case IO_STRESSOFFDIAG:
      strcpy(buf, "OffDiagonalStressTensor");
      break;
    case IO_STRESSBULK:
      strcpy(buf, "BulkStressTensor");
      break;
    case IO_SHEARCOEFF:
      strcpy(buf, "ShearCoefficient");
      break;
    case IO_TSTP:
      strcpy(buf, "TimeStep");
      break;
    case IO_BFLD:
      strcpy(buf, "MagneticField");
      break;
    case IO_DBDT:
      strcpy(buf, "RateOfChangeOfMagneticField");
      break;
    case IO_VRMS:
      strcpy(buf, "RMSVelocity");
      break;
    case IO_VBULK:
      strcpy(buf, "BulkVelocity");
      break;
    case IO_VRAD:
      strcpy(buf, "RMSRadialVelocity");
      break;
    case IO_VTAN:
      strcpy(buf, "RMSTangentialVelocity");
      break;
    case IO_TRUENGB:
      strcpy(buf, "TrueNumberOfNeighbours");
      break;
    case IO_DPP:
      strcpy(buf, "MagnetosReaccCoefficient");
      break;
    case IO_VDIV:
      strcpy(buf, "VelocityDivergence");
      break;
    case IO_VROT:
      strcpy(buf, "VelocityCurl");
      break;
    case IO_VORT:
      strcpy(buf, "Vorticity");
      break;
    case IO_IMF:
      strcpy(buf, "IMFTurnOverMass");
      break;
    case IO_DIVB:
      strcpy(buf, "DivergenceOfMagneticField");
      break;
    case IO_ABVC:
      strcpy(buf, "ArtificialViscosity");
      break;
    case IO_AMDC:
      strcpy(buf, "ArtMagneticDissipation");
      break;
    case IO_PHI:
      strcpy(buf, "DivBcleaningFunctionPhi");
      break;
    case IO_GRADPHI:
      strcpy(buf, "DivBcleaningFunctionGradPhi");
      break;
    case IO_ROTB:
      strcpy(buf, "RotationB");
      break;
    case IO_COOLRATE:
      strcpy(buf, "CoolingRate");
      break;
    case IO_CONDRATE:
      strcpy(buf, "ConductionRate");
      break;
    case IO_DENN:
      strcpy(buf, "Denn");
      break;
    case IO_CR_C0:
      strcpy(buf, "CR_C0");
      break;
    case IO_CR_Q0:
      strcpy(buf, "CR_q0");
      break;
    case IO_CR_P0:
      strcpy(buf, "CR_P0");
      break;
    case IO_CR_E0:
      strcpy(buf, "CR_E0");
      break;
    case IO_CR_n0:
      strcpy(buf, "CR_n0");
      break;
    case IO_CR_ThermalizationTime:
      strcpy(buf, "CR_ThermalizationTime");
      break;
    case IO_CR_DissipationTime:
      strcpy(buf, "CR_DissipationTime");
      break;
    case IO_BHMASS:
      strcpy(buf, "BH_Mass");
      break;
    case IO_BHMASSALPHA:
      strcpy(buf, "BH_Mass_AlphaDisk");
      break;
    case IO_ACRB:
      strcpy(buf, "BH_AccretionLength");
      break;
    case IO_BHMDOT:
      strcpy(buf, "BH_Mdot");
      break;
    case IO_BHPROGS:
      strcpy(buf, "BH_NProgs");
      break;
    case IO_BHMBUB:
      strcpy(buf, "BH_Mass_bubbles");
      break;
    case IO_BHMINI:
      strcpy(buf, "BH_Mass_ini");
      break;
    case IO_BHMRAD:
      strcpy(buf, "BH_Mass_radio");
      break;
    case IO_MACH:
      strcpy(buf, "MachNumber");
      break;
    case IO_DTENERGY:
      strcpy(buf, "DtEnergy");
      break;
    case IO_PRESHOCK_CSND:
      strcpy(buf, "Preshock_SoundSpeed");
      break;
    case IO_PRESHOCK_DENSITY:
      strcpy(buf, "Preshock_Density");
      break;
    case IO_PRESHOCK_ENERGY:
      strcpy(buf, "Preshock_Energy");
      break;
    case IO_PRESHOCK_XCR:
      strcpy(buf, "Preshock_XCR");
      break;
    case IO_DENSITY_JUMP:
      strcpy(buf, "DensityJump");
      break;
    case IO_ENERGY_JUMP:
      strcpy(buf, "EnergyJump");
      break;
    case IO_CRINJECT:
      strcpy(buf, "CR_DtE");
      break;
    case IO_TIDALTENSORPS:
      strcpy(buf, "TidalTensorPS");
      break;
    case IO_DISTORTIONTENSORPS:
      strcpy(buf, "DistortionTensorPS");
      break;
    case IO_CAUSTIC_COUNTER:
      strcpy(buf, "CausticCounter");
      break;
    case IO_FLOW_DETERMINANT:
      strcpy(buf, "FlowDeterminant");
      break;
    case IO_STREAM_DENSITY:
      strcpy(buf, "StreamDensity");
      break;
    case IO_SECONDORDERMASS:
      strcpy(buf, "2lpt-mass");
      break;
    case IO_PHASE_SPACE_DETERMINANT:
      strcpy(buf, "PhaseSpaceDensity");
      break;
    case IO_ANNIHILATION_RADIATION:
      strcpy(buf, "AnnihilationRadiation");
      break;
    case IO_LAST_CAUSTIC:
      strcpy(buf, "LastCaustic");
      break;
    case IO_SHEET_ORIENTATION:
      strcpy(buf, "SheetOrientation");
      break;
    case IO_INIT_DENSITY:
      strcpy(buf, "InitDensity");
      break;
    case IO_EOSTEMP:
      strcpy(buf, "Temperature");
      break;
    case IO_EOSXNUC:
      strcpy(buf, "Nuclear mass fractions");
      break;
    case IO_PRESSURE:
      strcpy(buf, "Pressure");
      break;
    case IO_EDDINGTON_TENSOR:
      strcpy(buf, "EddingtonTensor");
      break;
    case IO_DMHSML:
      strcpy(buf, "DM Hsml");
      break;
    case IO_DMDENSITY:
      strcpy(buf, "DM Density");
      break;
    case IO_DMVELDISP:
      strcpy(buf, "DM Velocity Dispersion");
      break;
    case IO_DMHSML_V:
      strcpy(buf, "DM Hsml Voronoi");
      break;
    case IO_DMDENSITY_V:
      strcpy(buf, "DM Density Voronoi");
      break;
    case IO_CHEM:
      strcpy(buf, "ChemicalAbundances");
      break;
    case IO_BPCR_pNORM:
      strcpy(buf, "BPCRpNormalization");
      break;
    case IO_BPCR_eNORM:
      strcpy(buf, "BPCReNormalization");
      break;
    case IO_BPCR_pSLOPE:
      strcpy(buf, "BPCRpSlope");
      break;
    case IO_BPCR_eSLOPE:
      strcpy(buf, "BPCReSlope");
      break;
    case IO_BPCR_pE:
      strcpy(buf, "BPCRpEnergy");
      break;
    case IO_BPCR_pN:
      strcpy(buf, "BPCRpNumber");
      break;
    case IO_BPCR_pPRESSURE:
      strcpy(buf, "BPCRpPressure");
      break;
    case IO_BPCR_ePRESSURE:
      strcpy(buf, "BPCRePressure");
      break;
    case IO_AGS_SOFT:
      strcpy(buf, "AGS-Softening");
      break;
    case IO_AGS_ZETA:
      strcpy(buf, "AGS-Zeta");
      break;
    case IO_AGS_OMEGA:
      strcpy(buf, "AGS-Omega");
      break;
    case IO_AGS_CORR:
      strcpy(buf, "AGS-Correction");
      break;
    case IO_AGS_NGBS:
      strcpy(buf, "AGS-Neighbours");
      break;
    case IO_VSTURB_DISS:
      strcpy(buf, "TurbulenceDissipation");
      break;
    case IO_VSTURB_DRIVE:
      strcpy(buf, "TurbulenceDriving");
      break;
    case IO_MG_PHI:
      strcpy(buf, "ModifiedGravityPhi");
      break;  
    case IO_MG_ACCEL:
      strcpy(buf, "ModifiedGravityAcceleration");
      break;
      
    case IO_LASTENTRY:
      endrun(218);
      break;
    }
}



/*! This function writes a snapshot file containing the data from processors
 *  'writeTask' to 'lastTask'. 'writeTask' is the one that actually writes.
 *  Each snapshot file contains a header first, then particle positions,
 *  velocities and ID's.  Then particle masses are written for those particle
 *  types with zero entry in MassTable.  After that, first the internal
 *  energies u, and then the density is written for the SPH particles.  If
 *  cooling is enabled, mean molecular weight and neutral hydrogen abundance
 *  are written for the gas particles. This is followed by the gas kernel
 *  length and further blocks of information, depending on included physics
 *  and compile-time flags.
 */
void write_file(char *fname, int writeTask, int lastTask)
{
  int type, bytes_per_blockelement, npart, nextblock, typelist[6];
  int n_for_this_task, n, p, pc, offset = 0, task, i;
  size_t blockmaxlen;
  int ntot_type[6], nn[6];
  enum iofields blocknr;
  char label[8];
  int bnr;
  int blksize;
  MPI_Status status;
  FILE *fd = 0;

#ifdef HAVE_HDF5
  hid_t hdf5_file = 0, hdf5_grp[6], hdf5_headergrp = 0, hdf5_dataspace_memory;
  hid_t hdf5_datatype = 0, hdf5_dataspace_in_file = 0, hdf5_dataset = 0;
  herr_t hdf5_status;
  hsize_t dims[2], count[2], start[2];
  int rank = 0, pcsum = 0;
  char buf[500];
#endif

#ifdef COSMIC_RAYS
  int CRpop;
#endif

#define SKIP  {my_fwrite(&blksize,sizeof(int),1,fd);}

#ifdef SUBFIND_RESHUFFLE_AND_POTENTIAL
  FILE *fd_pot = 0;

#define SKIP_POT  {my_fwrite(&blksize,sizeof(int),1,fd_pot);}
#endif

  /* determine particle numbers of each type in file */

  if(ThisTask == writeTask)
    {
      for(n = 0; n < 6; n++)
	ntot_type[n] = n_type[n];

      for(task = writeTask + 1; task <= lastTask; task++)
	{
	  MPI_Recv(&nn[0], 6, MPI_INT, task, TAG_LOCALN, MPI_COMM_WORLD, &status);
	  for(n = 0; n < 6; n++)
	    ntot_type[n] += nn[n];
	}

      for(task = writeTask + 1; task <= lastTask; task++)
	MPI_Send(&ntot_type[0], 6, MPI_INT, task, TAG_N, MPI_COMM_WORLD);
    }
  else
    {
      MPI_Send(&n_type[0], 6, MPI_INT, writeTask, TAG_LOCALN, MPI_COMM_WORLD);
      MPI_Recv(&ntot_type[0], 6, MPI_INT, writeTask, TAG_N, MPI_COMM_WORLD, &status);
    }

  /* fill file header */

  for(n = 0; n < 6; n++)
    {
      header.npart[n] = ntot_type[n];
      header.npartTotal[n] = (unsigned int) ntot_type_all[n];
      header.npartTotalHighWord[n] = (unsigned int) (ntot_type_all[n] >> 32);
    }

  if(header.flag_ic_info == FLAG_SECOND_ORDER_ICS)
    header.flag_ic_info = FLAG_EVOLVED_2LPT;

  if(header.flag_ic_info == FLAG_ZELDOVICH_ICS)
    header.flag_ic_info = FLAG_EVOLVED_ZELDOVICH;

  if(header.flag_ic_info == FLAG_NORMALICS_2LPT)
    header.flag_ic_info = FLAG_EVOLVED_2LPT;

  if(header.flag_ic_info == 0 && All.ComovingIntegrationOn != 0)
    header.flag_ic_info = FLAG_EVOLVED_ZELDOVICH;

  for(n = 0; n < 6; n++)
    header.mass[n] = All.MassTable[n];

#ifdef COSMIC_RAYS
  for(CRpop = 0; CRpop < NUMCRPOP; CRpop++)
    header.SpectralIndex_CR_Pop[CRpop] = All.CR_Alpha[CRpop];
#endif


  header.time = All.Time;

  if(All.ComovingIntegrationOn)
    header.redshift = 1.0 / All.Time - 1;
  else
    header.redshift = 0;

  header.flag_sfr = 0;
  header.flag_feedback = 0;
  header.flag_cooling = 0;
  header.flag_stellarage = 0;
  header.flag_metals = 0;

#ifdef COOLING
  header.flag_cooling = 1;
#endif

#ifdef GALSF
  header.flag_sfr = 1;
  header.flag_feedback = 1;
  header.flag_stellarage = 1;
#endif

#ifdef METALS
  header.flag_metals = NUM_METAL_SPECIES;
#endif

  header.num_files = All.NumFilesPerSnapshot;
  header.BoxSize = All.BoxSize;
  header.Omega0 = All.Omega0;
  header.OmegaLambda = All.OmegaLambda;
  header.HubbleParam = All.HubbleParam;

#ifdef OUTPUT_IN_DOUBLEPRECISION
  header.flag_doubleprecision = 1;
#else
  header.flag_doubleprecision = 0;
#endif

  /* open file and write header */

  if(ThisTask == writeTask)
    {
      if(All.SnapFormat == 3)
	{
#ifdef HAVE_HDF5
	  sprintf(buf, "%s.hdf5", fname);
	  hdf5_file = H5Fcreate(buf, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);

	  hdf5_headergrp = H5Gcreate(hdf5_file, "/Header", 0);

	  for(type = 0; type < 6; type++)
	    {
	      if(header.npart[type] > 0)
		{
		  sprintf(buf, "/PartType%d", type);
		  hdf5_grp[type] = H5Gcreate(hdf5_file, buf, 0);
		}
	    }

	  write_header_attributes_in_hdf5(hdf5_headergrp);
#endif
	}
      else
	{
	  if(!(fd = fopen(fname, "w")))
	    {
	      printf("can't open file `%s' for writing snapshot.\n", fname);
	      endrun(123);
	    }

	  if(All.SnapFormat == 2)
	    {
	      blksize = sizeof(int) + 4 * sizeof(char);
	      SKIP;
	      my_fwrite((void *) "HEAD", sizeof(char), 4, fd);
	      nextblock = sizeof(header) + 2 * sizeof(int);
	      my_fwrite(&nextblock, sizeof(int), 1, fd);
	      SKIP;
	    }

	  blksize = sizeof(header);
	  SKIP;
	  my_fwrite(&header, sizeof(header), 1, fd);
	  SKIP;
	}
#ifdef SUBFIND_RESHUFFLE_AND_POTENTIAL
      char fname_pot[1000], *s_split_1, *s_split_2;

      s_split_1 = strtok(fname, ".");
      s_split_2 = strtok(NULL, " ,");

      sprintf(fname_pot, "%s_pot.%s", s_split_1, s_split_2);
      if(!(fd_pot = fopen(fname_pot, "w")))
	{
	  printf("can't open file `%s' for writing pot_snapshot.\n", fname_pot);
	  endrun(1234);
	}
#endif
    }

  if((All.SnapFormat == 1 || All.SnapFormat == 2) && ThisTask == writeTask)
    {
      n_info = 0;
      InfoBlock = (struct info_block *) mymalloc("InfoBlock", sizeof(struct info_block) * 1000);

      for(bnr = 0; bnr < 1000; bnr++)
	{
	  blocknr = (enum iofields) bnr;

	  if(blocknr == IO_SECONDORDERMASS)
	    continue;

	  if(blocknr == IO_LASTENTRY)
	    break;

	  if(blockpresent(blocknr))
	    {
	      bytes_per_blockelement = get_bytes_per_blockelement(blocknr, 0);
	      npart = get_particles_in_block(blocknr, &typelist[0]);

	      if(npart > 0)
		{
		  for(type = 0; type < 6; type++)
		    InfoBlock[n_info].is_present[type] = typelist[type];
		  InfoBlock[n_info].ndim = get_values_per_blockelement(blocknr);
		  get_Tab_IO_Label(blocknr, label);
		  for(i = 0; i < 4; i++)
		    InfoBlock[n_info].label[i] = label[i];
		  switch (get_datatype_in_block(blocknr))
		    {
		    case 0:
		      if(InfoBlock[n_info].ndim <= 1)
			strncpy(InfoBlock[n_info].type, "LONG    ", 8);
		      else
			strncpy(InfoBlock[n_info].type, "LONGN   ", 8);
		      break;
		    case 1:
#ifdef OUTPUT_IN_DOUBLEPRECISION
                      if(InfoBlock[n_info].ndim <= 1)
                        strncpy(InfoBlock[n_info].type, "DOUBLE  ", 8);
                      else
                        strncpy(InfoBlock[n_info].type, "DOUBLEN ", 8);
                      break;
#else
		      if(InfoBlock[n_info].ndim <= 1)
			strncpy(InfoBlock[n_info].type, "FLOAT   ", 8);
		      else
			strncpy(InfoBlock[n_info].type, "FLOATN  ", 8);
		      break;
#endif
		    case 2:
		      if(InfoBlock[n_info].ndim <= 1)
			strncpy(InfoBlock[n_info].type, "LLONG   ", 8);
		      else
			strncpy(InfoBlock[n_info].type, "LLONGN  ", 8);
		      break;
		    }
		  n_info++;
		}
	    }
	}
    }

  for(bnr = 0; bnr < 1000; bnr++)
    {
      blocknr = (enum iofields) bnr;

      if(blocknr == IO_SECONDORDERMASS)
	continue;

      if(blocknr == IO_LASTENTRY)
	break;

      if(blockpresent(blocknr))
	{
	  bytes_per_blockelement = get_bytes_per_blockelement(blocknr, 0);

	  blockmaxlen = (size_t) ((All.BufferSize * 1024 * 1024) / bytes_per_blockelement);

	  npart = get_particles_in_block(blocknr, &typelist[0]);

	  if(npart > 0)
	    {
	      if(ThisTask == 0)
		{
		  char buf[1000];

		  get_dataset_name(blocknr, buf);
		  printf("writing block %d (%s)...\n", bnr, buf);
		}

	      if(ThisTask == writeTask)
		{

		  if(All.SnapFormat == 1 || All.SnapFormat == 2)
		    {
		      if(All.SnapFormat == 2)
			{
			  blksize = sizeof(int) + 4 * sizeof(char);
			  SKIP;
			  get_Tab_IO_Label(blocknr, label);
			  my_fwrite(label, sizeof(char), 4, fd);
			  nextblock = npart * bytes_per_blockelement + 2 * sizeof(int);
			  my_fwrite(&nextblock, sizeof(int), 1, fd);
			  SKIP;
			}
		      blksize = npart * bytes_per_blockelement;
#ifdef SUBFIND_RESHUFFLE_AND_POTENTIAL
		      if(blocknr == IO_POT)
			SKIP_POT;
		      if(blocknr != IO_POT)
			SKIP;
#else
		      SKIP;
#endif
		    }
		}

	      for(type = 0; type < 6; type++)
		{
		  if(typelist[type])
		    {
#ifdef HAVE_HDF5
			if(ThisTask == writeTask && All.SnapFormat == 3 && header.npart[type] > 0)
			  {
			    switch (get_datatype_in_block(blocknr))
			      {
			      case 0:
				hdf5_datatype = H5Tcopy(H5T_NATIVE_UINT);
				break;
			      case 1:
#ifdef OUTPUT_IN_DOUBLEPRECISION
				hdf5_datatype = H5Tcopy(H5T_NATIVE_DOUBLE);
#else
				hdf5_datatype = H5Tcopy(H5T_NATIVE_FLOAT);
#endif
				break;
			      case 2:
				hdf5_datatype = H5Tcopy(H5T_NATIVE_UINT64);
				break;
			      }

			    dims[0] = header.npart[type];
			    dims[1] = get_values_per_blockelement(blocknr);
			    if(dims[1] == 1)
			      rank = 1;
			    else
			      rank = 2;

			    get_dataset_name(blocknr, buf);

			    hdf5_dataspace_in_file = H5Screate_simple(rank, dims, NULL);
			    hdf5_dataset =
			      H5Dcreate(hdf5_grp[type], buf, hdf5_datatype, hdf5_dataspace_in_file,
					H5P_DEFAULT);

			    pcsum = 0;
			  }
#endif

		      for(task = writeTask, offset = 0; task <= lastTask; task++)
			{
			  if(task == ThisTask)
			    {
			      n_for_this_task = n_type[type];

			      for(p = writeTask; p <= lastTask; p++)
				if(p != ThisTask)
				  MPI_Send(&n_for_this_task, 1, MPI_INT, p, TAG_NFORTHISTASK, MPI_COMM_WORLD);
			    }
			  else
			    MPI_Recv(&n_for_this_task, 1, MPI_INT, task, TAG_NFORTHISTASK, MPI_COMM_WORLD,
				     &status);

			  while(n_for_this_task > 0)
			    {
			      pc = n_for_this_task;

			      if(pc > (int)blockmaxlen)
				pc = blockmaxlen;

			      if(ThisTask == task)
				fill_write_buffer(blocknr, &offset, pc, type);

			      if(ThisTask == writeTask && task != writeTask)
				MPI_Recv(CommBuffer, bytes_per_blockelement * pc, MPI_BYTE, task,
					 TAG_PDATA, MPI_COMM_WORLD, &status);

			      if(ThisTask != writeTask && task == ThisTask)
				MPI_Ssend(CommBuffer, bytes_per_blockelement * pc, MPI_BYTE, writeTask,
					  TAG_PDATA, MPI_COMM_WORLD);

			      if(ThisTask == writeTask)
				{
				  if(All.SnapFormat == 3)
				    {
#ifdef HAVE_HDF5
					  start[0] = pcsum;
					  start[1] = 0;

					  count[0] = pc;
					  count[1] = get_values_per_blockelement(blocknr);
					  pcsum += pc;

					  H5Sselect_hyperslab(hdf5_dataspace_in_file, H5S_SELECT_SET,
							      start, NULL, count, NULL);

					  dims[0] = pc;
					  dims[1] = get_values_per_blockelement(blocknr);
					  hdf5_dataspace_memory = H5Screate_simple(rank, dims, NULL);

					  hdf5_status =
					    H5Dwrite(hdf5_dataset, hdf5_datatype,
						     hdf5_dataspace_memory,
						     hdf5_dataspace_in_file, H5P_DEFAULT, CommBuffer);

					  H5Sclose(hdf5_dataspace_memory);
#endif
				    }
				  else
				    {
#ifdef SUBFIND_RESHUFFLE_AND_POTENTIAL
				      if(blocknr == IO_POT)
					my_fwrite(CommBuffer, bytes_per_blockelement, pc, fd_pot);
				      else
					my_fwrite(CommBuffer, bytes_per_blockelement, pc, fd);
#else
				      my_fwrite(CommBuffer, bytes_per_blockelement, pc, fd);
#endif
				    }
				}

			      n_for_this_task -= pc;
			    }
			}

#ifdef HAVE_HDF5
		      if(ThisTask == writeTask && All.SnapFormat == 3 && header.npart[type] > 0)
			{
			  if(All.SnapFormat == 3)
			    {
			      H5Dclose(hdf5_dataset);
			      H5Sclose(hdf5_dataspace_in_file);
			      H5Tclose(hdf5_datatype);
			    }
			}
#endif
		    }
		}

	      if(ThisTask == writeTask)
		{
		  if(All.SnapFormat == 1 || All.SnapFormat == 2)
		    {
#ifdef SUBFIND_RESHUFFLE_AND_POTENTIAL
		      if(blocknr == IO_POT)
			SKIP_POT;
		      if(blocknr != IO_POT)
			SKIP;
#else
		      SKIP;
#endif
		    }
		}
	    }
	}
    }

  if((All.SnapFormat == 1 || All.SnapFormat == 2) && ThisTask == writeTask)
    {
      myfree(InfoBlock);
    }

  if(ThisTask == writeTask)
    {
      if(All.SnapFormat == 3)
	{
#ifdef HAVE_HDF5
	  for(type = 5; type >= 0; type--)
	    if(header.npart[type] > 0)
	      H5Gclose(hdf5_grp[type]);
	  H5Gclose(hdf5_headergrp);
	  H5Fclose(hdf5_file);
#endif
	}
      else
	{
	  fclose(fd);
	}
#ifdef SUBFIND_RESHUFFLE_AND_POTENTIAL
      fclose(fd_pot);
#endif
    }
}




#ifdef HAVE_HDF5
void write_header_attributes_in_hdf5(hid_t handle)
{
  hsize_t adim[1] = { 6 };

#ifdef COSMIC_RAYS
  hsize_t adim_alpha[1] = { NUMCRPOP };
#endif
  hid_t hdf5_dataspace, hdf5_attribute;

  hdf5_dataspace = H5Screate(H5S_SIMPLE);
  H5Sset_extent_simple(hdf5_dataspace, 1, adim, NULL);
  hdf5_attribute = H5Acreate(handle, "NumPart_ThisFile", H5T_NATIVE_INT, hdf5_dataspace, H5P_DEFAULT);
  H5Awrite(hdf5_attribute, H5T_NATIVE_UINT, header.npart);
  H5Aclose(hdf5_attribute);
  H5Sclose(hdf5_dataspace);

  hdf5_dataspace = H5Screate(H5S_SIMPLE);
  H5Sset_extent_simple(hdf5_dataspace, 1, adim, NULL);
  hdf5_attribute = H5Acreate(handle, "NumPart_Total", H5T_NATIVE_UINT, hdf5_dataspace, H5P_DEFAULT);
  H5Awrite(hdf5_attribute, H5T_NATIVE_UINT, header.npartTotal);
  H5Aclose(hdf5_attribute);
  H5Sclose(hdf5_dataspace);

  hdf5_dataspace = H5Screate(H5S_SIMPLE);
  H5Sset_extent_simple(hdf5_dataspace, 1, adim, NULL);
  hdf5_attribute = H5Acreate(handle, "NumPart_Total_HighWord", H5T_NATIVE_UINT, hdf5_dataspace, H5P_DEFAULT);
  H5Awrite(hdf5_attribute, H5T_NATIVE_UINT, header.npartTotalHighWord);
  H5Aclose(hdf5_attribute);
  H5Sclose(hdf5_dataspace);

  hdf5_dataspace = H5Screate(H5S_SIMPLE);
  H5Sset_extent_simple(hdf5_dataspace, 1, adim, NULL);
  hdf5_attribute = H5Acreate(handle, "MassTable", H5T_NATIVE_DOUBLE, hdf5_dataspace, H5P_DEFAULT);
  H5Awrite(hdf5_attribute, H5T_NATIVE_DOUBLE, header.mass);
  H5Aclose(hdf5_attribute);
  H5Sclose(hdf5_dataspace);

#ifdef COSMIC_RAYS
  hdf5_dataspace = H5Screate(H5S_SIMPLE);
  H5Sset_extent_simple(hdf5_dataspace, 1, adim_alpha, NULL);
  hdf5_attribute = H5Acreate(handle, "SpectralIndex_CR_Pop", H5T_NATIVE_DOUBLE, hdf5_dataspace, H5P_DEFAULT);
  H5Awrite(hdf5_attribute, H5T_NATIVE_DOUBLE, header.SpectralIndex_CR_Pop);
  H5Aclose(hdf5_attribute);
  H5Sclose(hdf5_dataspace);
#endif


  hdf5_dataspace = H5Screate(H5S_SCALAR);
  hdf5_attribute = H5Acreate(handle, "Time", H5T_NATIVE_DOUBLE, hdf5_dataspace, H5P_DEFAULT);
  H5Awrite(hdf5_attribute, H5T_NATIVE_DOUBLE, &header.time);
  H5Aclose(hdf5_attribute);
  H5Sclose(hdf5_dataspace);

  hdf5_dataspace = H5Screate(H5S_SCALAR);
  hdf5_attribute = H5Acreate(handle, "Redshift", H5T_NATIVE_DOUBLE, hdf5_dataspace, H5P_DEFAULT);
  H5Awrite(hdf5_attribute, H5T_NATIVE_DOUBLE, &header.redshift);
  H5Aclose(hdf5_attribute);
  H5Sclose(hdf5_dataspace);

  hdf5_dataspace = H5Screate(H5S_SCALAR);
  hdf5_attribute = H5Acreate(handle, "BoxSize", H5T_NATIVE_DOUBLE, hdf5_dataspace, H5P_DEFAULT);
  H5Awrite(hdf5_attribute, H5T_NATIVE_DOUBLE, &header.BoxSize);
  H5Aclose(hdf5_attribute);
  H5Sclose(hdf5_dataspace);

  hdf5_dataspace = H5Screate(H5S_SCALAR);
  hdf5_attribute = H5Acreate(handle, "NumFilesPerSnapshot", H5T_NATIVE_INT, hdf5_dataspace, H5P_DEFAULT);
  H5Awrite(hdf5_attribute, H5T_NATIVE_INT, &header.num_files);
  H5Aclose(hdf5_attribute);
  H5Sclose(hdf5_dataspace);

  hdf5_dataspace = H5Screate(H5S_SCALAR);
  hdf5_attribute = H5Acreate(handle, "Omega0", H5T_NATIVE_DOUBLE, hdf5_dataspace, H5P_DEFAULT);
  H5Awrite(hdf5_attribute, H5T_NATIVE_DOUBLE, &header.Omega0);
  H5Aclose(hdf5_attribute);
  H5Sclose(hdf5_dataspace);

  hdf5_dataspace = H5Screate(H5S_SCALAR);
  hdf5_attribute = H5Acreate(handle, "OmegaLambda", H5T_NATIVE_DOUBLE, hdf5_dataspace, H5P_DEFAULT);
  H5Awrite(hdf5_attribute, H5T_NATIVE_DOUBLE, &header.OmegaLambda);
  H5Aclose(hdf5_attribute);
  H5Sclose(hdf5_dataspace);

  hdf5_dataspace = H5Screate(H5S_SCALAR);
  hdf5_attribute = H5Acreate(handle, "HubbleParam", H5T_NATIVE_DOUBLE, hdf5_dataspace, H5P_DEFAULT);
  H5Awrite(hdf5_attribute, H5T_NATIVE_DOUBLE, &header.HubbleParam);
  H5Aclose(hdf5_attribute);
  H5Sclose(hdf5_dataspace);

  hdf5_dataspace = H5Screate(H5S_SCALAR);
  hdf5_attribute = H5Acreate(handle, "Flag_Sfr", H5T_NATIVE_INT, hdf5_dataspace, H5P_DEFAULT);
  H5Awrite(hdf5_attribute, H5T_NATIVE_INT, &header.flag_sfr);
  H5Aclose(hdf5_attribute);
  H5Sclose(hdf5_dataspace);

  hdf5_dataspace = H5Screate(H5S_SCALAR);
  hdf5_attribute = H5Acreate(handle, "Flag_Cooling", H5T_NATIVE_INT, hdf5_dataspace, H5P_DEFAULT);
  H5Awrite(hdf5_attribute, H5T_NATIVE_INT, &header.flag_cooling);
  H5Aclose(hdf5_attribute);
  H5Sclose(hdf5_dataspace);

  hdf5_dataspace = H5Screate(H5S_SCALAR);
  hdf5_attribute = H5Acreate(handle, "Flag_StellarAge", H5T_NATIVE_INT, hdf5_dataspace, H5P_DEFAULT);
  H5Awrite(hdf5_attribute, H5T_NATIVE_INT, &header.flag_stellarage);
  H5Aclose(hdf5_attribute);
  H5Sclose(hdf5_dataspace);

  hdf5_dataspace = H5Screate(H5S_SCALAR);
  hdf5_attribute = H5Acreate(handle, "Flag_Metals", H5T_NATIVE_INT, hdf5_dataspace, H5P_DEFAULT);
  H5Awrite(hdf5_attribute, H5T_NATIVE_INT, &header.flag_metals);
  H5Aclose(hdf5_attribute);
  H5Sclose(hdf5_dataspace);

  hdf5_dataspace = H5Screate(H5S_SCALAR);
  hdf5_attribute = H5Acreate(handle, "Flag_Feedback", H5T_NATIVE_INT, hdf5_dataspace, H5P_DEFAULT);
  H5Awrite(hdf5_attribute, H5T_NATIVE_INT, &header.flag_feedback);
  H5Aclose(hdf5_attribute);
  H5Sclose(hdf5_dataspace);

  hdf5_dataspace = H5Screate(H5S_SCALAR);
  hdf5_attribute = H5Acreate(handle, "Flag_DoublePrecision", H5T_NATIVE_INT, hdf5_dataspace, H5P_DEFAULT);
  H5Awrite(hdf5_attribute, H5T_NATIVE_INT, &header.flag_doubleprecision);
  H5Aclose(hdf5_attribute);
  H5Sclose(hdf5_dataspace);

  hdf5_dataspace = H5Screate(H5S_SCALAR);
  hdf5_attribute = H5Acreate(handle, "Flag_IC_Info", H5T_NATIVE_INT, hdf5_dataspace, H5P_DEFAULT);
  H5Awrite(hdf5_attribute, H5T_NATIVE_INT, &header.flag_ic_info);
  H5Aclose(hdf5_attribute);
  H5Sclose(hdf5_dataspace);
}
#endif





/*! This catches I/O errors occuring for my_fwrite(). In this case we
 *  better stop.
 */
size_t my_fwrite(void *ptr, size_t size, size_t nmemb, FILE * stream)
{
  size_t nwritten;

  if(size * nmemb > 0)
    {
      if((nwritten = fwrite(ptr, size, nmemb, stream)) != nmemb)
	{
	  printf("I/O error (fwrite) on task=%d has occured: %s\n", ThisTask, strerror(errno));
	  fflush(stdout);
	  endrun(777);
	}
    }
  else
    nwritten = 0;

  return nwritten;
}


/*! This catches I/O errors occuring for fread(). In this case we
 *  better stop.
 */
size_t my_fread(void *ptr, size_t size, size_t nmemb, FILE * stream)
{
  size_t nread;

  if(size * nmemb == 0)
    return 0;

  if((nread = fread(ptr, size, nmemb, stream)) != nmemb)
    {
      if(feof(stream))
	printf("I/O error (fread) on task=%d has occured: end of file\n", ThisTask);
      else
	printf("I/O error (fread) on task=%d has occured: %s\n", ThisTask, strerror(errno));
      fflush(stdout);
      endrun(778);
    }
  return nread;
}

/** This is so we don't have to preface every printf call with the if
    statement to only make it print once. */
void mpi_printf(const char *fmt, ...)
{
  if(ThisTask == 0)
    {
      va_list l;
      va_start(l, fmt);
      vprintf(fmt, l);
      fflush(stdout);
      va_end(l);
    }
}

#if defined(ORDER_SNAPSHOTS_BY_ID) || defined(SUBFIND_READ_FOF) || defined(SUBFIND_RESHUFFLE_CATALOGUE)
int io_compare_P_ID(const void *a, const void *b)
{
  if(((struct particle_data *) a)->ID < (((struct particle_data *) b)->ID))
    return -1;

  if(((struct particle_data *) a)->ID > (((struct particle_data *) b)->ID))
    return +1;

  return 0;
}

int io_compare_P_GrNr_SubNr(const void *a, const void *b)
{
  if(((struct particle_data *) a)->GrNr < (((struct particle_data *) b)->GrNr))
    return -1;

  if(((struct particle_data *) a)->GrNr > (((struct particle_data *) b)->GrNr))
    return +1;

  if(((struct particle_data *) a)->SubNr < (((struct particle_data *) b)->SubNr))
    return -1;

  if(((struct particle_data *) a)->SubNr > (((struct particle_data *) b)->SubNr))
    return +1;

  return 0;
}

int io_compare_P_GrNr_ID(const void *a, const void *b)
{
  if(((struct particle_data *) a)->GrNr < (((struct particle_data *) b)->GrNr))
    return -1;

  if(((struct particle_data *) a)->GrNr > (((struct particle_data *) b)->GrNr))
    return +1;

  if(((struct particle_data *) a)->ID < (((struct particle_data *) b)->ID))
    return -1;

  if(((struct particle_data *) a)->ID > (((struct particle_data *) b)->ID))
    return +1;

  return 0;
}

#endif

