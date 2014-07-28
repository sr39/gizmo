#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <gsl/gsl_math.h>
#include "../allvars.h"
#include "../proto.h"
#include "../kernel.h"
#ifdef OMP_NUM_THREADS
#include <pthread.h>
#endif

/*! \file gradients.c
 *  \brief calculate gradients of hydro quantities
 *
 *  This file contains the "second hydro loop", where the gas hydro quantity 
 *   gradients are calculated. All gradients now use the second-order accurate 
 *   moving-least-squares formulation, and are calculated here consistently.
 */


#ifdef OMP_NUM_THREADS
extern pthread_mutex_t mutex_nexport;
extern pthread_mutex_t mutex_partnodedrift;
#define LOCK_NEXPORT     pthread_mutex_lock(&mutex_nexport);
#define UNLOCK_NEXPORT   pthread_mutex_unlock(&mutex_nexport);
#else
#define LOCK_NEXPORT
#define UNLOCK_NEXPORT
#endif

#define NV_MYSIGN(x) (( x > 0 ) - ( x < 0 ))

/* define a common 'gradients' structure to hold 
 everything we're going to take derivatives of */
struct Quantities_for_Gradients
{
    MyDouble Density;
    MyDouble Pressure;
    MyDouble Velocity[3];
#ifdef MAGNETIC
    MyDouble B[3];
#ifdef DIVBCLEANING_DEDNER
    MyDouble Phi;
#endif
#endif
#ifdef RADTRANSFER_FLUXLIMITER
    MyFloat n_gamma[N_BINS];
#endif
#ifdef NON_IDEAL_EOS
    MyDouble InternalEnergy[3];
    MyDouble SoundSpeed[3];
#endif
};

struct kernel_addSPH
{
    double dx, dy, dz;
    double r;
    double wk_i, wk_j, dwk_i, dwk_j;
    double h_i;
};

struct addSPHdata_in
{
    MyDouble Pos[3];
    MyFloat Hsml;
    MyFloat ConditionNumber;
    struct Quantities_for_Gradients GQuant;
#ifndef DONOTUSENODELIST
    int NodeList[NODELISTLENGTH];
#endif
}
*AddSPHDataIn, *AddSPHDataGet;

struct addSPHdata_out
{
#ifdef HYDRO_SPH
    MyFloat alpha_limiter;
#endif
    struct Quantities_for_Gradients Gradients[3];
    struct Quantities_for_Gradients Maxima;
    struct Quantities_for_Gradients Minima;
}
*AddSPHDataResult, *AddSPHDataOut;


/* this is a temporary structure for quantities used ONLY in the loop below,
 for example for computing the slope-limiters (for the Reimann problem) */
static struct temporary_data_topass
{
    struct Quantities_for_Gradients Maxima;
    struct Quantities_for_Gradients Minima;
}
*AddSPHDataPasser;



static inline void particle2in_addSPH(struct addSPHdata_in *in, int i);
static inline void out2particle_addSPH(struct addSPHdata_out *out, int i, int mode);

static inline void particle2in_addSPH(struct addSPHdata_in *in, int i)
{
    int k;
    for(k = 0; k < 3; k++)
        in->Pos[k] = P[i].Pos[k];
    in->Hsml = PPP[i].Hsml;
    in->ConditionNumber = SphP[i].ConditionNumber;
    in->GQuant.Density = SphP[i].Density;
    in->GQuant.Pressure = SphP[i].Pressure;
    for(k = 0; k < 3; k++)
        in->GQuant.Velocity[k] = SphP[i].VelPred[k];
#ifdef MAGNETIC
    for(k = 0; k < 3; k++)
        in->GQuant.B[k] = SphP[i].BPred[k];
#ifdef DIVBCLEANING_DEDNER
    in->GQuant.Phi = SphP[i].PhiPred;
#endif
#endif
#ifdef RADTRANSFER_FLUXLIMITER
    for(k = 0; k < N_BINS; k++)
        in->GQuant.n_gamma[k] = SphP[i].n_gamma[k];
#endif
#ifdef NON_IDEAL_EOS
    in->GQuant.InternalEnergy = SphP[i].InternalEnergyPred;
    in->GQuant.SoundSpeed = Particle_effective_soundspeed_i(i);
#endif
}

#define MAX_ADD(x,y,mode) (mode == 0 ? (x=y) : (((x)<(y)) ? (x=y) : (x)))
#define MIN_ADD(x,y,mode) (mode == 0 ? (x=y) : (((x)>(y)) ? (x=y) : (x)))

