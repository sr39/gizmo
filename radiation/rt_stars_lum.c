#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <gsl/gsl_math.h>

#include "allvars.h"
#include "proto.h"


#ifdef RADTRANSFER
#ifdef EDDINGTON_TENSOR_STARS

static struct stardata_in
{
  MyDouble Pos[3], Mass;
  MyFloat Hsml;
  int NodeList[NODELISTLENGTH];
}
 *StarDataIn, *StarDataGet;

void star_lum(void)
{
  int j;
  int i, dummy;
  int ngrp, recvTask, place, nexport, nimport, ndone, ndone_flag;

  /* clear Je in all gas particles */

  for(j = 0; j < N_gas; j++)
    if(P[j].Type == 0)
      for(i = 0; i < N_RT_FREQ_BINS; i++)
	SphP[j].Je[i] = 0;

  /* allocate buffers to arrange communication */

  Ngblist = (int *) mymalloc("Ngblist", NumPart * sizeof(int));

  All.BunchSize =
    (int) ((All.BufferSize * 1024 * 1024) / (sizeof(struct data_index) + sizeof(struct data_nodelist) +
					     2 * sizeof(struct stardata_in)));
  DataIndexTable =
    (struct data_index *) mymalloc("DataIndexTable", All.BunchSize * sizeof(struct data_index));
  DataNodeList =
    (struct data_nodelist *) mymalloc("DataNodeList", All.BunchSize * sizeof(struct data_nodelist));

  i = FirstActiveParticle;	/* beginn with this index */

  do
    {
      for(j = 0; j < NTask; j++)
	{
	  Send_count[j] = 0;
	  Exportflag[j] = -1;
	}

      /* do local particles and prepare export list */
      for(nexport = 0; i >= 0; i = NextActiveParticle[i])
	{
	  if(P[i].Type == 4)
	    {
	      if(star_lum_evaluate(i, 0, &nexport, Send_count) < 0)
		break;
	    }
	}

#ifdef MYSORT
      mysort_dataindex(DataIndexTable, nexport, sizeof(struct data_index), data_index_compare);
#else
      qsort(DataIndexTable, nexport, sizeof(struct data_index), data_index_compare);
#endif

      MPI_Alltoall(Send_count, 1, MPI_INT, Recv_count, 1, MPI_INT, MPI_COMM_WORLD);

      for(j = 0, nimport = 0, Recv_offset[0] = 0, Send_offset[0] = 0; j < NTask; j++)
	{
	  nimport += Recv_count[j];

	  if(j > 0)
	    {
	      Send_offset[j] = Send_offset[j - 1] + Send_count[j - 1];
	      Recv_offset[j] = Recv_offset[j - 1] + Recv_count[j - 1];
	    }
	}

      StarDataGet = (struct stardata_in *) mymalloc("StarDataGet", nimport * sizeof(struct stardata_in));
      StarDataIn = (struct stardata_in *) mymalloc("StarDataIn", nexport * sizeof(struct stardata_in));

      /* prepare particle data for export */
      for(j = 0; j < nexport; j++)
	{
	  place = DataIndexTable[j].Index;

	  StarDataIn[j].Pos[0] = P[place].Pos[0];
	  StarDataIn[j].Pos[1] = P[place].Pos[1];
	  StarDataIn[j].Pos[2] = P[place].Pos[2];
	  StarDataIn[j].Hsml = PPP[place].Hsml;
	  StarDataIn[j].Mass = P[place].Mass;

	  memcpy(StarDataIn[j].NodeList,
		 DataNodeList[DataIndexTable[j].IndexGet].NodeList, NODELISTLENGTH * sizeof(int));

	}

      /* exchange particle data */
      for(ngrp = 1; ngrp < (1 << PTask); ngrp++)
	{
	  recvTask = ThisTask ^ ngrp;

	  if(recvTask < NTask)
	    {
	      if(Send_count[recvTask] > 0 || Recv_count[recvTask] > 0)
		{
		  /* get the particles */
		  MPI_Sendrecv(&StarDataIn[Send_offset[recvTask]],
			       Send_count[recvTask] * sizeof(struct stardata_in), MPI_BYTE,
			       recvTask, TAG_DENS_A,
			       &StarDataGet[Recv_offset[recvTask]],
			       Recv_count[recvTask] * sizeof(struct stardata_in), MPI_BYTE,
			       recvTask, TAG_DENS_A, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		}
	    }
	}

      myfree(StarDataIn);


      /* now do the particles that were sent to us */

      for(j = 0; j < nimport; j++)
	star_lum_evaluate(j, 1, &dummy, &dummy);

      /* check whether this is the last iteration */
      if(i < 0)
	ndone_flag = 1;
      else
	ndone_flag = 0;

      MPI_Allreduce(&ndone_flag, &ndone, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

      myfree(StarDataGet);
    }
  while(ndone < NTask);

  myfree(DataNodeList);
  myfree(DataIndexTable);
  myfree(Ngblist);
}


int star_lum_evaluate(int target, int mode, int *nexport, int *nsend_local)
{
  int i, j, n, numngb;
  int startnode, listindex = 0;
  double h, hinv, h2, hinv3;
  double wk, mass, fac;
  double dx, dy, dz, r, r2, u;
  MyDouble *pos;

  if(mode == 0)
    {
      pos = P[target].Pos;
      h = PPP[target].Hsml;
      mass = P[target].Mass;
    }
  else
    {
      pos = StarDataGet[target].Pos;
      h = StarDataGet[target].Hsml;
      mass = StarDataGet[target].Mass;
    }

  /* flag = 1 -> popIII
   flag = 0 -> popII */

  h2 = h * h;
  hinv = 1.0 / h;
  hinv3 = hinv * hinv * hinv;

  fac = mass * All.UnitMass_in_g / SOLAR_MASS;

#ifndef RT_MULTI_FREQUENCY
  lum[0] = fac * All.IonizingLumPerSolarMass * All.UnitTime_in_s / All.HubbleParam;
#endif

  if(mode == 0)
    {
      startnode = All.MaxPart;	/* root node */
    }
  else
    {
      startnode = StarDataGet[target].NodeList[0];
      startnode = Nodes[startnode].u.d.nextnode;	/* open it */
    }

  while(startnode >= 0)
    {
      while(startnode >= 0)
	{
	  numngb = ngb_treefind_variable(pos, h, target, &startnode, mode, nexport, nsend_local);

	  if(numngb < 0)
	    return -1;

	  for(n = 0; n < numngb; n++)
	    {
	      j = Ngblist[n];

	      dx = pos[0] - P[j].Pos[0];
	      dy = pos[1] - P[j].Pos[1];
	      dz = pos[2] - P[j].Pos[2];
#ifdef PERIODIC
            NEAREST_XYZ(dx,dy,dz,1);
#endif
	      r2 = dx * dx + dy * dy + dz * dz;
	      r = sqrt(r2);

	      if(r2 < h2)
		{
		  u = r * hinv;

		  if(u < 0.5)
		    wk = hinv3 * (KERNEL_COEFF_1 + KERNEL_COEFF_2 * (u - 1) * u * u);
		  else
		    wk = hinv3 * KERNEL_COEFF_5 * (1.0 - u) * (1.0 - u) * (1.0 - u);

		}
	      else
		wk = 0;

	      for(i = 0; i < N_RT_FREQ_BINS; i++)
		{
		  SphP[j].Je[i] += lum[i] * wk * fac;
		}
	    }
	}
      
      if(mode == 1)
	{
	  listindex++;
	  if(listindex < NODELISTLENGTH)
	    {
	      startnode = StarDataGet[target].NodeList[listindex];
	      if(startnode >= 0)
		startnode = Nodes[startnode].u.d.nextnode;	/* open it */
	    }
	}
    }

  return 0;
}

#endif
#endif
