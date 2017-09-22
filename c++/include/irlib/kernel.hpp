#pragma once

#include <complex>
#include <memory>
#include <utility>

#include <Eigen/SVD>

#include "common.hpp"
#include "piecewise_polynomial.hpp"
#include "detail/aux.hpp"

namespace irlib {
    /**
     * Abstract class representing an analytical continuation kernel
     */
    template<typename T>
    class kernel {
    public:
        typedef T mp_type;

        virtual ~kernel() {};

        /// return the value of the kernel for given x and y in the [-1,1] interval.
        virtual T operator()(T x, T y) const = 0;

        /// return statistics
        virtual irlib::statistics::statistics_type get_statistics() const = 0;

        /// return lambda
        virtual double Lambda() const = 0;

#ifndef SWIG

        /// return a reference to a copy
        virtual std::shared_ptr<kernel> clone() const = 0;

#endif
    };

#ifdef SWIG
    %template(real_kernel) kernel<mpfr::mpreal>;
#endif

    /**
     * Fermionic kernel
     */
    class fermionic_kernel : public kernel<mpfr::mpreal> {
    public:
        fermionic_kernel(double Lambda) : Lambda_(Lambda) {}

        virtual ~fermionic_kernel() {};

        mpfr::mpreal operator()(mpfr::mpreal x, mpfr::mpreal y) const {
            mpfr::mpreal half_Lambda = mpfr::mpreal("0.5") * mpfr::mpreal(Lambda_);

            const double limit = 200.0;
            if (Lambda_ * y > limit) {
                return mpfr::exp(-half_Lambda * x * y - half_Lambda * y);
            } else if (Lambda_ * y < -limit) {
                return mpfr::exp(-half_Lambda * x * y + half_Lambda * y);
            } else {
                return mpfr::exp(-half_Lambda * x * y) / (2 * mpfr::cosh(half_Lambda * y));
            }
        }

        irlib::statistics::statistics_type get_statistics() const {
            return irlib::statistics::FERMIONIC;
        }

        double Lambda() const {
            return Lambda_;
        }

#ifndef SWIG

        std::shared_ptr<kernel> clone() const {
            return std::shared_ptr<kernel>(new fermionic_kernel(Lambda_));
        }

#endif

    private:
        double Lambda_;
    };

    /**
     * Bosonic kernel
     */
    class bosonic_kernel : public kernel<mpfr::mpreal> {
    public:
        bosonic_kernel(double Lambda) : Lambda_(Lambda) {}

        virtual ~bosonic_kernel() {};

        mpfr::mpreal operator()(mpfr::mpreal x, mpfr::mpreal y) const {
            const double limit = 200.0;
            mpfr::mpreal half_Lambda = mpfr::mpreal("0.5") * mpfr::mpreal(Lambda_);

            if (mpfr::abs(Lambda_ * y) < 1e-30) {
                return mpfr::exp(-half_Lambda * x * y) / Lambda_;
            } else if (Lambda_ * y > limit) {
                return y * mpfr::exp(-half_Lambda * x * y - half_Lambda * y);
            } else if (Lambda_ * y < -limit) {
                return -y * mpfr::exp(-half_Lambda * x * y + half_Lambda * y);
            } else {
                return y * mpfr::exp(-half_Lambda * x * y) / (2 * mpfr::sinh(half_Lambda * y));
            }
        }

        irlib::statistics::statistics_type get_statistics() const {
            return irlib::statistics::BOSONIC;
        }

        double Lambda() const {
            return Lambda_;
        }

#ifndef SWIG

        std::shared_ptr<kernel> clone() const {
            return std::shared_ptr<kernel>(new bosonic_kernel(Lambda_));
        }

#endif

    private:
        double Lambda_;
    };

    namespace detail {

