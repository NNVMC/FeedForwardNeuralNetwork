#include "NNTrainerGSL.hpp"

#include <gsl/gsl_matrix.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_multifit_nlinear.h>

#include <cstddef> // NULL

namespace nn_trainer_gsl_details {

    // --- Workspace struct methods

    void training_workspace::copyData(const NNTrainingData &tdata)
    {
        ndata = tdata.ndata;
        ntraining = tdata.ntraining;
        nvalidation = tdata.nvalidation;
        xndim = tdata.xndim;
        yndim = tdata.yndim;
        x = tdata.x;
        y = tdata.y;
        yd1 = tdata.yd1;
        yd2 = tdata.yd2;
        w = tdata.w;
    }

    void training_workspace::copyConfig(const NNTrainingConfig &tconfig)
    {
        lambda_r = tconfig.lambda_r;
        lambda_d1 = tconfig.lambda_d1;
        lambda_d2 = tconfig.lambda_d2;
        maxn_steps = tconfig.maxn_steps;
        maxn_novali = tconfig.maxn_novali;

        flag_r = (lambda_r > 0);
        flag_d1 = (lambda_d1 > 0);
        flag_d2 = (lambda_d2 > 0);
    }

    void training_workspace::copyDatConf(const NNTrainingData &tdata, const NNTrainingConfig &tconfig)
    {
        copyData(tdata);
        copyConfig(tconfig);
    }

    // --- Helper functions

    // set new NN betas
    int setBetas(FeedForwardNeuralNetwork * const ffnn, const gsl_vector * const betas)
    {
        const int nbeta = ffnn->getNBeta();
        for (int i=0; i<nbeta; ++i){
            ffnn->setBeta(i, gsl_vector_get(betas, i));
        }
        return nbeta;
    };

    // counts total residual vector size
    // set nbeta or xndim to >0 to count regularization and derivative residual terms, respectively
    // set nderiv = 1 if only one of both deriv residuals should be counted
    int calcNData(const int &nbase, const int &yndim, const int &nbeta, const int &xndim, const int &nderiv)
    {
        return (nbase > 0) ? nbase*yndim + nbeta + nderiv * nbase*xndim*yndim : 0;
    };

    // calculate index offset pointing right behind the basic residual part
    void calcOffset1(const int &nbase, const int &yndim, int &off)
    {
        off = nbase*yndim;
    };

    // also calculate offset behind first derivative part
    void calcOffset12(const int &nbase, const int &yndim, const int &xndim, int &offd1, int &offd2)
    {
        calcOffset1(nbase, yndim, offd1);
        offd2 = offd1 + nbase*xndim*yndim;
    };

    // also calculate offset behind second derivative part
    void calcOffset123(const int &nbase, const int &yndim, const int &xndim, int &offd1, int &offd2, int &offr)
    {
        calcOffset12(nbase, yndim, xndim, offd1, offd2);
        offr = offd2 + nbase*xndim*yndim;
    };

    // store (root) square sum of residual vector f in chisq (chi)
    void calcRSS(const gsl_vector * const f, double &chi, double &chisq)
    {
        gsl_blas_ddot(f, f, &chisq);
        chi = sqrt(chisq);
    };

    // calculate all costs from the two residual vectors
    void calcCosts(const gsl_vector * const f, double &chi, double &chisq, const gsl_vector * const fvali, double &chi_vali, double &chisq_vali)
    {
        calcRSS(f, chi, chisq);
        if (fvali) calcRSS(fvali, chi_vali, chisq_vali);
    };

    // calculate all costs (from workspace and vali vector)
    void calcCosts(gsl_multifit_nlinear_workspace * const w, double &chi, double &chisq, const gsl_vector * const fvali, double &chi_vali, double &chisq_vali)
    {
        calcCosts(gsl_multifit_nlinear_residual(w), chi, chisq, fvali, chi_vali, chisq_vali);
    };

