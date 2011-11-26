/* Copyright Singapore-MIT Alliance for Research and Technology */

#include "Loader.hpp"

#include <cmath>
#include <algorithm>
#include <stdexcept>

//NOTE: Ubuntu is pretty bad about where it puts the SOCI headers.
//      "soci-postgresql.h" is supposed to be in "$INC/soci", but Ubuntu puts it in
//      "$INC/soci/postgresql". For now, I'm just referencing it manually, but
//      we might want to use something like pkg-config to manage header file directories
//      eventually.
#include "soci/soci.h"
#include "soci-postgresql.h"

#include "CrossingLoader.hpp"
#include "LaneLoader.hpp"

#include "geospatial/Point2D.hpp"
#include "geospatial/Node.hpp"
#include "geospatial/UniNode.hpp"
#include "geospatial/MultiNode.hpp"
#include "geospatial/Intersection.hpp"
#include "geospatial/Link.hpp"
#include "geospatial/RoadSegment.hpp"
#include "geospatial/LaneConnector.hpp"
#include "geospatial/RoadNetwork.hpp"
#include "geospatial/Crossing.hpp"
#include "geospatial/Lane.hpp"

#include "conf/simpleconf.hpp"

#include "util/DynamicVector.hpp"
#include "util/OutputUtil.hpp"
#include "util/DailyTime.hpp"
#include "util/GeomHelpers.hpp"

#include "Node.hpp"
#include "Section.hpp"
#include "Crossing.hpp"
#include "Turning.hpp"
#include "Polyline.hpp"
#include "SOCI_Converters.hpp"

//Note: These will eventually have to be put into a separate Loader for non-AIMSUN data.
// fclim: I plan to move $topdir/geospatial/aimsun/* and entities/misc/aimsun/* to
// $topdir/database/ and rename the aimsun namespace to "database".
#include "entities/misc/TripChain.hpp"
#include "entities/misc/aimsun/TripChain.hpp"
#include "entities/misc/aimsun/SOCI_Converters.hpp"
#include "entities/Signal.hpp"


using namespace sim_mob::aimsun;
using sim_mob::DynamicVector;
using sim_mob::Point2D;
using std::vector;
using std::string;
using std::set;
using std::map;
using std::pair;
using std::multimap;


namespace {

class DatabaseLoader : private boost::noncopyable
{
public:
    explicit DatabaseLoader(string const & connectionString);

    void LoadBasicAimsunObjects(map<string, string> const & storedProcedures);
    void DecorateAndTranslateObjects();
    void PostProcessNetwork();
    void SaveSimMobilityNetwork(sim_mob::RoadNetwork& res, std::vector<sim_mob::TripChain*>& tcs);

    map<int, Section> const & sections() const { return sections_; }

private:
    soci::session sql_;

    map<int, Node> nodes_;
    map<int, Section> sections_;
    vector<Crossing> crossings_;
    vector<Lane> lanes_;
    map<int, Turning> turnings_;
    multimap<int, Polyline> polylines_;
    vector<TripChain> tripchains_;
    map<int, Signal> signals_;

private:
    void LoadNodes(const std::string& storedProc);
    void LoadSections(const std::string& storedProc);
    void LoadCrossings(const std::string& storedProc);
    void LoadLanes(const std::string& storedProc);
    void LoadTurnings(const std::string& storedProc);
    void LoadPolylines(const std::string& storedProc);
    void LoadTripchains(const std::string& storedProc);
    void LoadTrafficSignals(const std::string& storedProc);

