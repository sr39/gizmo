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
 *  \brief smoothing length determination for non-gas particles
 *
 *  This file contains a loop modeled on the gas density computation which 
 *    determines softening lengths (and appropriate correction terms) 
 *    for all particle types, to make softenings fully adaptive
 */

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
    ASSIGN_ADD(PPPZ[i].DhsmlNgbFactor, out->DhsmlNgb, mode);
}

struct kernel_density
{
    double dx, dy, dz;
    double r;
    double dvx, dvy, dvz;
    double wk, dwk;
    double hinv, hinv3, hinv4;
    double mj_wk, mj_dwk_r;
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

  CPU_Step[CPU_AGSDENSMISC] += measure_time();

  int NTaskTimesNumPart;

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
  desnumngb = All.AGS_DesNumNgb;
  desnumngbdev = All.AGS_MaxNumNgbDeviation;

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
		  printf("Task %d: Type=%d pos=(%g,%g,%g) mass=%g\n",ThisTask,P[NextParticle].Type,
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
                PPPZ[i].DhsmlNgbFactor *= PPP[i].Hsml / (NUMDIMS * PPP[i].NumNgb);
                P[i].Particle_DivVel /= PPP[i].NumNgb;
                /* spherical volume of the Kernel (use this to normalize 'effective neighbor number') */
                PPP[i].NumNgb *= NORM_COEFF * pow(PPP[i].Hsml,NUMDIMS);; /* now we define 'effective neighbor number */
                
                if(PPPZ[i].DhsmlNgbFactor > -0.9)
                    PPPZ[i].DhsmlNgbFactor = 1 / (1 + PPPZ[i].DhsmlNgbFactor);
                else
                    PPPZ[i].DhsmlNgbFactor = 1;
                P[i].Particle_DivVel *= PPPZ[i].DhsmlNgbFactor;
                
                /* now check whether we have enough neighbours */
                redo_particle = 0;
                
                /* the force softenings in the usual input now serve as the MINIMUM smoothings/force softenings allowed */
                if(PPP[i].NumNgb < (desnumngb - desnumngbdev) ||
                   (PPP[i].NumNgb > (desnumngb + desnumngbdev) && PPP[i].Hsml > (1.01 * All.ForceSoftening[P[i].Type])))
                    redo_particle = 1;
                
                double maxsoft = All.MaxHsml;
#ifdef PMGRID
                /*!< this gives the maximum allowed gravitational softening when using the TreePM method.
                 *  The quantity is given in units of the scale used for the force split (ASMTH) */
                maxsoft = DMIN(maxsoft, 0.5 * All.Asmth[0]); /* no more than 1/2 the size of the largest PM cell */
#endif
                if(PPP[i].NumNgb < (desnumngb - desnumngbdev) && PPP[i].Hsml >= maxsoft)
                {
                    PPP[i].Hsml = maxsoft;
                    redo_particle = 0;
                }
                
                if(redo_particle)
                {
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
                    
                    if(iter >= MAXITER - 10)
                    {
                        printf
                        ("i=%d task=%d ID=%llu Type=%d Hsml=%g Left=%g Right=%g Ngbs=%g Right-Left=%g\n   pos=(%g|%g|%g)\n",
                         i, ThisTask, (unsigned long long) P[i].ID, P[i].Type, PPP[i].Hsml, Left[i], Right[i],
                         (float) PPP[i].NumNgb, Right[i] - Left[i], P[i].Pos[0], P[i].Pos[1], P[i].Pos[2]);
                        fflush(stdout);
                    }
                    
                    // right/left define upper/lower bounds from previous iterations
                    if(Right[i] > 0 && Left[i] > 0)
                    {
                        PPP[i].Hsml = sqrt(Left[i]*Right[i]); // geometric interpolation between right/left
                    }
                    else
                    {
                        if(Right[i] == 0 && Left[i] == 0)
                        {
                            char buf[1000];
                            sprintf(buf, "Right[i] == 0 && Left[i] == 0 && PPP[i].Hsml=%g\n", PPP[i].Hsml);
                            terminate(buf);
                        }
                        
                        if(Right[i] == 0 && Left[i] > 0)
                        {
                            if (PPP[i].NumNgb > 1)
                                fac_lim = log( desnumngb / PPP[i].NumNgb ) / NUMDIMS; // this would give desnumgb if constant density (+0.231=2x desnumngb)
                            else
                                fac_lim = 1.4; // factor ~66 increase in N_NGB in constant-density medium
                            
                            if(fabs(PPP[i].NumNgb - desnumngb) < 0.75 * desnumngb)
                            {
                                fac = fac_lim * PPPZ[i].DhsmlNgbFactor; // account for derivative in making the 'corrected' guess
                                if(iter>=20)
                                    if(PPPZ[i].DhsmlNgbFactor==1) fac *= 10; // tries to help with being trapped in small steps
                                
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
                            
                            if(fabs(PPP[i].NumNgb - desnumngb) < 0.75 * desnumngb)
                            {
                                fac = fac_lim * PPPZ[i].DhsmlNgbFactor; // account for derivative in making the 'corrected' guess
                                if(iter>=20)
                                    if(PPPZ[i].DhsmlNgbFactor==1) fac *= 10; // tries to help with being trapped in small steps
                                
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
                    }
                    
                    if(PPP[i].Hsml < All.ForceSoftening[P[i].Type])
                        PPP[i].Hsml = All.ForceSoftening[P[i].Type];
                    
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
                printf("ngb iteration %d: need to repeat for %d%09d particles.\n", iter,
                       (int) (ntot / 1000000000), (int) (ntot % 1000000000));
                fflush(stdout);
            }
            if(iter > MAXITER)
            {
                printf("failed to converge in neighbour iteration in ags_density()\n");
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
                double ndenNGB = PPP[i].NumNgb / ( NORM_COEFF * pow(PPP[i].Hsml,NUMDIMS) );
                PPPZ[i].AGS_zeta *= 0.5 * P[i].Mass * PPP[i].Hsml / (NUMDIMS * ndenNGB) * PPPZ[i].DhsmlNgbFactor;
            } else {
                PPPZ[i].AGS_zeta = 0;
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
    double r2, h2, u, mass_j;
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
                
                kernel.dx = local.Pos[0] - P[j].Pos[0];
                kernel.dy = local.Pos[1] - P[j].Pos[1];
                kernel.dz = local.Pos[2] - P[j].Pos[2];
#ifdef PERIODIC /*  now find the closest image in the given box size  */
                kernel.dx = NEAREST_X(kernel.dx);
                kernel.dy = NEAREST_Y(kernel.dy);
                kernel.dz = NEAREST_Z(kernel.dz);
#endif
                r2 = kernel.dx * kernel.dx + kernel.dy * kernel.dy + kernel.dz * kernel.dz;
                
                if(r2 < h2)
                {
                    kernel.r = sqrt(r2);
                    u = kernel.r * kernel.hinv;
                    kernel_main(u, kernel.hinv3, kernel.hinv4, &kernel.wk, &kernel.dwk, 0);
                    mass_j = P[j].Mass;
                    kernel.mj_wk = FLT(mass_j * kernel.wk);
                    
                    out.Ngb += kernel.wk;
                    out.DhsmlNgb += -(NUMDIMS * kernel.hinv * kernel.wk + u * kernel.dwk);
                    out.AGS_zeta += mass_j * kernel_gravity(u, kernel.hinv, kernel.hinv3, 0);

                    if(kernel.r > 0)
                    {
                        kernel.dvx = local.Vel[0] - SphP[j].VelPred[0];
                        kernel.dvy = local.Vel[1] - SphP[j].VelPred[1];
                        kernel.dvz = local.Vel[2] - SphP[j].VelPred[2];
                        out.Particle_DivVel += kernel.dwk * (kernel.dx * kernel.dvx + kernel.dy * kernel.dvy + kernel.dz * kernel.dvz) / kernel.r;
                        /* this is the (not especially accurate) SPH div-v estimator: however we only need a crude
                         approximation to use in drift steps (consistency here does not affect convergence), so its fast and ok */
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
