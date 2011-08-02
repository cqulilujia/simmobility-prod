/* Copyright Singapore-MIT Alliance for Research and Technology */

#pragma once

#include <vector>

#include "Base.hpp"

namespace sim_mob
{

//Forward declarations
class Node;


namespace aimsun
{

//Forward declarations
class Section;


///An AIMSUN road intersection or segment intersection.
class Node : public Base {
public:
	double xPos;
	double yPos;
	bool isIntersection;

	Node() : Base() {}

	//Decorated data
	std::vector<Section*> sectionsAtNode;
	bool candidateForSegmentNode;

	//Reference to saved object
	sim_mob::Node* generatedNode;

};


}
}
