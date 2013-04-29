#include "geo10-pimpl.hpp"

using namespace sim_mob::xml;


void sim_mob::xml::linkAndCrossings_t_pimpl::pre ()
{
	model.clear();
}

sim_mob::LinkAndCrossingC sim_mob::xml::linkAndCrossings_t_pimpl::post_linkAndCrossings_t ()
{
	return model;
}

void sim_mob::xml::linkAndCrossings_t_pimpl::linkAndCrossing (sim_mob::LinkAndCrossing value)
{
	model.push_back(value);
}

