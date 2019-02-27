//
//  dm_density.c
//  ガスをターゲットとした時周囲のダークマター粒子の密度をP[i].dm_densityで与える。
//
//  Created by 市橋 on 2016/11/28.
//
//
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <gsl/gsl_math.h>
#include "../allvars.h"
#include "../proto.h"
#include "../kernel.h"
#include "dm_interaction.h"
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

/*struct dm_kernel_density
{
    double dp[3],dv[3],r,dv_vari;
    double wk, dwk;
    double hinv, hinv3, hinv4;
    double mj_wk, mj_dwk_r;
};*/


/*! Structure for communication during the density computation. Holds data that is sent to other processors.
 */
/*static struct dm_densdata_in
{
    MyDouble Pos[3];
#if defined(SPHAV_CD10_VISCOSITY_SWITCH)
    MyFloat Accel[3];
#endif
    MyFloat Vel[3];
    MyFloat Hsml;
    int NodeList[NODELISTLENGTH];
    int Type;
}
*dm_DensDataIn, *dm_DensDataGet;*/

/*static struct dm_densdata_out
{
    MyLongDouble dm_Ngb;//ガス粒子の周囲のダークマターの粒子数
    MyLongDouble dm_Rho;//ガス粒子周囲のダークマターの密度
    MyLongDouble dm_coll;//ダークマターとガスの衝突率
   // MyLongDouble dm_v_rel;      ダークマター粒子の平均速度に乗った座標から見たときのガスの粒子速度
    MyLongDouble dm_DhsmlNgb;
    MyLongDouble dm_Particle_DivVel;
    MyFloat dm_NV_T[3][3];
#ifdef HYDRO_SPH
    MyLongDouble dm_DhsmlHydroSumFactor;
#endif
//#ifdef RT_SOURCE_INJECTION
    MyLongDouble KernelSum_Around_RT_Source;
#endif
    
#ifdef SPHEQ_DENSITY_INDEPENDENT_SPH
    MyLongDouble dm_EgyRho;
#endif
    
#if defined(ADAPTIVE_GRAVSOFT_FORALL) || defined(ADAPTIVE_GRAVSOFT_FORGAS)
    MyFloat dm_AGS_zeta;
#endif
    
#if defined(SPHAV_CD10_VISCOSITY_SWITCH)
    MyFloat dm_NV_D[3][3];
    MyFloat dm_NV_A[3][3];
#endif
    
#ifdef DO_DENSITY_AROUND_STAR_PARTICLES
    MyFloat GradRho[3];
#endif
    
    
#if defined(FLAG_NOT_IN_PUBLIC_CODE) || defined(GRAIN_FLUID)
    MyDouble GasVel[3];
#endif
#if defined(GRAIN_FLUID)
    MyDouble Gas_InternalEnergy;
#ifdef GRAIN_LORENTZFORCE
    MyDouble Gas_B[3];
#endif
#endif
    
}
*dm_DensDataResult, *dm_DensDataOut;*/
#ifdef DM_BARYON_INTERACTION
void particle2in_dm_density(struct dm_densdata_in *in, int i);
void out2particle_dm_density(struct dm_densdata_out *out, int i, int mode);
void dm_density_evaluate_extra_physics_gas(struct dm_densdata_in *local, struct dm_densdata_out *out,
                                        struct dm_kernel_density *kernel, int j);


void particle2in_dm_density(struct dm_densdata_in *in, int i)
{
    int k;
    in->Type = P[i].Type;
    
    in->dm_Hsml = PPP[i].dm_Hsml;
    for(k = 0; k < 3; k++)
    {
        in->Pos[k] = P[i].Pos[k];
        if(P[i].Type==0) {in->Vel[k]=SphP[i].VelPred[k];} else {in->Vel[k]=P[i].Vel[k];}
    }
    
    if(P[i].Type == 0)
    {

    in->Density = SphP[i].Density;
/*#if defined(SPHAV_CD10_VISCOSITY_SWITCH)
        for(k = 0; k < 3; k++)
            in->Accel[k] = All.cf_a2inv*P[i].GravAccel[k] + SphP[i].HydroAccel[k]; // PHYSICAL units //
#endif*/
        
    }
}


