#ifndef DWL_AdjacencyEnvironment_H
#define DWL_AdjacencyEnvironment_H

#include <environment/EnvironmentInformation.h>
#include <robot/Robot.h>
#include <utils/Math.h>
#include <utils/utils.h>
#include <utils/Orientation.h>


namespace dwl
{

namespace environment
{

/**
 * @class AdjacencyEnvironment
 * @brief Abstract class for building adjacency map of the environment
 */
class AdjacencyEnvironment
{
	public:
		/** @brief Constructor function */
		AdjacencyEnvironment();

		/** @brief Destructor function */
		virtual ~AdjacencyEnvironment();

		/**
		 * @brief Specifies the settings of all components within AdjacencyEnvironment class
		 * @param dwl::environment::EnvironmentInformation* environment Pointer to object that defines the environment
		 */
		void reset(EnvironmentInformation* environment);

		void setCurrentPose(Pose current_pose);

		/**
		 * @brief Abstract method that computes the whole adjacency map, which is required by some algorithms such as Dijkstrap
		 * @param dwl::AdjacencyMap& adjacency_map Adjacency map
		 * @param dwl::Vertex source Source vertex
		 * @param dwl::Vertex target Target vertex
		 */
		virtual void computeAdjacencyMap(AdjacencyMap& adjacency_map, Vertex source, Vertex target);

		/**
		 * @brief Abstract method that gets the successors of a certain vertex
		 * @param std::list<Edge>& successors The successors of a certain vertex
		 * @param dwl::Vertex Current state vertex
		 */
		virtual void getSuccessors(std::list<Edge>& successors, Vertex state_vertex);

		/**
		 * @brief Gets the closest start and goal vertex if it is not belong to the terrain information
		 * @param dwl::Vertex& closest_source The closest vertex to the start
		 * @param dwl::Vertex& closest_target The closest vertex to the goal
		 * @param dwl::Vertex source Start vertex
		 * @param dwl::Vertex target Goal vertex
		 */
		void getTheClosestStartAndGoalVertex(Vertex& closest_source, Vertex& closest_target, Vertex source, Vertex target);

		/**
		 * @brief Gets the closest vertex to a certain vertex
		 * @param dwl::Vertex& closest_vertex The closest vertex
		 * @param dwl::Vertex Current vertex
		 */
		void getTheClosestVertex(Vertex& closest_vertex, Vertex vertex);

		/**
		 * @brief Estimates the heuristic cost from a source to a target vertex
		 * @param Vertex source Source vertex
		 * @param Vertex target Target vertex
		 */
		virtual double heuristicCostEstimate(Vertex source, Vertex target);

		/**
		 * @brief Indicates if it is reached the goal
		 * @param dwl::Vertex target Goal vertex
		 * @param dwl::Vertex current Current vertex
		 * @return bool Returns true if it is reached the goal and false otherwise
		 */
		bool isReachedGoal(Vertex target, Vertex current);

		/**
		 * @brief Indicates if the free of obstacle
		 * @param dwl::Vertex state_vertex State vertex
		 * @param dwl::TypeOfState state_representation State representation
		 * @param bool body Indicates it is desired to use the body space definition
		 * @return bool Returns true if it is free of obstacle, and false otherwise
		 */
		virtual bool isFreeOfObstacle(Vertex state_vertex, TypeOfState state_representation, bool body=false);

		/**
		 * @brief Indicates if it is a lattice representation of the environment
		 * @return bool Returns true if it is a lattice representation and false otherwise
		 */
		bool isLatticeRepresentation();

		/**
		 * @brief Gets the name of the adjacency model
		 * @return std::string Return the name of the adjacency model
		 */
		std::string getName();


	protected:
		/** @brief Name of the adjacency model */
		std::string name_;

		/** @brief Pointer of the EnvironmentInformation object which describes the environment */
		EnvironmentInformation* environment_;

		/** @brief Object of the Robot class for defining robot properties */
		robot::Robot robot_;

		/** @brief Indicates if it is a lattice-based graph */
		bool is_lattice_;

		/** @brief Uncertainty factor which is applicated in un-perceived environment */
		double uncertainty_factor_; // For unknown (non-perceive) areas

		std::map<Vertex, double> orientations_;

		Pose current_pose_;


	private:

};


} //@namespace environment
} //@namespace dwl


#endif