    void createSignals();
};

DatabaseLoader::DatabaseLoader(string const & connectionString)
  : sql_(soci::postgresql, connectionString)
{
}

//Sorting function for polylines
bool polyline_sorter (const Polyline* const p1, const Polyline* const p2)
{
	return p1->distanceFromSrc < p2->distanceFromSrc;
}


void DatabaseLoader::LoadNodes(const std::string& storedProc)
{
	//Our SQL statement
	soci::rowset<Node> rs = (sql_.prepare <<"select * from " + storedProc);

	//Exectue as a rowset to avoid repeatedly building the query.
	nodes_.clear();
	for (soci::rowset<Node>::const_iterator it=rs.begin(); it!=rs.end(); ++it)  {
		if (nodes_.count(it->id)>0) {
			throw std::runtime_error("Duplicate AIMSUN node.");
		}

		//Convert meters to cm
		it->xPos *= 100;
		it->yPos *= 100;

		nodes_[it->id] = *it;
	}
}


void DatabaseLoader::LoadSections(const std::string& storedProc)
{
	//Our SQL statement
	soci::rowset<Section> rs = (sql_.prepare <<"select * from " + storedProc);

	//Exectue as a rowset to avoid repeatedly building the query.
	sections_.clear();
	for (soci::rowset<Section>::const_iterator it=rs.begin(); it!=rs.end(); ++it)  {
		//Check nodes
		if(nodes_.count(it->TMP_FromNodeID)==0 || nodes_.count(it->TMP_ToNodeID)==0) {
			std::cout <<"From node: " <<it->TMP_FromNodeID  <<"  " <<nodes_.count(it->TMP_FromNodeID) <<"\n";
			std::cout <<"To node: " <<it->TMP_ToNodeID  <<"  " <<nodes_.count(it->TMP_ToNodeID) <<"\n";
			throw std::runtime_error("Invalid From or To node.");
		}

		//Convert meters to cm
		it->length *= 100;

		//Note: Make sure not to resize the Node map after referencing its elements.
		it->fromNode = &nodes_[it->TMP_FromNodeID];
		it->toNode = &nodes_[it->TMP_ToNodeID];
		sections_[it->id] = *it;
	}
}


void DatabaseLoader::LoadCrossings(const std::string& storedProc)
{
	//Our SQL statement
	soci::rowset<Crossing> rs = (sql_.prepare <<"select * from " + storedProc);

	//Exectue as a rowset to avoid repeatedly building the query.
	crossings_.clear();
	for (soci::rowset<Crossing>::const_iterator it=rs.begin(); it!=rs.end(); ++it)  {
		//Check sections
		if(sections_.count(it->TMP_AtSectionID)==0) {
			throw std::runtime_error("Crossing at Invalid Section");
		}

		//Convert meters to cm
		it->xPos *= 100;
		it->yPos *= 100;

		//Note: Make sure not to resize the Section vector after referencing its elements.
		it->atSection = &sections_[it->TMP_AtSectionID];
		crossings_.push_back(*it);
	}
}

void DatabaseLoader::LoadLanes(const std::string& storedProc)
{
	//Our SQL statement
	soci::rowset<Lane> rs = (sql_.prepare <<"select * from " + storedProc);

	//Exectue as a rowset to avoid repeatedly building the query.
	lanes_.clear();
	for (soci::rowset<Lane>::const_iterator it=rs.begin(); it!=rs.end(); ++it)  {
		//Check sections
		if(sections_.count(it->TMP_AtSectionID)==0) {
			throw std::runtime_error("Lane at Invalid Section");
		}

		//Convert meters to cm
		it->xPos *= 100;
		it->yPos *= 100;

		//Exclude "crossing" types
		if (it->laneType=="J" || it->laneType=="A4") {
			continue;
		}

		//Exclude lane markings which are not relevant to actual lane geometry
		if (it->laneType=="R" || it->laneType=="M" || it->laneType=="D" || it->laneType=="N"
			|| it->laneType=="Q" || it->laneType=="T" || it->laneType=="G" || it->laneType=="O"
			|| it->laneType=="A1" || it->laneType=="A3" || it->laneType=="L" || it->laneType=="H"
			|| it->laneType=="\\N"
			) {
			continue;
		}

		//Note: Make sure not to resize the Section vector after referencing its elements.
		it->atSection = &sections_[it->TMP_AtSectionID];
		lanes_.push_back(*it);
	}
}


void DatabaseLoader::LoadTurnings(const std::string& storedProc)
{
	//Our SQL statement
	soci::rowset<Turning> rs = (sql_.prepare <<"select * from " + storedProc);

	//Exectue as a rowset to avoid repeatedly building the query.
	vector<int> skippedTurningIDs;
	turnings_.clear();
	for (soci::rowset<Turning>::const_iterator it=rs.begin(); it!=rs.end(); ++it)  {
		//Check nodes
		bool fromMissing = sections_.count(it->TMP_FromSection)==0;
		bool toMissing = sections_.count(it->TMP_ToSection)==0;
		if(fromMissing || toMissing) {
			skippedTurningIDs.push_back(it->id);
			continue;
		}

		//Note: Make sure not to resize the Section map after referencing its elements.
		it->fromSection = &sections_[it->TMP_FromSection];
		it->toSection = &sections_[it->TMP_ToSection];
		turnings_[it->id] = *it;
	}

	//Print skipped turnings all at once.
	sim_mob::PrintArray(skippedTurningIDs, "Turnings skipped: ", "[", "]", ", ", 4);
}

void DatabaseLoader::LoadPolylines(const std::string& storedProc)
{
	//Our SQL statement
	soci::rowset<Polyline> rs = (sql_.prepare <<"select * from " + storedProc);

	//Exectue as a rowset to avoid repeatedly building the query.
	polylines_.clear();
	for (soci::rowset<Polyline>::const_iterator it=rs.begin(); it!=rs.end(); ++it)  {
		//Check nodes
		if(sections_.count(it->TMP_SectionId)==0) {
			throw std::runtime_error("Invalid polyline section reference.");
		}

		//Convert meters to cm
		it->xPos *= 100;
		it->yPos *= 100;

		//Note: Make sure not to resize the Section map after referencing its elements.
		it->section = &sections_[it->TMP_SectionId];
		polylines_.insert(std::make_pair(it->section->id, *it));
		//polylines_[it->id] = *it;
	}
}


void DatabaseLoader::LoadTripchains(const std::string& storedProc)
{
	//Our SQL statement
	soci::rowset<TripChain> rs = (sql_.prepare <<"select * from " + storedProc);

	//Exectue as a rowset to avoid repeatedly building the query.
	tripchains_.clear();
	for (soci::rowset<TripChain>::const_iterator it=rs.begin(); it!=rs.end(); ++it)  {
		//Check nodes
		if(nodes_.count(it->from.TMP_locationNodeID)==0) {
			throw std::runtime_error("Invalid trip chain from node reference.");
		}
		if(nodes_.count(it->to.TMP_locationNodeID)==0) {
			throw std::runtime_error("Invalid trip chain to node reference.");
		}

		//Set date
		it->startTime = sim_mob::DailyTime(it->TMP_startTimeStr);

		//Note: Make sure not to resize the Node map after referencing its elements.
		it->from.location = &nodes_[it->from.TMP_locationNodeID];
		it->to.location = &nodes_[it->to.TMP_locationNodeID];
		tripchains_.push_back(*it);
	}
}

void
DatabaseLoader::LoadTrafficSignals(std::string const & storedProcedure)
{
    // For testing purpose, we can disable automatic signal creation via database lookup
    // by putting an empty string for the 'signal' stored procedure in the config file.
    // Manual creation can be achieved by specifying the signal locations in the top level
    // <signals> section of the config file.  This feature will be removed soon
    // and without notice.
    if (storedProcedure.empty())
    {
        std::cout << "WARNING: An empty 'signal' stored-procedure was specified in the config file; "
                  << "will not lookup the database to create any signal found in there" << std::endl;
        return;
    }

    soci::rowset<Signal> rows = (sql_.prepare <<"select * from " + storedProcedure);
    for (soci::rowset<Signal>::const_iterator iter = rows.begin(); iter != rows.end(); ++iter)
    {
        Signal signal = *iter;
        // Convert from meters to centimeters.
        signal.xPos *= 100;
        signal.yPos *= 100;
        signals_.insert(std::make_pair(signal.id, signal));
    }
}

std::string const &
getStoredProcedure(map<string, string> const & storedProcs, string const & procedureName)
{
    map<string, string>::const_iterator iter = storedProcs.find(procedureName);
    if (iter != storedProcs.end())
        return iter->second;
    throw std::runtime_error("expected to find stored-procedure named '" + procedureName
                             + "' in the config file");
}

void DatabaseLoader::LoadBasicAimsunObjects(map<string, string> const & storedProcs)
{
	LoadNodes(getStoredProcedure(storedProcs, "node"));
	LoadSections(getStoredProcedure(storedProcs, "section"));
	LoadCrossings(getStoredProcedure(storedProcs, "crossing"));
	LoadLanes(getStoredProcedure(storedProcs, "lane"));
	LoadTurnings(getStoredProcedure(storedProcs, "turning"));
	LoadPolylines(getStoredProcedure(storedProcs, "polyline"));
	LoadTripchains(getStoredProcedure(storedProcs, "tripchain"));
	LoadTrafficSignals(getStoredProcedure(storedProcs, "signal"));
}



//Compute the distance from the source node of the polyline to a
// point on the line from the source to the destination nodes which
// is normal to the Poly-point.
void ComputePolypointDistance(Polyline& pt)
{
	//Our method is (fairly) simple.
	//First, compute the distance from the point to the polyline at a perpendicular angle.
	double dx2x1 = pt.section->toNode->xPos - pt.section->fromNode->xPos;
	double dy2y1 = pt.section->toNode->yPos - pt.section->fromNode->yPos;
	double dx1x0 = pt.section->fromNode->xPos - pt.xPos;
	double dy1y0 = pt.section->fromNode->yPos - pt.yPos;
	double numerator = dx2x1*dy1y0 - dx1x0*dy2y1;
	double denominator = sqrt(dx2x1*dx2x1 + dy2y1*dy2y1);
	double perpenDist = numerator/denominator;
	if (perpenDist<0.0) {
		//We simplify all the quadratic math to just a sign change, since
		//   it's known that this polypoint has a positive distance to the line.
		perpenDist *= -1;
	}

	//Second, compute the distance from the source point to the polypoint
	double realDist = sqrt(dx1x0*dx1x0 + dy1y0*dy1y0);

	//Finally, apply the Pythagorean theorum
	pt.distanceFromSrc = sqrt(realDist*realDist - perpenDist*perpenDist);

	//NOTE: There simplest method would be to just take the x-component of the vector
	//      from pt.x/y to pt.section.fromNode.x/y, but you'd have to factor in
	//      the fact that this vector is rotated with respect to pt.section.from->pt.section.to.
	//      I can't remember enough vector math to handle this, but if anyone wants to
	//      replace it the vector version would certainly be faster. ~Seth
}



/**
 * Temporary functions.
 */
DynamicVector GetCrossingNearLine(Node& atNode, Node& toNode)
{
	//Get the outgoing set of crossing IDs
	map<Node*, vector<int> >::iterator outgoing = atNode.crossingLaneIdsByOutgoingNode.find(&toNode);
	if (outgoing!=atNode.crossingLaneIdsByOutgoingNode.end()) {
		//Search for the one closest to the toNode.
		DynamicVector resLine;
		double minDist = -1;
		for (vector<int>::iterator it2=outgoing->second.begin(); it2!=outgoing->second.end(); it2++) {
			//Get this item
			map<int, vector<Crossing*> >::iterator crossingIt = atNode.crossingsAtNode.find(*it2);
			if (crossingIt!=atNode.crossingsAtNode.end()) {
				//Now make a vector for it.
				DynamicVector currPoint(
					crossingIt->second.front()->xPos, crossingIt->second.front()->yPos,
					crossingIt->second.back()->xPos, crossingIt->second.back()->yPos
				);
				DynamicVector midPoint(currPoint);
				midPoint.scaleVectTo(midPoint.getMagnitude()/2).translateVect();
				double currDist = sim_mob::dist(midPoint.getX(), midPoint.getY(), toNode.xPos, toNode.yPos);
				if (minDist==-1 || currDist < minDist) {
					resLine = currPoint;
					minDist = currDist;
				}
			}
		}
		if (minDist > -1) {
			return resLine;
		}
	}
	throw std::runtime_error("Can't find crossing near line in temporary cleanup function.");
}
Section& GetSection(Node& start, Node& end)
{
	for (vector<Section*>::iterator it=start.sectionsAtNode.begin(); it!=start.sectionsAtNode.end(); it++) {
		if ((*it)->toNode->id==end.id) {
			return **it;
		}
	}
	std::cout <<"Error finding section from " <<start.id <<" to " <<end.id <<std::endl;
	throw std::runtime_error("Can't find section in temporary cleanup function.");
}
void ScaleLanesToCrossing(Node& start, Node& end, bool scaleEnd)
{
	//Retrieve the section
	Section& sect = GetSection(start, end);

	//Retrieve the crossing's "near" line.
	DynamicVector endLine = GetCrossingNearLine(scaleEnd?end:start, scaleEnd?start:end);

	//We can't do much until lanes are generated (we could try to guess what our lane generator would
	// do, but it's easier to set a debug flag).
	//std::cout <<"Saving end line: " <<endLine.getX() <<"," <<endLine.getY() <<" ==> " <<endLine.getEndX() <<"," <<endLine.getEndY() <<"\n";
	if (scaleEnd) {
		sect.HACK_LaneLinesEndLineCut = endLine;
	} else {
		sect.HACK_LaneLinesStartLineCut = endLine;
	}
}
void ResizeTo2(vector<Crossing*>& vec)
{
	if (vec.size()<=2) {
		if (vec.size()==2) {
			return;
		}
		throw std::runtime_error("Can't resize if vector is empty or has only one element.");
	}

	vec[1] = vec.back();
	vec.resize(2, nullptr);
}
vector<Crossing*>& GetCrossing(Node& atNode, Node& toNode, size_t crossingID)
{
	//Get the outgoing set of crossing IDs
	map<Node*, vector<int> >::iterator outgoing = atNode.crossingLaneIdsByOutgoingNode.find(&toNode);
	if (outgoing!=atNode.crossingLaneIdsByOutgoingNode.end()) {
		//Narrow down to the one we want.
		for (vector<int>::iterator it2=outgoing->second.begin(); it2!=outgoing->second.end(); it2++) {
			if (*it2 != static_cast<int>(crossingID)) {
				continue;
			}
			map<int, vector<Crossing*> >::iterator crossingIt = atNode.crossingsAtNode.find(*it2);
			if (crossingIt!=atNode.crossingsAtNode.end()) {
				return crossingIt->second;
			}
			break;
		}
	}
	throw std::runtime_error("Can't find crossing in temporary cleanup function.");
}
void RebuildCrossing(Node& atNode, Node& toNode, size_t baseCrossingID, size_t resCrossingID, bool flipLeft, unsigned int crossingWidthCM, unsigned int paddingCM)
{
	//Retrieve the base Crossing and the Crossing we will store the result in.
	vector<Crossing*>& baseCrossing = GetCrossing(atNode, toNode, baseCrossingID);
	vector<Crossing*>& resCrossing = GetCrossing(atNode, toNode, resCrossingID);

	//Manual resize may be required
	ResizeTo2(baseCrossing);
	ResizeTo2(resCrossing);

	//Set point 1:
	{
		DynamicVector vec(baseCrossing.front()->xPos, baseCrossing.front()->yPos, baseCrossing.back()->xPos, baseCrossing.back()->yPos);
		vec.scaleVectTo(paddingCM).translateVect().flipNormal(!flipLeft);
		vec.scaleVectTo(crossingWidthCM).translateVect();
		resCrossing.front()->xPos = vec.getX();
		resCrossing.front()->yPos = vec.getY();
	}

	//Set point 2:
	{
		DynamicVector vec(baseCrossing.back()->xPos, baseCrossing.back()->yPos, baseCrossing.front()->xPos, baseCrossing.front()->yPos);
		vec.scaleVectTo(paddingCM).translateVect().flipNormal(flipLeft);
		vec.scaleVectTo(crossingWidthCM).translateVect();
		resCrossing.back()->xPos = vec.getX();
		resCrossing.back()->yPos = vec.getY();
	}


}
void ManuallyFixVictoriaStreetMiddleRoadIntersection(map<int, Node>& nodes, map<int, Section>& sections, vector<Crossing>& crossings, vector<Lane>& lanes, map<int, Turning>& turnings, multimap<int, Polyline>& polylines)
{
	//Step 1: Tidy up the crossings.
	RebuildCrossing(nodes[66508], nodes[93730], 683, 721, true, 450, 200);
	RebuildCrossing(nodes[66508], nodes[65120], 2419, 2111, false, 400, 200);
	RebuildCrossing(nodes[66508], nodes[75956], 3956, 3719, true, 450, 200);
	RebuildCrossing(nodes[66508], nodes[84882], 4579, 1251, true, 450, 200);

	//Step 2: Scale lane lines to match the crossings.
	ScaleLanesToCrossing(nodes[93730], nodes[66508], true);
	ScaleLanesToCrossing(nodes[66508], nodes[93730], false);
	ScaleLanesToCrossing(nodes[65120], nodes[66508], true);
	ScaleLanesToCrossing(nodes[66508], nodes[65120], false);
	ScaleLanesToCrossing(nodes[75956], nodes[66508], true);
	ScaleLanesToCrossing(nodes[66508], nodes[75956], false);
	ScaleLanesToCrossing(nodes[84882], nodes[66508], true);
	ScaleLanesToCrossing(nodes[66508], nodes[84882], false);
}




/**
 * Perform guided cleanup of the fully-loaded data. This step happens directly before the network is converted to
 * SimMobility format.
 *
 * \note
 * Currently, this process performs a single hard-coded check. Ideally, we would load data from another, smaller
 * database which contains a few "hints" to help nudge the various network components into the correct positions.
 * If you want a more heavy-handed approach, you should make a "PreProcessNetwork" function which does things like
 * deleting lanes, etc. (but be careful of invalidating references in that case).
 */
void DatabaseLoader::PostProcessNetwork()
{
	//TEMP: Heavy-handed tactics like this should only be used if you're desperate.
	// You know, like if you've got a demo tomorrow.
	bool TEMP_FLAG_ON = sim_mob::ConfigParams::GetInstance().TEMP_ManualFixDemoIntersection;
	if (TEMP_FLAG_ON) {
		ManuallyFixVictoriaStreetMiddleRoadIntersection(nodes_, sections_, crossings_, lanes_, turnings_, polylines_);
	}

}





void DatabaseLoader::DecorateAndTranslateObjects()
{
	//Step 1: Tag all Nodes with the Sections that meet there.
	for (map<int,Section>::iterator it=sections_.begin(); it!=sections_.end(); it++) {
		it->second.fromNode->sectionsAtNode.push_back(&(it->second));
		it->second.toNode->sectionsAtNode.push_back(&(it->second));
	}


	//Step 2: Tag all Nodes that might be "UniNodes". These fit the following criteria:
	//        1) In ALL sections that meet at this node, there are only two distinct nodes.
	//        2) Each of these distinct nodes has exactly ONE Segment leading "from->to" and one leading "to->from".
	//           This should take bi-directional Segments into account.
	//        3) All Segments share the same Road Name
	//        4) Optionally, there can be a single link in ONE direction, representing a one-way road.
	vector<int> nodeMismatchIDs;
	for (map<int,Node>::iterator it=nodes_.begin(); it!=nodes_.end(); it++) {
		Node* n = &it->second;
		n->candidateForSegmentNode = true; //Conditional pass

		//Perform both checks at the same time.
		pair<Node*, Node*> others(nullptr, nullptr);
		pair<unsigned int, unsigned int> flags(0, 0);  //1="from->to", 2="to->from"
		string expectedName;
		for (vector<Section*>::iterator it=n->sectionsAtNode.begin(); it!=n->sectionsAtNode.end(); it++) {
			//Get "other" node
			Node* otherNode = ((*it)->fromNode!=n) ? (*it)->fromNode : (*it)->toNode;

			//Manage property one.
			unsigned int* flagPtr;
			if (!others.first || others.first==otherNode) {
				others.first = otherNode;
				flagPtr = &flags.first;
			} else if (!others.second || others.second==otherNode) {
				others.second = otherNode;
				flagPtr = &flags.second;
			} else {
				n->candidateForSegmentNode = false; //Fail
				break;
			}

			//Manage property two.
			unsigned int toFlag = ((*it)->toNode==n) ? 1 : 2;
			if (((*flagPtr)&toFlag)==0) {
				*flagPtr = (*flagPtr) | toFlag;
			} else {
				n->candidateForSegmentNode = false; //Fail
				break;
			}

			//Manage property three.
			if (expectedName.empty()) {
				expectedName = (*it)->roadName;
			} else if (expectedName != (*it)->roadName) {
				n->candidateForSegmentNode = false; //Fail
				break;
			}
		}

		//One final check
		if (n->candidateForSegmentNode) {
			bool flagMatch =   (flags.first==3 && flags.second==3)  //Bidirectional
							|| (flags.first==1 && flags.second==2)  //One-way
							|| (flags.first==2 && flags.second==1); //One-way

			n->candidateForSegmentNode = others.first && others.second && flagMatch;
		}

		//Generate warnings if this value doesn't match the expected "is intersection" value.
		//This is usually a result of a network being cropped.
		if (n->candidateForSegmentNode == n->isIntersection) {
			nodeMismatchIDs.push_back(n->id);
		}
	}

	//Print all node mismatches at once
	sim_mob::PrintArray(nodeMismatchIDs, "UniNode/Intersection mismatches: ", "[", "]", ", ", 4);

	//Step 3: Tag all Sections with Turnings that apply to that Section
	for (map<int,Turning>::iterator it=turnings_.begin(); it!=turnings_.end(); it++) {
		it->second.fromSection->connectedTurnings.push_back(&(it->second));
		it->second.toSection->connectedTurnings.push_back(&(it->second));
	}

	//Step 4: Add polyline entries to Sections. As you do this, compute their distance
	//        from the origin ("from" node)
	for (map<int,Polyline>::iterator it=polylines_.begin(); it!=polylines_.end(); it++) {
		it->second.section->polylineEntries.push_back(&(it->second));
		ComputePolypointDistance(it->second);
	}

	//Step 4.5: Request the LaneLoader to tag some Lane-related data.
	LaneLoader::DecorateLanes(sections_, lanes_);

	//Steps 5,6: Request the CrossingsLoader to tag some Crossing-related data.
	CrossingLoader::DecorateCrossings(nodes_, crossings_);

}


//Another temporary function
void CutSingleLanePolyline(vector<Point2D>& laneLine, const DynamicVector& cutLine, bool trimStart)
{
	//Compute the intersection of our lane line and the crossing.
	Point2D intPt = sim_mob::LineLineIntersect(cutLine, laneLine.front(), laneLine.back());
	if (intPt.getX() == std::numeric_limits<int>::max()) {
		throw std::runtime_error("Temporary lane function is somehow unable to compute line intersections.");
	}

	//Now update either the first or last point
	laneLine[trimStart?0:laneLine.size()-1] = intPt;
}



void DatabaseLoader::SaveSimMobilityNetwork(sim_mob::RoadNetwork& res, std::vector<sim_mob::TripChain*>& tcs)
{
	//First, Nodes. These match cleanly to the Sim Mobility data structures
	std::cout <<"Warning: Units are not considered when converting AIMSUN data.\n";
	for (map<int,Node>::iterator it=nodes_.begin(); it!=nodes_.end(); it++) {
		sim_mob::aimsun::Loader::ProcessGeneralNode(res, it->second);
		it->second.generatedNode->originalDB_ID.setProps("aimsun-id", it->first);
	}

	//Next, Links and RoadSegments. See comments for our approach.
	for (map<int,Section>::iterator it=sections_.begin(); it!=sections_.end(); it++) {
		if (!it->second.hasBeenSaved) {  //Workaround...
			sim_mob::aimsun::Loader::ProcessSection(res, it->second);
		}
	}
	//Scan the vector to see if any skipped Sections were not filled in later.
	for (map<int,Section>::iterator it=sections_.begin(); it!=sections_.end(); it++) {
		if (!it->second.hasBeenSaved) {
			throw std::runtime_error("Section was skipped.");
		}
		it->second.generatedSegment->originalDB_ID.setProps("aimsun-id", it->first);
	}

	//Next, SegmentNodes (UniNodes), which are only partially initialized in the general case.
	for (map<int,Node>::iterator it=nodes_.begin(); it!=nodes_.end(); it++) {
		if (it->second.candidateForSegmentNode) {
			sim_mob::aimsun::Loader::ProcessUniNode(res, it->second);
		}
	}

	//Next, Turnings. These generally match up.
	std::cout <<"Warning: Lanes-Left-of-Divider incorrect when converting AIMSUN data.\n";
	for (map<int,Turning>::iterator it=turnings_.begin(); it!=turnings_.end(); it++) {
		sim_mob::aimsun::Loader::ProcessTurning(res, it->second);
	}

	//Next, save the Polylines. This is best done at the Section level
	for (map<int,Section>::iterator it=sections_.begin(); it!=sections_.end(); it++) {
		sim_mob::aimsun::Loader::ProcessSectionPolylines(res, it->second);
	}

	//Finalize our MultiNodes' circular arrays
	for (vector<sim_mob::MultiNode*>::const_iterator it=res.getNodes().begin(); it!=res.getNodes().end(); it++) {
		sim_mob::MultiNode::BuildClockwiseLinks(res, *it);
	}

	//Prune Crossings and convert to the "near" and "far" syntax of Sim Mobility. Also give it a "position", defined
	//   as halfway between the midpoints of the near/far lines, and then assign it as an Obstacle to both the incoming and
	//   outgoing RoadSegment that it crosses.
	for (map<int,Node>::iterator it=nodes_.begin(); it!=nodes_.end(); it++) {
		for (map<Node*, std::vector<int> >::iterator i2=it->second.crossingLaneIdsByOutgoingNode.begin(); i2!=it->second.crossingLaneIdsByOutgoingNode.end(); i2++) {
			CrossingLoader::GenerateACrossing(res, it->second, *i2->first, i2->second);
		}
	}

	//Prune lanes and figure out where the median is.
	// TODO: This should eventually allow other lanes to be designated too.
	LaneLoader::GenerateLinkLanes(res, nodes_, sections_);

	sim_mob::aimsun::Loader::FixupLanesAndCrossings(res);

	//Save all trip chains
	for (vector<TripChain>::iterator it=tripchains_.begin(); it!=tripchains_.end(); it++) {
		tcs.push_back(new sim_mob::TripChain());
		tcs.back()->from.description = it->from.description;
		tcs.back()->from.location = it->from.location->generatedNode;
		tcs.back()->to.description = it->to.description;
		tcs.back()->to.location = it->to.location->generatedNode;
		tcs.back()->mode = it->mode;
		tcs.back()->primary = it->primary;
		tcs.back()->flexible = it->flexible;
		tcs.back()->startTime = it->startTime;
	}



<<<<<<< HEAD
#if 0
	map<int,Node>::iterator it=nodes.find(66508);
=======
	//TEMP
	/*map<int,Node>::iterator it=nodes.find(66508);
>>>>>>> seth_vectmove
	if (it!=nodes.end()) {
		sim_mob::MultiNode* temp = dynamic_cast<sim_mob::MultiNode*>(it->second.generatedNode);
		if (temp) {
			for (set<sim_mob::RoadSegment*>::const_iterator it2=temp->getRoadSegments().begin(); it2!=temp->getRoadSegments().end(); it2++) {
				if (temp->hasOutgoingLanes(**it2)) {
					std::cout <<"  Segment crossings for:  " <<(*it2)->getStart()->originalDB_ID.getLogItem() <<" ==> " <<(*it2)->getEnd()->originalDB_ID.getLogItem() <<"\n";
					std::set<string> res;
					for (set<sim_mob::LaneConnector*>::const_iterator it3=temp->getOutgoingLanes(**it2).begin(); it3!=temp->getOutgoingLanes(**it2).end(); it3++) {
						res.insert((*it3)->getLaneTo()->getRoadSegment()->getEnd()->originalDB_ID.getLogItem());
					}

					for (std::set<string>::iterator it4=res.begin(); it4!=res.end(); it4++) {
						std::cout <<"    ...to:  " <<*it4 <<"\n";
					}
				}
			}

		}
	}
<<<<<<< HEAD
	throw 1;
#endif

        createSignals();
}

void
DatabaseLoader::createSignals()
{
    std::set<sim_mob::Node const *> uniNodes;

    for (map<int, Signal>::const_iterator iter = signals_.begin(); iter != signals_.end(); ++iter)
    {
        Signal const & dbSignal = iter->second;
        map<int, Node>::const_iterator iter2 = nodes_.find(dbSignal.nodeId);
        if (iter2 == nodes_.end())
        {
            std::ostringstream stream;
            stream << "cannot find node (id=" << dbSignal.nodeId
                   << ") in the database for signal id=" << iter->first;
            throw std::runtime_error(stream.str());
        }

        Node const & dbNode = iter2->second;
        sim_mob::Node const * node = dbNode.generatedNode;
        if (dynamic_cast<sim_mob::UniNode const *>(node))
        {
            if (uniNodes.count(node) == 0)
            {
                uniNodes.insert(node);
                std::cout << "cannot create signal at Uni-node (database-id=" << dbSignal.nodeId
                          << ") because Signal.cpp was written only for 4-way traffic at an "
                          << "intersection.  Need to fix this." << std::endl;
            }
            continue;
        }

        sim_mob::Signal & signal = sim_mob::Signal::signalAt(*node);
        // The 'signal' stored procedure did not retrieve the signal equipment's bearing.  Will
        // raise a ticket about this issue.
        signal.addSignalSite(dbSignal.xPos, dbSignal.yPos, dbSignal.typeCode, 0.0);
    }
=======
	throw 1;*/
	//END TEMP
>>>>>>> seth_vectmove
}



} //End anon namespace


