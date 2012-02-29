/* Copyright Singapore-MIT Alliance for Research and Technology */

#include "Person.hpp"

//For debugging
#include "entities/roles/driver/Driver.hpp"
#include "entities/roles/driver/BusDriver.hpp"
#include "entities/roles/pedestrian/Pedestrian.hpp"
#include "util/DebugFlags.hpp"

#include "geospatial/Node.hpp"
#include "entities/misc/TripChain.hpp"

#ifndef SIMMOB_DISABLE_MPI
#include "partitions/PackageUtils.hpp"
#include "partitions/UnPackageUtils.hpp"
#include "entities/misc/TripChain.hpp"
#endif

using std::vector;
using namespace sim_mob;

sim_mob::Person::Person(const MutexStrategy& mtxStrat, int id) :
	Agent(mtxStrat, id), currRole(nullptr), currTripChain(nullptr), firstFrameTick(true) {

}

sim_mob::Person::~Person() {
	safe_delete_item(currRole);
}

Person* sim_mob::Person::GeneratePersonFromPending(const PendingEntity& p)
{
	const ConfigParams& config = ConfigParams::GetInstance();

	//Raw agents can simply be returned as-is.
	if (p.type == ENTITY_RAWAGENT) {
		return p.rawAgent;
	}

	//Create a person object.
	Person* res = new Person(config.mutexStategy);

	//Set its mode.
	if (p.type == ENTITY_DRIVER) {
		res->changeRole(new Driver(res, config.mutexStategy, config.reacTime_LeadingVehicle,config.reacTime_SubjectVehicle,config.reacTime_Gap));
	} else if (p.type == ENTITY_PEDESTRIAN) {
		res->changeRole(new Pedestrian(res, res->getGenerator()));
	} else if (p.type == ENTITY_BUSDRIVER) {
		res->changeRole(new BusDriver(res, config.mutexStategy, config.reacTime_LeadingVehicle,config.reacTime_SubjectVehicle,config.reacTime_Gap));
	} else {
		throw std::runtime_error("PendingEntity currently only supports Drivers and Pedestrians.");
	}

	//Set its origin, destination, and startTime
	res->originNode = p.origin;
	res->destNode = p.dest;
	res->setStartTime(p.start);

	return res;
}

bool sim_mob::Person::update(frame_t frameNumber) {
	try {
		//First, we need to retrieve an UpdateParams subclass appropriate for this Agent.
		unsigned int currTimeMS = frameNumber * ConfigParams::GetInstance().baseGranMS;
		UpdateParams& params = currRole->make_frame_tick_params(frameNumber, currTimeMS);

		//Has update() been called early?
		if(currTimeMS < getStartTime()) {
			//This only represents an error if dynamic dispatch is enabled. Else, we silently skip this update.
			if (!ConfigParams::GetInstance().DynamicDispatchDisabled()) {
				std::stringstream msg;
				msg << "Agent(" << getId() << ") specifies a start time of: " << getStartTime()
						<< " but it is currently: " << currTimeMS
						<< "; this indicates an error, and should be handled automatically.";
				throw std::runtime_error(msg.str().c_str());
			}
			return true;
		}

		//Has update() been called too late?
		if (isToBeRemoved()) {
			//This only represents an error if dynamic dispatch is enabled. Else, we silently skip this update.
			if (!ConfigParams::GetInstance().DynamicDispatchDisabled()) {
				throw std::runtime_error("Agent is already done, but hasn't been removed.");
			}
			return true;
		}

		//Is this the first frame tick for this Agent?
		if (firstFrameTick) {
			//Helper check; not needed once we trust our Workers.
			if (!ConfigParams::GetInstance().DynamicDispatchDisabled()) {
				if (abs(currTimeMS-getStartTime())>=ConfigParams::GetInstance().baseGranMS) {
					std::stringstream msg;
					msg << "Agent was not started within one timespan of its requested start time.";
					msg << "\nStart was: " << getStartTime() << ",  Curr time is: " << currTimeMS << "\n";
					msg << "Agent ID: " << getId() << "\n";
					throw std::runtime_error(msg.str().c_str());
				}
			}

			currRole->frame_init(params);
			firstFrameTick = false;
		}

		//Now perform the main update tick
		if (!isToBeRemoved()) {
			currRole->frame_tick(params);
		}

		//Finally, save the output
		if (!isToBeRemoved()) {
			currRole->frame_tick_output(params);
		}

		//If we're "done", try checking to see if we have any more items in our Trip Chain.
		// This is not strictly the right way to do things (we shouldn't use "isToBeRemoved()"
		// in this manner), but it's the easiest solution that uses the current API.
		if (isToBeRemoved()) {
			checkAndReactToTripChain(currTimeMS);
		}

		//Output if removal requested.
		if (Debug::WorkGroupSemantics && isToBeRemoved()) {
#ifndef SIMMOB_DISABLE_OUTPUT
			boost::mutex::scoped_lock local_lock(sim_mob::Logger::global_mutex);
			std::cout << "Person requested removal: " << (dynamic_cast<Driver*> (currRole) ? "Driver"
					: dynamic_cast<Pedestrian*> (currRole) ? "Pedestrian" : "Other") << "\n";
#endif
		}
	} catch (std::exception& ex) {
		if (ConfigParams::GetInstance().StrictAgentErrors()) {
			//Provide diagnostics for all errors
			std::stringstream msg;
			msg <<"Error updating Agent[" <<getId() <<"]";
			msg <<"\nFrom node: " <<(originNode?originNode->originalDB_ID.getLogItem():"<Unknown>");
			msg <<"\nTo node: " <<(destNode?destNode->originalDB_ID.getLogItem():"<Unknown>");
			msg <<"\n" <<ex.what();
			throw std::runtime_error(msg.str().c_str());
		} else {
			//Add a line to the output file.
#ifndef SIMMOB_DISABLE_OUTPUT
			LogOut("ERROR: Agent " <<getId() <<" encountered an error and will be removed from the simulation." <<std::endl);
#endif
			setToBeRemoved();
		}
	}

	//Return true unless we are scheduled for removal.
	//NOTE: Make sure you set this flag AFTER performing your final output.
	return !isToBeRemoved();
}