static inline void out2particle_addSPH(struct addSPHdata_out *out, int i, int mode)
{
    int j,k;
#ifdef SPHAV_CD10_VISCOSITY_SWITCH
    ASSIGN_ADD(SphP[i].alpha_limiter, out->alpha_limiter, mode);
#endif
    
    MAX_ADD(AddSPHDataPasser[i].Maxima.Density,out->Maxima.Density,mode);
    MIN_ADD(AddSPHDataPasser[i].Minima.Density,out->Minima.Density,mode);
    MAX_ADD(AddSPHDataPasser[i].Maxima.Pressure,out->Maxima.Pressure,mode);
    MIN_ADD(AddSPHDataPasser[i].Minima.Pressure,out->Minima.Pressure,mode);
    for(k=0;k<3;k++)
    {
        ASSIGN_ADD(SphP[i].Gradients.Density[k],out->Gradients[k].Density,mode);
        ASSIGN_ADD(SphP[i].Gradients.Pressure[k],out->Gradients[k].Pressure,mode);
    }
#ifdef NON_IDEAL_EOS
    MAX_ADD(AddSPHDataPasser[i].Maxima.InternalEnergy,out->Maxima.InternalEnergy,mode);
    MIN_ADD(AddSPHDataPasser[i].Minima.InternalEnergy,out->Minima.InternalEnergy,mode);
    MAX_ADD(AddSPHDataPasser[i].Maxima.SoundSpeed,out->Maxima.SoundSpeed,mode);
    MIN_ADD(AddSPHDataPasser[i].Minima.SoundSpeed,out->Minima.SoundSpeed,mode);
    for(k=0;k<3;k++)
    {
        ASSIGN_ADD(SphP[i].Gradients.InternalEnergy[k],out->Gradients[k].InternalEnergy,mode);
        ASSIGN_ADD(SphP[i].Gradients.SoundSpeed[k],out->Gradients[k].SoundSpeed,mode);
    }
#endif
    
    for(j=0;j<3;j++)
    {
        MAX_ADD(AddSPHDataPasser[i].Maxima.Velocity[j],out->Maxima.Velocity[j],mode);
        MIN_ADD(AddSPHDataPasser[i].Minima.Velocity[j],out->Minima.Velocity[j],mode);
        for(k=0;k<3;k++)
        {
            ASSIGN_ADD(SphP[i].Gradients.Velocity[j][k],out->Gradients[k].Velocity[j],mode);
        }
    }
#ifdef MAGNETIC
    for(j=0;j<3;j++)
    {
        MAX_ADD(AddSPHDataPasser[i].Maxima.B[j],out->Maxima.B[j],mode);
        MIN_ADD(AddSPHDataPasser[i].Minima.B[j],out->Minima.B[j],mode);
        for(k=0;k<3;k++)
        {
            ASSIGN_ADD(SphP[i].Gradients.B[j][k],out->Gradients[k].B[j],mode);
        }
    }
#ifdef DIVBCLEANING_DEDNER
    MAX_ADD(AddSPHDataPasser[i].Maxima.Phi,out->Maxima.Phi,mode);
    MIN_ADD(AddSPHDataPasser[i].Minima.Phi,out->Minima.Phi,mode);
    for(k=0;k<3;k++)
    {
        ASSIGN_ADD(SphP[i].Gradients.Phi[k],out->Gradients[k].Phi,mode);
    }
#endif
#endif
#ifdef RADTRANSFER_FLUXLIMITER
    for(j=0;j<N_BINS;j++)
    {
        MAX_ADD(AddSPHDataPasser[i].Maxima.n_gamma[j],out->Maxima.n_gamma[j],mode);
        MIN_ADD(AddSPHDataPasser[i].Minima.n_gamma[j],out->Minima.n_gamma[j],mode);
        for(k=0;k<3;k++)
        {
            ASSIGN_ADD(SphP[i].Gradients.n_gamma[k][j],out->Gradients[k].n_gamma[j],mode);
        }
    }
#endif
}


