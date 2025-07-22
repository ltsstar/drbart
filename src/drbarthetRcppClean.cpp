#include <Rcpp.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <ctime>
#include <algorithm>

#include "read.h"
#include "rng.h"
#include "tree.h"
#include "info.h"
#include "funs.h"
#include "bd.h"
#include "slice.h"

#include <chrono>

using namespace Rcpp;

void draw_new_trees(
  std::vector<tree>& t,
  xinfo& xi,
  dinfo& di,
  double* allfit,
  double* r,
  double* ftemp,
  size_t m,
  pinfo& pi,
  RNG& gen,
  std::vector<tree>& tprec,
  xinfo& xiprec,
  dinfo& diprec,
  double* allfitprec,
  double* rprec,
  double* ftempprec,
  size_t mprec,
  size_t n,
  std::vector<double>& y,
  double phi0,
  double phistar,
  pinfo& piprec
);

void new_u_vals(
  size_t i,
  size_t burn,
  size_t thin,
  size_t n,
  size_t p,
  std::vector<tree>& using_u,
  std::vector<std::vector<int> >& leaf_counts,
  std::vector<tree>& using_uprec,
  std::vector<std::vector<int> >& leaf_countsprec,
  std::vector<double>& x,
  std::vector<double>& xprec,
  double* allfit,
  double* allfitprec,
  dinfo& di,
  dinfo& diprec,
  xinfo& xi,
  xinfo& xiprec,
  bool SCALE_MIX,
  NumericMatrix& uvals,
  std::vector<double>& y,
  ld_bartU& slice_density,
  std::vector<std::vector<double> >& ucuts_post,
  size_t m,
  std::ofstream& treef,
  std::vector<tree>& t,
  size_t mprec,
  std::ofstream& treefprec,
  std::vector<tree>& tprec,
  NumericVector& ssigma,
  double phistar,
  IntegerVector& trunc_below,
  NumericVector& y_,
  tree::npv& bnv,
  tree::npv& bnvprec,
  std::vector<tree::npv>& bnvs,
  std::vector<tree::npv>& bnvsprec
);