    // calculate fit and error arrays
    void calcFitErr(gsl_multifit_nlinear_workspace * const w, double * const fit, double * const err, const int &ndata, const int &npar, const double &chisq)
    {
        const double c = GSL_MAX_DBL(1, sqrt(chisq / (ndata-npar)));
        const gsl_matrix * const J = gsl_multifit_nlinear_jac(w);
        gsl_matrix * const covar = gsl_matrix_alloc (npar, npar);

        gsl_multifit_nlinear_covar(J, 0.0, covar);
        for (int i = 0; i<npar; ++i) {
            fit[i] = gsl_vector_get(w->x, i);
            err[i] = c*sqrt(gsl_matrix_get(covar,i,i));
        }
        gsl_matrix_free(covar);
    };


    // --- Cost functions

    // cost function without regularization and derivative terms
    int ffnn_f_pure(const gsl_vector * betas, void * const tws, gsl_vector * f) {
        const int ntrain = ((struct training_workspace *)tws)->ntraining;
        const int n = ntrain + ((struct training_workspace *)tws)->nvalidation; // we also fill the validation residuals here
        const int yndim = ((struct training_workspace *)tws)->yndim;
        const double * const * const x = ((struct training_workspace *)tws)->x;
        const double * const * const y = ((struct training_workspace *)tws)->y;
        const double * const * const w = ((struct training_workspace *)tws)->w;
        FeedForwardNeuralNetwork * const ffnn = ((struct training_workspace *)tws)->ffnn;
        gsl_vector * const fvali = ((struct training_workspace *)tws)->fvali_pure;

        double resi;

        setBetas(ffnn, betas);

        //get difference NN vs data
        for (int i=0; i<n; ++i) {
            ffnn->setInput(x[i]);
            ffnn->FFPropagate();
            for (int j=0; j<yndim; ++j) {
                resi = w[i][j] * (ffnn->getOutput(j) - y[i][j]);
                if (i<ntrain) gsl_vector_set(f, i*yndim + j, resi);
                else gsl_vector_set(fvali, (i-ntrain)*yndim + j, resi);
            }
        }

        return GSL_SUCCESS;
    };

    // gradient of cost function without regularization and derivative terms
    int ffnn_df_pure(const gsl_vector * betas, void * const tws, gsl_matrix * J) {
        const int ntrain = ((struct training_workspace *)tws)->ntraining; // gradients only for training set though
        const int yndim = ((struct training_workspace *)tws)->yndim;
        const double * const * const x = ((struct training_workspace *)tws)->x;
        const double * const * const w = ((struct training_workspace *)tws)->w;
        FeedForwardNeuralNetwork * const ffnn = ((struct training_workspace *)tws)->ffnn;

        const int nbeta = setBetas(ffnn, betas);

        //calculate cost gradient
        for (int ibeta=0; ibeta<nbeta; ++ibeta) {
            for (int i=0; i<ntrain; ++i) {
                ffnn->setInput(x[i]);
                ffnn->FFPropagate();
                for (int j=0; j<yndim; ++j) {
                    gsl_matrix_set(J, i*yndim + j, ibeta, w[i][j] * ffnn->getVariationalFirstDerivative(j, ibeta));
                }
            }
        }

        return GSL_SUCCESS;
    };

