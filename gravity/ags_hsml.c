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
#ifdef OMP_NUM_THREADS
extern pthread_mutex_t mutex_nexport;
extern pthread_mutex_t mutex_partnodedrift;
#define LOCK_NEXPORT     pthread_mutex_lock(&mutex_nexport);
#define UNLOCK_NEXPORT   pthread_mutex_unlock(&mutex_nexport);
#else
#define LOCK_NEXPORT
#define UNLOCK_NEXPORT
#endif

/*! \file ags_hsml.c
 *  \brief kernel length determination for non-gas particles
 *
 *  This file contains a loop modeled on the gas density computation which 
 *    determines softening lengths (and appropriate correction terms) 
 *    for all particle types, to make softenings fully adaptive
 */
/*
 * This file was written by Phil Hopkins (phopkins@caltech.edu) for GIZMO.
 */


/*! this routine is called by the adaptive gravitational softening neighbor search and forcetree (for application 
    of the appropriate correction terms), to determine which particle types "talk to" which other particle types 
    (i.e. which particle types you search for to determine the softening radii for gravity). For effectively volume-filling
    fluids like gas or dark matter, it makes sense for this to be 'matched' to particles of the same type. For other 
    particle types like stars or black holes, it's more ambiguous, and requires some judgement on the part of the user. */
int ags_gravity_kernel_shared_check(short int particle_type_primary, short int particle_type_secondary)
{
    /* gas particles see gas particles */
    if(particle_type_primary == 0)
        return (particle_type_secondary==particle_type_primary);

#ifdef ADAPTIVE_GRAVSOFT_FORALL
#ifdef BLACK_HOLES
    /* black hole particles see gas */
    if(particle_type_primary == 5)
        return (particle_type_secondary == 0);
#endif
#ifdef GALSF
    /* stars see baryons (any type) */
    if(All.ComovingIntegrationOn)
    {
        if(particle_type_primary == 4)
            return ((particle_type_secondary==0)||(particle_type_secondary==4));
    } else {
        if((particle_type_primary == 4)||(particle_type_primary == 2)||(particle_type_primary == 3))
            return ((particle_type_secondary==0)||(particle_type_secondary==4)||(particle_type_secondary == 2)||(particle_type_secondary == 3));
    }
#endif
#ifdef SIDM
    /* SIDM particles see other SIDM particles */
    if((1 << particle_type_primary) & (SIDM))
        return ((1 << particle_type_secondary) & (SIDM));
#endif
    
    /* if we haven't been caught by one of the above checks, we simply return whether or not we see 'ourselves' */
    return (particle_type_primary == particle_type_secondary);
#endif
    
    return 0;
}



#ifdef ADAPTIVE_GRAVSOFT_FORALL

/*! Structure for communication during the density computation. Holds data that is sent to other processors.
 */
static struct ags_densdata_in
{
  MyDouble Pos[3];
  MyFloat Vel[3];
  MyFloat Hsml;
  int NodeList[NODELISTLENGTH];
  int Type;
}
 *AGS_DensDataIn, *AGS_DensDataGet;

static struct ags_densdata_out
{
    MyLongDouble Ngb;
    MyLongDouble DhsmlNgb;
    MyLongDouble AGS_zeta;
    MyLongDouble Particle_DivVel;
}
 *AGS_DensDataResult, *AGS_DensDataOut;

void ags_particle2in_density(struct ags_densdata_in *in, int i);
void ags_out2particle_density(struct ags_densdata_out *out, int i, int mode);

void ags_particle2in_density(struct ags_densdata_in *in, int i)
{
    int k;
    for(k = 0; k < 3; k++)
    {
        in->Pos[k] = P[i].Pos[k];
        in->Vel[k] = P[i].Vel[k];
    }
    in->Hsml = PPP[i].Hsml;
    in->Type = P[i].Type;
}