//Another temporary function
void sim_mob::aimsun::Loader::TMP_TrimAllLaneLines(sim_mob::RoadSegment* seg, const DynamicVector& cutLine, bool trimStart)
{
	//Nothing to do?
	if (cutLine.getMagnitude()==0.0) {
		return;
	}

	//Ensure that this segment has built all its lane lines.
	seg->syncLanePolylines();

	//Now go through and manually edit all of them. This includes lane lines and lane edge lines
	{
	vector< vector<Point2D> >::iterator it = seg->laneEdgePolylines_cached.begin();
	for (;it!=seg->laneEdgePolylines_cached.end(); it++) {
		CutSingleLanePolyline(*it, cutLine, trimStart);
	}
	}
	{
	vector<sim_mob::Lane*>::iterator it = seg->lanes.begin();
	for (;it!=seg->lanes.end(); it++) {
		CutSingleLanePolyline((*it)->polyline_, cutLine, trimStart);
	}
	}
}


void sim_mob::aimsun::Loader::FixupLanesAndCrossings(sim_mob::RoadNetwork& res)
{
	//Fix up lanes
	const std::vector<sim_mob::Link*>& vecLinks = res.getLinks();
	int numLinks = vecLinks.size();

	//TODO more comments needed
	for(int n = 0; n < numLinks; ++n)
	{
		sim_mob::Link* link = vecLinks[n];

		const std::vector<sim_mob::RoadSegment*>& vecForwardSegs = link->getPath(true);
		const std::vector<sim_mob::RoadSegment*>& vecReverseSegs = link->getPath(false);
		std::set<sim_mob::RoadSegment*> roadSegs;
		roadSegs.insert(vecForwardSegs.begin(), vecForwardSegs.end());
		roadSegs.insert(vecReverseSegs.begin(), vecReverseSegs.end());
		for(std::set<sim_mob::RoadSegment*>::const_iterator itRS = roadSegs.begin(); itRS!=roadSegs.end(); ++itRS)
		{
			for(std::map<sim_mob::centimeter_t, const sim_mob::RoadItem*>::const_iterator itObstacles = (*itRS)->obstacles.begin(); itObstacles != (*itRS)->obstacles.end(); ++itObstacles)
			{
				const sim_mob::RoadItem* ri = (*itObstacles).second;

				const sim_mob::Crossing* cross = dynamic_cast<const sim_mob::Crossing*>(ri);
				if(!cross)
					continue;

				sim_mob::Point2D nearLinemidPoint((cross->nearLine.second.getX()-cross->nearLine.first.getX())/2 + cross->nearLine.first.getX(),
										(cross->nearLine.second.getY()-cross->nearLine.first.getY())/2 + cross->nearLine.first.getY());

				std::vector<sim_mob::Point2D>& segmentPolyline = (*itRS)->polyline;
				{
					//Segment polyline
					double d1 = dist(&(segmentPolyline[0]), &nearLinemidPoint);
					double d2 = dist(&(segmentPolyline[segmentPolyline.size()-1]), &nearLinemidPoint);
					if (d2<d1)
					{
						segmentPolyline[segmentPolyline.size()-1] = ProjectOntoLine(segmentPolyline[segmentPolyline.size()-1], cross->farLine.first, cross->farLine.second);
					}
					else
					{
						segmentPolyline[0] = ProjectOntoLine(segmentPolyline[0], cross->farLine.first, cross->farLine.second);
					}
				}

				//Lane edge polylines
				//TODO don't access variable that should be private here
				std::vector< std::vector<sim_mob::Point2D> >& vecPolylines = (*itRS)->laneEdgePolylines_cached;
				for(size_t i = 0; i < vecPolylines.size(); ++i)
				{
					//TODO move this functionality into a helper function
					std::vector<sim_mob::Point2D>& vecThisPolyline = vecPolylines[i];
					double d1 = dist(&(vecThisPolyline[0]), &nearLinemidPoint);
					double d2 = dist(&(vecThisPolyline[vecThisPolyline.size()-1]), &nearLinemidPoint);
					if (d2<d1)
					{
						vecThisPolyline[vecThisPolyline.size()-1] = ProjectOntoLine(vecThisPolyline[vecThisPolyline.size()-1], cross->farLine.first, cross->farLine.second);
					}
					else
					{
						vecThisPolyline[0] = ProjectOntoLine(vecThisPolyline[0], cross->farLine.first, cross->farLine.second);
					}

				}

<<<<<<< HEAD
#if 0
				//Lane polylines
				const std::vector<sim_mob::Lane*>& segmentLanes = (*itRS)->getLanes();
=======
				//Lane polylines
/*				const std::vector<sim_mob::Lane*>& segmentLanes = (*itRS)->getLanes();
>>>>>>> seth_vectmove
				for(std::vector<sim_mob::Lane*>::const_iterator itLanes = segmentLanes.begin(); itLanes != segmentLanes.end(); ++itLanes)
				{
				    ///TODO get rid of ugly const_cast
					std::vector<sim_mob::Point2D>& lanePolyline = const_cast<std::vector<sim_mob::Point2D>&>((*itLanes)->getPolyline());
					if(lanePolyline.empty())
						continue;

					double d1 = dist(&(lanePolyline[0]), &nearLinemidPoint);
					double d2 = dist(&(lanePolyline[lanePolyline.size()-1]), &nearLinemidPoint);
					if (d2<d1)
					{
						lanePolyline[lanePolyline.size()-1] = ProjectOntoLine(lanePolyline[lanePolyline.size()-1], cross->farLine.first, cross->farLine.second);
					}
					else
					{
						lanePolyline[0] = ProjectOntoLine(lanePolyline[0], cross->farLine.first, cross->farLine.second);
					}
				}
<<<<<<< HEAD
#endif
=======
				*/
>>>>>>> seth_vectmove
			}
		}
	}
}



