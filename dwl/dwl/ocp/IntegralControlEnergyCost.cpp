#include <dwl/ocp/IntegralControlEnergyCost.h>


namespace dwl
{

namespace ocp
{

IntegralControlEnergyCost::IntegralControlEnergyCost()
{
	name_ = "integral control energy";
}


IntegralControlEnergyCost::~IntegralControlEnergyCost()
{

}


void IntegralControlEnergyCost::compute(double& cost,
										const WholeBodyState& state)
{
	// Checking sizes
	if (state.joint_eff.size() != locomotion_weights_.joint_eff.size()) {
		printf(RED_ "FATAL: the joint efforts dimensions are not consistent\n" COLOR_RESET);
		exit(EXIT_FAILURE);
	}

	// Computing the control cost
	cost = state.joint_eff.transpose() * locomotion_weights_.joint_eff.asDiagonal() *
			state.joint_eff;
	cost *= state.duration;
}

} //@namespace ocp
} //@namespace dwl
