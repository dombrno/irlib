#pragma once

#include <iostream>
#include <complex>
#include <cmath>
#include <vector>
#include <set>
#include <assert.h>
#include <memory>

#include <Eigen/CXX11/Tensor>
#include <Eigen/MPRealSupport>

#include "common.hpp"
#include "kernel.hpp"
#include "piecewise_polynomial.hpp"

#include "detail/aux.hpp"

namespace irlib {
/**
 * Class for kernel Ir basis
 */
    class ir_basis_set {
    public:
        /**
         * Constructor
         * @param knl  kernel
         * @param max_dim  max number of basis functions to be computed.
         * @param cutoff  we drop basis functions corresponding to small singular values  |s_l/s_0~ < cutoff.
         * @param Nl   Number of Legendre polynomials used to expand basis functions in each sector
         */
        ir_basis_set(const irlib::kernel<mpfr::mpreal> &knl, int max_dim, double cutoff, int Nl) throw(std::runtime_error) {
            statistics_ = knl.get_statistics();
            std::tie(sv_, u_basis_, v_basis_) = generate_ir_basis_functions(knl, max_dim, cutoff, Nl);
            assert(u_basis_.size()>0);
            assert(u_basis_[0].num_sections()>0);
        }

    private:
        statistics::statistics_type statistics_;
        //singular values
        std::vector<double> sv_;
        std::vector< irlib::piecewise_polynomial<double,mpreal> > u_basis_, v_basis_;

    public:
        /**
         * Compute the values of the basis functions for a given x.
         * @param x    x = 2 * tau/beta - 1  (-1 <= x <= 1)
         * @param val  results
         */
        double sl(int l) const throw(std::runtime_error) {
            assert(l >= 0 && l < dim());
            python_runtime_check(l >= 0 && l < dim(), "Index l is out of range.");
            return sv_[l];
        }

        /**
         * @param l  order of basis function
         * @param x  x on [-1,1]
         * @return   The value of u_l(x)
         */
        double ulx(int l, double x) const throw(std::runtime_error) {
            return ulx(l, mpfr::mpreal(x));
        }

        /**
         * @param l  order of basis function
         * @param y  y on [-1,1]
         * @return   The value of v_l(y)
         */
        double vly(int l, double y) const throw(std::runtime_error) {
            return vly(l, mpfr::mpreal(y));
        }

#ifndef SWIG //DO NOT EXPOSE TO PYTHON
        /**
         * This function should not be called outside this library
         * @param l  order of basis function
         * @param x  x on [-1,1]
         * @return   The value of u_l(x)
         */
        double ulx(int l, const mpfr::mpreal& x) const throw(std::runtime_error) {
            assert(x >= -1 && x <= 1);
            assert(l >= 0 && l < dim());
            python_runtime_check(l >= 0 && l < dim(), "Index l is out of range.");
            python_runtime_check(x >= -1 && x <= 1, "x must be in [-1,1].");
            if (l < 0 || l >= dim()) {
                throw std::runtime_error("Invalid index of basis function!");
            }
            if (x < -1 || x > 1) {
                throw std::runtime_error("Invalid value of x!");
            }

            if (x >= 0) {
                return u_basis_[l].compute_value(x);
            } else {
                return u_basis_[l].compute_value(-x) * (l%2==0 ? 1 : -1);
            }
        }

        /**
         * This function should not be called outside this library
         * @param l  order of basis function
         * @param y  y on [-1,1]
         * @return   The value of v_l(y)
         */
        double vly(int l, const mpfr::mpreal& y) const throw(std::runtime_error) {
            assert(y >= -1 && y <= 1);
            assert(l >= 0 && l < dim());
            python_runtime_check(l >= 0 && l < dim(), "Index l is out of range.");
            python_runtime_check(y >= -1 && y <= 1, "y must be in [-1,1].");
            if (l < 0 || l >= dim()) {
                throw std::runtime_error("Invalid index of basis function!");
            }
            if (y < -1 || y > 1) {
                throw std::runtime_error("Invalid value of y!");
            }
            if (y >= 0) {
                return v_basis_[l].compute_value(y);
            } else {
                return v_basis_[l].compute_value(-y) * (l%2==0 ? 1 : -1);
            }
        }