void sim_mob::aimsun::Loader::ProcessGeneralNode(sim_mob::RoadNetwork& res, Node& src)
{
	src.hasBeenSaved = true;

	sim_mob::Node* newNode = nullptr;
	if (!src.candidateForSegmentNode) {
		//This is an Intersection
		newNode = new sim_mob::Intersection();

		//Store it in the global nodes array
		res.nodes.push_back(dynamic_cast<MultiNode*>(newNode));
	} else {
		//Just save for later so the pointer isn't invalid
		newNode = new UniNode();
		res.segmentnodes.insert(dynamic_cast<UniNode*>(newNode));
	}

	//Always save the location
	newNode->location = new Point2D(src.getXPosAsInt(), src.getYPosAsInt());

	//For future reference
	src.generatedNode = newNode;
}


void sim_mob::aimsun::Loader::ProcessUniNode(sim_mob::RoadNetwork& res, Node& src)
{
	//Find 2 sections "from" and 2 sections "to".
	//(Bi-directional segments will complicate this eventually)
	//Most of the checks done here are already done earlier in the Loading process, but it doesn't hurt to check again.
	pair<Section*, Section*> fromSecs(nullptr, nullptr);
	pair<Section*, Section*> toSecs(nullptr, nullptr);
	for (vector<Section*>::iterator it=src.sectionsAtNode.begin(); it!=src.sectionsAtNode.end(); it++) {
		if ((*it)->TMP_ToNodeID==src.id) {
			if (!fromSecs.first) {
				fromSecs.first = *it;
			} else if (!fromSecs.second) {
				fromSecs.second = *it;
			} else {
				throw std::runtime_error("UniNode contains unexpected additional Sections leading TO.");
			}
		} else if ((*it)->TMP_FromNodeID==src.id) {
			if (!toSecs.first) {
				toSecs.first = *it;
			} else if (!toSecs.second) {
				toSecs.second = *it;
			} else {
				throw std::runtime_error("UniNode contains unexpected additional Sections leading FROM.");
			}
		} else {
			throw std::runtime_error("UniNode contains a Section which actually does not lead to/from that Node.");
		}
	}

	//Ensure at least one path was found, and a non-partial second path.
	if (!(fromSecs.first && toSecs.first)) {
		throw std::runtime_error("UniNode contains no primary path.");
	}
	if ((fromSecs.second && !toSecs.second) || (!fromSecs.second && toSecs.second)) {
		throw std::runtime_error("UniNode contains partial secondary path.");
	}

	//This is a simple Road Segment joint
	UniNode* newNode = dynamic_cast<UniNode*>(src.generatedNode);
	//newNode->location = new Point2D(src.getXPosAsInt(), src.getYPosAsInt());

	//Set locations (ensure unset locations are null)
	//Also ensure that we don't point backwards from the same segment.
	bool parallel = fromSecs.first->fromNode->id == toSecs.first->toNode->id;
	newNode->firstPair.first = fromSecs.first->generatedSegment;
	newNode->firstPair.second = parallel ? toSecs.second->generatedSegment : toSecs.first->generatedSegment;
	if (fromSecs.second && toSecs.second) {
		newNode->secondPair.first = fromSecs.second->generatedSegment;
		newNode->secondPair.second = parallel ? toSecs.first->generatedSegment : toSecs.second->generatedSegment;
	} else {
		newNode->secondPair = pair<RoadSegment*, RoadSegment*>(nullptr, nullptr);
	}

	//Save it for later reference
	//res.segmentnodes.insert(newNode);

	//TODO: Actual connector alignment (requires map checking)
	sim_mob::UniNode::buildConnectorsFromAlignedLanes(newNode, std::make_pair(0, 0), std::make_pair(0, 0));

	//This UniNode can later be accessed by the RoadSegment itself.
}


