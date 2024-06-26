#include <cmath>
#include "rng.h"

  //standard normal, truncated to be >lo
  double rtnormlo0(double lo) {
    double x;
    if(lo<0) {
      x = R::rnorm(0.0, 1.0);
      while(x<lo) x = R::rnorm(0.0, 1.0);
    } else {
      double a = 0.5*(lo + sqrt(lo*lo + 4.0));
      x = R::rexp(1.0/a) + lo;
      double u = R::runif(0.0, 1.0);
      double diff = (x-a);
      double r = exp(-0.5*diff*diff);
      while(u > r) {
        x = R::rexp(1.0/a) + lo;
        u = R::runif(0.0, 1.0);
        diff = (x-a);
        r = exp(-0.5*diff*diff);
      }
    }
    return x;
  }

  double rtnormlo1(double mean, double lo) {
    return mean + rtnormlo0(lo - mean);
  }
  
  double rtnormlo(double mean, double sd, double lo) {
    double lostar = (lo-mean)/sd;
    return mean + rtnormlo0(lostar)*sd;
  }

	// TO CHECK
  double rtnormhi1(double mean, double hi) {
    return -rtnormlo1(-mean, -hi);
  }

	// TO CHECK
	double rtnormhi(double mean, double sd, double hi) {
		return -rtnormlo(-mean, sd, -hi);
	}