void hydro_gradient_calc(void)
{
  int i, j, k, k1, k2, ngrp, ndone, ndone_flag;
  int recvTask, place;
  double timeall = 0, timecomp1 = 0, timecomp2 = 0, timecommsumm1 = 0, timecommsumm2 = 0, timewait1 = 0, timewait2 = 0;
  double timecomp, timecomm, timewait, tstart, tend, t0, t1;
  int save_NextParticle;
  long long n_exported = 0;
#ifdef SPHAV_CD10_VISCOSITY_SWITCH
    double NV_dt,NV_dummy,NV_limiter,NV_A,divVel_physical,h_eff,alphaloc,cs_nv;
#endif
    
  /* allocate buffers to arrange communication */
  int NTaskTimesNumPart;
  AddSPHDataPasser = (struct temporary_data_topass *) mymalloc("AddSPHDataPasser",NumPart * sizeof(struct temporary_data_topass));
  NTaskTimesNumPart = maxThreads * NumPart;
  Ngblist = (int *) mymalloc("Ngblist", NTaskTimesNumPart * sizeof(int));
  All.BunchSize = (int) ((All.BufferSize * 1024 * 1024) / (sizeof(struct data_index) + sizeof(struct data_nodelist) +
					     sizeof(struct addSPHdata_in) +
					     sizeof(struct addSPHdata_out) +
					     sizemax(sizeof(struct addSPHdata_in),
						     sizeof(struct addSPHdata_out))));
  DataIndexTable = (struct data_index *) mymalloc("DataIndexTable", All.BunchSize * sizeof(struct data_index));
  DataNodeList = (struct data_nodelist *) mymalloc("DataNodeList", All.BunchSize * sizeof(struct data_nodelist));

  CPU_Step[CPU_DENSMISC] += measure_time();
  t0 = my_second();

  NextParticle = FirstActiveParticle;	/* beginn with this index */
  do
    {

      BufferFullFlag = 0;
      Nexport = 0;
      save_NextParticle = NextParticle;

      for(j = 0; j < NTask; j++)
	{
	  Send_count[j] = 0;
	  Exportflag[j] = -1;
	}

      /* do local particles and prepare export list */
      tstart = my_second();

#ifdef OMP_NUM_THREADS
      pthread_t mythreads[OMP_NUM_THREADS - 1];
      int threadid[OMP_NUM_THREADS - 1];
      pthread_attr_t attr;

      pthread_attr_init(&attr);
      pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
      pthread_mutex_init(&mutex_nexport, NULL);
      pthread_mutex_init(&mutex_partnodedrift, NULL);

      TimerFlag = 0;

      for(j = 0; j < OMP_NUM_THREADS - 1; j++)
	{
	  threadid[j] = j + 1;
	  pthread_create(&mythreads[j], &attr, addSPH_evaluate_primary, &threadid[j]);
	}
#endif
#ifdef _OPENMP
#pragma omp parallel
#endif
      {
#ifdef _OPENMP
	int mainthreadid = omp_get_thread_num();
#else
	int mainthreadid = 0;
#endif
	addSPH_evaluate_primary(&mainthreadid);	/* do local particles and prepare export list */
      }

#ifdef OMP_NUM_THREADS
      for(j = 0; j < OMP_NUM_THREADS - 1; j++)
	pthread_join(mythreads[j], NULL);
#endif


      tend = my_second();
      timecomp1 += timediff(tstart, tend);

      if(BufferFullFlag)
	{
	  int last_nextparticle = NextParticle;

	  NextParticle = save_NextParticle;

	  while(NextParticle >= 0)
	    {
	      if(NextParticle == last_nextparticle)
		break;

	      if(ProcessedFlag[NextParticle] != 1)
		break;

	      ProcessedFlag[NextParticle] = 2;

	      NextParticle = NextActiveParticle[NextParticle];
	    }

	  if(NextParticle == save_NextParticle)
	    {
	      /* in this case, the buffer is too small to process even a single particle */
	      endrun(113308);
	    }

	  int new_export = 0;

	  for(j = 0, k = 0; j < Nexport; j++)
	    if(ProcessedFlag[DataIndexTable[j].Index] != 2)
	      {
		if(k < j + 1)
		  k = j + 1;

		for(; k < Nexport; k++)
		  if(ProcessedFlag[DataIndexTable[k].Index] == 2)
		    {
		      int old_index = DataIndexTable[j].Index;

		      DataIndexTable[j] = DataIndexTable[k];
		      DataNodeList[j] = DataNodeList[k];
		      DataIndexTable[j].IndexGet = j;
		      new_export++;

		      DataIndexTable[k].Index = old_index;
		      k++;
		      break;
		    }
	      }
	    else
	      new_export++;

	  Nexport = new_export;

	}

      n_exported += Nexport;

      for(j = 0; j < NTask; j++)
          Send_count[j] = 0;
      for(j = 0; j < Nexport; j++)
          Send_count[DataIndexTable[j].Task]++;

      MYSORT_DATAINDEX(DataIndexTable, Nexport, sizeof(struct data_index), data_index_compare);

      tstart = my_second();

      MPI_Alltoall(Send_count, 1, MPI_INT, Recv_count, 1, MPI_INT, MPI_COMM_WORLD);

      tend = my_second();
      timewait1 += timediff(tstart, tend);

      for(j = 0, Nimport = 0, Recv_offset[0] = 0, Send_offset[0] = 0; j < NTask; j++)
	{
	  Nimport += Recv_count[j];

	  if(j > 0)
	    {
	      Send_offset[j] = Send_offset[j - 1] + Send_count[j - 1];
	      Recv_offset[j] = Recv_offset[j - 1] + Recv_count[j - 1];
	    }
	}

      AddSPHDataGet = (struct addSPHdata_in *) mymalloc("AddSPHDataGet", Nimport * sizeof(struct addSPHdata_in));
      AddSPHDataIn = (struct addSPHdata_in *) mymalloc("AddSPHDataIn", Nexport * sizeof(struct addSPHdata_in));

      /* prepare particle data for export */

      for(j = 0; j < Nexport; j++)
	{
	  place = DataIndexTable[j].Index;
	  particle2in_addSPH(&AddSPHDataIn[j], place);
#ifndef DONOTUSENODELIST
	  memcpy(AddSPHDataIn[j].NodeList,
		 DataNodeList[DataIndexTable[j].IndexGet].NodeList, NODELISTLENGTH * sizeof(int));
#endif

	}

      /* exchange particle data */
      tstart = my_second();
      for(ngrp = 1; ngrp < (1 << PTask); ngrp++)
	{
	  recvTask = ThisTask ^ ngrp;

	  if(recvTask < NTask)
	    {
	      if(Send_count[recvTask] > 0 || Recv_count[recvTask] > 0)
		{
		  /* get the particles */
		  MPI_Sendrecv(&AddSPHDataIn[Send_offset[recvTask]],
			       Send_count[recvTask] * sizeof(struct addSPHdata_in), MPI_BYTE,
			       recvTask, TAG_INTERLOOP_A,
			       &AddSPHDataGet[Recv_offset[recvTask]],
			       Recv_count[recvTask] * sizeof(struct addSPHdata_in), MPI_BYTE,
			       recvTask, TAG_INTERLOOP_A, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		}
	    }
	}
      tend = my_second();
      timecommsumm1 += timediff(tstart, tend);

      myfree(AddSPHDataIn);
      AddSPHDataResult = (struct addSPHdata_out *) mymalloc("AddSPHDataResult", Nimport * sizeof(struct addSPHdata_out));
      AddSPHDataOut = (struct addSPHdata_out *) mymalloc("AddSPHDataOut", Nexport * sizeof(struct addSPHdata_out));
      report_memory_usage(&HighMark_addSPH, "GRADIENTS_LOOP");

      /* now do the particles that were sent to us */
      tstart = my_second();
      NextJ = 0;

#ifdef OMP_NUM_THREADS
      for(j = 0; j < OMP_NUM_THREADS - 1; j++)
          pthread_create(&mythreads[j], &attr, addSPH_evaluate_secondary, &threadid[j]);
#endif
#ifdef _OPENMP
#pragma omp parallel
#endif
      {
#ifdef _OPENMP
	int mainthreadid = omp_get_thread_num();
#else
	int mainthreadid = 0;
#endif
	addSPH_evaluate_secondary(&mainthreadid);
      }

#ifdef OMP_NUM_THREADS
      for(j = 0; j < OMP_NUM_THREADS - 1; j++)
	pthread_join(mythreads[j], NULL);

      pthread_mutex_destroy(&mutex_partnodedrift);
      pthread_mutex_destroy(&mutex_nexport);
      pthread_attr_destroy(&attr);
#endif

      tend = my_second();
      timecomp2 += timediff(tstart, tend);

      if(NextParticle < 0)
          ndone_flag = 1;
      else
          ndone_flag = 0;

      tstart = my_second();
      MPI_Allreduce(&ndone_flag, &ndone, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
      tend = my_second();
      timewait2 += timediff(tstart, tend);

      /* get the result */
      tstart = my_second();
      for(ngrp = 1; ngrp < (1 << PTask); ngrp++)
	{
	  recvTask = ThisTask ^ ngrp;
	  if(recvTask < NTask)
	    {
	      if(Send_count[recvTask] > 0 || Recv_count[recvTask] > 0)
		{
		  /* send the results */
		  MPI_Sendrecv(&AddSPHDataResult[Recv_offset[recvTask]],
			       Recv_count[recvTask] * sizeof(struct addSPHdata_out),
			       MPI_BYTE, recvTask, TAG_INTERLOOP_B,
			       &AddSPHDataOut[Send_offset[recvTask]],
			       Send_count[recvTask] * sizeof(struct addSPHdata_out),
			       MPI_BYTE, recvTask, TAG_INTERLOOP_B, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		}
	    }
	}
      tend = my_second();
      timecommsumm2 += timediff(tstart, tend);

      /* add the result to the local particles */
      tstart = my_second();
      for(j = 0; j < Nexport; j++)
	{
	  place = DataIndexTable[j].Index;
	  out2particle_addSPH(&AddSPHDataOut[j], place, 1);
	}
      tend = my_second();
      timecomp1 += timediff(tstart, tend);

      myfree(AddSPHDataOut);
      myfree(AddSPHDataResult);
      myfree(AddSPHDataGet);
    }
  while(ndone < NTask);

  myfree(DataNodeList);
  myfree(DataIndexTable);
  myfree(Ngblist);

  /* do final operations on results */
  for(i = FirstActiveParticle; i >= 0; i = NextActiveParticle[i])
    if(P[i].Type == 0)
      {
          /* check if the matrix is well-conditioned: otherwise we will use the 'standard SPH-like' derivative estimation */
          if(SphP[i].ConditionNumber <= (double)CONDITION_NUMBER_DANGER)
          {
              /* now we can properly calculate (second-order accurate) gradients of hydrodynamic quantities from this loop */
              double v_tmp[3];
              for(k1=0;k1<3;k1++)
                  v_tmp[k1] = SphP[i].Gradients.Density[k1];
              for(k1=0;k1<3;k1++)
                  SphP[i].Gradients.Density[k1] = SphP[i].NV_T[k1][0]*v_tmp[0] + SphP[i].NV_T[k1][1]*v_tmp[1] + SphP[i].NV_T[k1][2]*v_tmp[2];
              for(k1=0;k1<3;k1++)
                  v_tmp[k1] = SphP[i].Gradients.Pressure[k1];
              for(k1=0;k1<3;k1++)
                  SphP[i].Gradients.Pressure[k1] = SphP[i].NV_T[k1][0]*v_tmp[0] + SphP[i].NV_T[k1][1]*v_tmp[1] + SphP[i].NV_T[k1][2]*v_tmp[2];
              for(k2=0;k2<3;k2++)
              {
                  for(k1=0;k1<3;k1++)
                      v_tmp[k1] = SphP[i].Gradients.Velocity[k2][k1];
                  for(k1=0;k1<3;k1++)
                      SphP[i].Gradients.Velocity[k2][k1] = SphP[i].NV_T[k1][0]*v_tmp[0] + SphP[i].NV_T[k1][1]*v_tmp[1] + SphP[i].NV_T[k1][2]*v_tmp[2];
              }
#ifdef NON_IDEAL_EOS
              for(k1=0;k1<3;k1++)
                  v_tmp[k1] = SphP[i].Gradients.InternalEnergy[k1];
              for(k1=0;k1<3;k1++)
                  SphP[i].Gradients.InternalEnergy[k1] = SphP[i].NV_T[k1][0]*v_tmp[0] + SphP[i].NV_T[k1][1]*v_tmp[1] + SphP[i].NV_T[k1][2]*v_tmp[2];
              for(k1=0;k1<3;k1++)
                  v_tmp[k1] = SphP[i].Gradients.SoundSpeed[k1];
              for(k1=0;k1<3;k1++)
                  SphP[i].Gradients.SoundSpeed[k1] = SphP[i].NV_T[k1][0]*v_tmp[0] + SphP[i].NV_T[k1][1]*v_tmp[1] + SphP[i].NV_T[k1][2]*v_tmp[2];
#endif
#ifdef MAGNETIC
              /* use the magnitude of the B-field gradients relative to smoothing length to calculate artificial resistivity */
              for(k2=0;k2<3;k2++)
              {
                  for(k1=0;k1<3;k1++)
                      v_tmp[k1] = SphP[i].Gradients.B[k2][k1];
                  for(k1=0;k1<3;k1++)
                      SphP[i].Gradients.B[k2][k1] = SphP[i].NV_T[k1][0]*v_tmp[0] + SphP[i].NV_T[k1][1]*v_tmp[1] + SphP[i].NV_T[k1][2]*v_tmp[2];
              }
#ifdef DIVBCLEANING_DEDNER
              for(k1=0;k1<3;k1++)
                  v_tmp[k1] = SphP[i].Gradients.Phi[k1];
              for(k1=0;k1<3;k1++)
                  SphP[i].Gradients.Phi[k1] = SphP[i].NV_T[k1][0]*v_tmp[0] + SphP[i].NV_T[k1][1]*v_tmp[1] + SphP[i].NV_T[k1][2]*v_tmp[2];
#endif
#endif
          }

          
          
          /* now the gradients are calculated: below are simply useful operations on the results */
          
#if defined(BH_POPIII_SEEDS) || defined(GALSF_FB_LOCAL_UV_HEATING) || defined(GALSF_FB_RPWIND_FROMSTARS) || defined(BH_PHOTONMOMENTUM) || defined(GALSF_FB_RT_PHOTON_LOCALATTEN)
              /* this is here because for the models of BH growth and self-shielding of stars, we
                   need to calculate GradRho: we don't bother doing it in density.c if we're already calculating it here! */
          for(k=0;k<3;k++)
              P[i].GradRho[k] = SphP[i].Gradients.Density[k];
#endif

          
#ifdef TRICCO_RESISTIVITY_SWITCH
          double GradBMag=0.0;
          double BMag=0.0;
          for(k=0;k<3;k++)
          {
              for(j=0;j<3;j++)
              {
                  GradBMag += SphP[i].Gradients.B[k][j]*SphP[i].Gradients.B[k][j];
              }
              BMag += SphP[i].BPred[k]*SphP[i].BPred[k];
          }
          GradBMag = sqrt(GradBMag);
          SphP[i].Balpha = (KERNEL_CORE_SIZE * PPP[i].Hsml) * sqrt(GradBMag/BMag);
          SphP[i].Balpha = DMIN(SphP[i].Balpha, All.ArtMagDispConst);
#endif
          

#ifdef HYDRO_SPH
#ifdef SPHAV_CD10_VISCOSITY_SWITCH
          SphP[i].alpha_limiter /= SphP[i].Density;
          NV_dt =  (P[i].TimeBin ? (((integertime) 1) << P[i].TimeBin) : 0) * All.Timebase_interval / All.cf_hubble_a; // physical
          NV_dummy = fabs(1.0 * pow(1.0 - SphP[i].alpha_limiter,4.0) * SphP[i].NV_DivVel); // NV_ quantities are in physical units
          NV_limiter = NV_dummy*NV_dummy / (NV_dummy*NV_dummy + SphP[i].NV_trSSt);
          NV_A = DMAX(-SphP[i].NV_dt_DivVel, 0.0);
          divVel_physical = SphP[i].NV_DivVel;
          
          // add a simple limiter here: alpha_loc is 'prepped' but only switches on when the divergence goes negative: want to add hubble flow here //
          if(All.ComovingIntegrationOn) divVel_physical += 3*All.cf_hubble_a; // hubble-flow correction added
          if(divVel_physical>=0.0) NV_A = 0.0;
          
          h_eff = (KERNEL_CORE_SIZE/0.5) * PPP[i].Hsml * All.cf_atime; // 'default' parameter choices are scaled for a cubic spline //
          cs_nv = Particle_effective_soundspeed_i(i) * All.cf_afac3; // converts to physical velocity units //
          alphaloc = All.ViscosityAMax * h_eff*h_eff*NV_A / (0.36*cs_nv*cs_nv*(0.05/SPHAV_CD10_VISCOSITY_SWITCH) + h_eff*h_eff*NV_A);
          // 0.25 in front of vsig is the 'noise parameter' that determines the relative amplitude which will trigger the switch:
          //    that choice was quite large (requires approach velocity rate-of-change is super-sonic); better to use c_s (above), and 0.05-0.25 //
          // NV_A is physical 1/(time*time), but Hsml and vsig can be comoving, so need appropriate correction terms above //
          
          if(SphP[i].alpha < alphaloc)
              SphP[i].alpha = alphaloc;
          else if (SphP[i].alpha > alphaloc)
              SphP[i].alpha = alphaloc + (SphP[i].alpha - alphaloc) * exp(-NV_dt * (0.5*fabs(SphP[i].MaxSignalVel)*All.cf_afac3)/(0.5*h_eff) * SPHAV_CD10_VISCOSITY_SWITCH);
          
          if(SphP[i].alpha < All.ViscosityAMin)
              SphP[i].alpha = All.ViscosityAMin;
          
          SphP[i].alpha_limiter = DMAX(NV_limiter,All.ViscosityAMin/SphP[i].alpha);
#else
          /* compute the traditional Balsara limiter (now that we have velocity gradients) */
          double divVel = All.cf_a2inv * fabs(SphP[i].Gradients.Velocity[0][0] + SphP[i].Gradients.Velocity[1][1] + SphP[i].Gradients.Velocity[2][2]);
          if(All.ComovingIntegrationOn) divVel += 3*All.cf_hubble_a; // hubble-flow correction added (physical units)
          double CurlVel[3];
          double MagCurl;
          CurlVel[0] = SphP[i].Gradients.Velocity[1][2] - SphP[i].Gradients.Velocity[2][1];
          CurlVel[1] = SphP[i].Gradients.Velocity[2][0] - SphP[i].Gradients.Velocity[0][2];
          CurlVel[2] = SphP[i].Gradients.Velocity[0][1] - SphP[i].Gradients.Velocity[1][0];
          MagCurl = All.cf_a2inv * sqrt(CurlVel[0]*CurlVel[0] + CurlVel[1]*CurlVel[1] + CurlVel[2]*CurlVel[2]);
          double fac_mu = 1 / (All.cf_afac3 * All.cf_atime);
          SphP[i].alpha_limiter = divVel / (divVel + MagCurl + 0.0001 * Particle_effective_soundspeed_i(i) /
                                            (KERNEL_CORE_SIZE*PPP[i].Hsml) / fac_mu);
#endif
#endif

          
#ifdef CONDUCTION_EXPLICIT
          /* calculate the thermal conductivities */
          double u_conduction = Particle_Internal_energy_i(i);
          SphP[i].Kappa_Conduction = All.ConductionCoeff * pow(u_conduction, 2.5);
          // ok this has units of ~mass_physical/(length_physical * time_physical), but -physical- for each, not comoving //
#ifdef CONDUCTION_SATURATION
          double electron_free_path = All.ElectronFreePathFactor * u_conduction * u_conduction / (SphP[i].Density * All.cf_a3inv);
          /* need an estimate of the internal energy gradient scale length, which we get by d(P/rho) = P/rho * (dP/P - drho/rho) */
          double du_conduction;
          u_conduction = 0;
          for(k=0;k<3;k++)
          {
              du_conduction = (SphP[i].Gradients.Pressure[k]/SphP[i].Pressure - SphP[i].Gradients.Density[k]/SphP[i].Density);
              u_conduction = du_conduction * du_conduction;
          }
          double temp_scale_length = 1 / sqrt(u_conduction);
          if(All.ComovingIntegrationOn) temp_scale_length *= All.Time;
          SphP[i].Kappa_Conduction /= (1 + 4.2 * electron_free_path / temp_scale_length);
#endif
#endif

          
#ifdef TURB_DIFFUSION
          /* estimate local turbulent diffusion coefficient from velocity gradients using Smagorinsky mixing model */
          SphP[i].TD_DiffCoeff = All.TurbDiffusion_Coefficient * // overall normalization
            (PPP[i].Hsml*PPP[i].Hsml / pow(PPP[i].NumNgb,2./3.)) * // scales with inter-particle spacing
            sqrt(
               (1./2.)*((SphP[i].Gradients.Velocity[1][0]+SphP[i].Gradients.Velocity[0][1])*(SphP[i].Gradients.Velocity[1][0]+SphP[i].Gradients.Velocity[0][1]) +
                        (SphP[i].Gradients.Velocity[2][0]+SphP[i].Gradients.Velocity[0][2])*(SphP[i].Gradients.Velocity[2][0]+SphP[i].Gradients.Velocity[0][2]) +
                        (SphP[i].Gradients.Velocity[2][1]+SphP[i].Gradients.Velocity[1][2])*(SphP[i].Gradients.Velocity[2][1]+SphP[i].Gradients.Velocity[1][2])) +
               (2./3.)*((SphP[i].Gradients.Velocity[0][0]*SphP[i].Gradients.Velocity[0][0] +
                         SphP[i].Gradients.Velocity[1][1]*SphP[i].Gradients.Velocity[1][1] +
                         SphP[i].Gradients.Velocity[2][2]*SphP[i].Gradients.Velocity[2][2]) -
                        (SphP[i].Gradients.Velocity[1][1]*SphP[i].Gradients.Velocity[2][2] +
                         SphP[i].Gradients.Velocity[0][0]*SphP[i].Gradients.Velocity[1][1] +
                         SphP[i].Gradients.Velocity[0][0]*SphP[i].Gradients.Velocity[2][2]))
               ) * All.cf_a2inv; // norm of matrix of velocity gradient tensor
#endif
          
          
          /* finally, we need to apply a sensible slope limiter to the gradients, to prevent overshooting */
          //double a_limiter = DMIN(0.5, 0.125 * SphP[i].ConditionNumber); //a_limiter = 0.5; // ???
          double a_limiter = 0.25; if(SphP[i].ConditionNumber>100) a_limiter=DMIN(0.5, 0.25 + 0.25 * (SphP[i].ConditionNumber-100)/100);
          /* fraction of H at which maximum reconstruction is allowed (=0.5 for 'standard') */
          double d_norm, d_abs, d_tmp[3], h0 = 1 / (a_limiter * PPP[i].Hsml);
          
          d_abs=0; for(k=0;k<3;k++) {d_tmp[k]=SphP[i].Gradients.Density[k]; d_abs+=d_tmp[k]*d_tmp[k];}
          if(d_abs>0)
          {
              d_abs=h0/sqrt(d_abs); d_norm = DMIN(1, d_abs * DMIN(AddSPHDataPasser[i].Maxima.Density,-AddSPHDataPasser[i].Minima.Density));
              for(k=0;k<3;k++) SphP[i].Gradients.Density[k] *= d_norm;
          }
          
          d_abs=0; for(k=0;k<3;k++) {d_tmp[k]=SphP[i].Gradients.Pressure[k]; d_abs+=d_tmp[k]*d_tmp[k];}
          if(d_abs>0)
          {
              d_abs=h0/sqrt(d_abs); d_norm = DMIN(1, d_abs * DMIN(AddSPHDataPasser[i].Maxima.Pressure,-AddSPHDataPasser[i].Minima.Pressure));
              for(k=0;k<3;k++) SphP[i].Gradients.Pressure[k] *= d_norm;
          }
#ifdef NON_IDEAL_EOS
          d_abs=0; for(k=0;k<3;k++) {d_tmp[k]=SphP[i].Gradients.InternalEnergy[k]; d_abs+=d_tmp[k]*d_tmp[k];}
          if(d_abs>0)
          {
              d_abs=h0/sqrt(d_abs); d_norm = DMIN(1, d_abs * DMIN(AddSPHDataPasser[i].Maxima.InternalEnergy,-AddSPHDataPasser[i].Minima.InternalEnergy));
              for(k=0;k<3;k++) SphP[i].Gradients.InternalEnergy[k] *= d_norm;
          }
          
          d_abs=0; for(k=0;k<3;k++) {d_tmp[k]=SphP[i].Gradients.SoundSpeed[k]; d_abs+=d_tmp[k]*d_tmp[k];}
          if(d_abs>0)
          {
              d_abs=h0/sqrt(d_abs); d_norm = DMIN(1, d_abs * DMIN(AddSPHDataPasser[i].Maxima.SoundSpeed,-AddSPHDataPasser[i].Minima.SoundSpeed));
              for(k=0;k<3;k++) SphP[i].Gradients.SoundSpeed[k] *= d_norm;
          }
#endif
          
          for(k1=0;k1<3;k1++)
          {
              d_abs=0; for(k=0;k<3;k++) {d_tmp[k]=SphP[i].Gradients.Velocity[k1][k]; d_abs+=d_tmp[k]*d_tmp[k];}
              if(d_abs>0)
              {
                  d_abs=h0/sqrt(d_abs); d_norm = DMIN(1, d_abs * DMIN(AddSPHDataPasser[i].Maxima.Velocity[k1],-AddSPHDataPasser[i].Minima.Velocity[k1]));
                  for(k=0;k<3;k++) SphP[i].Gradients.Velocity[k1][k] *= d_norm;
              }
          }
#ifdef MAGNETIC
          for(k1=0;k1<3;k1++)
          {
              d_abs=0; for(k=0;k<3;k++) {d_tmp[k]=SphP[i].Gradients.B[k1][k]; d_abs+=d_tmp[k]*d_tmp[k];}
              if(d_abs>0)
              {
                  d_abs=h0/sqrt(d_abs); d_norm = DMIN(1, d_abs * DMIN(AddSPHDataPasser[i].Maxima.B[k1],-AddSPHDataPasser[i].Minima.B[k1]));
                  for(k=0;k<3;k++) SphP[i].Gradients.B[k1][k] *= d_norm;
              }
          }
#ifdef DIVBCLEANING_DEDNER
          d_abs=0; for(k=0;k<3;k++) {d_tmp[k]=SphP[i].Gradients.Phi[k]; d_abs+=d_tmp[k]*d_tmp[k];}
          if(d_abs>0)
          {
              d_abs=h0/sqrt(d_abs); d_norm = DMIN(1, d_abs * DMIN(AddSPHDataPasser[i].Maxima.Phi,-AddSPHDataPasser[i].Minima.Phi));
              for(k=0;k<3;k++) SphP[i].Gradients.Phi[k] *= d_norm;
          }
#endif
#endif

      }
    
    /* free the temporary structure we created for the MinMax and additional data passing */
    myfree(AddSPHDataPasser);
    
    /* collect some timing information */
    t1 = WallclockTime = my_second();
    timeall += timediff(t0, t1);
    timecomp = timecomp1 + timecomp2;
    timewait = timewait1 + timewait2;
    timecomm = timecommsumm1 + timecommsumm2;

    CPU_Step[CPU_DENSCOMPUTE] += timecomp;
    CPU_Step[CPU_DENSWAIT] += timewait;
    CPU_Step[CPU_DENSCOMM] += timecomm;
    CPU_Step[CPU_DENSMISC] += timeall - (timecomp + timewait + timecomm);
}


int addSPH_evaluate(int target, int mode, int *exportflag, int *exportnodecount, int *exportindex,
		   int *ngblist)
{
    int startnode, numngb, listindex = 0;
    int j, k, k2, n;
    double hinv, hinv3, hinv4, r2, u, wk;
    int sph_like_gradients_flag;
    struct kernel_addSPH kernel;
    struct addSPHdata_in local;
    struct addSPHdata_out out;
    int kernel_mode;
    memset(&out, 0, sizeof(struct addSPHdata_out));
    memset(&kernel, 0, sizeof(struct kernel_addSPH));
    
    if(mode == 0)
        particle2in_addSPH(&local, target);
    else
        local = AddSPHDataGet[target];
    
    /* set particle-i centric quantities so we don't have to later */
    kernel.h_i = local.Hsml;
    double h2_i = kernel.h_i*kernel.h_i;
    kernel_hinv(kernel.h_i, &hinv, &hinv3, &hinv4);
    if(local.ConditionNumber > (double)CONDITION_NUMBER_DANGER) {sph_like_gradients_flag = 1;} else {sph_like_gradients_flag = 0;}
    kernel_mode = -1;
    if(sph_like_gradients_flag) kernel_mode = 1;
#ifdef SPHAV_CD10_VISCOSITY_SWITCH
    kernel_mode = 0;
#endif
    
    /* Now start the actual SPH computation for this particle */
    
    if(mode == 0)
    {
        startnode = All.MaxPart;	/* root node */
    }
    else
    {
        startnode = AddSPHDataGet[target].NodeList[0];
        startnode = Nodes[startnode].u.d.nextnode;	/* open it */
    }
    
    while(startnode >= 0)
    {
        while(startnode >= 0)
        {
            numngb = ngb_treefind_variable_threads(local.Pos, kernel.h_i, target, &startnode, mode, exportflag,
                                                   exportnodecount, exportindex, ngblist);
            
            if(numngb < 0)
                return -1;
            
            for(n = 0; n < numngb; n++)
            {
                j = ngblist[n];
                
                kernel.dx = local.Pos[0] - P[j].Pos[0];
                kernel.dy = local.Pos[1] - P[j].Pos[1];
                kernel.dz = local.Pos[2] - P[j].Pos[2];
#ifdef PERIODIC			/*  now find the closest image in the given box size  */
                kernel.dx = NEAREST_X(kernel.dx);
                kernel.dy = NEAREST_Y(kernel.dy);
                kernel.dz = NEAREST_Z(kernel.dz);
#endif
                r2 = kernel.dx * kernel.dx + kernel.dy * kernel.dy + kernel.dz * kernel.dz;
                
                if((r2>0)&&(r2<h2_i))
                {
                    kernel.r = sqrt(r2);
                    u = kernel.r * hinv;
                    kernel_main(u, hinv3, hinv4, &kernel.wk_i, &kernel.dwk_i, kernel_mode); /* only wk calculated now */
                    
#ifdef SPHAV_CD10_VISCOSITY_SWITCH
                    out.alpha_limiter += NV_MYSIGN(SphP[j].NV_DivVel) * P[j].Mass * kernel.wk_i;
#endif
                    if(sph_like_gradients_flag==1)
                    {
                        wk = -kernel.dwk_i/kernel.r * P[j].Mass/SphP[j].Density; /* use the SPH-like gradient estimator */
                    } else {
                        wk = kernel.wk_i; /* use 2nd-order matrix gradient estimators */
                    }
                    
                    /* now the vectors for the basic hydro gradients we will need */
                    double dpos[3]; dpos[0]=kernel.dx; dpos[1]=kernel.dy; dpos[2]=kernel.dz;
                    
                    /* get the differences for use in the loop below */
                    double dd = SphP[j].Density - local.GQuant.Density;
                    double dp = SphP[j].Pressure - local.GQuant.Pressure;
#ifdef NON_IDEAL_EOS
                    double du = SphP[j].InternalEnergyPred - local.GQuant.InternalEnergy;
                    double dc = Particle_effective_soundspeed_i(j) - local.GQuant.SoundSpeed;
#endif
                    double dv[3];
                    for(k=0;k<3;k++)
                        dv[k] = SphP[j].VelPred[k] - local.GQuant.Velocity[k];
#ifdef MAGNETIC
                    double dB[3];
                    for(k=0;k<3;k++)
                        dB[k] = SphP[j].BPred[k] - local.GQuant.B[k];
#ifdef DIVBCLEANING_DEDNER
                    double dphi = SphP[j].PhiPred - local.GQuant.Phi;
#endif
#endif
#ifdef RADTRANSFER_FLUXLIMITER
                    double dn[N_BINS];
                    for(k=0;k<N_BINS;k++)
                        dn[k] = SphP[j].n_gamma[k] - local.GQuant.n_gamma[k];
#endif
                    
                    /* need to check maxima and minima of particle values in the kernel, to avoid
                     'overshoot' with our gradient estimators */
                    if(dd > out.Maxima.Density) out.Maxima.Density = dd;
                    if(dd < out.Minima.Density) out.Minima.Density = dd;
                    if(dp > out.Maxima.Pressure) out.Maxima.Pressure = dp;
                    if(dp < out.Minima.Pressure) out.Minima.Pressure = dp;
#ifdef NON_IDEAL_EOS
                    if(dd > out.Maxima.InternalEnergy) out.Maxima.InternalEnergy = du;
                    if(dd < out.Minima.InternalEnergy) out.Minima.InternalEnergy = du;
                    if(dp > out.Maxima.SoundSpeed) out.Maxima.SoundSpeed = dc;
                    if(dp < out.Minima.SoundSpeed) out.Minima.SoundSpeed = dc;
#endif
                    for(k=0;k<3;k++)
                    {
                        if(dv[k] > out.Maxima.Velocity[k]) out.Maxima.Velocity[k] = dv[k];
                        if(dv[k] < out.Minima.Velocity[k]) out.Minima.Velocity[k] = dv[k];
#ifdef MAGNETIC
                        if(dB[k] > out.Maxima.B[k]) out.Maxima.B[k] = dB[k];
                        if(dB[k] < out.Minima.B[k]) out.Minima.B[k] = dB[k];
#endif
                    }
#ifdef DIVBCLEANING_DEDNER
                    if(dphi > out.Maxima.Phi) out.Maxima.Phi = dphi;
                    if(dphi < out.Minima.Phi) out.Minima.Phi = dphi;
#endif
#ifdef RADTRANSFER_FLUXLIMITER
                    for(k = 0; k < N_BINS; k++)
                    {
                        if(dn[k] > out.Maxima.n_gamma[k]) out.Maxima.n_gamma[k] = dn[k];
                        if(dn[k] < out.Minima.n_gamma[k]) out.Minima.n_gamma[k] = dn[k];
                    }
#endif
                    
                    for(k=0;k<3;k++)
                    {
                        double wk_xyz = -wk * dpos[k]; /* sign is important here! */
                        out.Gradients[k].Density += wk_xyz * dd;
                        out.Gradients[k].Pressure += wk_xyz * dp;
                        for(k2=0;k2<3;k2++)
                            out.Gradients[k].Velocity[k2] += wk_xyz * dv[k2];
#ifdef NON_IDEAL_EOS
                        out.Gradients[k].InternalEnergy += wk_xyz * du;
                        out.Gradients[k].SoundSpeed += wk_xyz * dc;
#endif
                        
#ifdef MAGNETIC
                        for(k2=0;k2<3;k2++)
                            out.Gradients[k].B[k2] += wk_xyz * dB[k2];
#ifdef DIVBCLEANING_DEDNER
                        out.Gradients[k].Phi += wk_xyz * dphi;
#endif
#endif
#ifdef RADTRANSFER_FLUXLIMITER
                        for(k2=0;k2<N_BINS;k2++)
                            out->Gradients[k].n_gamma[k2] += wk_xyz * dn[k2];
#endif
                    } // for(k=0;k<3;k++) //
                } // r2 < h2
            } // numngb loop
        } // while(startnode)
        
#ifndef DONOTUSENODELIST
        if(mode == 1)
        {
            listindex++;
            if(listindex < NODELISTLENGTH)
            {
                startnode = AddSPHDataGet[target].NodeList[listindex];
                if(startnode >= 0)
                    startnode = Nodes[startnode].u.d.nextnode;	/* open it */
            }
        }
#endif
    }
    
    /* Now collect the result at the right place */
    if(mode == 0)
        out2particle_addSPH(&out, target, 0);
    else
        AddSPHDataResult[target] = out;
    
    return 0;
}





void *addSPH_evaluate_primary(void *p)
{
  int thread_id = *(int *) p;
  int i, j;
  int *exportflag, *exportnodecount, *exportindex, *ngblist;

  ngblist = Ngblist + thread_id * NumPart;
  exportflag = Exportflag + thread_id * NTask;
  exportnodecount = Exportnodecount + thread_id * NTask;
  exportindex = Exportindex + thread_id * NTask;

  /* Note: exportflag is local to each thread */
  for(j = 0; j < NTask; j++)
    exportflag[j] = -1;

  while(1)
    {
      int exitFlag = 0;
      LOCK_NEXPORT;
#ifdef _OPENMP
#pragma omp critical(_nexport_)
#endif
      {
	if(BufferFullFlag != 0 || NextParticle < 0)
	  {
	    exitFlag = 1;
	  }
	else
	  {
	    i = NextParticle;
	    ProcessedFlag[i] = 0;
	    NextParticle = NextActiveParticle[NextParticle];
	  }
      }
      UNLOCK_NEXPORT;
      if(exitFlag)
	break;

      if(P[i].Type == 0)
	{
	  if(addSPH_evaluate(i, 0, exportflag, exportnodecount, exportindex, ngblist) < 0)
	    break;		/* export buffer has filled up */
	}
      ProcessedFlag[i] = 1; /* particle successfully finished */
    }

  return NULL;
}

void *addSPH_evaluate_secondary(void *p)
{
  int thread_id = *(int *) p;
  int j, dummy, *ngblist;

  ngblist = Ngblist + thread_id * NumPart;

  while(1)
    {
      LOCK_NEXPORT;
#ifdef _OPENMP
#pragma omp critical(_nexport_)
#endif
      {
	j = NextJ;
	NextJ++;
      }
      UNLOCK_NEXPORT;

      if(j >= Nimport)
	break;

      addSPH_evaluate(j, 1, &dummy, &dummy, &dummy, ngblist);
    }

  return NULL;
}

