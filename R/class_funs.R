Rcpp::loadModule("TreeSamples", TRUE)

#' Posterior Predictive Quantities from DR-BART
#'
#' Compute and plot conditional density functions, distribution functions,
#' quantiles, and means.
#'
#' \code{predict.drbart} returns posterior predictive draws of functionals of
#' the conditional densities and is called by \code{plot.drbart} to generate
#' appropriate plots. The functional is determined by the value of \code{type};
#' either entire density or distribution functions, selected quantiles, or means
#' may be estimated. Because quantiles and means are derived from the relevant
#' densities themselves, in all cases, prediction is a relatively time intensive
#' procedure, dependent on the number of posterior samples and on the size of
#' \code{ygrid}. For this reason, the \code{predict} method returns an object of
#' class \code{predict.drbart}, which has its own associated method:
#' \code{plot.predict.drbart}. In this way, plots can be re-generated without
#' the need to repeatedly call \code{predict.drbart}. If multiple cores are
#' available, however, the predictions can be parallelized by passing a number
#' of cores to parallelize across via \code{n_cores}. Doing so requires the
#' \code{doParallel} package and if \code{n_cores} is supplied, a parallel
#' backend is registered via \code{doParallel::registerDoParallel()} and used
#' for the parallelization. \emph{This functionality is currently untested on
#' Windows.}
#'
#' Note: estimated quantities will be inaccurate if \code{ygrid} does not fully
#' capture the high density regions of the conditional densities.
#'
#' @param object,x If calling the \code{predict} or \code{plot} methods, an
#'   object of class \code{drbart}; else, if calling the \code{predict.plot}
#'   method, an object of class \code{predict.drbart} .
#' @param xpred A matrix of points describing which conditional densities /
#'   distributions should be estimated. Rows correspond to different conditional
#'   densities and columns to different covariates.
#' @param ygrid A numeric vector of y values at which the conditional density /
#'   distribution should be evaluated.
#' @param type Type of predictions to be returned. If \code{'density'}, returns
#'   an estimate of the conditional density functions (pdfs) evaluated at points
#'   in \code{ygrid}. If \code{'distribution'}, returns an estimate of the
#'   conditional distribution functions (cdfs), evaluated at points in
#'   \code{'ygrid'}. If \code{'quantiles'}, returns the quantiles of the
#'   conditional density specified by \code{quantiles}. If \code{mean}, returns
#'   the conditional mean.
#' @param quantiles If \code{type = 'quantiles'}, the quantiles of the
#'   conditional densities that should be estimated.
#' @param n_cores Number of cores to parallelize the predictions of different
#'   conditional distributions across. If not supplied, predictions are run
#'   serially.
#' @param ... Ignored.
#' @name Methods
#' @return An object of class \code{predict.drbart}, which is a list with five
#'   elements. The first, \code{preds}, is an array containing posterior draws
#'   of the conditional densities, conditional distributions, conditional
#'   quantiles, or conditional means, depending on the value of \code{type}. The
#'   remaining elements provide information about how \code{predict.drbart} was
#'   called and are used by \code{plot.predict.drbart}. If calling \code{plot}
#'   methods, this is returned invisibly.
#' @export
predict.drbart <- function(object, xpred, ygrid,
                           type = c('density', 'distribution',
                                    'quantiles', 'mean'),
                           quantiles = c(0.025, 0.5, 0.975), n_cores, ...) {

  tmp <- preprocess_predict(object, xpred, ygrid, type, quantiles, n_cores)
  type <- tmp$type
  variance <- tmp$variance
  mean_file <- tmp$mean_file
  prec_file <- tmp$prec_file
  post_fun <- tmp$post_fun
  preds <- tmp$preds
  
  # Read in trees
  ts_mean <- TreeSamples$new()
  ts_mean$load(mean_file)
  
  if (variance != 'const') {
    ts_prec <- TreeSamples$new()
    ts_prec$load(prec_file)
  }
  
  n_unique <- apply(xpred, 2, function(col) length(unique(col)))
  non_const <- which(n_unique > 1)
  unique_vals <- xpred[, non_const]

  fit <- object$fit
  logprobs <- lapply(fit$ucuts, function(u) log(diff(c(0, u, 1))))
  mids <- lapply(fit$ucuts, function(u) c(0, u) + diff(c(0, u, 1)) / 2)
  
  if (!missing(n_cores)) {
    preds <- 
      predict_parallel(xpred, mids, fit, ts_mean, ts_prec, type, variance, quantiles, ygrid, logprobs, post_fun)
  }
  else {
    preds <- 
      predict_serial(xpred, mids, fit, ts_mean, ts_prec, type, variance, quantiles, ygrid, preds, logprobs, post_fun)

  }
  
  if (type == 'mean') {
    preds <- apply(preds, 2:3, function(all_samples) {
      apply(all_samples, 2, function(sample) {
        get_mean_from_pdf(ygrid, sample)
      })
    })
    dimnames(preds) <- list(x = xpred, NULL, sample = seq_len(dim(preds)[3]))
  }
  else if (type == 'quantiles') {
    preds <- apply(preds, 2:3, function(all_samples) {
      apply(all_samples, 2, function(sample) {
        get_q_from_cdf(quantiles, ygrid, sample)
      })
    })
    
    dimnames(preds) <- 
      list(x = xpred, quantile = quantiles, sample = seq_len(dim(preds)[3]))
  }
  else {
    #dimnames(preds) <- 
    #  list(x = xpred, y = ygrid, sample = seq_len(dim(preds)[3]))
  }

  out <- list(preds = preds,
              type = type,
              xpred = xpred,
              quantiles = quantiles,
              ygrid = ygrid)

  class(out) <- 'predict.drbart'
  return(out)
}

