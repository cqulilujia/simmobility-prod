//Copyright (c) 2013 Singapore-MIT Alliance for Research and Technology
//Licensed under the terms of the MIT License, as described in the file:
//   license.txt   (http://opensource.org/licenses/MIT)

/*
 * AndroidClientRegistration.hpp
 *
 *  Created on: May 20, 2013
 *      Author: vahid
 */

#pragma once

#include "entities/commsim/communicator/client-registration/base/ClinetRegistrationHandler.hpp"
#include "entities/commsim/communicator/broker/Broker.hpp"

namespace sim_mob {

class AndroidClientRegistration: public sim_mob::ClientRegistrationHandler {
	sim_mob::AgentsMap<std::string>::type usedAgents;
public:
	AndroidClientRegistration(/*ConfigParams::ClientType type_ = ConfigParams::ANDROID_EMULATOR*/);
	bool handle(sim_mob::Broker&, sim_mob::ClientRegistrationRequest);
	virtual ~AndroidClientRegistration();
};

} /* namespace sim_mob */