// [[Rcpp::export]]
List drbartRcppHeteroClean(NumericVector y_, 
              NumericVector x_, 
              NumericVector xprec_, 
              List xinfo_list,
              List xinfo_prec_list, 
              int burn, int nd, int thin, int printevery,
              int m, int mprec, double alpha, double beta,
              double nu, double kfac,
              double phi0, 
              bool scalemix,
              IntegerVector trunc_below,
              CharacterVector treef_name_,
              CharacterVector treef_prec_name_)
{
  
  bool SCALE_MIX = scalemix;
  
  std::string treef_name = as<std::string>(treef_name_); 
  std::ofstream treef(treef_name.c_str());
  
  //begin hetero
  treef_name = as<std::string>(treef_prec_name_); 
  std::ofstream treefprec(treef_name.c_str());  
  //end hetero
  
  RNGScope scope;  
  RNG gen; //this one random number generator is used in all draws
  
  /*****************************************************************************
   Read, format y
  *****************************************************************************/
  std::vector<double> y; //storage for y
  double miny = INFINITY, maxy = -INFINITY;
  sinfo allys;       //sufficient stats for all of y, use to initialize the bart trees.
  
  for (NumericVector::iterator it = y_.begin(); it != y_.end(); ++it) {
    y.push_back(*it);
    if (*it < miny) {
      miny = *it;
    }
    if (*it > maxy) {
      maxy = *it;
    }

    allys.sy += *it; // sum of y
    allys.sy2 += (*it) * (*it); // sum of y^2
  }

  size_t n = y.size();
  allys.n = n;
  
  double ybar = allys.sy / n; //sample mean
  double shat = sqrt((allys.sy2 - n * ybar * ybar) / (n - 1)); //sample standard deviation
  
  /*****************************************************************************
   Read, format X, Xpred
  *****************************************************************************/
  //read x   
  //the n*p numbers for x are stored as the p for first obs, then p for second, and so on.
  std::vector<double> x = load_x(x_);
  size_t p = x.size() / n;
  
  std::vector<double> xprec = load_x(xprec_);
  size_t pprec = xprec.size() / n;
  
  // cutpoints
  xinfo xi = load_cutpoints(xinfo_list, p);
  xinfo xiprec = load_cutpoints(xinfo_prec_list, pprec);
    
  /*****************************************************************************
   Setup for MCMC
  *****************************************************************************/
  
  //trees
  
  std::vector<tree> t(m);
  for (size_t i = 0;i < m; i++) {
    t[i].setm(ybar / m); //if you sum the fit over the trees you get the fit.
  }
  
  std::vector<tree> tprec(mprec);
  double tleaf = 1.0;//pow(phi0, 1.0/mprec);
  for (size_t i= 0 ; i < mprec; i++) {
    tprec[i].setm(tleaf); //if you sum the fit over the trees you get the fit.
  }

  // n x m matrix for fits
  NumericMatrix prec_fits(n, m);
  prec_fits.fill(tleaf);
  
  double phistar = phi0;
  
  //--------------------------------------------------
  // prior and mcmc
  // maybe introduce pb/pbd probs as defaults
  // maybe pimean and piprec structs derived from a pinfo struct 
  pinfo pi(1.0, 0.5, alpha, beta, miny, maxy, kfac, m, shat); 
  pinfo piprec(1.0, 0.5, alpha, beta, nu * mprec, 0.0); // phi_m ~ G(tau, tau)
  //--------------------------------------------------
  
  // dinfo
  double* allfit = new double[n]; //sum of fit of all trees
  for (size_t i = 0; i < n; i++) {
    allfit[i] = ybar;
  }
  double* r = new double[n]; //y-(allfit-ftemp) = y-allfit+ftemp
  double* ftemp = new double[n]; //fit of current tree
  dinfo di;
  di.n = n;
  di.p = p;
  // di.y = r; //the y for each draw will be the residual
  
  // dinfo di(n, p, x);
  di.x = &x[0];
  di.y = r;
  
  //--------------------------------------------------
  // dinfo for precision
  double* allfitprec = new double[n]; //sum of fit of all trees
  for (size_t i = 0; i < n; i++) {
    allfitprec[i] = phi0; //phi0 is an "offset"
  }
  double* rprec = new double[n]; // scaled residual
  double* global_rprec = new double[n*m];
  double* ftempprec = new double[n]; //fit of current tree
  dinfo diprec;
  diprec.n = n;
  diprec.p = pprec;
  diprec.x = &xprec[0];
  diprec.y = rprec; //the y for each draw will be the residual
  //end hetero
  // dinfo diprec(n, pprec, xprec); 
  
  NumericVector ssigma(nd);
  
  //save stuff to tree file
  treef << xi << endl; //cutpoints
  treef << m << endl;  //number of trees
  treef << p << endl;  //dimension of x's
	treef << nd << endl;
  
  //begin hetero
  //save stuff to tree file
  treefprec << xiprec << endl; //cutpoints
  treefprec << mprec << endl;  //number of trees
	treefprec << pprec << endl;  //dimension of x's
	treefprec << nd << endl;
  //end hetero

  int niters = nd * thin + burn; 
  
  /*****************************************************************************
   MCMC
  *****************************************************************************/
  //begin dr bart
  
  tree::npv bnv;
  std::vector<tree::npv> bnvs;
  std::vector<std::vector<int> > leaf_counts(m);
  std::vector<double> lik(xi[0].size());
  std::vector<tree> using_u;
  std::vector<std::vector<double> > ucuts_post(nd);
  
  tree::npv bnvprec;
  std::vector<tree::npv> bnvsprec;
  std::vector<std::vector<int> > leaf_countsprec(mprec);
  //std::vector<double> lik(xiprec[0].size());
  std::vector<tree> using_uprec;
  std::vector<std::vector<double> > ucuts_prec_post(nd);

	NumericMatrix uvals(nd, n); 
  
  ld_bartU slice_density(0.0, 1.0);
  slice_density.xi = xi;
  slice_density.di = di;
  slice_density.i = 0;
  slice_density.using_u = using_u;

  slice_density.scalemix = SCALE_MIX;

  slice_density.xiprec = xiprec;
  slice_density.diprec = diprec;
  slice_density.using_uprec = using_uprec;
  
  // ld_bartU slice_density(0.0, 1.0, SCALE_MIX);
  //end dr bart
  
  for (size_t i = 0; i < niters; i++) {
    if (i % printevery == 0) {
      Rcout << "Iteration " << i << " / " << niters << 
        " (" << (int) 100 * i / niters << "%)\n";
    }

    //double sum_r = 0.0;
    //for (size_t j = 0; j < n; j++) {
    //  sum_r += r[j]*r[j];
    //}
    //Rcout << "residuals2: " << sum_r << endl;

    auto start = std::chrono::high_resolution_clock::now();
    draw_new_trees(
      t, xi, di, allfit, r, ftemp, m, pi, gen,
      tprec, xiprec, diprec, allfitprec, rprec, ftempprec, mprec, n,
      y, phi0, phistar, piprec
    );
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Execution time draw_new_trees: " << duration.count() << " milliseconds" << std::endl;

//    This is how rg workds (nx is a bot)
//    int L,U;
//    L=0; U = xi[v].size()-1;
//    nx->rg(v,&L,&U);
//    size_t c = L + floor(gen.uniform()*(U-L+1)); //U-L+1 is number of available split points
    
  start = std::chrono::high_resolution_clock::now();
    new_u_vals(
      i, burn, thin, n, p, using_u, leaf_counts, using_uprec, leaf_countsprec,
      x, xprec, allfit, allfitprec, di, diprec, xi, xiprec,
      SCALE_MIX, uvals, y, slice_density, ucuts_post,
      m, treef, t, mprec, treefprec, tprec,
      ssigma, phistar, trunc_below,
      y_, bnv, bnvprec, bnvs, bnvsprec
    );
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Execution time new_u_vals: " << duration.count() << " milliseconds" << std::endl;

    static const double log_sqrt_2pi = 0.9189385332046727; // 0.5*log(2*pi)

    double unnorm_loglikelihood_sum = 0.0;
    for (size_t j = 0; j < n; j++) {
      double log_prec = std::log(allfitprec[j]);
      unnorm_loglikelihood_sum += -log_sqrt_2pi + 0.5 * log_prec - 0.5 * rprec[j] * rprec[j];
    }
    Rcout << "Log-likelihood (unnormalized): " << unnorm_loglikelihood_sum << endl;

    // calculate current likelihood
    // real slow, we stick to the unnormalized likelihood for now
    /*
    double lik = 0.0;
    for (size_t j = 0; j < n; j++)
    {
      double original_u = x[j * p];
      double* x_ptr = &x[j * p]; 

      double mixture_likelihood = 0.0;
      std::vector<double> log_p(xi[0].size()+1);
      double usplit;
      for (int uu = -1; uu < static_cast<int>(xi[0].size()); ++uu) {
        if (uu == -1) {
          usplit = xi[0][0] / 2;
        } else if (uu == xi[0].size() - 1) {
          usplit = xi[0][uu] + (1 - xi[0][uu]) / 2;
        } else {
          usplit = xi[0][uu] + (xi[0][uu + 1] - xi[0][uu]) / 2;
        }
        x[j*p] = usplit;
        double mu = 0.0;
        for (size_t k = 0; k < m; ++k) {
          tree::tree_cp tbn = t[k].bn(x_ptr,xi);
          mu += tbn->getm();
        }

        double var = 1.0;
        for (size_t k = 0; k < mprec; ++k) {
          tree::tree_cp tbn = tprec[k].bn(x_ptr, xiprec);
          var *= tbn->getm();
        }

        double prec = 1 / sqrt(var);
        double diff = y[j] - mu;
        log_p[uu + 1] = -log_sqrt_2pi - std::log(prec) - 0.5 * (diff * diff) / (prec * prec);        

        //double ll = R::dnorm(y[j], mu, prec, 1);

        //mixture_likelihood += ll;
      }
        // Log-sum-exp trick
      double max_log = *std::max_element(log_p.begin(), log_p.end());
      double sum_exp = 0.0;
      for (int k = 0; k < log_p.size(); ++k) {
        sum_exp += std::exp(log_p[k] - max_log);
      }

      double log_mix = max_log + std::log(sum_exp) - std::log(log_p.size());
      lik += log_mix;
      x[j * p] = original_u;
    }
    Rcout << "Current Log-likelihood: " << lik << endl;
    */
  }
  t.clear();
  delete[] allfit;
  delete[] r;
  delete[] ftemp;
  
  treef.close();
  
  return(List::create(_["phistar"] = ssigma,
                      _["ucuts"] = ucuts_post,
											_["uvals"] = uvals));
}

