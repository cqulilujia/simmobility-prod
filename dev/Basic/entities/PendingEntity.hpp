/* Copyright Singapore-MIT Alliance for Research and Technology */

#pragma once

namespace sim_mob
{

///Type of entities that can be "Pending"
enum KNOWN_ENTITY_TYPES {
	ENTITY_DRIVER,      ///<A Driver entity.
	ENTITY_PEDESTRIAN,  ///<A Pedestrian entity.
	ENTITY_BUSDRIVER,   ///<A BusDriver entity.
	ENTITY_RAWAGENT,    ///<Any Person which cannot be represented generically.
};

/**
 * Lightweight entity container. Used to hold Entities waiting to be scheduled.
 *
 * \note
 * Use the RAWAGENT type only if the Agent type in question is truly complex enough to warrant it.
 * The entire point of PendingEntity is to delay creating an Agent until it is scheduled for an update,
 * so RAWAGENT will waste memory unless it is only used in small doses. (For example, we currently
 * only use it for ns3 agents).
 */
struct PendingEntity {
	explicit PendingEntity(KNOWN_ENTITY_TYPES type)
		: type(type), origin(nullptr), dest(nullptr), rawAgent(nullptr), start(0) {}

	KNOWN_ENTITY_TYPES type;  ///<Entity type.
	Node* origin;             ///<Entity's origin. Null if ENTITY_RAWAGENT is the type.
	Node* dest;               ///<Entity's destination. Null if ENTITY_RAWAGENT is the type.
	Person* rawAgent;         ///<The actual entity. Null \b unless ENTITY_RAWAGENT is the type.
	unsigned int start;       ///<Entity's destination. Null if ENTITY_RAWAGENT is the type.
};

}