        template<typename mp_type>
        std::vector<std::pair<mp_type, mp_type> >
        composite_gauss_legendre_nodes(
                const std::vector<mp_type> &section_edges,
                const std::vector<std::pair<mp_type, mp_type> > &nodes
        ) {
            int num_sec = section_edges.size() - 1;
            int num_local_nodes = nodes.size();

            std::vector<std::pair<mp_type, mp_type> > all_nodes(num_sec * num_local_nodes);
            for (int s = 0; s < num_sec; ++s) {
                auto a = section_edges[s];
                auto b = section_edges[s + 1];
                for (int n = 0; n < num_local_nodes; ++n) {
                    mp_type x = a + ((b - a) / mp_type(2)) * (nodes[n].first + mp_type(1));
                    mp_type w = ((b - a) / mp_type(2)) * nodes[n].second;
                    all_nodes[s * num_local_nodes + n] = std::make_pair(x, w);
                }
            }
            return all_nodes;
        };
    }


    /**
     * Compute Matrix representation of a given Kernel
     * @tparam mp_type
     * @tparam K
     * @param kernel
     * @param section_edges_x
     * @param section_edges_y
     * @param num_local_nodes
     * @param Nl
     * @return Matrix representation
     */
    template<typename mp_type, typename K>
    Eigen::Matrix<mp_type, Eigen::Dynamic, Eigen::Dynamic>
    matrix_rep(const K &kernel,
               const std::vector<mp_type> &section_edges_x,
               const std::vector<mp_type> &section_edges_y,
               int num_local_nodes,
               int Nl) {

        using matrix_type = Eigen::Matrix<mp_type, Eigen::Dynamic, Eigen::Dynamic>;

        int num_sec = section_edges_x.size() - 1;

        // nodes for Gauss-Legendre integration
        std::vector<std::pair<mp_type, mp_type >> nodes = detail::gauss_legendre_nodes<mp_type>(num_local_nodes);
        auto nodes_x = detail::composite_gauss_legendre_nodes(section_edges_x, nodes);
        auto nodes_y = detail::composite_gauss_legendre_nodes(section_edges_y, nodes);

        std::vector<matrix_type> phi_x(num_sec);
        std::vector<matrix_type> phi_y(num_sec);
        for (int s = 0; s < num_sec; ++s) {
            phi_x[s] = matrix_type(Nl, num_local_nodes);
            phi_y[s] = matrix_type(Nl, num_local_nodes);
            for (int n = 0; n < num_local_nodes; ++n) {
                for (int l = 0; l < Nl; ++l) {
                    auto leg_val = normalized_legendre_p(l, nodes[n].first);
                    phi_x[s](l, n) = mpfr::sqrt(mp_type(2) / (section_edges_x[s + 1] - section_edges_x[s])) * leg_val *
                                     nodes_x[s * num_local_nodes + n].second;
                    phi_y[s](l, n) = mpfr::sqrt(mp_type(2) / (section_edges_y[s + 1] - section_edges_y[s])) * leg_val *
                                     nodes_y[s * num_local_nodes + n].second;
                }
            }
        }

        matrix_type K_mat(num_sec * Nl, num_sec * Nl);
        for (int s2 = 0; s2 < num_sec; ++s2) {
            for (int s = 0; s < num_sec; ++s) {

                matrix_type K_nn(num_local_nodes, num_local_nodes);
                for (int n = 0; n < num_local_nodes; ++n) {
                    for (int n2 = 0; n2 < num_local_nodes; ++n2) {
                        K_nn(n, n2) = kernel(nodes_x[s * num_local_nodes + n].first,
                                             nodes_y[s2 * num_local_nodes + n2].first);
                    }
                }

                // phi_x(l, n) * K_nn(n, n2) * phi_y(l2, n2)^T
                matrix_type r = phi_x[s] * K_nn * phi_y[s2].transpose();

                for (int l2 = 0; l2 < Nl; ++l2) {
                    for (int l = 0; l < Nl; ++l) {
                        K_mat(Nl * s + l, Nl * s2 + l2) = r(l, l2);
                    }
                }
            }
        }

        return K_mat;
    }


