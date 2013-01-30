#include "conf1-pimpl.hpp"

using namespace sim_mob::conf;

#include <stdexcept>
#include <iostream>

using std::string;
using std::pair;

void sim_mob::conf::pedestrians_pimpl::pre ()
{
}

void sim_mob::conf::pedestrians_pimpl::post_pedestrians ()
{
}

void sim_mob::conf::pedestrians_pimpl::database_loader (const std::pair<std::string, std::string>& value)
{
	config->simulation().agentsLoaders.push_back(new DbLoader(value.first, value.second));
}

void sim_mob::conf::pedestrians_pimpl::xml_loader (const std::pair<std::string, std::string>& value)
{
	config->simulation().agentsLoaders.push_back(new DbLoader(value.first, value.second));
}

void sim_mob::conf::pedestrians_pimpl::pedestrian (const sim_mob::PedestrianSpec& value)
{
	//To avoid too much waste, we stack multiple agent definitions in a row onto the same loader.
	std::list<DataLoader*>& loaders = config->simulation().agentsLoaders;
	if (loaders.empty() || !dynamic_cast<AgentLoader<PedestrianSpec>*>(loaders.back())) {
		loaders.push_back(new AgentLoader<PedestrianSpec>());
	}
	dynamic_cast<AgentLoader<PedestrianSpec>*>(loaders.back())->addAgentSpec(value);
}




