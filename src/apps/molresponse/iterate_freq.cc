

// Copyright 2021 Adrian Hurtado
#include <math.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "../chem/NWChem.h"  // For nwchem interface
#include "../chem/SCFOperators.h"
#include "../chem/molecule.h"
#include "Plot_VTK.h"
#include "TDDFT.h"
#include "madness/chem/potentialmanager.h"
#include "madness/chem/projector.h"  // For easy calculation of (1 - \hat{\rho}^0)
#include "madness/mra/funcdefaults.h"
#include "molresponse/basic_operators.h"
#include "molresponse/density.h"
#include "molresponse/global_functions.h"
#include "molresponse/property.h"
#include "molresponse/response_functions.h"
#include "molresponse/timer.h"

// Iterate Frequency Response
void TDDFT::iterate_freq(World& world) {
  // Variables needed to iterate
  size_t iteration = 0;  // Iteration counter
  QProjector<double, 3> projector(world, ground_orbitals);
  size_t n = r_params.num_orbitals();  // Number of ground state orbitals
  size_t m = r_params.n_states();      // Number of excited states

  // Holds the norms of y function residuals (for convergence)
  Tensor<double> x_norms(m);
  Tensor<double> y_norms(m);
  // Holds wave function corrections
  response_space x_differences(world, m, n);
  response_space y_differences(world, m, n);

  response_space x_residuals(world, m, n);
  response_space y_residuals(world, m, n);
  // response functions

  real_function_3d v_xc;   // For TDDFT
  bool converged = false;  // Converged flag
  const double dconv =
      std::max(FunctionDefaults<3>::get_thresh(), r_params.dconv());

  response_space bsh_x_resp(world, m, n);  // Holds wave function corrections
  response_space bsh_y_resp(world, m, n);  // Holds wave function corrections

  // initialize DFT XC functional operator
  XCOperator<double, 3> xc =
      create_XCOperator(world, ground_orbitals, r_params.xc());

  /***Create X space and X Vectors for Kain*************************************
   *
   *
   *
   * X space refers to X and Y vector spaces |X,Y>
   * X vector is a single |X_b,Y_b> b is a single response state
   * For kain we need a vector of X_vectors
   */
  // create X space residuals
  X_space residuals(world, m, n);
  X_space old_Chi(world, m, n);
  // Create the X space
  // vector of Xvectors
  std::vector<X_vector> Xvector;
  std::vector<X_vector> Xresidual;
  for (size_t b = 0; b < m; b++) {
    Xvector.push_back(X_vector(Chi, b));
    Xresidual.push_back(X_vector(residuals, b));
  }
  // If DFT, initialize the XCOperator<double,3>
  std::vector<XNonlinearSolver<X_vector, double, X_space_allocator>>
      kain_x_space;
  size_t nkain = m;  // (r_params.omega() != 0.0) ? 2 * m : m;
  for (size_t b = 0; b < nkain; b++) {
    kain_x_space.push_back(
        XNonlinearSolver<X_vector, double, X_space_allocator>(
            X_space_allocator(world, n), false));
    if (r_params.kain()) kain_x_space[b].set_maxsub(r_params.maxsub());
  }
  //
  double omega_n = r_params.omega();
  omega_n = abs(omega_n);
  omega[0] = omega_n;
  // We compute with positive frequencies
  print("Warning input frequency is assumed to be positive");
  print("Computing at positive frequency omega = ", omega_n);
  double x_shifts{0};
  double y_shifts{0};
  // if less negative orbital energy + frequency is positive or greater than 0
  print("Ground State orbitals");
  print(ground_energies);
  if ((ground_energies[n - 1] + omega_n) >= 0.0) {
    // Calculate minimum shift needed such that \eps + \omega + shift < 0
    print("*** we are shifting just so you know!!!");
    x_shifts = -(omega_n + ground_energies[n - 1]);
  }
  // Construct BSH operators
  std::vector<std::shared_ptr<real_convolution_3d>> bsh_x_operators =
      CreateBSHOperatorPropertyVector(
          world, x_shifts, ground_energies, omega_n, .001, 1e-6);
  std::vector<std::shared_ptr<real_convolution_3d>> bsh_y_operators;

  // Negate omega to make this next set of BSH operators \eps - omega
  if (omega_n != 0.0) {
    omega_n = -omega_n;
    bsh_y_operators = CreateBSHOperatorPropertyVector(
        world, y_shifts, ground_energies, omega_n, .001, 1e-6);
    omega_n = -omega_n;
  }
  // create couloumb operator
  // Now to iterate
  // while (iteration < r_params.maxiter() and !converged) {

  for (int iter = 0; iter < r_params.maxiter(); ++iter) {
    // Basic output
    if (r_params.print_level() >= 1) {
      molresponse::start_timer(world);
      if (world.rank() == 0)
        printf("\n   Iteration %d at time %.1fs\n",
               static_cast<int>(iteration),
               wall_time());
      if (world.rank() == 0) print(" -------------------------------");
    }

    // If omega = 0.0, x = y
    if (r_params.omega() == 0.0) Chi.Y = Chi.X.copy();
    // Save current to old
    // deep copy of response functions
    old_Chi = Chi.copy();

    X_space theta_X = Compute_Theta_X(world, Chi, xc, r_params.calc_type());
    // Apply shifts and rhs
    theta_X.X += Chi.X * x_shifts;
    theta_X.X += PQ.X;
    theta_X.X = theta_X.X * -2;
    theta_X.X.truncate_rf();

    if (r_params.omega() != 0.0) {
      theta_X.Y += PQ.Y;
      theta_X.Y = theta_X.Y * -2;
      theta_X.Y.truncate_rf();
    }
    // Load Balancing
    if (world.size() > 1 && (iteration < 2 or iteration % 5 == 0)) {
      // Start a timer
      if (r_params.print_level() >= 1) molresponse::start_timer(world);
      if (world.rank() == 0) print("");  // Makes it more legible
      // (TODO Ask Robert about load balancing)
      LoadBalanceDeux<3> lb(world);
      for (size_t j = 0; j < n; j++) {
        for (size_t k = 0; k < r_params.n_states(); k++) {
          lb.add_tree(Chi.X[k][j], lbcost<double, 3>(1.0, 8.0), true);
          lb.add_tree(theta_X.X[k][j], lbcost<double, 3>(1.0, 8.0), true);
        }
      }
      FunctionDefaults<3>::redistribute(world, lb.load_balance(2));
      if (r_params.print_level() >= 1)
        molresponse::end_timer(world, "Load balancing:");
    }
    X_space temp(world, m, n);
    // Debugging output
    if (r_params.print_level() >= 2) {
      if (world.rank() == 0)
        print("   Norms of RHS x components before application of BSH:");
      print_norms(world, theta_X.X);

      if (r_params.omega() != 0.0) {
        if (world.rank() == 0)
          print("   Norms of RHS y components before application BSH:");
        print_norms(world, theta_X.Y);
      }
    }
    // apply bsh
    bsh_x_resp = apply(world, bsh_x_operators, theta_X.X);
    if (r_params.omega() != 0.0)
      bsh_y_resp = apply(world, bsh_y_operators, theta_X.Y);

    // Project out ground state
    for (size_t i = 0; i < m; i++) bsh_x_resp[i] = projector(bsh_x_resp[i]);
    if (not r_params.tda()) {
      for (size_t i = 0; i < m; i++) bsh_y_resp[i] = projector(bsh_y_resp[i]);
    }

    temp.X = bsh_x_resp.copy();
    if (r_params.omega() != 0.0) {
      temp.Y = bsh_y_resp.copy();
    } else {
      temp.Y = temp.X.copy();
    }
    temp.X.truncate_rf();
    temp.Y.truncate_rf();
    // compute differences
    x_differences = old_Chi.X - temp.X;
    if (omega_n != 0.0) y_differences = old_Chi.Y - temp.Y;
    // Next calculate 2-norm of these vectors of differences
    // Remember: the entire vector is one state
    for (size_t i = 0; i < m; i++) x_norms(i) = norm2(world, x_differences[i]);
    if (omega_n != 0.0) {
      for (size_t i = 0; i < m; i++)
        y_norms(i) = norm2(world, y_differences[i]);
    }

    // Basic output
    if (r_params.print_level() >= 0 and world.rank() == 0) {
      if (omega_n != 0.0) {
        std::cout << "res " << iteration << " X :";
        for (size_t i(0); i < m; i++) {
          std::cout << x_norms[i] << "  ";
        }
        std::cout << " Y :";
        for (size_t i(0); i < m; i++) {
          std::cout << y_norms[i] << "  ";
        }
        std::cout << endl;
      } else {
        print("resX ", iteration, " :", x_norms);
      }
    }

    if (r_params.kain()) {
      residuals = X_space(x_differences, y_differences);
      // seperate X_space vectors into individual vectors
      for (size_t b = 0; b < m; b++) {
        Xvector[b] = (X_vector(temp, b));
        Xresidual[b] = (X_vector(residuals, b));
      }

      molresponse::start_timer(world);
      for (size_t b = 0; b < nkain; b++) {
        X_vector kain_X = kain_x_space[b].update(
            Xvector[b], Xresidual[b], FunctionDefaults<3>::get_thresh(), 3.0);
        temp.X[b].assign(kain_X.X[0].begin(), kain_X.X[0].end());
        temp.Y[b].assign(kain_X.Y[0].begin(), kain_X.Y[0].end());
      }
      molresponse::end_timer(world, " KAIN update:");
    }
    if (iteration > 0) {
      for (size_t b = 0; b < m; b++) {
        do_step_restriction(world, old_Chi.X[b], temp.X[b], "x_response");
        if (omega_n != 0.0) {
          do_step_restriction(world, old_Chi.Y[b], temp.X[b], "y_response");
        }
      }
    }
    // print x norms
    temp.X.truncate_rf();
    if (omega_n == 0.0) temp.Y = temp.X.copy();
    if (omega_n != 0.0) temp.Y.truncate_rf();
    // temp-> Chi
    Chi = temp.copy();
    if (r_params.print_level() >= 1) {
      print("Chi.x norms in iteration after truncate: ", iteration);
      print(Chi.X.norm2());

      print("Chi.y norms in iteration after truncate: ", iteration);
      print(Chi.Y.norm2());
    }

    // Check convergence
    if (std::max(x_norms.absmax(), y_norms.absmax()) < dconv and
        iteration > 0) {
      if (r_params.print_level() >= 1)
        molresponse::end_timer(world, "This iteration:");
      if (world.rank() == 0) print("\n   Converged!");
      converged = true;
      break;
    }
    // Update counter
    iteration += 1;
    // X_space PQ(P, rhs_y);
    Tensor<double> G = -2 * inner(Chi, PQ);
    // Polarizability Tensor
    print("Polarizability Tensor");
    print(G);
    // Save
    if (r_params.save()) {
      molresponse::start_timer(world);
      save(world, r_params.save_file());
      if (r_params.print_level() >= 1) molresponse::end_timer(world, "Save:");
    }
    // Basic output
    if (r_params.print_level() >= 1)
      molresponse::end_timer(world, " This iteration:");
    // plot orbitals
    if (r_params.plot_all_orbitals()) {
      PlotGroundandResponseOrbitals(
          world, iteration, Chi.X, Chi.Y, r_params, g_params);
    }
    for (size_t b = 0; b < m; b++) {
      Xvector[b] = (X_vector(world, 0));
      Xresidual[b] = (X_vector(world, 0));
    }

  }  // while converged
}