    std::tuple<
            std::vector<double>,
            std::vector<irlib::piecewise_polynomial<double>>,
            std::vector<irlib::piecewise_polynomial<double>>
            >
    generate_ir_basis_functions_impl(
            const kernel<mpfr::mpreal>& kernel,
            int max_dim,
            double sv_cutoff,
            int Nl,
            int num_nodes_gauss_legendre,
            const std::vector<mpfr::mpreal>& section_edges_x,
            const std::vector<mpfr::mpreal>& section_edges_y
    ) throw(std::runtime_error) {
        using mpfr::mpreal;
        using vector_t = Eigen::Matrix<mpreal, Eigen::Dynamic, 1>;

        // Compute Kernel matrix and do SVD for even/odd sector
        auto kernel_even = [&](const mpreal& x, const mpreal& y) { return kernel(x, y) + kernel(x, -y); };
        auto Kmat_even = irlib::matrix_rep(kernel_even, section_edges_x, section_edges_y, num_nodes_gauss_legendre, Nl);
        Eigen::BDCSVD<MatrixXmp> svd_even(Kmat_even, Eigen::ComputeFullU | Eigen::ComputeFullV);

        auto kernel_odd = [&](const mpreal& x, const mpreal& y) { return kernel(x, y) - kernel(x, -y); };
        auto Kmat_odd = irlib::matrix_rep(kernel_odd, section_edges_x, section_edges_y, num_nodes_gauss_legendre, Nl);
        Eigen::BDCSVD<MatrixXmp> svd_odd(Kmat_odd, Eigen::ComputeFullU | Eigen::ComputeFullV);

        // Pick up singular values and basis functions larger than cutoff
        std::vector<double> sv;
        std::vector<vector_t> Uvec, Vvec;
        auto s0 = svd_even.singularValues()[0];
        for (int i=0; i < svd_even.singularValues().size(); ++i) {
            if (sv.size() == max_dim || svd_even.singularValues()[i]/s0 < sv_cutoff) {
                break;
            }
            sv.push_back(static_cast<double>(svd_even.singularValues()[i]));
            Uvec.push_back(svd_even.matrixU().col(i));
            Vvec.push_back(svd_even.matrixV().col(i));
            if (sv.size() == max_dim || svd_odd.singularValues()[i]/s0 < sv_cutoff) {
                break;
            }
            sv.push_back(static_cast<double>(svd_odd.singularValues()[i]));
            Uvec.push_back(svd_odd.matrixU().col(i));
            Vvec.push_back(svd_odd.matrixV().col(i));
        }
        assert(sv.size() <= max_dim);

        // Check if singular values are in decresing order
        for (int l=0; l<sv.size()-1; ++l) {
            if (sv[l] < sv[l+1]) {
                throw std::runtime_error("Singular values are not in decreasing order. This may be due to numerical round erros. You may ask for fewer basis functions!");
            }
        }

        // Construct basis functions
        std::vector<std::vector<mpreal>> deriv_xm1 = normalized_legendre_p_derivatives(Nl, mpreal(-1));
        std::vector<mpreal> inv_factorial;
        inv_factorial.push_back(mpreal(1));
        for (int l=1; l<Nl; ++l) {
            inv_factorial.push_back(inv_factorial.back()/mpreal(l));
        }


        auto gen_pp = [&] (const std::vector<mpreal>& section_edges, const std::vector<vector_t>& vectors) {
            // Construct section edges for piecewise polynomial representations of basis functions
            std::vector<double> section_edges_pp{0.0};
            for (int i=1; i<section_edges.size(); ++i) {
                section_edges_pp.push_back((double) section_edges[i]);
                section_edges_pp.push_back((double) -section_edges[i]);
            }
            std::sort(section_edges_pp.begin(), section_edges_pp.end());

            std::vector<piecewise_polynomial<double>> pp;

            int ns_pp = section_edges_pp.size()-1;
            assert(section_edges_pp.size()-1 == 2*(section_edges.size()-1));
            for (int v=0; v<vectors.size(); ++v) {
                mpreal norm = (vectors[v].transpose() * vectors[v])(0,0);
                assert(mpfr::abs(norm - 1) < 1e-8);

                boost::multi_array<double,2> coeff(boost::extents[ns_pp][Nl]);
                std::fill(coeff.origin(), coeff.origin()+coeff.num_elements(), 0.0);
                int parity = v%2 == 0 ? 1 : -1;
                // loop over sections in [0, 1]
                for (int s=0; s<section_edges.size()-1; ++s) {
                    // loop over normalized Ledendre polynomials
                    for (int l=0; l<Nl; ++l) {
                        mpreal coeff2(1/mpfr::sqrt(section_edges[s + 1] - section_edges[s]));
                        // loop over the orders of derivatives
                        for (int d=0; d<Nl; ++d) {
                            auto tmp = static_cast<double>(
                                    inv_factorial[d] * coeff2 * vectors[v][s*Nl+l] * deriv_xm1[l][d]
                            );
                            coeff[s + ns_pp / 2][d] += tmp;
                            coeff[-s + ns_pp / 2 - 1][d] += parity * tmp * (l % 2 == 0 ? 1 : -1);
                            coeff2 *= 2 / (section_edges[s + 1] - section_edges[s]);
                        }
                    }
                }
                pp.push_back(piecewise_polynomial<double>(section_edges_pp.size()-1, section_edges_pp, coeff));
            }

            return pp;
        };

        auto u_basis_pp = gen_pp(section_edges_x, Uvec);
        auto v_basis_pp = gen_pp(section_edges_y, Vvec);

        for (int i=0; i<u_basis_pp.size(); ++i) {
            if (u_basis_pp[i].compute_value(1) < 0) {
                u_basis_pp[i] = -1.0 * u_basis_pp[i];
                v_basis_pp[i] = -1.0 * v_basis_pp[i];
            }
        }

        return std::make_tuple(sv, u_basis_pp, v_basis_pp);
    }

