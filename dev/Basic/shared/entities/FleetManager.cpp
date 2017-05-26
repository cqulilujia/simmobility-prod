/*
 * TaxiFleetManager.cpp
 *
 *  Created on: Jan 11, 2017
 *      Author: zhang huai peng
 */

#include "FleetManager.hpp"
#include "conf/ConfigManager.hpp"
#include "conf/ConfigParams.hpp"
#include <soci/postgresql/soci-postgresql.h>
#include <soci/soci.h>
#include "util/Utils.hpp"
#include <iostream>
#include <boost/date_time.hpp>

namespace bt = boost::posix_time;
namespace sim_mob
{
FleetManager* FleetManager::instance = nullptr;

double getSecondFrmTimeString(const std::string& startTime)
{
	std::istringstream is(startTime);
	is.imbue(std::locale(is.getloc(),new bt::time_input_facet("%d-%m-%Y %H:%M")));
	bt::ptime pt;
	is >> pt;
	return (double)pt.time_of_day().ticks() / (double)bt::time_duration::rep_type::ticks_per_second;
}

FleetManager::FleetManager()
{
	LoadTaxiFleetFromDB();
}

FleetManager* FleetManager::getInstance()
{
	if(!instance){
		instance = new FleetManager();
	}
	return instance;
}

FleetManager::~FleetManager()
{

}

void FleetManager::LoadTaxiFleetFromDB()
{
	ConfigParams& cfg = ConfigManager::GetInstanceRW().FullConfig();
	soci::session sql_(soci::postgresql,cfg.getDatabaseConnectionString(false));
	const std::map<std::string, std::string>& storedProcs =
			cfg.getDatabaseProcMappings().procedureMappings;
	std::map<std::string, std::string>::const_iterator spIt = storedProcs.find("taxi_fleet");
	if (spIt == storedProcs.end())
	{
		return;
	}

	std::map<std::string, std::vector<TripChainItem> > tripchains;
	soci::rowset<soci::row> rs = (sql_.prepare<< "select * from " + spIt->second);

	for (soci::rowset<soci::row>::const_iterator it = rs.begin();it != rs.end(); ++it)
	{
		FleetItem fleetItem;
		const soci::row& r = (*it);
		fleetItem.vehicleNo = r.get<std::string>(0);
		fleetItem.driverId = r.get<std::string>(1);
		double x = r.get<double>(2);
		double y = r.get<double>(3);
		Utils::convertWGS84_ToUTM(x, y);
		fleetItem.startNode = Node::allNodesMap.searchNearestObject(x, y);
		std::string startTime = r.get<std::string>(4);
		fleetItem.startTime = getSecondFrmTimeString(startTime);
		taxiFleet.push_back(fleetItem);
	}
}

std::vector<FleetManager::FleetItem> FleetManager::dispatchTaxiAtCurrentTime(const unsigned int currentTimeSec)
{
	std::vector<FleetItem> res;
	auto i=taxiFleet.begin();
	while(i!=taxiFleet.end())
	{
		if(i->startTime <= currentTimeSec)
		{
			res.push_back(*i);
			i = taxiFleet.erase(i);
		}
		else
		{
			i++;
		}
	}
	return res;
}

const std::vector<FleetManager::FleetItem>& FleetManager::getAllTaxiFleet() const
{
	return taxiFleet;
}

}