void sim_mob::aimsun::Loader::ProcessSection(sim_mob::RoadNetwork& res, Section& src)
{
	//Skip Sections which start at a non-intersection. These will be filled in later.
	if (src.fromNode->candidateForSegmentNode) {
		return;
	}

	//Process this section, and continue processing Sections along the direction of
	// travel until one of these ends on an intersection.
	//NOTE: This approach is far from foolproof; for example, if a Link contains single-directional
	//      Road segments that fail to match at every UniNode. Need to find a better way to
	//      group RoadSegments into Links, but at least this works for our test network.
	Section* currSect = &src;
	sim_mob::Link* ln = new sim_mob::Link();
	src.generatedSegment = new sim_mob::RoadSegment(ln);
	ln->roadName = currSect->roadName;
	ln->start = currSect->fromNode->generatedNode;
	set<RoadSegment*> linkSegments;

	//Make sure the link's start node is represented at the Node level.
	//TODO: Try to avoid dynamic casting if possible.
	for (;;) {
		//Update
		ln->end = currSect->toNode->generatedNode;

		//Now, check for segments going both forwards and backwards. Add both.
		for (size_t i=0; i<2; i++) {
			//Phase 1 = find a reverse segment
			Section* found = nullptr;
			if (i==0) {
				found = currSect;
			} else {
				for (vector<Section*>::iterator iSec=currSect->toNode->sectionsAtNode.begin(); iSec!=currSect->toNode->sectionsAtNode.end(); iSec++) {
					Section* newSec = *iSec;
					if (newSec->fromNode==currSect->toNode && newSec->toNode==currSect->fromNode) {
						found = newSec;
						break;
					}
				}
			}

			//Check: No reverse segment
			if (!found) {
				break;
			}

			//Check: not processing an existing segment
			if (found->hasBeenSaved) {
				throw std::runtime_error("Section processed twice.");
			}

			//Mark saved
			found->hasBeenSaved = true;

			//Check name
			if (ln->roadName != found->roadName) {
				throw std::runtime_error("Road names don't match up on RoadSegments in the same Link.");
			}

			//Prepare a new segment IF required, and save it for later reference (or load from past ref.)
			if (!found->generatedSegment) {
				found->generatedSegment = new sim_mob::RoadSegment(ln);
			}

			//Save this segment if either end points are multinodes
			for (size_t tempID=0; tempID<2; tempID++) {
				sim_mob::MultiNode* nd = dynamic_cast<sim_mob::MultiNode*>(tempID==0?found->fromNode->generatedNode:found->toNode->generatedNode);
				if (nd) {
					nd->roadSegmentsAt.insert(found->generatedSegment);
				}
			}

			//Retrieve the generated segment
			sim_mob::RoadSegment* rs = found->generatedSegment;

			//Start/end need to be added properly
			rs->start = found->fromNode->generatedNode;
			rs->end = found->toNode->generatedNode;

			//Process
			rs->maxSpeed = found->speed;
			rs->length = found->length;
			for (int laneID=0; laneID<found->numLanes; laneID++) {
				rs->lanes.push_back(new sim_mob::Lane(rs, laneID));
			}
			rs->width = 0;

			//TODO: How do we determine if lanesLeftOfDivider should be 0 or lanes.size()
			//      In other words, how do we apply driving direction?
			//NOTE: This can be done easily later from the Link's point-of-view.
			rs->lanesLeftOfDivider = 0;
			linkSegments.insert(rs);
		}

		//Break?
		if (!currSect->toNode->candidateForSegmentNode) {
			//Make sure the link's end node is represented at the Node level.
			//TODO: Try to avoid dynamic casting if possible.
			//std::cout <<"Adding segment: " <<src.fromNode->id <<"->" <<src.toNode->id <<" to to node\n";
			//dynamic_cast<MultiNode*>(currSect->toNode->generatedNode)->roadSegmentsAt.insert(currSect->generatedSegment);

			//Save it.
			ln->initializeLinkSegments(linkSegments);
			break;
		}


		//Increment.
		Section* nextSection = nullptr;
		for (vector<Section*>::iterator it2=currSect->toNode->sectionsAtNode.begin(); it2!=currSect->toNode->sectionsAtNode.end(); it2++) {
			//Our eariler check guarantees that there will be only ONE node which leads "from" the given segment "to" a node which is not the
			//  same node.
			if ((*it2)->fromNode==currSect->toNode && (*it2)->toNode!=currSect->fromNode) {
				if (nextSection) {
					throw std::runtime_error("UniNode has competing outgoing Sections.");
				}
				nextSection = *it2;
			}
		}
		if (!nextSection) {
			std::cout <<"PATH ERROR:\n";
			std::cout <<"  Starting at Node: " <<src.fromNode->id <<"\n";
			std::cout <<"  Currently at Node: " <<currSect->toNode->id <<"\n";
			throw std::runtime_error("No path reachable from RoadSegment.");
		}
		currSect = nextSection;
	}

	//Now add the link
	res.links.push_back(ln);

}