void out2particle_dm_density(struct dm_densdata_out *out, int i, int mode)
{
    int j,k;
    ASSIGN_ADD(PPP[i].dm_NumNgb, out->dm_Ngb, mode);
    ASSIGN_ADD(PPP[i].dm_DhsmlNgbFactor, out->dm_DhsmlNgb, mode);
    ASSIGN_ADD(P[i].dm_Particle_DivVel, out->dm_Particle_DivVel,   mode);
  
    
    if(P[i].Type == 0)
    {
        ASSIGN_ADD(SphP[i].dm_coll, out->dm_coll, mode);
        ASSIGN_ADD(SphP[i].dm_density, out->dm_Rho, mode);
 /*       ASSIGN_ADD(SphP[i].dm_v_rel, out->dm_v_rel);      周囲のダークマターの平均速度に乗った座標から見たときのガスの粒子の速度を表している*/
        
        for(k = 0; k < 3; k++)
            for(j = 0; j < 3; j++)
                ASSIGN_ADD(SphP[i].dm_NV_T[k][j], out->dm_NV_T[k][j], mode);
        
#ifdef HYDRO_SPH
        ASSIGN_ADD(SphP[i].dm_DhsmlHydroSumFactor, out->dm_DhsmlHydroSumFactor, mode);
#endif
        
#if defined(ADAPTIVE_GRAVSOFT_FORGAS)
        ASSIGN_ADD(PPPZ[i].dm_AGS_zeta, out->dm_AGS_zeta,   mode);
#endif
        
/*#ifdef SPHEQ_DENSITY_INDEPENDENT_SPH
        ASSIGN_ADD(SphP[i].EgyWtDensity,   out->EgyRho,   mode);
#endif
        
        
#if defined(SPHAV_CD10_VISCOSITY_SWITCH)
        for(k = 0; k < 3; k++)
            for(j = 0; j < 3; j++)
            {
                ASSIGN_ADD(SphP[i].dm_NV_D[k][j], out->dm_NV_D[k][j], mode);
                ASSIGN_ADD(SphP[i].dm_NV_A[k][j], out->dm_NV_A[k][j], mode);
            }
#endif
    } // P[i].Type == 0 //
    
#if defined(GRAIN_FLUID)
    if(P[i].Type > 0)
    {
        ASSIGN_ADD(P[i].Gas_Density, out->Rho, mode);
        ASSIGN_ADD(P[i].Gas_InternalEnergy, out->Gas_InternalEnergy, mode);
        for(k = 0; k<3; k++) {ASSIGN_ADD(P[i].Gas_Velocity[k], out->GasVel[k], mode);}
#ifdef GRAIN_LORENTZFORCE
        for(k = 0; k<3; k++) {ASSIGN_ADD(P[i].Gas_B[k], out->Gas_B[k], mode);}
#endif
    }
#endif
    
#ifdef DO_DENSITY_AROUND_STAR_PARTICLES
    if(P[i].Type != 0)
    {
        ASSIGN_ADD(P[i].DensAroundStar, out->Rho, mode);
        for(k = 0; k<3; k++) {ASSIGN_ADD(P[i].GradRho[k], out->GradRho[k], mode);}
    }
#endif
    
#if defined(RT_SOURCE_INJECTION)
    if((1 << P[i].Type) & (RT_SOURCES)) {ASSIGN_ADD(P[i].KernelSum_Around_RT_Source, out->KernelSum_Around_RT_Source, mode);}
#endif
    
 */   
}
}
void dm_density(void)
{
    MyFloat *Left, *Right;
    int i, j, k, k1, k2, ndone, ndone_flag, npleft, iter = 0;
    int ngrp, recvTask, place;
    long long ntot;
    double fac, fac_lim;
    double Tinv[3][3], detT, CNumHolder=0, ConditionNumber=0;
    double timeall = 0, timecomp1 = 0, timecomp2 = 0, timecommsumm1 = 0, timecommsumm2 = 0, timewait1 =
    0, timewait2 = 0;
    double timecomp, timecomm, timewait;
    double tstart, tend, t0, t1;
    double desnumngb, desnumngbdev;
    int save_NextParticle;
    long long n_exported = 0;
    int redo_particle;
    int particle_set_to_minhsml_flag = 0;
    int particle_set_to_maxhsml_flag = 0;
    
    CPU_Step[CPU_DENSMISC] += measure_time();
    
    long long NTaskTimesNumPart;
    NTaskTimesNumPart = maxThreads * NumPart;
    Ngblist = (int *) mymalloc("Ngblist", NTaskTimesNumPart * sizeof(int));
    
    Left = (MyFloat *) mymalloc("Left", NumPart * sizeof(MyFloat));
    Right = (MyFloat *) mymalloc("Right", NumPart * sizeof(MyFloat));
    
    for(i = FirstActiveParticle; i >= 0; i = NextActiveParticle[i])
    
{
//printf("\n%3f\n",PPP[i].Hsml);
        if(dm_density_isactive(i))
        {
            Left[i] = Right[i] = 0;
        }
    } /* done with intial zero-out loop */
    
    /* allocate buffers to arrange communication */
    size_t MyBufferSize = All.BufferSize;
    All.BunchSize =
    (int) ((MyBufferSize * 1024 * 1024) / (sizeof(struct data_index) + sizeof(struct data_nodelist) +
                                           sizeof(struct dm_densdata_in) + sizeof(struct dm_densdata_out) +
                                           sizemax(sizeof(struct dm_densdata_in),
                                                   sizeof(struct dm_densdata_out))));
    DataIndexTable =
    (struct data_index *) mymalloc("DataIndexTable", All.BunchSize * sizeof(struct data_index));
    DataNodeList =
    (struct data_nodelist *) mymalloc("DataNodeList", All.BunchSize * sizeof(struct data_nodelist));
    
    t0 = my_second();
    
    desnumngb = All.DesNumNgb;
    desnumngbdev = All.MaxNumNgbDeviation;
    /* in the initial timestep and iteration, use a much more strict tolerance for the neighbor number */
    if(All.Time==All.TimeBegin) {if(All.MaxNumNgbDeviation > 0.05) desnumngbdev=0.05;}
    double desnumngbdev_0 = desnumngbdev;
    
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

           printf("283行め");
 
            for(j = 0; j < OMP_NUM_THREADS - 1; j++)
            {
                threadid[j] = j + 1;
                pthread_create(&mythreads[j], &attr, dm_density_evaluate_primary, &threadid[j]);
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
                dm_density_evaluate_primary(&mainthreadid);	/* do local particles and prepare export list */
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
                    if(P[NextParticle].Type == 0)
                        printf("   rho=%g hsml=%g\n",SphP[NextParticle].dm_density,PPP[NextParticle].dm_Hsml);
                    
                    endrun(112208);
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
            
            dm_DensDataGet = (struct dm_densdata_in *) mymalloc("dm_DensDataGet", Nimport * sizeof(struct dm_densdata_in));
            dm_DensDataIn = (struct dm_densdata_in *) mymalloc("dm_DensDataIn", Nexport * sizeof(struct dm_densdata_in));
            /* prepare particle data for export */
            for(j = 0; j < Nexport; j++)
            {
                place = DataIndexTable[j].Index;
                
                particle2in_dm_density(&dm_DensDataIn[j], place);
                
                memcpy(dm_DensDataIn[j].NodeList,
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
                        MPI_Sendrecv(&dm_DensDataIn[Send_offset[recvTask]],
                                     Send_count[recvTask] * sizeof(struct dm_densdata_in), MPI_BYTE,
                                     recvTask, TAG_DENS_A,
                                     &dm_DensDataGet[Recv_offset[recvTask]],
                                     Recv_count[recvTask] * sizeof(struct dm_densdata_in), MPI_BYTE,
                                     recvTask, TAG_DENS_A, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    }
                }
            }
            tend = my_second();
            timecommsumm1 += timediff(tstart, tend);
            
            myfree(dm_DensDataIn);
            dm_DensDataResult =
            (struct dm_densdata_out *) mymalloc("dm_DensDataResult", Nimport * sizeof(struct dm_densdata_out));
            dm_DensDataOut =
            (struct dm_densdata_out *) mymalloc("dm_DensDataOut", Nexport * sizeof(struct dm_densdata_out));
            
            report_memory_usage(&HighMark_sphdensity, "SPH_DENSITY");
            
            /* now do the particles that were sent to us */
            
            tstart = my_second();
            
            NextJ = 0;
            
#ifdef OMP_NUM_THREADS
            for(j = 0; j < OMP_NUM_THREADS - 1; j++)
                pthread_create(&mythreads[j], &attr, dm_density_evaluate_secondary, &threadid[j]);
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
                dm_density_evaluate_secondary(&mainthreadid);
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
                        MPI_Sendrecv(&dm_DensDataResult[Recv_offset[recvTask]],
                                     Recv_count[recvTask] * sizeof(struct dm_densdata_out),
                                     MPI_BYTE, recvTask, TAG_DENS_B,
                                     &dm_DensDataOut[Send_offset[recvTask]],
                                     Send_count[recvTask] * sizeof(struct dm_densdata_out),
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
                out2particle_dm_density(&dm_DensDataOut[j], place, 1);
            }
            tend = my_second();
            timecomp1 += timediff(tstart, tend);
            
            
            myfree(dm_DensDataOut);
            myfree(dm_DensDataResult);
            myfree(dm_DensDataGet);
        }
        while(ndone < NTask);
        
        
        /* do check on whether we have enough neighbors, and iterate for density-hsml solution */
        tstart = my_second();
        for(i = FirstActiveParticle, npleft = 0; i >= 0; i = NextActiveParticle[i])
        {
            if(dm_density_isactive(i))
            {
//printf("%3f\n",PPP[i].Hsml);

                if(PPP[i].dm_NumNgb > 0)
                {
                    PPP[i].dm_DhsmlNgbFactor *= PPP[i].dm_Hsml / (NUMDIMS * PPP[i].dm_NumNgb);
                    P[i].dm_Particle_DivVel /= PPP[i].dm_NumNgb;
                    /* spherical volume of the Kernel (use this to normalize 'effective neighbor number') */
                    PPP[i].dm_NumNgb *= NORM_COEFF * pow(PPP[i].dm_Hsml,NUMDIMS);
                } else {
                    PPP[i].dm_NumNgb = PPP[i].dm_DhsmlNgbFactor = P[i].dm_Particle_DivVel = 0;
                }
#ifdef ADAPTIVE_GRAVSOFT_FORALL
                if(P[i].Type > 0) {PPP[i].dm_Particle_DivVel = 0;}
#endif
                
                // inverse of SPH volume element (to satisfy constraint implicit in Lagrange multipliers)
                if(PPP[i].dm_DhsmlNgbFactor > -0.9)	/* note: this would be -1 if only a single particle at zero lag is found */
                    PPP[i].dm_DhsmlNgbFactor = 1 / (1 + PPP[i].dm_DhsmlNgbFactor);
                else
                    PPP[i].dm_DhsmlNgbFactor = 1;
                P[i].dm_Particle_DivVel *= PPP[i].dm_DhsmlNgbFactor;
                
                if(P[i].Type == 0)
                {
                    /* fill in the missing elements of NV_T (it's symmetric, so we saved time not computing these directly) */
                    SphP[i].dm_NV_T[1][0]=SphP[i].dm_NV_T[0][1]; SphP[i].dm_NV_T[2][0]=SphP[i].dm_NV_T[0][2]; SphP[i].dm_NV_T[2][1]=SphP[i].dm_NV_T[1][2];
                    /* Now invert the NV_T matrix we just measured */
                    /* Also, we want to be able to calculate the condition number of the matrix to be inverted, since
                     this will tell us how robust our procedure is (and let us know if we need to expand the neighbor number */
                    ConditionNumber=CNumHolder=0;
                    for(k1=0;k1<3;k1++) {for(k2=0;k2<3;k2++) {ConditionNumber += SphP[i].dm_NV_T[k1][k2]*SphP[i].dm_NV_T[k1][k2];}}
#if (NUMDIMS==1)
                    /* one-dimensional case */
                    for(k1=0;k1<3;k1++) {for(k2=0;k2<3;k2++) {Tinv[k1][k2]=0;}}
                    detT = SphP[i].dm_NV_T[0][0];
                    if(SphP[i].dm_NV_T[0][0]!=0 && !isnan(SphP[i].dm_NV_T[0][0])) Tinv[0][0] = 1/detT; /* only one non-trivial element in 1D! */
#endif
#if (NUMDIMS==2)
                    /* two-dimensional case */
                    for(k1=0;k1<3;k1++) {for(k2=0;k2<3;k2++) {Tinv[k1][k2]=0;}}
                    detT = SphP[i].dm_NV_T[0][0]*SphP[i].dm_NV_T[1][1] - SphP[i].dm_NV_T[0][1]*SphP[i].dm_NV_T[1][0];
                    if((detT != 0)&&(!isnan(detT)))
                    {
                        Tinv[0][0] = SphP[i].dm_NV_T[1][1] / detT;
                        Tinv[0][1] = -SphP[i].dm_NV_T[0][1] / detT;
                        Tinv[1][0] = -SphP[i].dm_NV_T[1][0] / detT;
                        Tinv[1][1] = SphP[i].dm_NV_T[0][0] / detT;
                    }
#endif
#if (NUMDIMS==3)
                    /* three-dimensional case */
                    detT = SphP[i].dm_NV_T[0][0] * SphP[i].dm_NV_T[1][1] * SphP[i].dm_NV_T[2][2] +
                    SphP[i].dm_NV_T[0][1] * SphP[i].dm_NV_T[1][2] * SphP[i].dm_NV_T[2][0] +
                    SphP[i].dm_NV_T[0][2] * SphP[i].dm_NV_T[1][0] * SphP[i].dm_NV_T[2][1] -
                    SphP[i].dm_NV_T[0][2] * SphP[i].dm_NV_T[1][1] * SphP[i].dm_NV_T[2][0] -
                    SphP[i].dm_NV_T[0][1] * SphP[i].dm_NV_T[1][0] * SphP[i].dm_NV_T[2][2] -
                    SphP[i].dm_NV_T[0][0] * SphP[i].dm_NV_T[1][2] * SphP[i].dm_NV_T[2][1];
                    /* check for zero determinant */
                    if((detT != 0) && !isnan(detT))
                    {
                        Tinv[0][0] = (SphP[i].dm_NV_T[1][1] * SphP[i].dm_NV_T[2][2] - SphP[i].dm_NV_T[1][2] * SphP[i].dm_NV_T[2][1]) / detT;
                        Tinv[0][1] = (SphP[i].dm_NV_T[0][2] * SphP[i].dm_NV_T[2][1] - SphP[i].dm_NV_T[0][1] * SphP[i].dm_NV_T[2][2]) / detT;
                        Tinv[0][2] = (SphP[i].dm_NV_T[0][1] * SphP[i].dm_NV_T[1][2] - SphP[i].dm_NV_T[0][2] * SphP[i].dm_NV_T[1][1]) / detT;
                        Tinv[1][0] = (SphP[i].dm_NV_T[1][2] * SphP[i].dm_NV_T[2][0] - SphP[i].dm_NV_T[1][0] * SphP[i].dm_NV_T[2][2]) / detT;
                        Tinv[1][1] = (SphP[i].dm_NV_T[0][0] * SphP[i].dm_NV_T[2][2] - SphP[i].dm_NV_T[0][2] * SphP[i].dm_NV_T[2][0]) / detT;
                        Tinv[1][2] = (SphP[i].dm_NV_T[0][2] * SphP[i].dm_NV_T[1][0] - SphP[i].dm_NV_T[0][0] * SphP[i].dm_NV_T[1][2]) / detT;
                        Tinv[2][0] = (SphP[i].dm_NV_T[1][0] * SphP[i].dm_NV_T[2][1] - SphP[i].dm_NV_T[1][1] * SphP[i].dm_NV_T[2][0]) / detT;
                        Tinv[2][1] = (SphP[i].dm_NV_T[0][1] * SphP[i].dm_NV_T[2][0] - SphP[i].dm_NV_T[0][0] * SphP[i].dm_NV_T[2][1]) / detT;
                        Tinv[2][2] = (SphP[i].dm_NV_T[0][0] * SphP[i].dm_NV_T[1][1] - SphP[i].dm_NV_T[0][1] * SphP[i].dm_NV_T[1][0]) / detT;
                    } else {
                        for(k1=0;k1<3;k1++) {for(k2=0;k2<3;k2++) {Tinv[k1][k2]=0;}}
                    }
#endif
                    
                    for(k1=0;k1<3;k1++) {for(k2=0;k2<3;k2++) {CNumHolder += Tinv[k1][k2]*Tinv[k1][k2];}}
                    ConditionNumber = sqrt(ConditionNumber*CNumHolder) / NUMDIMS;
                    if(ConditionNumber<1) ConditionNumber=1;
                    /* this = sqrt( ||NV_T^-1||*||NV_T|| ) :: should be ~1 for a well-conditioned matrix */
                    for(k1=0;k1<3;k1++) {for(k2=0;k2<3;k2++) {SphP[i].dm_NV_T[k1][k2]=Tinv[k1][k2];}}
                    /* now NV_T holds the inverted matrix elements, for use in hydro */
                } // P[i].Type == 0 //
                
                /* now check whether we had enough neighbours */
                double ncorr_ngb = 1.0;
                double cn=1;
                double c0 = 0.1 * (double)CONDITION_NUMBER_DANGER;
                if(P[i].Type==0)
                {
                    /* use the previous timestep condition number to correct how many neighbors we should use for stability */
                    if((iter==0)&&(ConditionNumber>SphP[i].dm_ConditionNumber))
                    {
                        /* if we find ourselves with a sudden increase in condition number - check if we have a reasonable
                         neighbor number for the previous iteration, and if so, use the new (larger) correction */
                        ncorr_ngb=1; cn=SphP[i].dm_ConditionNumber; if(cn>c0) {ncorr_ngb=sqrt(1.0+(cn-c0)/((double)CONDITION_NUMBER_DANGER));} if(ncorr_ngb>2) ncorr_ngb=2;
                        double dn_ngb = fabs(PPP[i].dm_NumNgb-All.DesNumNgb*ncorr_ngb)/(desnumngbdev_0*ncorr_ngb);
                        ncorr_ngb=1; cn=ConditionNumber; if(cn>c0) {ncorr_ngb=sqrt(1.0+(cn-c0)/((double)CONDITION_NUMBER_DANGER));} if(ncorr_ngb>2) ncorr_ngb=2;
                        double dn_ngb_alt = fabs(PPP[i].dm_NumNgb-All.DesNumNgb*ncorr_ngb)/(desnumngbdev_0*ncorr_ngb);
                        dn_ngb = DMIN(dn_ngb,dn_ngb_alt);
                        if(dn_ngb < 10.0) SphP[i].dm_ConditionNumber = ConditionNumber;
                    }
                    ncorr_ngb=1; cn=SphP[i].ConditionNumber; if(cn>c0) {ncorr_ngb=sqrt(1.0+(cn-c0)/((double)CONDITION_NUMBER_DANGER));} if(ncorr_ngb>2) ncorr_ngb=2;
                }
                
                desnumngb = All.DesNumNgb * ncorr_ngb;
                desnumngbdev = desnumngbdev_0 * ncorr_ngb;
                /* allow the neighbor tolerance to gradually grow as we iterate, so that we don't spend forever trapped in a narrow iteration */
                if(iter > 1) {desnumngbdev = DMIN( 0.25*desnumngb , desnumngbdev * exp(0.1*log(desnumngb/(16.*desnumngbdev))*(double)iter) );}
                
                
/*#ifdef GRAIN_FLUID*/
                /* for the grains, we only need to estimate neighboring gas properties, we don't need to worry about
                 condition numbers or conserving an exact neighbor number */
/*                if(P[i].Type>0)
                {
                    desnumngb = All.DesNumNgb;
                    desnumngbdev = All.DesNumNgb / 4;
                }
#endif*/
                
                double minsoft = All.MinHsml;
                double maxsoft = All.MaxHsml;
                
#ifdef DO_DENSITY_AROUND_STAR_PARTICLES
                /* use a much looser check for N_neighbors when the central point is a star particle,
                 since the accuracy is limited anyways to the coupling efficiency -- the routines use their
                 own estimators+neighbor loops, anyways, so this is just to get some nearby particles */
                if((P[i].Type!=0)&&(P[i].Type!=5))
                {
                    desnumngb = All.DesNumNgb;
/*#if defined(RT_SOURCE_INJECTION)
                    if(desnumngb < 64.0) {desnumngb = 64.0;} // we do want a decent number to ensure the area around the particle is 'covered'
#endif*/
                    desnumngbdev = desnumngb / 2; // enforcing exact number not important
                }
#endif
                
                
                redo_particle = 0;
                
                /* check if we are in the 'normal' range between the max/min allowed values */
                if((PPP[i].dm_NumNgb < (desnumngb - desnumngbdev) && PPP[i].dm_Hsml < 0.99*maxsoft) ||
                   (PPP[i].dm_NumNgb > (desnumngb + desnumngbdev) && PPP[i].dm_Hsml > 1.01*minsoft))
                    redo_particle = 1;
                
                /* check maximum kernel size allowed */
                particle_set_to_maxhsml_flag = 0;
                if((PPP[i].dm_Hsml >= 0.99*maxsoft) && (PPP[i].dm_NumNgb < (desnumngb - desnumngbdev)))
                {
                    redo_particle = 0;
                    if(PPP[i].dm_Hsml == maxsoft)
                    {
                        /* iteration at the maximum value is already complete */
                        particle_set_to_maxhsml_flag = 0;
                    } else {
                        /* ok, the particle needs to be set to the maximum, and (if gas) iterated one more time */
                        if(P[i].Type==0) redo_particle = 1;
                        PPP[i].dm_Hsml = maxsoft;
                        particle_set_to_maxhsml_flag = 1;
                    }
                }
                
                /* check minimum kernel size allowed */
                particle_set_to_minhsml_flag = 0;
                if((PPP[i].dm_Hsml <= 1.01*minsoft) && (PPP[i].dm_NumNgb > (desnumngb + desnumngbdev)))
                {
                    redo_particle = 0;
                    if(PPP[i].dm_Hsml == minsoft)
                    {
                        /* this means we've already done an iteration with the MinHsml value, so the
                         neighbor weights, etc, are not going to be wrong; thus we simply stop iterating */
                        particle_set_to_minhsml_flag = 0;
                    } else {
                        /* ok, the particle needs to be set to the minimum, and (if gas) iterated one more time */
                        if(P[i].Type==0) redo_particle = 1;
                        PPP[i].dm_Hsml = minsoft;
                        particle_set_to_minhsml_flag = 1;
                    }
                }
                
//printf("%3f\n",PPP[i].Hsml);                
                if((redo_particle==0)&&(P[i].Type == 0))
                {
                    /* ok we have reached the desired number of neighbors: save the condition number for next timestep */
                    if(ConditionNumber > 1000.0 * (double)CONDITION_NUMBER_DANGER)
                    {
#ifndef IO_REDUCED_MODE
                        printf("Warning: Condition number=%g CNum_prevtimestep=%g Num_Ngb=%g desnumngb=%g dm_Hsml=%g Hsml_min=%g Hsml_max=%g\n",
                               ConditionNumber,SphP[i].dm_ConditionNumber,PPP[i].dm_NumNgb,desnumngb,PPP[i].dm_Hsml,All.MinHsml,All.MaxHsml);
#endif
                    }
                    SphP[i].dm_ConditionNumber = ConditionNumber;
                }
                
                if(redo_particle)
                {
                    if(iter >= MAXITER - 10)
                    {
                        printf("i=%d task=%d ID=%llu Type=%d dm_Hsml=%g dhsml=%g Left=%g Right=%g Ngbs=%g Right-Left=%g maxh_flag=%d minh_flag=%d  minsoft=%g maxsoft=%g desnum=%g desnumtol=%g redo=%d pos=(%g|%g|%g)\n",
                               i, ThisTask, (unsigned long long) P[i].ID, P[i].Type, PPP[i].dm_Hsml, PPP[i].dm_DhsmlNgbFactor, Left[i], Right[i],
                               (float) PPP[i].dm_NumNgb, Right[i] - Left[i], particle_set_to_maxhsml_flag, particle_set_to_minhsml_flag, minsoft,
                               maxsoft, desnumngb, desnumngbdev, redo_particle, P[i].Pos[0], P[i].Pos[1], P[i].Pos[2]);
                    }
                    
                    /* need to redo this particle */
                    npleft++;
                    
                    if(Left[i] > 0 && Right[i] > 0)
                        if((Right[i] - Left[i]) < 1.0e-3 * Left[i])
                        {
                            /* this one should be ok */
                            npleft--;
                            P[i].TimeBin = -P[i].TimeBin - 1;	/* Mark as inactive */
                            SphP[i].dm_ConditionNumber = ConditionNumber;
                            continue;
                        }
                    
                    if((particle_set_to_maxhsml_flag==0)&&(particle_set_to_minhsml_flag==0))
                    {
                        if(PPP[i].dm_NumNgb < (desnumngb - desnumngbdev))
                            Left[i] = DMAX(PPP[i].dm_Hsml, Left[i]);
                        else
                        {
                            if(Right[i] != 0)
                            {
                                if(PPP[i].dm_Hsml < Right[i])
                                    Right[i] = PPP[i].dm_Hsml;
                            }
                            else
                                Right[i] = PPP[i].dm_Hsml;
                        }
                        
                        // right/left define upper/lower bounds from previous iterations
                        if(Right[i] > 0 && Left[i] > 0)
                        {
                            // geometric interpolation between right/left //
                            double maxjump=0;
                            if(iter>1) {maxjump = 0.2*log(Right[i]/Left[i]);}
                            if(PPP[i].dm_NumNgb > 1)
                            {
                                double jumpvar = PPP[i].dm_DhsmlNgbFactor * log( desnumngb / PPP[i].dm_NumNgb ) / NUMDIMS;
                                if(iter>1) {if(fabs(jumpvar) < maxjump) {if(jumpvar<0) {jumpvar=-maxjump;} else {jumpvar=maxjump;}}}
                                PPP[i].dm_Hsml *= exp(jumpvar);
                            } else {
                                PPP[i].dm_Hsml *= 2.0;
                            }
                            if((PPP[i].dm_Hsml<Right[i])&&(PPP[i].dm_Hsml>Left[i]))
                            {
                                if(iter > 1)
                                {
                                    double hfac = exp(maxjump);
                                    if(PPP[i].dm_Hsml > Right[i] / hfac) {PPP[i].dm_Hsml = Right[i] / hfac;}
                                    if(PPP[i].dm_Hsml < Left[i] * hfac) {PPP[i].dm_Hsml = Left[i] * hfac;}
                                }
                            } else {
                                if(PPP[i].dm_Hsml>Right[i]) PPP[i].dm_Hsml=Right[i];
                                if(PPP[i].dm_Hsml<Left[i]) PPP[i].dm_Hsml=Left[i];
                                PPP[i].dm_Hsml = pow(PPP[i].dm_Hsml * Left[i] * Right[i] , 1.0/3.0);
                            }
                        }
                        else
                        {
                            if(Right[i] == 0 && Left[i] == 0)
                            {
                                char buf[1000];
                                sprintf(buf, "Right[i] == 0 && Left[i] == 0 && PPP[i].dm_Hsml=%g\n", PPP[i].dm_Hsml);
                                terminate(buf);
                            }
                            
                            if(Right[i] == 0 && Left[i] > 0)
                            {
                                if (PPP[i].dm_NumNgb > 1)
                                    fac_lim = log( desnumngb / PPP[i].dm_NumNgb ) / NUMDIMS; // this would give desnumgb if constant density (+0.231=2x desnumngb)
                                else
                                    fac_lim = 1.4; // factor ~66 increase in N_NGB in constant-density medium
                                
                                if((PPP[i].dm_NumNgb < 2*desnumngb)&&(PPP[i].dm_NumNgb > 0.1*desnumngb))
                                {
                                    double slope = PPP[i].dm_DhsmlNgbFactor;
                                    if(iter>2 && slope<1) slope = 0.5*(slope+1);
                                    fac = fac_lim * slope; // account for derivative in making the 'corrected' guess
                                    if(iter>=4)
                                        if(PPP[i].dm_DhsmlNgbFactor==1) fac *= 10; // tries to help with being trapped in small steps
                                    
                                    if(fac < fac_lim+0.231)
                                    {
                                        PPP[i].dm_Hsml *= exp(fac); // more expensive function, but faster convergence
                                    }
                                    else
                                    {
                                        PPP[i].dm_Hsml *= exp(fac_lim+0.231);
                                        // fac~0.26 leads to expected doubling of number if density is constant,
                                        //   insert this limiter here b/c we don't want to get *too* far from the answer (which we're close to)
                                    }
                                }
                                else
                                    PPP[i].dm_Hsml *= exp(fac_lim); // here we're not very close to the 'right' answer, so don't trust the (local) derivatives
                            }
                            
                            if(Right[i] > 0 && Left[i] == 0)
                            {
                                if (PPP[i].dm_NumNgb > 1)
                                    fac_lim = log( desnumngb / PPP[i].dm_NumNgb ) / NUMDIMS; // this would give desnumgb if constant density (-0.231=0.5x desnumngb)
                                else
                                    fac_lim = 1.4; // factor ~66 increase in N_NGB in constant-density medium
                                
                                if (fac_lim < -1.535) fac_lim = -1.535; // decreasing N_ngb by factor ~100
                                
                                if((PPP[i].dm_NumNgb < 2*desnumngb)&&(PPP[i].dm_NumNgb > 0.1*desnumngb))
                                {
                                    double slope = PPP[i].dm_DhsmlNgbFactor;
                                    if(iter>2 && slope<1) slope = 0.5*(slope+1);
                                    fac = fac_lim * slope; // account for derivative in making the 'corrected' guess
                                    if(iter>=4)
                                        if(PPP[i].dm_DhsmlNgbFactor==1) fac *= 10; // tries to help with being trapped in small steps
                                    
                                    if(fac > fac_lim-0.231)
                                    {
                                        PPP[i].dm_Hsml *= exp(fac); // more expensive function, but faster convergence
                                    }
                                    else
                                        PPP[i].dm_Hsml *= exp(fac_lim-0.231); // limiter to prevent --too-- far a jump in a single iteration
                                }
                                else
                                    PPP[i].dm_Hsml *= exp(fac_lim); // here we're not very close to the 'right' answer, so don't trust the (local) derivatives
                            }
                        } // closes if[particle_set_to_max/minhsml_flag]
                    } // closes redo_particle
                    /* resets for max/min values */
                    if(PPP[i].dm_Hsml < minsoft) PPP[i].dm_Hsml = minsoft;
                    if(particle_set_to_minhsml_flag==1) PPP[i].dm_Hsml = minsoft;
                    if(PPP[i].dm_Hsml > maxsoft) PPP[i].dm_Hsml = maxsoft;
                    if(particle_set_to_maxhsml_flag==1) PPP[i].dm_Hsml = maxsoft;
                }
                else
                    P[i].TimeBin = -P[i].TimeBin - 1;	/* Mark as inactive */
            }
        }
        tend = my_second();
        timecomp1 += timediff(tstart, tend);
        sumup_large_ints(1, &npleft, &ntot);
        if(ntot > 0)
        {
            iter++;
            if(iter > 0 && ThisTask == 0)
            {
#ifdef IO_REDUCED_MODE
                if(iter > 10)
#endif
                    printf("dm_ngb iteration %d: need to repeat for %d%09d particles.\n", iter,
                           (int) (ntot / 1000000000), (int) (ntot % 1000000000));
            }
            if(iter > MAXITER)
            {
                printf("failed to converge in neighbour iteration in density()\n");
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
    
    
    /* now that we are DONE iterating to find hsml, we can do the REAL final operations on the results
     ( any quantities that only need to be evaluated once, on the final iteration --
     won't save much b/c the real cost is in the neighbor loop for each particle, but it's something )
     -- also, some results (for example, viscosity suppression below) should not be calculated unless
     the quantities are 'stabilized' at their final values -- */
    for(i = FirstActiveParticle; i >= 0; i = NextActiveParticle[i])
    {
        if(dm_density_isactive(i))
        {
            if(P[i].Type == 0 && P[i].Mass > 0)
            {
                if(SphP[i].dm_density > 0)
                {
#ifdef HYDRO_SPH
/*#ifdef SPHEQ_DENSITY_INDEPENDENT_SPH
                    if(SphP[i].InternalEnergyPred > 0)
                    {
                        SphP[i].EgyWtDensity /= SphP[i].InternalEnergyPred;
                    } else {
                        SphP[i].EgyWtDensity = 0;
                    }
#endif*/
                    /* need to divide by the sum of x_tilde=1, i.e. numden_ngb */
                    if((PPP[i].dm_Hsml > 0)&&(PPP[i].dm_NumNgb > 0))
                    {
                        double numden_ngb = PPP[i].dm_NumNgb / ( NORM_COEFF * pow(PPP[i].dm_Hsml,NUMDIMS) );
                        SphP[i].dm_DhsmlHydroSumFactor *= PPP[i].dm_Hsml / (NUMDIMS * numden_ngb);
                        SphP[i].dm_DhsmlHydroSumFactor *= -PPP[i].dm_DhsmlNgbFactor; /* now this is ready to be called in hydro routine */
                    } else {
                        SphP[i].dm_DhsmlHydroSumFactor = 0;
                    }
#endif
                    
                    
/*#if defined(SPHAV_CD10_VISCOSITY_SWITCH)
                    for(k1 = 0; k1 < 3; k1++)
                        for(k2 = 0; k2 < 3; k2++)
                        {
                            SphP[i].NV_D[k2][k1] *= All.cf_a2inv; // converts to physical velocity/length
                            SphP[i].NV_A[k2][k1] /= All.cf_atime; // converts to physical accel/length
                        }
                    // all quantities below in this block should now be in proper PHYSICAL units, for subsequent operations //
                    double dtDV[3][3], A[3][3], V[3][3], S[3][3];
                    for(k1=0;k1<3;k1++)
                        for(k2=0;k2<3;k2++)
                        {
                            V[k1][k2] = SphP[i].NV_D[k1][0]*SphP[i].NV_T[0][k2] + SphP[i].NV_D[k1][1]*SphP[i].NV_T[1][k2] + SphP[i].NV_D[k1][2]*SphP[i].NV_T[2][k2];
                            A[k1][k2] = SphP[i].NV_A[k1][0]*SphP[i].NV_T[0][k2] + SphP[i].NV_A[k1][1]*SphP[i].NV_T[1][k2] + SphP[i].NV_A[k1][2]*SphP[i].NV_T[2][k2];
                        }
                    SphP[i].NV_DivVel = V[0][0] + V[1][1] + V[2][2];
                    SphP[i].NV_trSSt = 0;
                    for(k1=0;k1<3;k1++)
                        for(k2=0;k2<3;k2++)
                        {
                            dtDV[k1][k2] = A[k1][k2] - (V[k1][0]*V[0][k2] + V[k1][1]*V[1][k2] + V[k1][2]*V[2][k2]);
   */                         /* S = 0.5*(V+V_transpose) - delta_ij*div_v/3 */
/*                            S[k1][k2] = 0.5 * (V[k1][k2] + V[k2][k1]);
                            if(k2==k1) S[k1][k2] -= SphP[i].NV_DivVel / NUMDIMS;
  */                          /* Trace[S*S_transpose] = SSt[0][0]+SSt[1][1]+SSt[2][2] = |S|^2 = sum(Sij^2) */
/*                            SphP[i].NV_trSSt += S[k1][k2]*S[k1][k2];
                        }
                    SphP[i].NV_dt_DivVel = dtDV[0][0] + dtDV[1][1] + dtDV[2][2];
#endif*/
                    
                    
                }
                
#ifndef HYDRO_SPH
                if((PPP[i].dm_Hsml > 0)&&(PPP[i].dm_NumNgb > 0))
                {
                    SphP[i].dm_density = P[i].Mass * PPP[i].dm_NumNgb / ( NORM_COEFF * pow(PPP[i].dm_Hsml,NUMDIMS) ); // divide mass by volume
                } else {
                    if(PPP[i].dm_Hsml <= 0)
                    {
                        SphP[i].dm_density = 0; // in this case, give up, no meaningful volume
                    } else {
                        SphP[i].dm_density = P[i].Mass / ( NORM_COEFF * pow(PPP[i].dm_Hsml,NUMDIMS) ); // divide mass (lone particle) by volume
                    }
                }
#endif
                SphP[i].Pressure = get_pressure(i);		// should account for density independent pressure
                
            } // P[i].Type == 0
            
            
/*#if defined(GRAIN_FLUID)
            if(P[i].Type > 0)
            {
                if(P[i].Gas_Density > 0)
                {
                    P[i].Gas_InternalEnergy /= P[i].Gas_Density;
                    for(k = 0; k<3; k++) {P[i].Gas_Velocity[k] /= P[i].Gas_Density;}
                } else {
                    P[i].Gas_InternalEnergy = 0;
                    for(k = 0; k<3; k++) {P[i].Gas_Velocity[k] = 0;}
#ifdef GRAIN_LORENTZFORCE
                    for(k = 0; k<3; k++) {P[i].Gas_B[k] = 0;}
#endif
                }
            }
#endif*/
            
            
#if defined(ADAPTIVE_GRAVSOFT_FORGAS) || defined(ADAPTIVE_GRAVSOFT_FORALL)
            /* non-gas particles are handled separately, in the ags_hsml routine */
            if(P[i].Type==0)
            {
                PPPZ[i].AGS_zeta = 0;
                double zeta_0 = 0; // 2.0 * P[i].Mass*P[i].Mass * PPP[i].Hsml*PPP[i].Hsml; // self-value of zeta if no neighbors are found //
                if((PPP[i].dm_Hsml > 0)&&(PPP[i].dm_NumNgb > 0))
                {
                    /* the zeta terms ONLY control errors if we maintain the 'correct' neighbor number: for boundary
                     particles, it can actually be worse. so we need to check whether we should use it or not */
                    if((PPP[i].dm_Hsml > 1.01*All.MinHsml) && (PPP[i].dm_Hsml < 0.99*All.MaxHsml) &&
                       (fabs(PPP[i].dm_NumNgb-All.DesNumNgb)/All.DesNumNgb < 0.05))
                    {
                        double ndenNGB = PPP[i].dm_NumNgb / ( NORM_COEFF * pow(PPP[i].dm_Hsml,NUMDIMS) );
                        PPPZ[i].dm_AGS_zeta *= 0.5 * P[i].Mass * PPP[i].dm_Hsml / (NUMDIMS * ndenNGB) * PPP[i].dm_DhsmlNgbFactor;
                    } else {
                        PPPZ[i].dm_AGS_zeta = zeta_0;
                    }
                } else {
                    PPPZ[i].dm_AGS_zeta = zeta_0;
                }
            }
#endif
            
            
            
            /* finally, convert NGB to the more useful format, NumNgb^(1/NDIMS),
             which we can use to obtain the corrected particle sizes. Because of how this number is used above, we --must-- make
             sure that this operation is the last in the loop here */
            if(PPP[i].dm_NumNgb > 0) {PPP[i].dm_NumNgb=pow(PPP[i].dm_NumNgb,1./NUMDIMS);} else {PPP[i].dm_NumNgb=0;}
            
        } // density_isactive(i)
    } // for(i = FirstActiveParticle; i >= 0; i = NextActiveParticle[i])
    
    
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






/*! This function represents the core of the SPH density computation. The
 *  target particle may either be local, or reside in the communication
 *  buffer.
 */
int dm_density_evaluate(int target, int mode, int *exportflag, int *exportnodecount, int *exportindex,
                     int *ngblist)
{
    int j, n;
    int startnode, numngb_inbox, listindex = 0;
    double r2, h2, u, mass_j, wk, n_i, N_j, N_i;
    struct dm_kernel_density kernel;
    struct dm_densdata_in local;
    struct dm_densdata_out out;
    int TARGET_BITMASK = 2;
    memset(&out, 0, sizeof(struct dm_densdata_out));

    if(mode == 0)
        particle2in_dm_density(&local, target);
    else
        local = dm_DensDataGet[target];
    h2 = local.dm_Hsml * local.dm_Hsml;
    kernel_hinv(local.dm_Hsml, &kernel.hinv, &kernel.hinv3, &kernel.hinv4);
    
    if(mode == 0)
    {
        startnode = All.MaxPart;	/* root node */
    }
    else
    {
        startnode = dm_DensDataGet[target].NodeList[0];
        startnode = Nodes[startnode].u.d.nextnode;	/* open it */
    }
    
    while(startnode >= 0)
    {
        while(startnode >= 0)
        {
            numngb_inbox = ngb_treefind_variable_threads_targeted(local.Pos, local.dm_Hsml , target, &startnode,
                                            mode,  exportflag,  exportnodecount,  exportindex, ngblist, TARGET_BITMASK);
                                                                                                                         /*i番目のガス粒子周辺のダークマター粒子数*/
            if(numngb_inbox < 0) return -1;
           
            int count = 0;
            for(n = 0; n < numngb_inbox; n++)
            {
            j = ngblist[n];
                if(P[j].Mass <= 0) continue;
               
                kernel.dp[0] = local.Pos[0] - P[j].Pos[0];
                kernel.dp[1] = local.Pos[1] - P[j].Pos[1];
                kernel.dp[2] = local.Pos[2] - P[j].Pos[2];
#ifdef PERIODIC
                NEAREST_XYZ(kernel.dp[0],kernel.dp[1],kernel.dp[2],1);
#endif
                r2 = kernel.dp[0] * kernel.dp[0] + kernel.dp[1] * kernel.dp[1] + kernel.dp[2] * kernel.dp[2];



                if(r2 < h2)
                {
                kernel.dv[0] = (P[j].Vel[0] - local.Vel[0]) / All.cf_atime;
                kernel.dv[1] = (P[j].Vel[1] - local.Vel[1]) / All.cf_atime;
                kernel.dv[2] = (P[j].Vel[2] - local.Vel[2]) / All.cf_atime;

                  kernel.dv_vari += kernel.dv[0] * kernel.dv[0] + kernel.dv[1] * kernel.dv[1] + kernel.dv[2] * kernel.dv[2]; 
                  count++;
                }
           }
            if(count !=0){
 
                kernel.dv_vari /= count;
                                }else{
                kernel.dv_vari == 0;
                                     }
                kernel.dv_vari = sqrt(kernel.dv_vari); /*ダークマターとガスの相対速度の速度分散。ターゲットとしているガス粒子のにおいてこの速度は一定であるとする。*/
 
            double sigma_alldm_proton = CROSS_SECTION_XP * pow(kernel.dv_vari / REF_VELOCITY_XP, P_XP-4);/*ダークマターとプロトンの２粒子における散乱断面積*/
            
            for(n = 0; n < numngb_inbox; n++)
            {
                j = ngblist[n];
                if(P[j].Mass <= 0) continue;
                
                kernel.dp[0] = local.Pos[0] - P[j].Pos[0];
                kernel.dp[1] = local.Pos[1] - P[j].Pos[1];
                kernel.dp[2] = local.Pos[2] - P[j].Pos[2];
#ifdef PERIODIC
                NEAREST_XYZ(kernel.dp[0],kernel.dp[1],kernel.dp[2],1);
#endif
                r2 = kernel.dp[0] * kernel.dp[0] + kernel.dp[1] * kernel.dp[1] + kernel.dp[2] * kernel.dp[2];
                
                if(r2 < h2)
                {
                    kernel.r = sqrt(r2);
                    u = kernel.r * kernel.hinv;
                    kernel_main(u, kernel.hinv3, kernel.hinv4, &kernel.wk, &kernel.dwk, 0);
                    N_j = P[j].Mass * All.UnitMass_in_g / DARKMATTERMASS;/*ダークマター粒子の個数*/
                    N_i = P[target].Mass * All.UnitMass_in_g / PROTONMASS;/*プロトンの個数*/
                    mass_j = P[j].Mass;
                    kernel.dm_mj_wk = FLT(mass_j * kernel.wk);

                    out.dm_Ngb += kernel.wk;
                    out.dm_Rho += kernel.dm_mj_wk;
                    out.dm_coll +=  N_i * (kernel.wk * All.cf_a3inv) * (kernel.dv_vari * All.UnitVelocity_in_cm_per_s) * sigma_alldm_proton * All.UnitTime_in_s / pow(All.UnitLength_in_cm, 3) / All.HubbleParam;
                    
                    
/*#if defined(RT_SOURCE_INJECTION)
                    if((1 << local.Type) & (RT_SOURCES)) {out.KernelSum_Around_RT_Source += 1.-u*u;}
#endif*/
                    out.dm_DhsmlNgb += -(NUMDIMS * kernel.hinv * kernel.wk + u * kernel.dwk);
#ifdef HYDRO_SPH
                    double mass_eff = mass_j;
/*#ifdef SPHEQ_DENSITY_INDEPENDENT_SPH
                    mass_eff *= SphP[j].InternalEnergyPred;
                    out.EgyRho += kernel.wk * mass_eff;
#endif*/
                    out.dm_DhsmlHydroSumFactor += -mass_eff * (NUMDIMS * kernel.hinv * kernel.wk + u * kernel.dwk);
#endif
                    
#if defined(ADAPTIVE_GRAVSOFT_FORALL) || defined(ADAPTIVE_GRAVSOFT_FORGAS)
                    if(local.Type == 0) {out.dm_AGS_zeta += mass_j * kernel_gravity(u, kernel.hinv, kernel.hinv3, 0);}
#endif
                    /* for everything below, we do NOT include the particle self-contribution! */
                    if(kernel.r > 0)
                    {
                        if(local.Type == 0)
                        {
                            wk = kernel.wk; /* MAKE SURE THIS MATCHES CHOICE IN GRADIENTS.c!!! */
                            /* the weights for the MLS tensor used for gradient estimation */
                            out.dm_NV_T[0][0] +=  wk * kernel.dp[0] * kernel.dp[0];
                            out.dm_NV_T[0][1] +=  wk * kernel.dp[0] * kernel.dp[1];
                            out.dm_NV_T[0][2] +=  wk * kernel.dp[0] * kernel.dp[2];
                            out.dm_NV_T[1][1] +=  wk * kernel.dp[1] * kernel.dp[1];
                            out.dm_NV_T[1][2] +=  wk * kernel.dp[1] * kernel.dp[2];
                            out.dm_NV_T[2][2] +=  wk * kernel.dp[2] * kernel.dp[2];
                        }
                        
                        
                    
#ifdef SHEARING_BOX
                        if(local.Pos[0] - P[j].Pos[0] > +boxHalf_X) {kernel.dv[SHEARING_BOX_PHI_COORDINATE] += Shearing_Box_Vel_Offset;}
                        if(local.Pos[0] - P[j].Pos[0] < -boxHalf_X) {kernel.dv[SHEARING_BOX_PHI_COORDINATE] -= Shearing_Box_Vel_Offset;}
#endif
                        out.dm_Particle_DivVel -= kernel.dwk * (kernel.dp[0] * kernel.dv[0] + kernel.dp[1] * kernel.dv[1] + kernel.dp[2] * kernel.dv[2]) / kernel.r;
                        /* this is the -particle- divv estimator, which determines how Hsml will evolve (particle drift) */
                        
                        dm_density_evaluate_extra_physics_gas(&local, &out, &kernel, j);
                    } // kernel.r > 0 //
                }
            }
                
            
        }
        
        if(mode == 1)
        {
            listindex++;
            if(listindex < NODELISTLENGTH)
            {
                startnode = dm_DensDataGet[target].NodeList[listindex];
                if(startnode >= 0)
                    startnode = Nodes[startnode].u.d.nextnode;	/* open it */
            }
        }
    }
    
    if(mode == 0)
        out2particle_dm_density(&out, target, 0);
    else
        dm_DensDataResult[target] = out;
   
    return 0;
}



void *dm_density_evaluate_primary(void *p)
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
        
        if(dm_density_isactive(i))
        {
            if(dm_density_evaluate(i, 0, exportflag, exportnodecount, exportindex, ngblist) < 0)
                break;		/* export buffer has filled up */
        }
        ProcessedFlag[i] = 1;	/* particle successfully finished */
    }
    return NULL;
}



void *dm_density_evaluate_secondary(void *p)
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
        
        dm_density_evaluate(j, 1, &dummy, &dummy, &dummy, ngblist);
    }
    
    return NULL;
    
}