    std::tuple<
            std::vector<double>,
            std::vector<irlib::piecewise_polynomial<double>>,
            std::vector<irlib::piecewise_polynomial<double>>
    >
    generate_ir_basis_functions(
            const kernel<mpfr::mpreal>& kernel,
            int max_dim,
            double sv_cutoff = 1e-12,
            int Nl = 10,
            int num_nodes_gauss_legendre = 12
    ) throw(std::runtime_error) {
        using mpfr::mpreal;
        using vector_t = Eigen::Matrix<mpreal, Eigen::Dynamic, 1>;

        // Increase default precision if needed
        {
            int min_prec = std::max(static_cast<int>(3.33333*(std::log10(1/sv_cutoff) + 15)), 100);
            if (min_prec > mpreal::get_default_prec()) {
                mpreal::set_default_prec(min_prec);
            }
        }

        // Compute approximate positions of nodes of the highest basis function in the even sector
        std::vector<double> nodes_x, nodes_y;
        std::tie(nodes_x, nodes_y) = compute_approximate_nodes_even_sector(kernel, 250, 1e-12);

        auto gen_section_edges = [](const std::vector<double>& nodes) {
            std::vector<mpreal> section_edges;
            section_edges.push_back(0);
            for (auto it = nodes.begin(); it != nodes.end(); ++it) {
                section_edges.push_back(mpreal(*it));
            }
            section_edges.push_back(1);
            return section_edges;
        };
        std::vector<mpreal> section_edges_x = gen_section_edges(nodes_x);
        std::vector<mpreal> section_edges_y = gen_section_edges(nodes_y);
        //std::cout << " C " << std::endl;

        auto r = generate_ir_basis_functions_impl(kernel, max_dim, sv_cutoff, Nl, num_nodes_gauss_legendre,
                                                section_edges_x,
                                                section_edges_y
        );
        return r;
    };

}