void new_u_vals(
  size_t i,
  size_t burn,
  size_t thin,
  size_t n,
  size_t p,
  std::vector<tree>& using_u,
  std::vector<std::vector<int> >& leaf_counts,
  std::vector<tree>& using_uprec,
  std::vector<std::vector<int> >& leaf_countsprec,
  std::vector<double>& x,
  std::vector<double>& xprec,
  double* allfit,
  double* allfitprec,
  dinfo& di,
  dinfo& diprec,
  xinfo& xi,
  xinfo& xiprec,
  bool SCALE_MIX,
  NumericMatrix& uvals,
  std::vector<double>& y,
  ld_bartU& slice_density,
  std::vector<std::vector<double> >& ucuts_post,
  size_t m,
  std::ofstream& treef,
  std::vector<tree>& t,
  size_t mprec,
  std::ofstream& treefprec,
  std::vector<tree>& tprec,
  NumericVector& ssigma,
  double phistar,
  IntegerVector& trunc_below,
  NumericVector& y_,
  tree::npv& bnv,
  tree::npv& bnvprec,
  std::vector<tree::npv>& bnvs,
  std::vector<tree::npv>& bnvsprec
) {  
    double max_prec = 1e10;
    //begin dr bart
    
    // impute censored values
    for (size_t k = 0; k < n; ++k) {
      if (trunc_below[k] > 0) {
        y[k] = rtnormlo(allfit[k], 1.0 / sqrt(allfitprec[k]), y_[k]);// original y_ is obs value
      }
    }
    
    /*** sample u ***/
    using_u.clear();
    leaf_counts.clear();
    bnvs.clear();
    vector<std::map<tree::tree_cp,size_t> > bnmaps;
    std::set<size_t> ucuts; ucuts.insert(0); ucuts.insert(xi[0].size() - 1);
    std::vector<size_t> using_u_ix, using_u_ix_prec;
    
    if (SCALE_MIX) {
      using_uprec.clear();
      leaf_countsprec.clear();
      bnvsprec.clear();
    }
    vector<std::map<tree::tree_cp,size_t> > bnmapsprec;
    
    //get trees splitting on u, the first variable
    int tsu = 0;
    for (size_t tt = 0; tt< m ; ++tt) {
      if (t[tt].nuse(0)) {
        using_u.push_back(t[tt]);
        using_u_ix.push_back(tt);
        tsu++;
      }
    }
    Rcout << "Number of mean trees splitting on u: " << tsu << endl;
    
    tsu = 0;
    if (SCALE_MIX) {
      for (size_t tt = 0; tt < mprec; ++tt) {
        if (tprec[tt].nuse(0)) {
          using_uprec.push_back(tprec[tt]);
          using_u_ix_prec.push_back(tt);
          tsu++;
        }
      }
    }
    Rcout << "Number of var. trees splitting on u: " << tsu << endl;

    //update slice_density object
    slice_density.using_u = using_u;
    if (SCALE_MIX) {
      slice_density.using_uprec = using_uprec;
    }
    
    //get leaf counts for each tree splitting on u & also get partition of u
    for (size_t tt = 0; tt < using_u.size(); ++tt) {
      leaf_counts.push_back(counts(using_u[tt], xi, di, bnv)); //clears & populates bnv
      bnvs.push_back(bnv);
      using_u[tt].varsplits(ucuts, 0);
    }
    
    if (SCALE_MIX) {
      for (size_t tt = 0; tt < using_uprec.size(); ++tt) {
        leaf_countsprec.push_back(counts(using_uprec[tt], xiprec, diprec, bnvprec)); //clears & populates bnv
        bnvsprec.push_back(bnvprec);
        using_uprec[tt].varsplits(ucuts, 0);
      }
    }
    std::vector<size_t> ucutsv(ucuts.begin(), ucuts.end());
    std::vector<double> logpr(ucuts.size() - 1);

    //prebuild ix->bottom node maps for each tree splitting on u, big time saver.
    typedef tree::npv::size_type bvsz;
    for (size_t tt = 0; tt < using_u.size(); ++tt) {
      std::map<tree::tree_cp,size_t> bnmap;
      for (bvsz ii = 0;ii != bnvs[tt].size(); ii++) {
        bnmap[bnvs[tt][ii]] = ii; 
      }
      bnmaps.push_back(bnmap);
    }
    
    if (SCALE_MIX) {
      for (size_t tt = 0; tt < using_uprec.size(); ++tt) {
        std::map<tree::tree_cp,size_t> bnmap;
        for (bvsz ii = 0; ii != bnvsprec[tt].size(); ii++) {
          bnmap[bnvsprec[tt][ii]] = ii; 
        }
        bnmapsprec.push_back(bnmap);
      }
    }

      //loop over each observation
    std::vector<int> tmpcounts;
    std::vector<int> tmpcountsprec;
    std::vector<std::vector<int> > new_counts(using_u.size());
    size_t jj = 0; //<-- single latent variable for now.
  for (size_t k = 0; k < n; k++) {
    bool proceed = true;

    int L, U;
    L = 0;
    U = xi[0].size() - 1;

    //check that removing u won't result in bottom nodes 
    for (size_t tt = 0; tt < using_u.size(); ++tt) {
      tmpcounts = leaf_counts[tt];
      update_counts(k, tmpcounts, using_u[tt], xi, di, bnmaps[tt], -1); 
      new_counts[tt] = tmpcounts;
      if (*std::min_element(tmpcounts.begin(), tmpcounts.end()) < 5) {
        proceed = false;
        break;
      }
    }

    if (SCALE_MIX) {
      for (size_t tt = 0; tt < using_uprec.size(); ++tt) {
        tmpcountsprec = leaf_countsprec[tt];
        update_counts(k, tmpcountsprec, using_uprec[tt], xiprec, diprec, bnmapsprec[tt], -1); 
        if (*std::min_element(tmpcountsprec.begin(), tmpcountsprec.end()) < 5) {
          proceed = false;
          break;
        }
      }
    }

    //resample u
    if (proceed) {
      leaf_counts = new_counts;

      if (SCALE_MIX) {
        for (size_t tt = 0; tt < using_uprec.size(); ++tt) {
          update_counts(k, leaf_countsprec[tt], using_uprec[tt], xiprec, diprec, bnmapsprec[tt], -1);
        }
      }

      double f = allfit[k] - fit_i(k, using_u, xi, di); //fit from trees that don't use u
      double s;
      double fprec;
      if (SCALE_MIX) {
        fprec = allfitprec[k] / fit_i_mult(k, using_uprec, xiprec, diprec);
        s = 1 / sqrt(fprec);
      } else {
        s = 1 / sqrt(allfitprec[k]);
      }

      slice_density.sigma = s;
      slice_density.i = k;
      slice_density.f = f;
      slice_density.yobs = y[k];
      double oldu = x[jj + k * p];
      double newu = slice(oldu, &slice_density, 1.0, INFINITY, 0., 1.);
      x[jj + k * p] = newu;

      if (SCALE_MIX) {
        xprec[jj + k * p] = newu;
      }

      //update counts with new u
      for (size_t tt = 0; tt < using_u.size(); ++tt) {
        update_counts(k, leaf_counts[tt], using_u[tt], xi, di, bnmaps[tt], 1);
      }
      // add back the fit from trees splitting on u
      allfit[k] = f + fit_i(k, using_u, xi, di);

      if (SCALE_MIX) {
        //update counts with new u
        for (size_t tt = 0; tt < using_uprec.size(); ++tt) {
          update_counts(k, leaf_countsprec[tt], using_uprec[tt], xiprec, diprec, bnmapsprec[tt], 1);
        }
        // add back the fit from trees splitting on u
        double new_fitprec = fprec * fit_i_mult(k, using_uprec, xiprec, diprec);
        allfitprec[k] = std::min(max_prec, new_fitprec);
      }
    }
    if (i >= burn && i % thin == 0) {
      uvals((i - burn) / thin, k) = x[jj + k * p];
    }
  }
  //end dr bart
    if (i >= burn & i % thin == 0) {
// 			for (size_t k = 0; k < n; k++) {
// 				uvals((i - burn) / thin) = x[jj + k * p];
//			}
      for (size_t uu = 0; uu < ucutsv.size(); ++uu) {
        ucuts_post[(i - burn) / thin].push_back(xi[jj][ucutsv[uu]]);
      }
      for (size_t j = 0; j < m;j ++) {
        treef << t[j] << endl;
      }
      for (size_t j = 0; j < mprec; j++) {
        treefprec << tprec[j] << endl;
      }
      
      ssigma((i - burn) / thin) = phistar;
    }
}