void sim_mob::aimsun::Loader::ProcessTurning(sim_mob::RoadNetwork& res, Turning& src)
{
	//Check
	if (src.fromSection->toNode->id != src.toSection->fromNode->id) {
		throw std::runtime_error("Turning doesn't match with Sections and Nodes.");
	}

	//Skip Turnings which meet at UniNodes; these will be handled elsewhere.
	sim_mob::Node* meetingNode = src.fromSection->toNode->generatedNode;
	if (dynamic_cast<UniNode*>(meetingNode)) {
		return;
	}

	//Essentially, just expand each turning into a set of LaneConnectors.
	//TODO: This becomes slightly more complex at RoadSegmentNodes, since these
	//      only feature one primary connector per Segment pair.
	for (int fromLaneID=src.fromLane.first; fromLaneID<=src.fromLane.second; fromLaneID++) {
		for (int toLaneID=src.toLane.first; toLaneID<=src.toLane.second; toLaneID++) {
			sim_mob::LaneConnector* lc = new sim_mob::LaneConnector();
			lc->laneFrom = src.fromSection->generatedSegment->lanes.at(fromLaneID);
			lc->laneTo = src.toSection->generatedSegment->lanes.at(toLaneID);

			//Expanded a bit...
			RoadSegment* key = src.fromSection->generatedSegment;
			map<const RoadSegment*, set<LaneConnector*> >& connectors = dynamic_cast<MultiNode*>(src.fromSection->toNode->generatedNode)->connectors;
			connectors[key].insert(lc);
		}
	}

}



