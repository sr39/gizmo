#ifndef KERNEL_H
#define KERNEL_H

/* fall back to standard kernel */
#if !defined(KERNEL_QUINTIC)
#define STANDARD_KERNEL   
#endif

/* fall back to three dimensions */
#if !defined(TWODIMS) && !defined(ONEDIM)
#define THREEDIM
#endif

/* Norms */
#ifdef STANDARD_KERNEL
#ifdef THREEDIM
#define  NORM 8.0/M_PI                   /*!< For 3D-normalized kernel */
#endif
#ifdef TWODIMS
#define  NORM 40.0/(7.0*M_PI)	         /*!< For 2D-normalized kernel */
#endif
#ifdef ONEDIM
#define  NORM  4.0/3.0        	         /*!< For 1D-normalized kernel */
#endif
#endif /* STANDARD_KERNEL */

#ifdef KERNEL_QUINTIC
#ifdef THREEDIM
#define  NORM 2187.0/(40.0*M_PI)	    /*!< For 3D-normalized kernel */
#endif
#ifdef TWODIMS
#define  NORM 15309.0/(478.0*M_PI)	    /*!< For 2D-normalized kernel */
#endif
#ifdef ONEDIM
#define  NORM 243.0/40.0        	    /*!< For 1D-normalized kernel */
#endif
#endif  /* KERNEL_QUINTIC */

static inline void kernel_hinv(double h, double *hinv, double *hinv3, double *hinv4)
{
  *hinv = 1.0 / h;

#ifdef THREEDIM
  *hinv3 = *hinv * *hinv * *hinv;
#endif

#ifdef TWODIMS
  *hinv3 = *hinv * *hinv;
#endif

#ifdef ONEDIM
  *hinv3 = *hinv;
#endif

  *hinv4 = *hinv3 * *hinv;

  return;
} 

/* Attention: Here we assume that kernel is only called 
   with range 0..1 for u as done in hydra or density !! 
   Call with mode 0 to calculate dwk and wk
   Call with mode -1 to calculate only wk
   Call with mode +1 to calculate only dwk */

static inline void kernel_main(double u, double hinv3, double hinv4, 
        double *wk, double *dwk, int mode)
{
#ifdef STANDARD_KERNEL /* cubic spline */
  if(u < 0.5)
    {
      if(mode >= 0) 
          *dwk = u * (18.0 * u - 12.0);
      if(mode <= 0) 
          *wk = (1.0 + 6.0 * (u - 1.0) * u * u);
    }
  else
    {
      double t1 = (1.0 - u);
      double t2 = t1 * t1;
      if(mode >= 0) 
          *dwk = -6.0 * t2;
      if(mode <= 0) 
          *wk = 2.0 * t2 * t1;
    }
#endif /* STANDARD_KERNEL */

#ifdef KERNEL_QUINTIC
  double t1 = (1.0 - u);
  double t2 = t1 * t1;
  double t4 = t2 * t2;

  if(mode >= 0) 
      *dwk = -5.0 * t4;
  if(mode <= 0) 
      *wk = t4 * t1;

  if (u < 2.0/3.0)
    {
      t1 = (2.0/3.0 - u);
      t2 = t1 * t1;
      t4 = t2 * t2;
      if(mode >= 0) 
          *dwk += 30.0 * t4;
      if(mode <= 0) 
          *wk -= 6.0 * t4 * t1;
    }
  if (u < 1.0/3.0)
    {
      t1 = (1.0/3.0 - u);
      t2 = t1 * t1;
      t4 = t2 * t2;
      if(mode >= 0) 
          *dwk -= 75.0 * t4;
      if(mode <= 0) 
          *wk += 15.0 * t4 * t1;
    }
#endif /* KERNEL_QUINTIC */

  if(mode >= 0) 
      *dwk *= NORM * hinv4;
  if(mode <= 0) 
      *wk *= NORM * hinv3;

  return;
}