    // cost function with derivative but without regularization
    int ffnn_f_deriv(const gsl_vector * betas, void * const tws, gsl_vector * f) {
        const int ntrain = ((struct training_workspace *)tws)->ntraining;
        const int nvali = ((struct training_workspace *)tws)->nvalidation;
        const int n = ntrain + nvali;
        const int xndim = ((struct training_workspace *)tws)->xndim;
        const int yndim = ((struct training_workspace *)tws)->yndim;
        const double * const * const x = ((struct training_workspace *)tws)->x;
        const double * const * const y = ((struct training_workspace *)tws)->y;
        const double * const * const * const yd1 = ((struct training_workspace *)tws)->yd1;
        const double * const * const * const yd2 = ((struct training_workspace *)tws)->yd2;
        const double * const * const w = ((struct training_workspace *)tws)->w;
        const double lambda_d1 = ((struct training_workspace *)tws)->lambda_d1;
        const double lambda_d2 = ((struct training_workspace *)tws)->lambda_d2;
        const bool flag_d1 = ((struct training_workspace *)tws)->flag_d1;
        const bool flag_d2 = ((struct training_workspace *)tws)->flag_d2;
        FeedForwardNeuralNetwork * const ffnn = ((struct training_workspace *)tws)->ffnn;
        gsl_vector * const fvali = ((struct training_workspace *)tws)->fvali_noreg;
        gsl_vector * const fvali_pure = ((struct training_workspace *)tws)->fvali_pure;

        gsl_vector * fnow;
        int nshift, nshift2, ishift;
        const double lambda_d1_red = sqrt(lambda_d1), lambda_d2_red = sqrt(lambda_d2);

        setBetas(ffnn, betas);

        fnow = f;
        calcOffset12(ntrain, yndim, xndim, nshift, nshift2);
        //get difference NN vs data
        for (int i=0; i<n; ++i) {
            ffnn->setInput(x[i]);
            ffnn->FFPropagate();

            if (i < ntrain) ishift = i*yndim;
            else {
                if (i == ntrain) {
                    fnow = fvali; // switch working pointer
                    calcOffset12(nvali, yndim, xndim, nshift, nshift2);
                }
                ishift = (i-ntrain)*yndim;
            }
            int inshift = ishift + nshift;
            int inshift2 = ishift + nshift2;

            for (int j=0; j<yndim; ++j) {
                gsl_vector_set(fnow, ishift + j,  w[i][j] * (ffnn->getOutput(j) - y[i][j]));
                if (i >= ntrain) gsl_vector_set(fvali_pure, ishift + j, gsl_vector_get(fnow, ishift + j)); // let's also fill fvali_pure here
                for (int k=0; k<xndim; ++k) {
                    gsl_vector_set(fnow, inshift + k*nshift + j, flag_d1 ? w[i][j] * lambda_d1_red * (ffnn->getFirstDerivative(j, k) - yd1[i][j][k]) : 0.0);
                    gsl_vector_set(fnow, inshift2 + k*nshift + j, flag_d2 ? w[i][j] * lambda_d2_red * (ffnn->getSecondDerivative(j, k) - yd2[i][j][k]) : 0.0);
                }
            }
        }

        return GSL_SUCCESS;
    };

    // gradient of cost function with derivative but without regularization
    int ffnn_df_deriv(const gsl_vector * betas, void * const tws, gsl_matrix * J) {
        const int ntrain = ((struct training_workspace *)tws)->ntraining;
        const int xndim = ((struct training_workspace *)tws)->xndim;
        const int yndim = ((struct training_workspace *)tws)->yndim;
        const double * const * const x = ((struct training_workspace *)tws)->x;
        const double * const * const w = ((struct training_workspace *)tws)->w;
        const double lambda_d1 = ((struct training_workspace *)tws)->lambda_d1;
        const double lambda_d2 = ((struct training_workspace *)tws)->lambda_d2;
        const bool flag_d1 = ((struct training_workspace *)tws)->flag_d1;
        const bool flag_d2 = ((struct training_workspace *)tws)->flag_d2;
        FeedForwardNeuralNetwork * const ffnn = ((struct training_workspace *)tws)->ffnn;

        const double lambda_d1_red = sqrt(lambda_d1), lambda_d2_red = sqrt(lambda_d2);
        int nshift, nshift2, ishift, inshift, inshift2;

        const int nbeta = setBetas(ffnn, betas);
        calcOffset12(ntrain, yndim, xndim, nshift, nshift2);

        //calculate cost gradient
        for (int ibeta=0; ibeta<nbeta; ++ibeta) {
            for (int i=0; i<ntrain; ++i) {
                ffnn->setInput(x[i]);
                ffnn->FFPropagate();

                ishift = i*yndim;
                inshift = ishift + nshift;
                inshift2 = ishift + nshift2;

                for (int j=0; j<yndim; ++j) {
                    gsl_matrix_set(J, ishift + j, ibeta, w[i][j] * ffnn->getVariationalFirstDerivative(j, ibeta));
                    for (int k=0; k<xndim; ++k) {
                        gsl_matrix_set(J, inshift + k*nshift + j, ibeta, flag_d1? w[i][j] * lambda_d1_red * ffnn->getCrossFirstDerivative(j, k, ibeta) : 0.0);
                        gsl_matrix_set(J, inshift2 + k*nshift + j, ibeta, flag_d2? w[i][j] * lambda_d2_red * ffnn->getCrossSecondDerivative(j, k, ibeta) : 0.0);
                    }
                }
            }
        }

        return GSL_SUCCESS;
    };