void sim_mob::Person::checkAndReactToTripChain(unsigned int currTimeMS) {
	//Do we have at least one more item in our Trip Chain?
	TripChain* currTrip = getTripChain();
	if (!currTrip) {
		return;
	}

	//Delete the previous Role.
	safe_delete_item(currRole);  //TODO: Dangerous! Do this in the worker thread.

	//Update our origin/dest pair.
	//TODO: This might "teleport" us to the origin; might need to fix that later.
	originNode = currTrip->from.location;
	destNode = currTrip->to.location;

	//Set our start time to the NEXT time tick so that frame_init is called
	//  on the first pass through.
	//TODO: This might also be better handled in the worker class.
	setStartTime(currTimeMS + ConfigParams::GetInstance().baseGranMS);
	firstFrameTick = false;

	//Create a new Role based on the trip chain type
	if (currTrip->mode == "Car") {
		//Temp. (Easy to add in later)
		throw std::runtime_error("Cars not supported in Trip Chain role change.");
	} else if (currTrip->mode == "Walk") {
		currRole = new Pedestrian(this, gen);
	} else {
		throw std::runtime_error("Unknown role type for trip chain role change.");
	}

	//Null out our trip chain, remove the "removed" flag, and return
	setTripChain(nullptr);
	clearToBeRemoved();
}



void sim_mob::Person::buildSubscriptionList(vector<BufferedBase*>& subsList) {
	//First, add the x and y co-ordinates
	Agent::buildSubscriptionList(subsList);

	//Now, add our own properties.
	vector<BufferedBase*> roleParams = this->getRole()->getSubscriptionParams();
	for (vector<BufferedBase*>::iterator it = roleParams.begin(); it != roleParams.end(); it++) {
		subsList.push_back(*it);
	}
}

void sim_mob::Person::changeRole(sim_mob::Role* newRole) {
	if (this->currRole) {
		this->currRole->setParent(nullptr);
	}

	this->currRole = newRole;

	if (this->currRole) {
		this->currRole->setParent(this);
	}
}

sim_mob::Role* sim_mob::Person::getRole() const {
	return currRole;
}

#ifndef SIMMOB_DISABLE_MPI
/*
 * package Entity, Agent, Person and Role
 */
void sim_mob::Person::pack(PackageUtils& packageUtil) {
	//package Entity
	//std::cout << "Person package Called" << this->getId() << std::endl;
	sim_mob::Agent::pack(packageUtil);

	//package person
	packageUtil.packBasicData(specialStr);
	sim_mob::TripChain::pack(packageUtil, currTripChain);
	packageUtil.packBasicData(firstFrameTick);
}

void sim_mob::Person::unpack(UnPackageUtils& unpackageUtil) {

	sim_mob::Agent::unpack(unpackageUtil);
	//std::cout << "Person unpackage Called" << this->getId() << std::endl;

	specialStr = unpackageUtil.unpackBasicData<std::string> ();
	currTripChain = sim_mob::TripChain::unpack(unpackageUtil);
	firstFrameTick = unpackageUtil.unpackBasicData<bool> ();
}

void sim_mob::Person::packProxy(PackageUtils& packageUtil) {
	//package Entity
	sim_mob::Agent::pack(packageUtil);

	//package person
	packageUtil.packBasicData(specialStr);
	packageUtil.packBasicData(firstFrameTick);
}

void sim_mob::Person::unpackProxy(UnPackageUtils& unpackageUtil) {
	sim_mob::Agent::unpack(unpackageUtil);

	specialStr = unpackageUtil.unpackBasicData<std::string> ();
	firstFrameTick = unpackageUtil.unpackBasicData<bool> ();
}

#endif