#' @rdname Methods
#' @export
plot.predict.drbart <-
  function(x, CI = FALSE, alpha = 0.05, legend_position = 'topleft', ...) {
  xpred <- x$xpred
  ygrid <- x$ygrid
  quantiles <- x$quantiles
  type <- x$type
  preds <- x$preds

  plot_args_out <-
    preprocess_plot_args(xpred, ygrid, type,
                         quantiles, CI, alpha, legend_position)

  colors <- plot_args_out$colors
  n_colors <- length(colors)
  estimand_size <- plot_args_out$estimand_size
  vals <- plot_args_out$vals
  xpred <- plot_args_out$xpred

  if (CI) {
    summary_preds <- array(dim = c(nrow(xpred), estimand_size, 3))
  }
  else {
    summary_preds <- array(dim = c(nrow(xpred), estimand_size, 1))
  }

  summary_preds[, , 1] <- apply(preds, 1:2, mean)
  if (CI) {
    summary_preds[, , 2] <- apply(preds, 1:2, quantile, alpha / 2)
    summary_preds[, , 3] <- apply(preds, 1:2, quantile, 1 - alpha / 2)
  }

  limits <- range(summary_preds) + c(0, 0.05)

  if (type == 'density' | type == 'distribution') {
    if (type == 'density') {
      ylab <- 'p(y|x)'
    }
    else {
      ylab <- 'P(y|x)'
    }
    for (i in seq_len(nrow(xpred))) {
      if (i == 1) {
        plot(ygrid, summary_preds[i, , 1], type = 'l', col = colors[i],
             xlab = 'y', ylab = ylab, ylim = limits)
      }
      else {
        lines(ygrid, summary_preds[i, , 1], col = colors[i])
      }
      if (CI) {
        lines(ygrid, summary_preds[i, , 2], lty = 'dashed', col = colors[i])
        lines(ygrid, summary_preds[i, , 3], lty = 'dashed', col = colors[i])
      }
    }
  }
  else if (type == 'quantiles') {
    for (i in seq_along(quantiles)) {
      if (i == 1) {
        plot(vals, summary_preds[, i, 1], pch = 19, col = colors[i],
             xlab = 'x', ylab = 'Q(y|x)', ylim = limits)
      }
      else {
        points(vals, summary_preds[, i, 1], pch = 19, col = colors[i])
      }
      if (CI) {
        # lines(vals, summary_preds[, i, 2], lty = 'dashed', col = colors[i])
        # lines(vals, summary_preds[, i, 3], lty = 'dashed', col = colors[i])
        arrows(vals, summary_preds[, i, 2], vals,
               summary_preds[, i, 3], length = 0.05,
               angle = 90, code = 3, col = colors[i])
      }
    }
  }
  else {
    plot(vals, summary_preds[, 1, 1], col = colors[1], pch = 19,
         xlab = 'x', ylab = 'E(y|x)', ylim = limits)
    if (CI) {
      arrows(vals, summary_preds[, 1, 2], vals,
             summary_preds[, 1, 3], length = 0.05,
             angle = 90, code = 3, col = colors[1])
    }
  }

  if (type == 'mean' | legend_position == 'none' | n_colors > 9) {
    return(invisible(x))
  }

  if (type == 'quantiles') {
    legend(legend_position, legend = quantiles, fill = colors)
  }
  else {
    legend(legend_position, legend = vals, fill = colors)
  }

  return(invisible(x))
}

#' @param CI Whether credible intervals should be plotted.
#' @param alpha If \code{CI = TRUE}, plots \code{100(1 - alpha)\%} credible
#'   intervals.
#' @param legend_position A legend position keyword as accepted by
#'   \code{\link{legend}} or \code{'none'} if the legend should be omitted.
#' @rdname Methods
#' @export
plot.drbart <-
  function(x, xpred, ygrid,
           type = c('density', 'distribution', 'quantiles', 'mean'),
           quantiles = c(0.025, 0.5, 0.975),
           CI = FALSE, alpha = 0.05, legend_position = 'topleft', ...) {

  type <- match.arg(type)

  tmp <- preprocess_plot_args(xpred, ygrid, type, quantiles,
                              CI, alpha, legend_position)

  all_preds <- predict(x, xpred, ygrid, type, quantiles)

  return(plot(all_preds, CI, alpha, legend_position, ...))
}