    // cost function for fitting, without derivative but with regularization
    int ffnn_f_pure_reg(const gsl_vector * betas, void * const tws, gsl_vector * f) {
        const int ntrain = ((struct training_workspace *)tws)->ntraining;
        const int nvali =  ((struct training_workspace *)tws)->nvalidation;
        const int yndim = ((struct training_workspace *)tws)->yndim;
        const double lambda_r = ((struct training_workspace *)tws)->lambda_r;
        FeedForwardNeuralNetwork * const ffnn = ((struct training_workspace *)tws)->ffnn;
        gsl_vector * const fvali = ((struct training_workspace *)tws)->fvali_full;
        gsl_vector * const fvali_pure = ((struct training_workspace *)tws)->fvali_pure;

        const int nbeta = ffnn->getNBeta(), n_reg = calcNData(ntrain, yndim, nbeta), nvali_reg = calcNData(nvali, yndim, nbeta);
        const double lambda_r_red = sqrt(lambda_r / nbeta);
        int nshift;

        ffnn_f_pure(betas, tws, f);

        //append regularization
        calcOffset1(ntrain, yndim, nshift);
        for (int i=nshift; i<n_reg; ++i) {
            gsl_vector_set(f, i, lambda_r_red * gsl_vector_get(betas, i-nshift));
        }

        if (nvali > 0) {
            calcOffset1(nvali, yndim, nshift);
            for (int i=0; i<nvali_reg; ++i) {
                if (i<nshift) gsl_vector_set(fvali, i, gsl_vector_get(fvali_pure, i));
                else gsl_vector_set(fvali, i, lambda_r_red * gsl_vector_get(betas, i-nshift));
            }
        }

        return GSL_SUCCESS;
    };

    // gradient of cost function without derivatives but with regularization
    int ffnn_df_pure_reg(const gsl_vector * betas, void * const tws, gsl_matrix * J) {
        const int ntrain = ((struct training_workspace *)tws)->ntraining;
        const int yndim = ((struct training_workspace *)tws)->yndim;
        const double lambda_r = ((struct training_workspace *)tws)->lambda_r;
        FeedForwardNeuralNetwork * ffnn = ((struct training_workspace *)tws)->ffnn;

        const int nbeta = ffnn->getNBeta(), n_reg = calcNData(ntrain, yndim, nbeta);
        const double lambda_r_red = sqrt(lambda_r / nbeta);
        int nshift;

        ffnn_df_pure(betas, tws, J);

        //append regularization gradient
        calcOffset1(ntrain, yndim, nshift);
        for (int i=nshift; i<n_reg; ++i) {
            for (int j=0; j<nbeta; ++j) {
                gsl_matrix_set(J, i, j, 0.0);
            }
            gsl_matrix_set(J, i, i-nshift, lambda_r_red);
        }

        return GSL_SUCCESS;
    };

    // cost function for fitting, with derivative and regularization
    int ffnn_f_deriv_reg(const gsl_vector * betas, void * const tws, gsl_vector * f) {
        const int ntrain = ((struct training_workspace *)tws)->ntraining;
        const int nvali = ((struct training_workspace *)tws)->nvalidation;
        const int xndim = ((struct training_workspace *)tws)->xndim;
        const int yndim = ((struct training_workspace *)tws)->yndim;
        const double lambda_r = ((struct training_workspace *)tws)->lambda_r;
        FeedForwardNeuralNetwork * const ffnn = ((struct training_workspace *)tws)->ffnn;
        gsl_vector * const fvali = ((struct training_workspace *)tws)->fvali_full;
        gsl_vector * const fvali_noreg = ((struct training_workspace *)tws)->fvali_noreg;

        const int nshift = calcNData(ntrain, yndim, 0, xndim), nshift_vali = calcNData(nvali, yndim, 0, xndim);
        const int nbeta = ffnn->getNBeta(), n_reg = nshift + nbeta, nvali_reg = nshift_vali + nbeta;
        const double lambda_r_red = sqrt(lambda_r / nbeta);

        ffnn_f_deriv(betas, tws, f);

        //append regularization
        for (int i=nshift; i<n_reg; ++i) {
            gsl_vector_set(f, i, lambda_r_red * gsl_vector_get(betas, i-nshift));
        }

        if (nvali > 0) {
            for (int i=0; i<nvali_reg; ++i) {
                if (i<nshift_vali) gsl_vector_set(fvali, i, gsl_vector_get(fvali_noreg, i));
                else gsl_vector_set(fvali, i, lambda_r_red * gsl_vector_get(betas, i-nshift_vali));
            }
        }

        return GSL_SUCCESS;
    };