void draw_new_trees(
  std::vector<tree>& t,
  xinfo& xi,
  dinfo& di,
  double* allfit,
  double* r,
  double* ftemp,
  size_t m,
  pinfo& pi,
  RNG& gen,
  std::vector<tree>& tprec,
  xinfo& xiprec,
  dinfo& diprec,
  double* allfitprec,
  double* rprec,
  double* ftempprec,
  size_t mprec,
  size_t n,
  std::vector<double>& y,
  double phi0,
  double phistar,
  pinfo& piprec
) {

    //draw trees
    int birth_count = 0; //count of births
    int death_count = 0; //count of deaths
    int birth_count_prec = 0; //count of births for precision trees
    int death_count_prec = 0; //count of deaths for precision trees

    int birth_accept = 0;
    int death_accept = 0;
    int birth_accept_prec = 0; //accept/reject count for precision trees
    int death_accept_prec = 0; //accept/reject count for precision trees

    for (size_t j = 0; j < m; j++) {
       fit(t[j] ,xi, di, ftemp);
       for (size_t k=0;k<n;k++) {
          allfit[k] = allfit[k] - ftemp[k];
          r[k] = (y[k] - allfit[k]);
          // di.y[k] = y[k] - allfit[k]; 
       }
      auto bdhet_result = bdhet(t[j], xi, di, allfitprec, pi, gen);
      auto [birth_death, accept_reject] = bdhet_result;

      if (birth_death) {
        birth_count++;
        if (accept_reject) {
          birth_accept++;
        }
      } else {
        death_count++;
        if (accept_reject) {
          death_accept++;
        }
      }

       drmuhet(t[j], xi, di, allfitprec, pi, gen);
       fit(t[j], xi, di, ftemp);
       for (size_t k = 0; k < n; k++) { 
         allfit[k] += ftemp[k];
       }
    }
    for (size_t k = 0; k < n; k++) {
      
    }

    
    phistar = phi0;
    //end hetero
    
     //begin hetero
    for (size_t j = 0; j < mprec; j++) {
       fit(tprec[j], xiprec, diprec, ftempprec);
       for (size_t k = 0; k < n; k++) {
          if (ftempprec[k] != ftempprec[k]) {
            Rcout << "tree " << j <<" obs "<< k<<" "<< endl;
            Rcout << tprec[j] << endl;
            stop("nan in ftemp");
           }
          if(ftempprec[k] <= 0) {
	          Rcout << "ftempprec <= 0: " << ftempprec[k] << endl;
	        }
          allfitprec[k] = allfitprec[k] / ftempprec[k];
          rprec[k] = (y[k] - allfit[k]) * sqrt(allfitprec[k]);
          // diprec.y[k] = (y[k] - allfit[k]) * sqrt(allfitprec[k]);
       }
      auto bdprec_result = bdprec(tprec[j], xiprec, diprec, piprec, gen); 
      auto [birth_death_prec, accept_reject_prec] = bdprec_result;

      if (birth_death_prec) {
        birth_count_prec++;
        if (accept_reject_prec) {
          birth_accept_prec++;
        }
      } else {
        death_count_prec++;
        if (accept_reject_prec) {
          death_accept_prec++;
        }
      }

       drphi(tprec[j], xiprec, diprec, piprec, gen);
       fit(tprec[j], xiprec, diprec, ftempprec);
       for (size_t k = 0; k < n; k++) {
        allfitprec[k] *= ftempprec[k];
      }
    }
    //end hetero

    Rcout << "Births: " << birth_count << ", Deaths: " << death_count 
          << ", Birth Accepts: " << birth_accept << ", Death Accepts: " << death_accept << endl;
    Rcout << "Precision Births: " << birth_count_prec << ", Deaths: " << death_count_prec 
          << ", Birth Accepts: " << birth_accept_prec << ", Death Accepts: " << death_accept_prec << endl;
}