int dm_density_isactive(int n)
{
    /* first check our 'marker' for particles which have finished iterating to an Hsml solution (if they have, dont do them again) */
    if(P[n].TimeBin < 0) return 0;
    
/*#if defined(GRAIN_FLUID)*/
    /* all particles can potentially interact with the gas in this mode, if drag > 0 */
/*    if(P[n].Type >= 0) return 1;
#endif*/
    
/*#if defined(RT_SOURCE_INJECTION)
    if((1 << P[n].Type) & (RT_SOURCES)) 
    {
        if(Flag_FullStep) {return 1;} // only do on full timesteps
    }
#endif
    
#ifdef DO_DENSITY_AROUND_STAR_PARTICLES
    if(((P[n].Type == 4)||((All.ComovingIntegrationOn==0)&&((P[n].Type == 2)||(P[n].Type==3))))&&(P[n].Mass>0))
    {
    }
#endif*/
    
    
    if(P[n].Type == 0 && P[n].Mass > 0) return 1;
    return 0;
}





void dm_density_evaluate_extra_physics_gas(struct dm_densdata_in *local, struct dm_densdata_out *out,
                                        struct dm_kernel_density *kernel, int j)
{
    kernel->mj_dwk_r = P[j].Mass * kernel->dwk / kernel->r;
    
    
    if(local->Type != 0)
    {
        
/*#if defined(GRAIN_FLUID)
        out->Gas_InternalEnergy += kernel->mj_wk * SphP[j].InternalEnergyPred;
        out->GasVel[0] += kernel->mj_wk * (local->Vel[0]-kernel->dv[0]);
        out->GasVel[1] += kernel->mj_wk * (local->Vel[1]-kernel->dv[1]);
        out->GasVel[2] += kernel->mj_wk * (local->Vel[2]-kernel->dv[2]);
#ifdef GRAIN_LORENTZFORCE
        out->Gas_B[0] += kernel->wk * SphP[j].BPred[0];
        out->Gas_B[1] += kernel->wk * SphP[j].BPred[1];
        out->Gas_B[2] += kernel->wk * SphP[j].BPred[2];
#endif
#endif*/
        
        
#ifdef DO_DENSITY_AROUND_STAR_PARTICLES
        /* this is here because for the models of BH growth and self-shielding of stars, we
         just need a quick-and-dirty, single-pass approximation for the gradients (the error from
         using this as opposed to the higher-order gradient estimators is small compared to the
         Sobolev approximation): use only for -non-gas- particles */
        out->GradRho[0] += kernel->mj_dwk_r * kernel->dp[0];
        out->GradRho[1] += kernel->mj_dwk_r * kernel->dp[1];
        out->GradRho[2] += kernel->mj_dwk_r * kernel->dp[2];
#endif
        
    } else { /* local.Type == 0 */
        
        
/*#if defined(SPHAV_CD10_VISCOSITY_SWITCH)
        double wk = kernel->wk;
        out->NV_A[0][0] += (local->Accel[0] - All.cf_a2inv*P[j].GravAccel[0] - SphP[j].HydroAccel[0]) * kernel->dp[0] * wk;
        out->NV_A[0][1] += (local->Accel[0] - All.cf_a2inv*P[j].GravAccel[0] - SphP[j].HydroAccel[0]) * kernel->dp[1] * wk;
        out->NV_A[0][2] += (local->Accel[0] - All.cf_a2inv*P[j].GravAccel[0] - SphP[j].HydroAccel[0]) * kernel->dp[2] * wk;
        out->NV_A[1][0] += (local->Accel[1] - All.cf_a2inv*P[j].GravAccel[1] - SphP[j].HydroAccel[1]) * kernel->dp[0] * wk;
        out->NV_A[1][1] += (local->Accel[1] - All.cf_a2inv*P[j].GravAccel[1] - SphP[j].HydroAccel[1]) * kernel->dp[1] * wk;
        out->NV_A[1][2] += (local->Accel[1] - All.cf_a2inv*P[j].GravAccel[1] - SphP[j].HydroAccel[1]) * kernel->dp[2] * wk;
        out->NV_A[2][0] += (local->Accel[2] - All.cf_a2inv*P[j].GravAccel[2] - SphP[j].HydroAccel[2]) * kernel->dp[0] * wk;
        out->NV_A[2][1] += (local->Accel[2] - All.cf_a2inv*P[j].GravAccel[2] - SphP[j].HydroAccel[2]) * kernel->dp[1] * wk;
        out->NV_A[2][2] += (local->Accel[2] - All.cf_a2inv*P[j].GravAccel[2] - SphP[j].HydroAccel[2]) * kernel->dp[2] * wk;
        
        out->NV_D[0][0] += kernel->dv[0] * kernel->dp[0] * wk;
        out->NV_D[0][1] += kernel->dv[0] * kernel->dp[1] * wk;
        out->NV_D[0][2] += kernel->dv[0] * kernel->dp[2] * wk;
        out->NV_D[1][0] += kernel->dv[1] * kernel->dp[0] * wk;
        out->NV_D[1][1] += kernel->dv[1] * kernel->dp[1] * wk;
        out->NV_D[1][2] += kernel->dv[1] * kernel->dp[2] * wk;
        out->NV_D[2][0] += kernel->dv[2] * kernel->dp[0] * wk;
        out->NV_D[2][1] += kernel->dv[2] * kernel->dp[1] * wk;
        out->NV_D[2][2] += kernel->dv[2] * kernel->dp[2] * wk;
#endif*/
        
    } // Type = 0 check
}
#endif