void ags_out2particle_density(struct ags_densdata_out *out, int i, int mode)
{
    ASSIGN_ADD(PPP[i].NumNgb, out->Ngb, mode);
    ASSIGN_ADD(PPPZ[i].AGS_zeta, out->AGS_zeta,   mode);
    ASSIGN_ADD(P[i].Particle_DivVel, out->Particle_DivVel,   mode);
    ASSIGN_ADD(PPP[i].DhsmlNgbFactor, out->DhsmlNgb, mode);
}

struct kernel_density
{
    double dp[3],dv[3],r;
    double wk, dwk;
    double hinv, hinv3, hinv4;
};


void ags_density(void)
{
  MyFloat *Left, *Right;
  int i, j, k, ndone, ndone_flag, npleft, iter = 0;
  int ngrp, recvTask, place;
  long long ntot;
  double fac, fac_lim;
  double timeall = 0, timecomp1 = 0, timecomp2 = 0, timecommsumm1 = 0, timecommsumm2 = 0, timewait1 = 0, timewait2 = 0;
  double timecomp, timecomm, timewait;
  double tstart, tend, t0, t1;
  double desnumngb, desnumngbdev;
  int save_NextParticle;
  long long n_exported = 0;
  int redo_particle;
  int particle_set_to_minhsml_flag = 0;
  int particle_set_to_maxhsml_flag = 0;

  CPU_Step[CPU_AGSDENSMISC] += measure_time();

  long long NTaskTimesNumPart;
  NTaskTimesNumPart = maxThreads * NumPart;
  Ngblist = (int *) mymalloc("Ngblist", NTaskTimesNumPart * sizeof(int));

  Left = (MyFloat *) mymalloc("Left", NumPart * sizeof(MyFloat));
  Right = (MyFloat *) mymalloc("Right", NumPart * sizeof(MyFloat));

  for(i = FirstActiveParticle; i >= 0; i = NextActiveParticle[i])
    {
      if(ags_density_isactive(i))
              Left[i] = Right[i] = 0;
    }

  /* allocate buffers to arrange communication */
  size_t MyBufferSize = All.BufferSize;
  All.BunchSize = (int) ((MyBufferSize * 1024 * 1024) / (sizeof(struct data_index) + sizeof(struct data_nodelist) +
					     sizeof(struct ags_densdata_in) + sizeof(struct ags_densdata_out) +
					     sizemax(sizeof(struct ags_densdata_in),
						     sizeof(struct ags_densdata_out))));
  DataIndexTable = (struct data_index *) mymalloc("DataIndexTable", All.BunchSize * sizeof(struct data_index));
  DataNodeList = (struct data_nodelist *) mymalloc("DataNodeList", All.BunchSize * sizeof(struct data_nodelist));

  t0 = my_second();

  /* we will repeat the whole thing for those particles where we didn't find enough neighbours */
  do
    {

      NextParticle = FirstActiveParticle;	/* begin with this index */

      do
	{
	  BufferFullFlag = 0;
	  Nexport = 0;
	  save_NextParticle = NextParticle;

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
	      pthread_create(&mythreads[j], &attr, ags_density_evaluate_primary, &threadid[j]);
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
	    ags_density_evaluate_primary(&mainthreadid);	/* do local particles and prepare export list */
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
		  printf("ags-Task %d: Type=%d pos=(%g,%g,%g) mass=%g\n",ThisTask,P[NextParticle].Type,
			 P[NextParticle].Pos[0],P[NextParticle].Pos[1],P[NextParticle].Pos[2],P[NextParticle].Mass);

		  endrun(111008);
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

	  AGS_DensDataGet = (struct ags_densdata_in *) mymalloc("AGS_DensDataGet", Nimport * sizeof(struct ags_densdata_in));
	  AGS_DensDataIn = (struct ags_densdata_in *) mymalloc("AGS_DensDataIn", Nexport * sizeof(struct ags_densdata_in));

	  /* prepare particle data for export */
	  for(j = 0; j < Nexport; j++)
	    {
	      place = DataIndexTable[j].Index;

	      ags_particle2in_density(&AGS_DensDataIn[j], place);

	      memcpy(AGS_DensDataIn[j].NodeList,
		     DataNodeList[DataIndexTable[j].IndexGet].NodeList, NODELISTLENGTH * sizeof(int));
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
		      MPI_Sendrecv(&AGS_DensDataIn[Send_offset[recvTask]],
				   Send_count[recvTask] * sizeof(struct ags_densdata_in), MPI_BYTE,
				   recvTask, TAG_DENS_A,
				   &AGS_DensDataGet[Recv_offset[recvTask]],
				   Recv_count[recvTask] * sizeof(struct ags_densdata_in), MPI_BYTE,
				   recvTask, TAG_DENS_A, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		    }
		}
	    }
	  tend = my_second();
	  timecommsumm1 += timediff(tstart, tend);

	  myfree(AGS_DensDataIn);
	  AGS_DensDataResult = (struct ags_densdata_out *) mymalloc("AGS_DensDataResult", Nimport * sizeof(struct ags_densdata_out));
	  AGS_DensDataOut = (struct ags_densdata_out *) mymalloc("AGS_DensDataOut", Nexport * sizeof(struct ags_densdata_out));

	  /* now do the particles that were sent to us */

	  tstart = my_second();

	  NextJ = 0;

#ifdef OMP_NUM_THREADS
	  for(j = 0; j < OMP_NUM_THREADS - 1; j++)
	    pthread_create(&mythreads[j], &attr, ags_density_evaluate_secondary, &threadid[j]);
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
	    ags_density_evaluate_secondary(&mainthreadid);
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
		      MPI_Sendrecv(&AGS_DensDataResult[Recv_offset[recvTask]],
				   Recv_count[recvTask] * sizeof(struct ags_densdata_out),
				   MPI_BYTE, recvTask, TAG_DENS_B,
				   &AGS_DensDataOut[Send_offset[recvTask]],
				   Send_count[recvTask] * sizeof(struct ags_densdata_out),
				   MPI_BYTE, recvTask, TAG_DENS_B, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
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
	      ags_out2particle_density(&AGS_DensDataOut[j], place, 1);
	    }
	  tend = my_second();
	  timecomp1 += timediff(tstart, tend);


	  myfree(AGS_DensDataOut);
	  myfree(AGS_DensDataResult);
	  myfree(AGS_DensDataGet);
	}
      while(ndone < NTask);


      /* do check on whether we have enough neighbors, and iterate for density-hsml solution */
        tstart = my_second();
        for(i = FirstActiveParticle, npleft = 0; i >= 0; i = NextActiveParticle[i])
        {
            if((P[i].Mass>0)&&(PPP[i].Hsml>0)&&(PPP[i].NumNgb>0)&&(ags_density_isactive(i)))
            {
                /* first use Ngb as summed in neighbor loop to normalize DhsmlNgb and DivVel */
                PPP[i].DhsmlNgbFactor *= PPP[i].Hsml / (NUMDIMS * PPP[i].NumNgb);
                P[i].Particle_DivVel /= PPP[i].NumNgb;
                /* spherical volume of the Kernel (use this to normalize 'effective neighbor number') */
                PPP[i].NumNgb *= NORM_COEFF * pow(PPP[i].Hsml,NUMDIMS);; /* now we define 'effective neighbor number */
                
                if(PPP[i].DhsmlNgbFactor > -0.9)
                    PPP[i].DhsmlNgbFactor = 1 / (1 + PPP[i].DhsmlNgbFactor);
                else
                    PPP[i].DhsmlNgbFactor = 1;
                P[i].Particle_DivVel *= PPP[i].DhsmlNgbFactor;
                
                /* now check whether we have enough neighbours */
                redo_particle = 0;
                
                double minsoft = All.ForceSoftening[P[i].Type];
                double maxsoft = All.MaxHsml;
                maxsoft = DMIN(maxsoft, 50.0 * All.ForceSoftening[P[i].Type]);
#ifdef PMGRID
                /*!< this gives the maximum allowed gravitational softening when using the TreePM method.
                 *  The quantity is given in units of the scale used for the force split (ASMTH) */
                maxsoft = DMIN(maxsoft, 0.5 * All.Asmth[0]); /* no more than 1/2 the size of the largest PM cell */
#endif

#ifdef BLACK_HOLES
                if(P[i].Type == 5) {maxsoft = All.BlackHoleMaxAccretionRadius;}
#endif
                
                desnumngb = All.AGS_DesNumNgb;
                desnumngbdev = All.AGS_MaxNumNgbDeviation;
                if(All.Time==All.TimeBegin) {if(All.AGS_MaxNumNgbDeviation > 0.05) desnumngbdev=0.05;}
                /* allow the neighbor tolerance to gradually grow as we iterate, so that we don't spend forever trapped in a narrow iteration */
                if(iter > 1) {desnumngbdev = DMIN( 0.25*desnumngb , desnumngbdev * exp(0.1*log(desnumngb/(16.*desnumngbdev))*(double)iter) );}
                
                /* check if we are in the 'normal' range between the max/min allowed values */
                if((PPP[i].NumNgb < (desnumngb - desnumngbdev) && PPP[i].Hsml < 0.99*maxsoft) ||
                   (PPP[i].NumNgb > (desnumngb + desnumngbdev) && PPP[i].Hsml > 1.01*minsoft))
                    redo_particle = 1;
                
                /* check maximum kernel size allowed */
                particle_set_to_maxhsml_flag = 0;
                if((PPP[i].Hsml >= 0.99*maxsoft) && (PPP[i].NumNgb < (desnumngb - desnumngbdev)))
                {
                    redo_particle = 0;
                    if(PPP[i].Hsml == maxsoft)
                    {
                        /* iteration at the maximum value is already complete */
                        particle_set_to_maxhsml_flag = 0;
                    } else {
                        /* ok, the particle needs to be set to the maximum, and (if gas) iterated one more time */
                        if(P[i].Type==0) redo_particle = 1;
                        PPP[i].Hsml = maxsoft;
                        particle_set_to_maxhsml_flag = 1;
                    }
                }
                
                /* check minimum kernel size allowed */
                particle_set_to_minhsml_flag = 0;
                if((PPP[i].Hsml <= 1.01*minsoft) && (PPP[i].NumNgb > (desnumngb + desnumngbdev)))
                {
                    redo_particle = 0;
                    if(PPP[i].Hsml == minsoft)
                    {
                        /* this means we've already done an iteration with the MinHsml value, so the
                         neighbor weights, etc, are not going to be wrong; thus we simply stop iterating */
                        particle_set_to_minhsml_flag = 0;
                    } else {
                        /* ok, the particle needs to be set to the minimum, and (if gas) iterated one more time */
                        if(P[i].Type==0) redo_particle = 1;
                        PPP[i].Hsml = minsoft;
                        particle_set_to_minhsml_flag = 1;
                    }
                }
                
                if(redo_particle)
                {
                    if(iter >= MAXITER - 10)
                    {
                        printf("AGS: i=%d task=%d ID=%llu Type=%d Hsml=%g dhsml=%g Left=%g Right=%g Ngbs=%g Right-Left=%g maxh_flag=%d minh_flag=%d  minsoft=%g maxsoft=%g desnum=%g desnumtol=%g redo=%d pos=(%g|%g|%g)\n",
                               i, ThisTask, (unsigned long long) P[i].ID, P[i].Type, PPP[i].Hsml, PPP[i].DhsmlNgbFactor, Left[i], Right[i],
                               (float) PPP[i].NumNgb, Right[i] - Left[i], particle_set_to_maxhsml_flag, particle_set_to_minhsml_flag, minsoft,
                               maxsoft, desnumngb, desnumngbdev, redo_particle, P[i].Pos[0], P[i].Pos[1], P[i].Pos[2]);
                        fflush(stdout);
                    }
                    
                    /* need to redo this particle */
                    npleft++;
                    
                    if(Left[i] > 0 && Right[i] > 0)
                        if((Right[i] - Left[i]) < 1.0e-3 * Left[i])
                        {
                            /* this one should be ok */
                            npleft--;
                            P[i].TimeBin = -P[i].TimeBin - 1;	/* Mark as inactive */
                            continue;
                        }
                    
                    if((particle_set_to_maxhsml_flag==0)&&(particle_set_to_minhsml_flag==0))
                    {
                        if(PPP[i].NumNgb < (desnumngb - desnumngbdev))
                            Left[i] = DMAX(PPP[i].Hsml, Left[i]);
                        else
                        {
                            if(Right[i] != 0)
                            {
                                if(PPP[i].Hsml < Right[i])
                                    Right[i] = PPP[i].Hsml;
                            }
                            else
                                Right[i] = PPP[i].Hsml;
                        }
                        
                        // right/left define upper/lower bounds from previous iterations
                        if(Right[i] > 0 && Left[i] > 0)
                        {
                            // geometric interpolation between right/left //
                            double maxjump=0;
                            if(iter>1) {maxjump = 0.2*log(Right[i]/Left[i]);}
                            if(PPP[i].NumNgb > 1)
                            {
                                double jumpvar = PPP[i].DhsmlNgbFactor * log( desnumngb / PPP[i].NumNgb ) / NUMDIMS;
                                if(iter>1) {if(fabs(jumpvar) < maxjump) {if(jumpvar<0) {jumpvar=-maxjump;} else {jumpvar=maxjump;}}}
                                PPP[i].Hsml *= exp(jumpvar);
                            } else {
                                PPP[i].Hsml *= 2.0;
                            }
                            if((PPP[i].Hsml<Right[i])&&(PPP[i].Hsml>Left[i]))
                            {
                                if(iter > 1)
                                {
                                    double hfac = exp(maxjump);
                                    if(PPP[i].Hsml > Right[i] / hfac) {PPP[i].Hsml = Right[i] / hfac;}
                                    if(PPP[i].Hsml < Left[i] * hfac) {PPP[i].Hsml = Left[i] * hfac;}
                                }
                            } else {
                                if(PPP[i].Hsml>Right[i]) PPP[i].Hsml=Right[i];
                                if(PPP[i].Hsml<Left[i]) PPP[i].Hsml=Left[i];
                                PPP[i].Hsml = pow(PPP[i].Hsml * Left[i] * Right[i] , 1.0/3.0);
                            }
                        }
                        else
                        {
                            if(Right[i] == 0 && Left[i] == 0)
                            {
                                char buf[1000];
                                sprintf(buf, "AGS: Right[i] == 0 && Left[i] == 0 && PPP[i].Hsml=%g\n", PPP[i].Hsml);
                                terminate(buf);
                            }
                            
                            if(Right[i] == 0 && Left[i] > 0)
                            {
                                if (PPP[i].NumNgb > 1)
                                    fac_lim = log( desnumngb / PPP[i].NumNgb ) / NUMDIMS; // this would give desnumgb if constant density (+0.231=2x desnumngb)
                                else
                                    fac_lim = 1.4; // factor ~66 increase in N_NGB in constant-density medium
                                
                                if((PPP[i].NumNgb < 2*desnumngb)&&(PPP[i].NumNgb > 0.1*desnumngb))
                                {
                                    double slope = PPP[i].DhsmlNgbFactor;
                                    if(iter>2 && slope<1) slope = 0.5*(slope+1);
                                    fac = fac_lim * slope; // account for derivative in making the 'corrected' guess
                                    if(iter>=10)
                                        if(PPP[i].DhsmlNgbFactor==1) fac *= 10; // tries to help with being trapped in small steps
                                    
                                    if(fac < fac_lim+0.231)
                                    {
                                        PPP[i].Hsml *= exp(fac); // more expensive function, but faster convergence
                                    }
                                    else
                                    {
                                        PPP[i].Hsml *= exp(fac_lim+0.231);
                                        // fac~0.26 leads to expected doubling of number if density is constant,
                                        //   insert this limiter here b/c we don't want to get *too* far from the answer (which we're close to)
                                    }
                                }
                                else
                                    PPP[i].Hsml *= exp(fac_lim); // here we're not very close to the 'right' answer, so don't trust the (local) derivatives
                            }
                            
                            if(Right[i] > 0 && Left[i] == 0)
                            {
                                if (PPP[i].NumNgb > 1)
                                    fac_lim = log( desnumngb / PPP[i].NumNgb ) / NUMDIMS; // this would give desnumgb if constant density (-0.231=0.5x desnumngb)
                                else
                                    fac_lim = 1.4; // factor ~66 increase in N_NGB in constant-density medium
                                
                                if (fac_lim < -1.535) fac_lim = -1.535; // decreasing N_ngb by factor ~100
                                
                                if((PPP[i].NumNgb < 2*desnumngb)&&(PPP[i].NumNgb > 0.1*desnumngb))
                                {
                                    double slope = PPP[i].DhsmlNgbFactor;
                                    if(iter>2 && slope<1) slope = 0.5*(slope+1);
                                    fac = fac_lim * slope; // account for derivative in making the 'corrected' guess
                                    if(iter>=10)
                                        if(PPP[i].DhsmlNgbFactor==1) fac *= 10; // tries to help with being trapped in small steps
                                    
                                    if(fac > fac_lim-0.231)
                                    {
                                        PPP[i].Hsml *= exp(fac); // more expensive function, but faster convergence
                                    }
                                    else
                                        PPP[i].Hsml *= exp(fac_lim-0.231); // limiter to prevent --too-- far a jump in a single iteration
                                }
                                else
                                    PPP[i].Hsml *= exp(fac_lim); // here we're not very close to the 'right' answer, so don't trust the (local) derivatives
                            }
                        } // closes if[particle_set_to_max/minhsml_flag]
                    } // closes redo_particle
                    /* resets for max/min values */
                    if(PPP[i].Hsml < minsoft) PPP[i].Hsml = minsoft;
                    if(particle_set_to_minhsml_flag==1) PPP[i].Hsml = minsoft;
                    if(PPP[i].Hsml > maxsoft) PPP[i].Hsml = maxsoft;
                    if(particle_set_to_maxhsml_flag==1) PPP[i].Hsml = maxsoft;
                }
                else
                    P[i].TimeBin = -P[i].TimeBin - 1;	/* Mark as inactive */
            } //  if(ags_density_isactive(i))
        } // for(i = FirstActiveParticle, npleft = 0; i >= 0; i = NextActiveParticle[i])
        
        tend = my_second();
        timecomp1 += timediff(tstart, tend);
        sumup_large_ints(1, &npleft, &ntot);
        if(ntot > 0)
        {
            iter++;
            if(iter > 0 && ThisTask == 0)
            {
                printf("ags-ngb iteration %d: need to repeat for %d%09d particles.\n", iter,
                       (int) (ntot / 1000000000), (int) (ntot % 1000000000));
                fflush(stdout);
            }
            if(iter > MAXITER)
            {
                printf("ags-failed to converge in neighbour iteration in density()\n");
                fflush(stdout);
                endrun(1155);
            }
        }
    }
    while(ntot > 0);

    myfree(DataNodeList);
    myfree(DataIndexTable);
    myfree(Right);
    myfree(Left);
    myfree(Ngblist);
    
    /* mark as active again */
    for(i = FirstActiveParticle; i >= 0; i = NextActiveParticle[i])
    {
        if(P[i].TimeBin < 0)
            P[i].TimeBin = -P[i].TimeBin - 1;
    }

    /* now that we are DONE iterating to find hsml, we can do the REAL final operations on the results */
    for(i = FirstActiveParticle; i >= 0; i = NextActiveParticle[i])
    {
        if(ags_density_isactive(i))
        {
            if((P[i].Mass>0)&&(PPP[i].Hsml>0)&&(PPP[i].NumNgb>0))
            {
                
                double minsoft = All.ForceSoftening[P[i].Type];
                double maxsoft = All.MaxHsml;
                maxsoft = DMIN(maxsoft, 50.0 * All.ForceSoftening[P[i].Type]);
#ifdef PMGRID
                maxsoft = DMIN(maxsoft, 0.5 * All.Asmth[0]); /* no more than 1/2 the size of the largest PM cell */
#endif
                /* check that we're within the 'valid' range for adaptive softening terms, otherwise zeta=0 */
                if((fabs(PPP[i].NumNgb-All.AGS_DesNumNgb)/All.AGS_DesNumNgb < 0.05)
                   &&(PPP[i].Hsml <= 0.99*maxsoft)&&(PPP[i].Hsml >= 1.01*minsoft)
                   &&(PPP[i].NumNgb >= (All.AGS_DesNumNgb - All.AGS_MaxNumNgbDeviation))
                   &&(PPP[i].NumNgb <= (All.AGS_DesNumNgb + All.AGS_MaxNumNgbDeviation)))
                {
                    double ndenNGB = PPP[i].NumNgb / ( NORM_COEFF * pow(PPP[i].Hsml,NUMDIMS) );
                    PPPZ[i].AGS_zeta *= 0.5 * P[i].Mass * PPP[i].Hsml / (NUMDIMS * ndenNGB) * PPP[i].DhsmlNgbFactor;
                } else {
                    PPPZ[i].AGS_zeta = 0;
                }
                /* convert NGB to the more useful format, NumNgb^(1/NDIMS), which we can use to obtain the corrected particle sizes */
                PPP[i].NumNgb = pow(PPP[i].NumNgb , 1./NUMDIMS);
            } else {
                PPPZ[i].AGS_zeta = 0;
                PPP[i].NumNgb = 0;
            }
        }
    }

    /* collect some timing information */
    
    t1 = WallclockTime = my_second();
    timeall += timediff(t0, t1);
    
    timecomp = timecomp1 + timecomp2;
    timewait = timewait1 + timewait2;
    timecomm = timecommsumm1 + timecommsumm2;
    
    CPU_Step[CPU_AGSDENSCOMPUTE] += timecomp;
    CPU_Step[CPU_AGSDENSWAIT] += timewait;
    CPU_Step[CPU_AGSDENSCOMM] += timecomm;
    CPU_Step[CPU_AGSDENSMISC] += timeall - (timecomp + timewait + timecomm);
}