    // gradient of cost function with derivatives and regularization
    int ffnn_df_deriv_reg(const gsl_vector * betas, void * const tws, gsl_matrix * J) {
        const int ntrain = ((struct training_workspace *)tws)->ntraining;
        const int xndim = ((struct training_workspace *)tws)->xndim;
        const int yndim = ((struct training_workspace *)tws)->yndim;
        const double lambda_r = ((struct training_workspace *)tws)->lambda_r;
        FeedForwardNeuralNetwork * ffnn = ((struct training_workspace *)tws)->ffnn;

        const int nbeta = ffnn->getNBeta(), nshift = calcNData(ntrain, yndim, 0, xndim), n_reg = nshift + nbeta;
        const double lambda_r_red = sqrt(lambda_r / nbeta);

        ffnn_df_deriv(betas, tws, J);

        //append regularization gradient
        for (int i=nshift; i<n_reg; ++i) {
            for (int j=0; j<nbeta; ++j) {
                gsl_matrix_set(J, i, j, 0.0);
            }
            gsl_matrix_set(J, i, i-nshift, lambda_r_red);
        }

        return GSL_SUCCESS;
    };


    // --- Custom driver routines

    // if verbose, this is used to print info on every fit iteration
    void printStepInfo(const gsl_multifit_nlinear_workspace * const w, const training_workspace * const tws, const int &status) {
        gsl_vector *f = gsl_multifit_nlinear_residual(w);
        gsl_vector *x = gsl_multifit_nlinear_position(w);
        double rcond = 0.0;

        // compute reciprocal condition number of J(x)
        gsl_multifit_nlinear_rcond(&rcond, w);

        // print
        fprintf(stderr, "status = %s\n", gsl_strerror(status));
        fprintf(stderr, "iter %zu: cond(J) = %8.4f, |f(x)| = %.8f (train), %.8f (vali)\n", gsl_multifit_nlinear_niter(w), 1.0 / rcond, gsl_blas_dnrm2(f), (tws->fvali_full) ? gsl_blas_dnrm2(tws->fvali_full) : 0.);
        for (size_t i=0; i<x->size; ++i) fprintf(stderr, "b%zu: %f, ", i,  gsl_vector_get(x, i));
        fprintf(stderr, "\n");
    };

    // solve the system with a maximum of tws->max_nsteps iterations, stopping early when validation error doesn't decrease for too long
    void earlyStopDriver(gsl_multifit_nlinear_workspace * const w, const training_workspace * const tws, const int &verbose, int &status, int &info)
    {
        double bestvali = -1.;
        int count_novali = 0;

        while (true) {
            status = gsl_multifit_nlinear_iterate(w); // iterate workspace
            if (verbose > 1) printStepInfo(w, tws, status);

            if (((int)gsl_multifit_nlinear_niter(w)) >= tws->maxn_steps) {  // check if we reached maxnsteps
                info = 0;
                break;
            }

            if (tws->nvalidation > 0) { // then check if validation residual went down
                double resih = gsl_blas_dnrm2(tws->fvali_noreg);

                if (resih == 0 || std::isnan(resih)) { // if it is 0 or nan, stop
                    info = 0;
                    if (verbose>1) fprintf(stderr, "Unregularized validation residual reached 0 (or NaN). Stopping early.\n\n");
                    break;
                }

                if (bestvali >= 0. && resih >= bestvali) { // count how long it didn't go down
                    if (verbose>1) fprintf(stderr, "Unregularized validation residual %.4f did not decrease from previous minimum %.4f. No new minimum since %i iteration(s).\n\n", resih, bestvali, count_novali);

                    ++count_novali;
                    if (count_novali < tws->maxn_novali) continue;
                    else { // if too long, break
                        info = 1;
                        if (verbose>1) fprintf(stderr, "Reached maximal number of iterations (%i) without new validation minimum. Stopping early.\n\n", count_novali);
                        break;
                    }
                }
                else { // new validation minimum found
                    count_novali = 0;
                    bestvali = resih;
                }
            }

            if (verbose>1) fprintf(stderr, "\n");
        }
    };
};