/* this defines the kernel for the short-range gravitational softening, 
 which does not have to correspond to that of the gas (although for the gas
 itself, it should). dwk is the force kernel, wk is the potential kernel
  Call with mode 0 to calculate only dphi_dh (for zeta correction)
  Call with mode -1 to calculate only phi
  Call with mode +1 to calculate only dphi_du */


static inline double kernel_gravity(double u, double hinv, double hinv3, int mode)
{
    /* here everything is newtonian, add this as a check just in case */
    if(u >= 1)
    {
        if(mode ==  0) return 0;
        if(mode ==  1) return hinv3/(u*u*u);
        if(mode == -1) return -hinv/u;
    }
    
    double wk;
#ifdef STANDARD_KERNEL /* cubic spline */
    if(mode == 1)
    {
        if(u < 0.5)
            wk = (10.666666666667 + u * u * (32.0 * u - 38.4));
        else
            wk = (21.333333333333 - 48.0 * u +
                  38.4 * u * u - 10.666666666667 * u * u * u - 0.066666666667 / (u * u * u));
        return wk * hinv3;
    }
    else
    {
    if(mode == -1)
    {
        if(u < 0.5)
            wk = -2.8 + u * u * (5.333333333333 + u * u * (6.4 * u - 9.6));
        else
            wk = -3.2 + 0.066666666667 / u + u * u * (10.666666666667 +
                    u * (-16.0 + u * (9.6 - 2.133333333333 * u)));
        return wk * hinv;
    }
    else
    {
    if(mode == 0)
    {
        if(u < 0.5)
            wk = 2.8 + 16.0 * u * u * (-1.0 + 3.0 * u * u * (1.0 - 0.8 * u));
        else
            wk = 3.2 + 32.0 * u * u * (-1.0 + u * (2.0 - 1.5 * u + 0.4 * u * u));
        return wk * hinv * hinv;
    } // mode==0
    } // mode==-1 else
    } // mode==1 else
#endif /* STANDARD_KERNEL */

#ifdef KERNEL_QUINTIC
    double u2=u*u;
    if(mode == 1)
    {
        if(u < 1./3.)
            wk = -(9./280.)*(-616.+27.*u2*(112.+45.*u2*(-8.+7.*u)));
        else
        if(u < 2./3.)
            wk = (5.+27.*u2*u*(952.+9.*u*(350.+3.*u*(-784.+5.*u*(280.+9.*u*(-24.+7.*u)))))) / (1680.*u2*u);
        else
            wk = -(169.+729.*u2*u*(-56.+u*(210.+u*(-336.+u*(280.+3.*u*(-40.+7.*u)))))) / (560.*u2*u);
        return wk * hinv3;
    }
    else
    {
    if(mode == -1)
    {
        if(u < 1./3.)
            wk = (-956.-9.*u2*(-308.+27.*u2*(28.+15.*u2*(-4.+3.*u)))) / 280.;
        else
        if(u < 2./3.)
            wk = (-5.+3.*u*(-1892.+9.*u2*(476.+3.*u*(350.+9.*u*(-196.+5.*u*(56.+9.*u*(-4.+u))))))) / (1680.*u);
        else
            wk = (169.-729*u*(4.+(-2.+u)*u2*(14.+u*(-28.+u*(28.+u*(-14.+3.*u)))))) / (560.*u);
        return wk * hinv;
    }
    else
    {
    if(mode == 0)
    {
        if(u < 1./3.)
            wk = (239.+27.*u2*(-77.+45.*u2*(7.+3.*u2*(-7.+6.*u)))) / 70.;
        else
        if(u < 2./3.)
            wk = (473.-27.*u2*(119.+5.*u*(70.+9*u*(-49.+3*u*(28.+3.*u*(-7.+2.*u)))))) / 140.;
        else
        {
            double um=1.0-u; um*=um*um; um*=um;
            wk = (729./140.) * um * (1.+6.*u);
        }
        return wk * hinv * hinv;
    } // mode==0
    } // mode==-1 else
    } // mode==1 else
#endif /* QUINTIC KERNEL */

    return 0;
}


#endif /* KERNEL_H */