/*! This function represents the core of the density computation. The
 *  target particle may either be local, or reside in the communication
 *  buffer.
 */
int ags_density_evaluate(int target, int mode, int *exportflag, int *exportnodecount, int *exportindex, int *ngblist)
{
    int j, n;
    int startnode, numngb_inbox, listindex = 0;
    double r2, h2, u;
    struct kernel_density kernel;
    struct ags_densdata_in local;
    struct ags_densdata_out out;
    memset(&out, 0, sizeof(struct ags_densdata_out));
    
    if(mode == 0)
        ags_particle2in_density(&local, target);
    else
        local = AGS_DensDataGet[target];
    
    h2 = local.Hsml * local.Hsml;
    kernel_hinv(local.Hsml, &kernel.hinv, &kernel.hinv3, &kernel.hinv4);
    
    if(mode == 0)
    {
        startnode = All.MaxPart;	/* root node */
    }
    else
    {
        startnode = AGS_DensDataGet[target].NodeList[0];
        startnode = Nodes[startnode].u.d.nextnode;	/* open it */
    }
    
    while(startnode >= 0)
    {
        while(startnode >= 0)
        {
            numngb_inbox = ags_ngb_treefind_variable_threads(local.Pos, local.Hsml, target, &startnode, mode, exportflag,
                                          exportnodecount, exportindex, ngblist, local.Type);
            
            if(numngb_inbox < 0)
                return -1;
            
            for(n = 0; n < numngb_inbox; n++)
            {
                j = ngblist[n];
                if(P[j].Mass <= 0) continue;
                
                kernel.dp[0] = local.Pos[0] - P[j].Pos[0];
                kernel.dp[1] = local.Pos[1] - P[j].Pos[1];
                kernel.dp[2] = local.Pos[2] - P[j].Pos[2];
#ifdef PERIODIC /*  now find the closest image in the given box size  */
                kernel.dp[0] = NEAREST_X(kernel.dp[0]);
                kernel.dp[1] = NEAREST_Y(kernel.dp[1]);
                kernel.dp[2] = NEAREST_Z(kernel.dp[2]);
#endif
                r2 = kernel.dp[0] * kernel.dp[0] + kernel.dp[1] * kernel.dp[1] + kernel.dp[2] * kernel.dp[2];
                if(r2 < h2)
                {
                    kernel.r = sqrt(r2);
                    u = kernel.r * kernel.hinv;
                    kernel_main(u, kernel.hinv3, kernel.hinv4, &kernel.wk, &kernel.dwk, 0);

                    out.Ngb += kernel.wk;
                    out.DhsmlNgb += -(NUMDIMS * kernel.hinv * kernel.wk + u * kernel.dwk);
                    out.AGS_zeta += P[j].Mass * kernel_gravity(u, kernel.hinv, kernel.hinv3, 0);

                    if(kernel.r > 0)
                    {
                        if(P[j].Type==0)
                        {
                            kernel.dv[0] = local.Vel[0] - SphP[j].VelPred[0];
                            kernel.dv[1] = local.Vel[1] - SphP[j].VelPred[1];
                            kernel.dv[2] = local.Vel[2] - SphP[j].VelPred[2];
                        } else {
                            kernel.dv[0] = local.Vel[0] - P[j].Vel[0];
                            kernel.dv[1] = local.Vel[1] - P[j].Vel[1];
                            kernel.dv[2] = local.Vel[2] - P[j].Vel[2];
                        }
#ifdef SHEARING_BOX
                        if(local.Pos[0] - P[j].Pos[0] > +boxHalf_X) {kernel.dv[SHEARING_BOX_PHI_COORDINATE] += Shearing_Box_Vel_Offset;}
                        if(local.Pos[0] - P[j].Pos[0] < -boxHalf_X) {kernel.dv[SHEARING_BOX_PHI_COORDINATE] -= Shearing_Box_Vel_Offset;}
#endif
                        out.Particle_DivVel -= kernel.dwk * (kernel.dp[0] * kernel.dv[0] + kernel.dp[1] * kernel.dv[1] + kernel.dp[2] * kernel.dv[2]) / kernel.r;
                        /* this is the -particle- divv estimator, which determines how Hsml will evolve */
                    }
                }
            }
        }
        
        if(mode == 1)
        {
            listindex++;
            if(listindex < NODELISTLENGTH)
            {
                startnode = AGS_DensDataGet[target].NodeList[listindex];
                if(startnode >= 0)
                    startnode = Nodes[startnode].u.d.nextnode;	/* open it */
            }
        }
    }
    
    if(mode == 0)
        ags_out2particle_density(&out, target, 0);
    else
        AGS_DensDataResult[target] = out;
    
    return 0;
}



void *ags_density_evaluate_primary(void *p)
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
        
        if(ags_density_isactive(i))
        {
            if(ags_density_evaluate(i, 0, exportflag, exportnodecount, exportindex, ngblist) < 0)
                break;		/* export buffer has filled up */
        }
        ProcessedFlag[i] = 1;	/* particle successfully finished */
    }
    return NULL;
}



void *ags_density_evaluate_secondary(void *p)
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
        
        ags_density_evaluate(j, 1, &dummy, &dummy, &dummy, ngblist);
    }
    return NULL;
}


/* routine to determine if we need to use ags_density to calculate Hsml */
int ags_density_isactive(MyIDType i)
{
    if(P[i].TimeBin < 0) return 0;
    /* check our 'marker' for particles which have finished
     iterating to an Hsml solution (if they have, dont do them again) */
    if(density_isactive(i)) return 0;
    /* would have already been handled in hydro density routine */
    return 1;
}


#endif // ADAPTIVE_GRAVSOFT_FORALL //