void sim_mob::aimsun::Loader::ProcessSectionPolylines(sim_mob::RoadNetwork& res, Section& src)
{
	//The start point is first
	// NOTE: We agreed earlier to include the start/end points; I think this was because it makes lane polylines consistent. ~Seth
	{
		sim_mob::Point2D pt(src.fromNode->generatedNode->location->getX(), src.fromNode->generatedNode->location->getY());
		src.generatedSegment->polyline.push_back(pt);
	}

	//Polyline points are sorted by their "distance from source" and then added.
	std::sort(src.polylineEntries.begin(), src.polylineEntries.end(), polyline_sorter);
	for (std::vector<Polyline*>::iterator it=src.polylineEntries.begin(); it!=src.polylineEntries.end(); it++) {
		//TODO: This might not trace the median, and the start/end points are definitely not included.
		sim_mob::Point2D pt((*it)->xPos, (*it)->yPos);
		src.generatedSegment->polyline.push_back(pt);
	}

	//The ending point is last
	sim_mob::Point2D pt(src.toNode->generatedNode->location->getX(), src.toNode->generatedNode->location->getY());
	src.generatedSegment->polyline.push_back(pt);
}




string sim_mob::aimsun::Loader::LoadNetwork(const string& connectionStr, const map<string, string>& storedProcs, sim_mob::RoadNetwork& rn, std::vector<sim_mob::TripChain*>& tcs)
{
	try {
            //Connection string will look something like this:
            //"host=localhost port=5432 dbname=SimMobility_DB user=postgres password=XXXXX"
            std::cout << "Attempting to connect to remote database..." << std::flush;
            DatabaseLoader loader(connectionStr);
            std::cout << " Success." << std::endl;

		//Step One: Load
		loader.LoadBasicAimsunObjects(storedProcs);

		//Step Two: Translate
		loader.DecorateAndTranslateObjects();

		//Step Three: Perform data-guided cleanup.
		loader.PostProcessNetwork();

		//Step Four: Save
		loader.SaveSimMobilityNetwork(rn, tcs);

		//Temporary workaround; Cut lanes short/extend them as reuquired.
		for (map<int,Section>::const_iterator it=loader.sections().begin(); it!=loader.sections().end(); it++) {
			TMP_TrimAllLaneLines(it->second.generatedSegment, it->second.HACK_LaneLinesStartLineCut, true);
			TMP_TrimAllLaneLines(it->second.generatedSegment, it->second.HACK_LaneLinesEndLineCut, false);
		}

<<<<<<< HEAD
=======
		//Step Three: Perform data-guided cleanup.
		PostProcessNetwork(nodes, sections, crossings, lanes, turnings, polylines);

		//Step Four: Save
		SaveSimMobilityNetwork(rn, tcs, nodes, sections, turnings, polylines, tripchains);
>>>>>>> seth_vectmove

		//Temporary workaround; Cut lanes short/extend them as reuquired.
		for (map<int,Section>::iterator it=sections.begin(); it!=sections.end(); it++) {
			TMP_TrimAllLaneLines(it->second.generatedSegment, it->second.HACK_LaneLinesStartLineCut, true);
			TMP_TrimAllLaneLines(it->second.generatedSegment, it->second.HACK_LaneLinesEndLineCut, false);
		}




	} catch (std::exception& ex) {
		return string(ex.what());
	}

	std::cout <<"AIMSUN Network successfully imported.\n";
	return "";
}

