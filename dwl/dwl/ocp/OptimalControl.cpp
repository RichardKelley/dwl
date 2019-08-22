#include <dwl/ocp/OptimalControl.h>


namespace dwl
{

namespace ocp
{

OptimalControl::OptimalControl() : dynamical_system_(NULL),
		is_added_dynamic_system_(false), is_added_constraint_(false), is_added_cost_(false),
		terminal_constraint_dimension_(0), horizon_(1)
{

}


OptimalControl::~OptimalControl()
{
	delete dynamical_system_;

	typedef std::vector<Constraint<WholeBodyState>*>::iterator ConstraintItr;
	typedef std::vector<Cost*>::iterator CostItr;
	if (is_added_constraint_) {
		for (ConstraintItr i = constraints_.begin(); i != constraints_.end(); i++)
			delete *i;
	}

	if (is_added_cost_) {
		for (CostItr i = costs_.begin(); i != costs_.end(); i++)
			delete *i;
	}
}


void OptimalControl::init(bool only_soft_constraints)
{
	// Reading the state dimension
	state_dimension_ = dynamical_system_->getDimensionOfState();

	// Initializing the constraint dimension
	constraint_dimension_ = 0;
	if (!only_soft_constraints) {
		if (!dynamical_system_->isSoftConstraint())
			constraint_dimension_ += dynamical_system_->getConstraintDimension();
		if (is_added_constraint_) {
			for (unsigned int i = 0; i < constraints_.size(); i++) {
				if (!constraints_[i]->isSoftConstraint())
					constraint_dimension_ += constraints_[i]->getConstraintDimension();
			}
		}
		// Initializing the terminal constraint dimension
		if (dynamical_system_->isFullTrajectoryOptimization())
			terminal_constraint_dimension_ = dynamical_system_->getTerminalConstraintDimension();
	} else { // Imposing all the constraint as soft
		// Defining the dynamical system constraint as soft
		dynamical_system_->defineAsSoftConstraint();

		// Defining the all constraints as soft
		for (unsigned int i = 0; i < constraints_.size(); i++)
			constraints_[i]->defineAsSoftConstraint();
	}
}


void OptimalControl::setStartingTrajectory(WholeBodyTrajectory& initial_trajectory)
{
	//TODO should convert to the defined horizon and time step integration
	motion_solution_ = initial_trajectory;
}


void OptimalControl::getStartingPoint(double* decision, int decision_dim)
{
	// Eigen interfacing to raw buffers
	Eigen::Map<Eigen::VectorXd> full_initial_point(decision, decision_dim);

	if (motion_solution_.size() == 0) {
		// Getting the initial and ending locomotion state
		WholeBodyState starting_system_state = dynamical_system_->getInitialState();
		WholeBodyState ending_system_state = dynamical_system_->getTerminalState();

		// Defining splines //TODO for the time being only cubic interpolation is OK
		unsigned int num_joints = dynamical_system_->getFloatingBaseSystem().getJointDoF();
		std::vector<math::CubicSpline> base_spline, joint_spline;
		base_spline.resize(6);
		joint_spline.resize(num_joints);

		// Computing a starting point from interpolation of the starting and ending states
		WholeBodyState current_system_state = starting_system_state;
		math::Spline::Point current_point;
		current_system_state.duration = 1. / horizon_;
		for (unsigned int k = 0; k < horizon_; k++) {
			current_system_state.time = k * current_system_state.duration;

			// Base interpolation
			for (unsigned int base_idx = 0; base_idx < 6; base_idx++) {
				if (k == 0) {
					// Initialization of the base spline
					math::Spline::Point starting_base(starting_system_state.base_pos(base_idx));
					math::Spline::Point ending_base(ending_system_state.base_pos(base_idx));
					base_spline[base_idx].setBoundary(0, 1, starting_base, ending_base);
				} else {
					// Getting and setting the interpolated point
					base_spline[base_idx].getPoint(current_system_state.time, current_point);
					current_system_state.base_pos(base_idx) = current_point.x;
				}
			}

			// Joint interpolations
			for (unsigned int joint_idx = 0; joint_idx < num_joints; joint_idx++) {
				if (k == 0) {
					// Initialization of the joint spline
					math::Spline::Point starting_joint(starting_system_state.joint_pos(joint_idx));
					math::Spline::Point ending_joint(ending_system_state.joint_pos(joint_idx));
					joint_spline[joint_idx].setBoundary(0, 1, starting_joint, ending_joint);
				} else {
					// Getting and setting the interpolated point
					joint_spline[joint_idx].getPoint(current_system_state.time, current_point);
					current_system_state.joint_pos(joint_idx) = current_point.x;
				}
			}

			// Getting the current state vector
			Eigen::VectorXd current_state;
			dynamical_system_->fromWholeBodyState(current_state, current_system_state);

			// Adding the current state vector
			full_initial_point.segment(k * state_dimension_, state_dimension_) = current_state;
		}
	} else {
		// Defining the current locomotion solution as starting point
		unsigned int state_dimension = dynamical_system_->getDimensionOfState();
		for (unsigned int k = 0; k < horizon_; k++) {
			Eigen::VectorXd current_state;
			dynamical_system_->fromWholeBodyState(current_state, motion_solution_[k]);

			full_initial_point.segment(k * state_dimension, state_dimension) = current_state;
		}
	}
}


void OptimalControl::evaluateBounds(double* decision_lbound, int decision_dim1,
									double* decision_ubound, int decision_dim2,
									double* constraint_lbound, int constraint_dim1,
									double* constraint_ubound, int constraint_dim2)
{
	// Eigen interfacing to raw buffers
	Eigen::Map<Eigen::VectorXd> full_state_lower_bound(decision_lbound, decision_dim1);
	Eigen::Map<Eigen::VectorXd> full_state_upper_bound(decision_ubound, decision_dim2);
	Eigen::Map<Eigen::VectorXd> full_constraint_lower_bound(constraint_lbound, constraint_dim1);
	Eigen::Map<Eigen::VectorXd> full_constraint_upper_bound(constraint_ubound, constraint_dim2);


	// Getting the lower and upper bound of the locomotion state
	WholeBodyState locomotion_lower_bound, locomotion_upper_bound;
	dynamical_system_->getStateBounds(locomotion_lower_bound, locomotion_upper_bound);

	// Converting locomotion state bounds to state bounds
	Eigen::VectorXd state_lower_bound, state_upper_bound;
	dynamical_system_->fromWholeBodyState(state_lower_bound, locomotion_lower_bound);
	dynamical_system_->fromWholeBodyState(state_upper_bound, locomotion_upper_bound);

	// Getting the lower and upper constraint bounds for a certain time
	if (constraint_dimension_ != 0) {
		unsigned int index = 0;
		unsigned int num_constraints = constraints_.size();
		Eigen::VectorXd constraint_lower_bound = Eigen::VectorXd::Zero(constraint_dimension_);
		Eigen::VectorXd constraint_upper_bound = Eigen::VectorXd::Zero(constraint_dimension_);
		for (unsigned int j = 0; j < num_constraints + 1; j++) {
			Eigen::VectorXd lower_bound, upper_bound;
			unsigned int current_bound_dim = 0;
			if (j == 0) {
				if (!dynamical_system_->isSoftConstraint()) {
					// Getting the dynamical constraint bounds
					dynamical_system_->getBounds(lower_bound, upper_bound);

					// Checking the bound dimension
					current_bound_dim = dynamical_system_->getConstraintDimension();
					if (current_bound_dim != (unsigned) lower_bound.size()) {
						printf(RED_ "FATAL: the bound dimension of %s constraint is not consistent\n"
								COLOR_RESET, dynamical_system_->getName().c_str());
						exit(EXIT_FAILURE);
					}
				}
			} else if (!constraints_[j-1]->isSoftConstraint()) {
				// Getting the set of constraint bounds
				constraints_[j-1]->getBounds(lower_bound, upper_bound);

				// Checking the bound dimension
				current_bound_dim = constraints_[j-1]->getConstraintDimension();
				if (current_bound_dim != (unsigned) lower_bound.size()) {
					printf(RED_ "FATAL: the bound dimension of %s constraint is not consistent\n"
							COLOR_RESET, constraints_[j-1]->getName().c_str());
					exit(EXIT_FAILURE);
				}
			}

			// Setting the full constraint vector
			constraint_lower_bound.segment(index, current_bound_dim) = lower_bound;
			constraint_upper_bound.segment(index, current_bound_dim) = upper_bound;

			index += current_bound_dim;
		}

		// Setting the full-constraint lower and upper bounds for the predefined horizon
		for (unsigned int k = 0; k < horizon_; k++) {
			// Setting dynamic system bounds
			full_constraint_lower_bound.segment(k * constraint_dimension_,
												constraint_dimension_) = constraint_lower_bound;
			full_constraint_upper_bound.segment(k * constraint_dimension_,
												constraint_dimension_) = constraint_upper_bound;
		}
	}

	// Setting the full-state lower and upper bounds for the predefined horizon
	for (unsigned int k = 0; k < horizon_; k++) {
		// Setting state bounds
		full_state_lower_bound.segment(k * state_dimension_, state_dimension_) = state_lower_bound;
		full_state_upper_bound.segment(k * state_dimension_, state_dimension_) = state_upper_bound;
	}

	// Computing the terminal bounds in case of full trajectory optimization
	if (dynamical_system_->isFullTrajectoryOptimization()) {
		// Computing the terminal bounds
		Eigen::VectorXd terminal_lower_bound, terminal_upper_bound;
		dynamical_system_->getTerminalBounds(terminal_lower_bound, terminal_upper_bound);

		// Checking the terminal bound dimension
		if (terminal_constraint_dimension_ != (unsigned) terminal_lower_bound.size()) {
			printf(RED_ "FATAL: the terminal bound dimension is not consistent\n");
			exit(EXIT_FAILURE);
		}

		// Setting the terminal bounds
		full_constraint_lower_bound.segment(horizon_ * constraint_dimension_,
											terminal_constraint_dimension_) = terminal_lower_bound;
		full_constraint_upper_bound.segment(horizon_ * constraint_dimension_,
											terminal_constraint_dimension_) = terminal_upper_bound;
	}
}


void OptimalControl::evaluateConstraints(double* constraint, int constraint_dim,
										 const double* decision, int decision_dim)
{
	// Eigen interfacing to raw buffers
	const Eigen::Map<const Eigen::VectorXd> decision_var(decision, decision_dim);
	Eigen::Map<Eigen::VectorXd> full_constraint(constraint, constraint_dim);
	full_constraint.setZero();

	if (state_dimension_ != (decision_var.size() / horizon_)) {
		printf(RED_ "FATAL: the state and decision dimensions are not consistent\n" COLOR_RESET);
		exit(EXIT_FAILURE);
	}

	// Getting the initial conditions of the locomotion state
	WholeBodyState locomotion_initial_cond = dynamical_system_->getInitialState();

	// Setting the initial state
	unsigned int num_constraints = constraints_.size();
	for (unsigned int j = 0; j < num_constraints + 1; j++) {
		if (j == 0) // dynamic system constraint
			dynamical_system_->setLastState(locomotion_initial_cond);
		else
			constraints_[j-1]->setLastState(locomotion_initial_cond);
	}

	// Computing the active and inactive constraints for a predefined horizon
	WholeBodyState system_state(dynamical_system_->getFloatingBaseSystem().getJointDoF());
	Eigen::VectorXd decision_state = Eigen::VectorXd::Zero(state_dimension_);
	unsigned int index = 0;
	for (unsigned int k = 0; k < horizon_; k++) {
		// Converting the decision variable for a certain time to a robot state
		decision_state = decision_var.segment(k * state_dimension_, state_dimension_);
		dynamical_system_->toWholeBodyState(system_state, decision_state);

		// Adding the time information in cases that time is not a decision variable
		if (dynamical_system_->isFixedStepIntegration())
			system_state.duration = dynamical_system_->getFixedStepTime();
		system_state.time += system_state.duration;

		// Computing the constraints for a certain time
		if (constraint_dimension_ != 0) {
			for (unsigned int j = 0; j < num_constraints + 1; j++) {
				Eigen::VectorXd constraint;
				unsigned int current_constraint_dim = 0;
				if (j == 0) {// dynamic system constraint
					// Evaluating the dynamical constraint
					if (!dynamical_system_->isSoftConstraint()) {
						dynamical_system_->compute(constraint, system_state);
						dynamical_system_->setLastState(system_state);

						// Checking the constraint dimension
						current_constraint_dim = dynamical_system_->getConstraintDimension();
						if (current_constraint_dim != (unsigned) constraint.size()) {
							printf(RED_ "FATAL: the constraint dimension of %s constraint is not consistent\n"
									COLOR_RESET, dynamical_system_->getName().c_str());
							exit(EXIT_FAILURE);
						}
					}
				} else {
					// Evaluating the constraints
					if (!constraints_[j-1]->isSoftConstraint()) {
						constraints_[j-1]->compute(constraint, system_state);
						constraints_[j-1]->setLastState(system_state);

						// Checking the constraint dimension
						current_constraint_dim = constraints_[j-1]->getConstraintDimension();
						if (current_constraint_dim != (unsigned) constraint.size()) {
							printf(RED_ "FATAL: the constraint dimension of %s constraint is not consistent\n"
									COLOR_RESET, constraints_[j-1]->getName().c_str());
							exit(EXIT_FAILURE);
						}
					}
				}

				// Setting in the full constraint vector
				full_constraint.segment(index, current_constraint_dim) = constraint;

				index += current_constraint_dim;
			}
		}

		// Computing the terminal constraint in case of full trajectory optimization
		if (dynamical_system_->isFullTrajectoryOptimization()) {
			if (k == horizon_ - 1) {
				Eigen::VectorXd constraint;
				dynamical_system_->computeTerminalConstraint(constraint, system_state);

				// Setting in the full constraint vector
				full_constraint.segment(index, terminal_constraint_dimension_) = constraint;

				index += terminal_constraint_dimension_;
			}
		}
	}

	// Resetting the state buffer
	for (unsigned int j = 0; j < num_constraints + 1; j++) {
		if (j == 0) // dynamic system constraint
			dynamical_system_->resetStateBuffer();
		else
			constraints_[j-1]->resetStateBuffer();
	}
}


void OptimalControl::evaluateCosts(double& cost,
								   const double* decision, int decision_dim)
{
	// Eigen interfacing to raw buffers
	const Eigen::Map<const Eigen::VectorXd> decision_var(decision, decision_dim);

	if (state_dimension_ != (decision_var.size() / horizon_)) {
		printf(RED_ "FATAL: the state and decision dimensions are not consistent\n" COLOR_RESET);
		exit(EXIT_FAILURE);
	}

	// Initializing the cost value
	cost = 0;

	// Getting the initial conditions of the locomotion state
	WholeBodyState locomotion_initial_cond = dynamical_system_->getInitialState();

	// Setting the initial state
	unsigned int num_constraints = constraints_.size();
	for (unsigned int j = 0; j < num_constraints + 1; j++) {
		if (j == 0) // dynamic system constraint
			dynamical_system_->setLastState(locomotion_initial_cond);
		else
			constraints_[j-1]->setLastState(locomotion_initial_cond);
	}

	// Computing the cost for predefined horizon
	WholeBodyState system_state(dynamical_system_->getFloatingBaseSystem().getJointDoF());
	for (unsigned int k = 0; k < horizon_; k++) {
		// Converting the decision variable for a certain time to a robot state
		Eigen::VectorXd decision_state = decision_var.segment(k * state_dimension_, state_dimension_);
		dynamical_system_->toWholeBodyState(system_state, decision_state);

		// Adding the time information in cases that time is not a decision variable
		if (dynamical_system_->isFixedStepIntegration())
			system_state.duration = dynamical_system_->getFixedStepTime();
		system_state.time += system_state.duration;

		// Computing the cost function for a certain time
		double simple_cost;
		unsigned int num_cost_functions = costs_.size();
		for (unsigned int j = 0; j < num_cost_functions; j++) {
			costs_[j]->compute(simple_cost, system_state);
			cost += simple_cost;
		}

		// Computing the soft-constraints for a certain time
		for (unsigned int j = 0; j < num_constraints + 1; j++) {
			if (j == 0) {// dynamic system constraint
				if (dynamical_system_->isSoftConstraint()) {
					dynamical_system_->computeSoft(simple_cost, system_state);
					dynamical_system_->setLastState(system_state);
					cost += simple_cost;
				}
			} else if (constraints_[j-1]->isSoftConstraint()) {
				constraints_[j-1]->computeSoft(simple_cost, system_state);
				constraints_[j-1]->setLastState(system_state);
				cost += simple_cost;
			}
		}
	}

	// Resetting the state buffer
	for (unsigned int j = 0; j < num_constraints + 1; j++) {
		if (j == 0) // dynamic system constraint
			dynamical_system_->resetStateBuffer();
		else
			constraints_[j-1]->resetStateBuffer();
	}
}


WholeBodyTrajectory& OptimalControl::evaluateSolution(const Eigen::Ref<const Eigen::VectorXd>& solution)
{
	// Getting the state dimension
	unsigned int state_dim = dynamical_system_->getDimensionOfState();

	// Recording the solution
	motion_solution_.clear();
	motion_solution_.push_back(dynamical_system_->getInitialState());
	Eigen::VectorXd decision_state = Eigen::VectorXd::Zero(state_dim);
	double current_time = dynamical_system_->getInitialState().time;
	for (unsigned int k = 0; k < horizon_; k++) {
		// Converting the decision variable for a certain time to a robot state
		WholeBodyState system_state;
		decision_state = solution.segment(k * state_dim, state_dim);
		dynamical_system_->toWholeBodyState(system_state, decision_state);

		// Setting the time information in cases where time is not a decision variable
		if (dynamical_system_->isFixedStepIntegration())
			system_state.duration = dynamical_system_->getFixedStepTime();
		current_time += system_state.duration;
		system_state.time = current_time;


		// Setting the acceleration information in cases where the accelerations are not decision
		// variables
		WholeBodyState last_system_state;
		if (k == 0)
			last_system_state = dynamical_system_->getInitialState();
		else {
			Eigen::VectorXd last_decision_state = solution.segment((k - 1) * state_dim, state_dim);
			dynamical_system_->toWholeBodyState(last_system_state, last_decision_state);
		}

		if (system_state.base_acc.isZero() && system_state.joint_acc.isZero()) {
			// Computing (estimating) the accelerations
			system_state.base_acc = (system_state.base_vel - last_system_state.base_vel) /
					system_state.duration;
			system_state.joint_acc = (system_state.joint_vel - last_system_state.joint_vel) /
					system_state.duration;
		}


		// Setting the contact information in cases where time is not a decision variable
		urdf_model::LinkID contacts = dynamical_system_->getFloatingBaseSystem().getEndEffectors();
		rbd::BodySelector contact_names = dynamical_system_->getFloatingBaseSystem().getEndEffectorNames();
		for (urdf_model::LinkID::const_iterator contact_it = contacts.begin();
				contact_it != contacts.end(); contact_it++) {
			std::string name = contact_it->first;

			// Defining the contact iterators
			dwl::rbd::BodyVectorXd::const_iterator pos_it, vel_it, acc_it;
			dwl::rbd::BodyVector6d::const_iterator eff_it;

			// Filling the desired contact state
			pos_it = system_state.contact_pos.find(name);
			vel_it = system_state.contact_vel.find(name);
			acc_it = system_state.contact_acc.find(name);
			eff_it = system_state.contact_eff.find(name);
			if (pos_it != system_state.contact_pos.end()) {
				if (system_state.contact_pos[name].isZero()) {
					dynamical_system_->getKinematics().computeForwardKinematics(system_state.contact_pos,
																				system_state.base_pos,
																				system_state.joint_pos,
																				contact_names, rbd::Linear);
				}
			}
			if (vel_it != system_state.contact_vel.end()) {
				if (system_state.contact_vel[name].isZero()) {
					dynamical_system_->getKinematics().computeVelocity(system_state.contact_vel,
															   	   	   system_state.base_pos,
																	   system_state.joint_pos,
																	   system_state.base_vel,
																	   system_state.joint_vel,
																	   contact_names, rbd::Linear);
				}
			}
			if (acc_it != system_state.contact_acc.end()) {
				if (system_state.contact_acc[name].isZero()) {
					dynamical_system_->getKinematics().computeAcceleration(system_state.contact_acc,
															   	   	   	   system_state.base_pos,
																		   system_state.joint_pos,
																		   system_state.base_vel,
																		   system_state.joint_vel,
																		   system_state.base_acc,
																		   system_state.joint_acc,
																		   contact_names, rbd::Linear);
				}
			}
		}

		// Pushing the current state
		motion_solution_.push_back(system_state);



		std::cout << "-------------------------------------" << std::endl;
		std::cout << "x = " << decision_state.transpose() << std::endl;
		std::cout << "time = " << system_state.time << std::endl;
		std::cout << "duration = " << system_state.duration << std::endl;
		std::cout << "base_pos = " << system_state.base_pos.transpose() << std::endl;
		std::cout << "joint_pos = " << system_state.joint_pos.transpose() << std::endl;
		std::cout << "base_vel = " << system_state.base_vel.transpose() << std::endl;
		std::cout << "joint_vel = " << system_state.joint_vel.transpose() << std::endl;
		std::cout << "base_acc = " << system_state.base_acc.transpose() << std::endl;
		std::cout << "joint_acc = " << system_state.joint_acc.transpose() << std::endl;
		std::cout << "joint_eff = " << system_state.joint_eff.transpose() << std::endl;
		for (urdf_model::LinkID::const_iterator contact_it = contacts.begin();
				contact_it != contacts.end(); contact_it++) {
			std::string name = contact_it->first;

			std::cout << "contact_pos[" << name << "] = " << system_state.contact_pos[name].transpose() << std::endl;
			std::cout << "contact_vel[" << name << "] = " << system_state.contact_vel[name].transpose() << std::endl;
			std::cout << "contact_acc[" << name << "] = " << system_state.contact_acc[name].transpose() << std::endl;
			std::cout << "contact_for[" << name << "] = " << system_state.contact_eff[name].transpose() << std::endl;
		}
		std::cout << "-------------------------------------" << std::endl;
	}

	return motion_solution_;
}


void OptimalControl::addDynamicalSystem(DynamicalSystem* dynamical_system)
{
	if (is_added_dynamic_system_) {
		printf(YELLOW_ "Could not added two dynamical systems\n" COLOR_RESET);
		return;
	}

	printf(GREEN_ "Adding the %s dynamical system\n" COLOR_RESET, dynamical_system->getName().c_str());
	dynamical_system_ = dynamical_system;
	is_added_dynamic_system_ = true;
}


void OptimalControl::removeDynamicalSystem()
{
	if (is_added_dynamic_system_)
		is_added_dynamic_system_ = false;
	else
		printf(YELLOW_ "There was not added a dynamical system\n" COLOR_RESET);
}


void OptimalControl::addConstraint(Constraint<WholeBodyState>* constraint)
{
	printf(GREEN_ "Adding the %s constraint\n" COLOR_RESET, constraint->getName().c_str());
	constraints_.push_back(constraint);

	if (!is_added_constraint_)
		is_added_constraint_ = true;
}


void OptimalControl::removeConstraint(std::string constraint_name)
{
	if (is_added_constraint_) {
		unsigned int num_constraints = constraints_.size();
		for (unsigned int i = 0; i < num_constraints; i++) {
			if (constraint_name == constraints_[i]->getName().c_str()) {
				printf(GREEN_ "Removing the %s constraint\n" COLOR_RESET, constraints_[i]->getName().c_str());

				// Deleting the constraint
				delete constraints_.at(i);
				constraints_.erase(constraints_.begin() + i);

				return;
			}
		}
	} else {
		printf(YELLOW_ "Could not removed the %s constraint because has not been added an "
				"constraint\n" COLOR_RESET,	constraint_name.c_str());
		return;
	}
}


void OptimalControl::addCost(Cost* cost)
{
	printf(GREEN_ "Adding the %s cost\n" COLOR_RESET, cost->getName().c_str());

	costs_.push_back(cost);
	is_added_cost_ = true;
}


void OptimalControl::removeCost(std::string cost_name)
{
	if (is_added_cost_) {
		if (costs_.size() == 0)
			printf(YELLOW_ "Could not removed the %s cost because there is not cost\n" COLOR_RESET,
					cost_name.c_str());
		else {
			unsigned int costs_size = costs_.size();
			for (unsigned int i = 0; i < costs_size; i++) {
				if (cost_name == costs_[i]->getName().c_str()) {
					printf(GREEN_ "Removing the %s cost\n" COLOR_RESET, costs_[i]->getName().c_str());

					// Deleting the cost
					delete costs_.at(i);
					costs_.erase(costs_.begin() + i);

					return;
				}
				else if (i == costs_.size() - 1) {
					printf(YELLOW_ "Could not removed the %s cost\n" COLOR_RESET, cost_name.c_str());
				}
			}
		}
	}
	else
		printf(YELLOW_ "Could not removed the %s cost because has not been added an cost\n"
				COLOR_RESET, cost_name.c_str());
}


void OptimalControl::setHorizon(unsigned int horizon)
{
	if (horizon == 0)
		horizon_ = 1;
	else
		horizon_ = horizon;
}


DynamicalSystem* OptimalControl::getDynamicalSystem()
{
	return dynamical_system_;
}


std::vector<Constraint<WholeBodyState>*> OptimalControl::getConstraints()
{
	return constraints_;
}


std::vector<Cost*> OptimalControl::getCosts()
{
	return costs_;
}


const unsigned int& OptimalControl::getHorizon()
{
	return horizon_;
}

} //@namespace ocp
} //@namespace dwl