// --- Class method implementation

void NNTrainerGSL::findFit(FeedForwardNeuralNetwork * const ffnn, double * const fit, double * const err, const int &verbose) {
    //   Fit NN with the following passed variables:
    //   fit: holds the to be fitted variables, i.e. betas
    //   err: holds the corresponding fit error
    //   verbose: print verbose output while fitting
    //
    //   Everything else is already configured via the
    //   _tdata, _tconfig and optional _gsl_params
    //   structs passed at creation to constructor

    using namespace nn_trainer_gsl_details; // to use local workspace / functions above

    int npar = ffnn->getNBeta(), ntrain = _tdata.ntraining, nvali = _tdata.nvalidation;
    const gsl_multifit_nlinear_type *T_full = gsl_multifit_nlinear_trust, *T_noreg = gsl_multifit_nlinear_trust, *T_pure = gsl_multifit_nlinear_trust;
    gsl_multifit_nlinear_fdf fdf_full, fdf_noreg, fdf_pure;
    gsl_multifit_nlinear_workspace * w_full, * w_noreg, * w_pure;
    gsl_vector_view gx = gsl_vector_view_array (fit, npar);

    const int dof = ntrain - npar;
    const bool flag_d = _flag_d1 || _flag_d2;
    int ntrain_pure, ntrain_noreg, nvali_pure, nvali_noreg, ntrain_full, nvali_full;
    int status, info;
    double resih, chisq, chi0, chi0_vali = 0.;
    double resi_full, resi_noreg, resi_pure;
    double resi_vali_full = 0., resi_vali_noreg = 0., resi_vali_pure = 0.;


    // configure training workspace
    training_workspace tws;
    tws.copyDatConf(_tdata, _tconfig);
    tws.ffnn = ffnn; // set the to-be-fitted FFNN

    // configure all three fdf objects

    ntrain_pure = calcNData(ntrain, tws.yndim);
    nvali_pure = calcNData(nvali, tws.yndim);

    // first the pure fdf
    fdf_pure.f = ffnn_f_pure;
    fdf_pure.df = ffnn_df_pure;
    fdf_pure.fvv = NULL;
    fdf_pure.n = ntrain_pure;
    fdf_pure.p = npar;
    fdf_pure.params = &tws;

    if (flag_d) {
        ntrain_noreg = calcNData(ntrain, tws.yndim, 0, tws.xndim);
        nvali_noreg = calcNData(nvali, tws.yndim, 0, tws.xndim);

        // deriv fdf without regularization
        fdf_noreg.f = ffnn_f_deriv;
        fdf_noreg.df = ffnn_df_deriv;
        fdf_noreg.fvv = NULL;
        fdf_noreg.n = ntrain_noreg;
        fdf_noreg.p = npar;
        fdf_noreg.params = &tws;
    }
    else {
        ntrain_noreg = calcNData(ntrain, tws.yndim);
        nvali_noreg = calcNData(nvali, tws.yndim);
        fdf_noreg = fdf_pure;
    };

    if (tws.flag_r) {
        ntrain_full = ntrain_noreg + npar;
        nvali_full = nvali_noreg + npar;

        if (flag_d) {
            // deriv with regularization
            fdf_full.f = ffnn_f_deriv_reg;
            fdf_full.df = ffnn_df_deriv_reg;
        }
        else {
            // pure fdf with regularization
            fdf_full.f = ffnn_f_pure_reg;
            fdf_full.df = ffnn_df_pure_reg;
        }
        fdf_full.fvv = NULL;
        fdf_full.n = ntrain_full;
        fdf_full.p = npar;
        fdf_full.params = &tws;
    }
    else {
        ntrain_full = ntrain_noreg;
        nvali_full = nvali_noreg;
        fdf_full = fdf_noreg;
    };

    // allocate workspace with default parameters, also allocate space for validation
    w_full = gsl_multifit_nlinear_alloc (T_full, &_gsl_params, ntrain_full, npar);
    w_noreg = gsl_multifit_nlinear_alloc (T_noreg, &_gsl_params, ntrain_noreg, npar);
    w_pure = gsl_multifit_nlinear_alloc (T_pure, &_gsl_params, ntrain_pure, npar);
    if (_flag_vali) {
        tws.fvali_pure = gsl_vector_alloc(nvali_pure);
        tws.fvali_noreg = flag_d ? gsl_vector_alloc(nvali_noreg) : tws.fvali_pure;
        tws.fvali_full = tws.flag_r ? gsl_vector_alloc(nvali_full) : tws.fvali_noreg;
    }
    else {
        tws.fvali_pure = NULL;
        tws.fvali_noreg = NULL;
        tws.fvali_full = NULL;
        if (verbose > 1) fprintf(stderr, "[NNTrainerGSL] Warning: Validation residual calculation disabled, i.e. no early stopping.\n");
    }

    // initialize solver with starting point and calculate initial cost
    gsl_multifit_nlinear_init(&gx.vector, &fdf_full, w_full);
    calcCosts(w_full, chi0, resih, tws.fvali_full, chi0_vali, resih);

    // run driver to find fit
    earlyStopDriver(w_full, &tws, verbose, status, info);

    // compute again final full cost and error of best fit parameters
    calcCosts(w_full, resi_full, chisq, tws.fvali_full, resi_vali_full, resih);
    calcFitErr(w_full, fit, err, ntrain, npar, chisq);

    // final unregularized cost calculation
    gsl_multifit_nlinear_init(&gx.vector, &fdf_noreg, w_noreg);
    calcCosts(w_noreg, resi_noreg, resih, tws.fvali_noreg, resi_vali_noreg, resih);

    // final pure (no deriv, no reg) cost calculation
    gsl_multifit_nlinear_init(&gx.vector, &fdf_pure, w_pure);
    calcCosts(w_pure, resi_pure, resih, tws.fvali_pure, resi_vali_pure, resih);

    if (verbose > 1) {
        fprintf(stderr, "summary from method '%s/%s'\n", gsl_multifit_nlinear_name(w_full), gsl_multifit_nlinear_trs_name(w_full));
        fprintf(stderr, "number of iterations: %zu\n", gsl_multifit_nlinear_niter(w_full));
        fprintf(stderr, "function evaluations: %zu\n", fdf_full.nevalf);
        fprintf(stderr, "Jacobian evaluations: %zu\n", fdf_full.nevaldf);
        fprintf(stderr, "reason for stopping: %s\n", (info == 1) ? "failed validation" : "max steps || 0 residual");
        fprintf(stderr, "status = %s\n", gsl_strerror (status));

        fprintf(stderr, "initial |f(x)| = %f (train), %f (vali)\n", chi0, chi0_vali);
        fprintf(stderr, "final   |f(x)| = %f (train), %f (vali)\n", resi_full, resi_vali_full);
        fprintf(stderr, "w/o reg |f(x)| = %f (train), %f (vali)\n", resi_noreg, resi_vali_noreg);
        fprintf(stderr, "pure    |f(x)| = %f (train), %f (vali)\n", resi_pure, resi_vali_pure);
        fprintf(stderr, "chisq/dof = %g\n", chisq / dof);

        for(int i=0; i<npar; ++i) fprintf(stderr, "b%i      = %.5f +/- %.5f\n", i, fit[i], err[i]);
        fprintf(stderr, "\n");
    }

    gsl_multifit_nlinear_free(w_full);
    gsl_multifit_nlinear_free(w_noreg);
    gsl_multifit_nlinear_free(w_pure);
    if (_flag_vali) {
        gsl_vector_free(tws.fvali_pure);
        if (flag_d) gsl_vector_free(tws.fvali_noreg);
        if (tws.flag_r) gsl_vector_free(tws.fvali_full);
    }
};