        /**
         * Return a reference to the l-th basis function
         * @param l l-th basis function
         * @return  reference to the l-th basis function
         */
        const irlib::piecewise_polynomial<double,mpfr::mpreal> &ul(int l) const throw(std::runtime_error) {
            assert(l >= 0 && l < dim());
            python_runtime_check(l >= 0 && l < dim(), "Index l is out of range.");
            return u_basis_[l];
        }

        const irlib::piecewise_polynomial<double,mpfr::mpreal> &vl(int l) const throw(std::runtime_error) {
            assert(l >= 0 && l < dim());
            python_runtime_check(l >= 0 && l < dim(), "Index l is out of range.");
            return v_basis_[l];
        }
#endif

        /**
         * Return a reference to all basis functions
         */
        //const std::vector<irlib::piecewise_polynomial<double> > all() const { return u_basis_; }

        /**
         * Return number of basis functions
         * @return  number of basis functions
         */
        int dim() const { return u_basis_.size(); }

        /// Return statistics
        irlib::statistics::statistics_type get_statistics() const {
            return statistics_;
        }

#ifndef SWIG //DO NOT EXPOSE TO PYTHON
        /**
         * Compute transformation matrix to Matsubara freq.
         * The computation may take some time. You may store the result somewhere and do not call this routine frequenctly.
         * @param n_vec  This vector must contain indices of non-negative Matsubara freqencies (i.e., n>=0) in ascending order.
         * @param Tnl    Results
         */
        void compute_Tnl(
                const std::vector<long> &n_vec,
                Eigen::Tensor<std::complex<double>, 2> &Tnl
        ) const {
            compute_transformation_matrix_to_matsubara<double>(n_vec,
                                                                                   statistics_,
                                                                                   u_basis_,
                                                                                   Tnl);
        }
#endif

        /**
         * Compute transformation matrix to Matsubara freq.
         * The computation may take some time. You may store the result somewhere and do not call this routine frequenctly.
         * @param n_vec  This vector must contain indices of non-negative Matsubara freqencies (i.e., n>=0) in ascending order.
         * @return Results
         */
        Eigen::Tensor<std::complex<double>, 2>
        compute_Tnl(const std::vector<long> &n_vec) const {
            Eigen::Tensor<std::complex<double>, 2> Tnl;
            compute_Tnl(n_vec, Tnl);
            return Tnl;
        }

        /**
         * Compute transformation matrix (Lewin's shifted representation)
         * The computation may take some time. You may store the result somewhere and do not call this routine frequenctly.
         * @param o_vec  This vector must contain o >= in ascending order.
         * @return Results
         */
        Eigen::Tensor<std::complex<double>, 2>
        compute_Tbar_ol(const std::vector<long> &o_vec) const {
            int no = o_vec.size();
            int nl = u_basis_.size();

            Eigen::Tensor<std::complex<double>, 2> Tbar_ol(no, nl);
            irlib::compute_Tbar_ol(o_vec, u_basis_, Tbar_ol);

            return Tbar_ol;
        }


    };

    /**
     * Fermionic IR basis
     */
    class basis_f : public ir_basis_set {
    public:
        basis_f(double Lambda, int max_dim, double cutoff = 1e-12, int Nl=10) throw(std::runtime_error)
                : ir_basis_set(fermionic_kernel(Lambda), max_dim, cutoff, Nl) {}
    };

    /**
     * Bosonic IR basis
     */
    class basis_b : public ir_basis_set {
    public:
        basis_b(double Lambda, int max_dim, double cutoff = 1e-12, int Nl=10) throw(std::runtime_error)
                : ir_basis_set(bosonic_kernel(Lambda), max_dim, cutoff, Nl) {}
    };
}
