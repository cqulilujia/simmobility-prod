//Copyright (c) 2013 Singapore-MIT Alliance for Research and Technology
//Licensed under the terms of the MIT License, as described in the file:
//   license.txt   (http://opensource.org/licenses/MIT)

#include "SplitPlan.hpp"

#include <cstdio>
#include <sstream>

#include "Signal.hpp"

using std::vector;

namespace {
const int NUMBER_OF_VOTING_CYCLES = 5;
} //End un-named namespace

using namespace sim_mob;

void sim_mob::SplitPlan::setCycleLength(std::size_t c = 96) {
	cycleLength = c;
}
void sim_mob::SplitPlan::setcurrSplitPlanID(std::size_t index) {
	currSplitPlanID = index;
}
void sim_mob::SplitPlan::setChoiceSet(std::vector<vector<double> > choiceset) {
	choiceSet = choiceset;
	NOF_Plans = choiceSet.size();
}
const std::vector<vector<double> > &sim_mob::SplitPlan::getChoiceSet() const {
	return choiceSet;
}

std::size_t sim_mob::SplitPlan::CurrSplitPlanID() {
	return currSplitPlanID;
}
double sim_mob::SplitPlan::getCycleLength() const {
	return cycleLength;
}

std::size_t sim_mob::SplitPlan::getOffset() const {
	return offset;
}

void sim_mob::SplitPlan::setOffset(std::size_t value) {
	offset = value;
}

/**
 * This function has 2 duties
 * 1- Update the Votes data structure
 * 2- Return the index having the highest value(vote) with the help of another function(getMaxVote())
 */
std::size_t sim_mob::SplitPlan::Vote(std::vector<double> maxproDS) {
	std::vector<int> vote(NOF_Plans, 0); //choiceSet.size()=no of plans
	vote[fmin_ID(maxproDS)]++; //the corresponding split plan's vote is incremented by one(the rests are zero)
	votes.push_back(vote);
	if (votes.size() > NUMBER_OF_VOTING_CYCLES)
		votes.erase(votes.begin()); //removing the oldest vote if necessary(wee keep only NUMBER_OF_VOTING_CYCLES records)
	/*
	 * step 3: The split plan with the highest vote in the last certain number of cycles will win the vote
	 *         no need to eat up your brain, in the following chunk of code I will get the id of the maximum
	 *         value in vote vector and that id will actually be my nextSplitPlanID
	 */
	return getMaxVote();
}

///calculate the projected DS and max Projected DS for each split plan (for internal use only, refer to section 4.3 table-4)
void sim_mob::SplitPlan::calMaxProDS(std::vector<double> &maxproDS,
		std::vector<double> DS) {

	vector<double> proDS(parentSignal->getNOF_Phases(), 0);
	for (int i = 0; i < NOF_Plans; i++) //traversing the columns of Phase::choiceSet matrix
	{
		double maax = 0.0;
		for (int j = 0; j < parentSignal->getNOF_Phases(); j++) {
			//calculate the projected DS for this plan
			proDS[j] = DS[j] * choiceSet[currSplitPlanID][j] / choiceSet[i][j];
			//then find the Maximum Projected DS for each Split plan(used in the next step)
			if (maax < proDS[j])
				maax = proDS[j];
		}
		maxproDS[i] = maax;
	}
}

///	Split Plan Selection (use DS to choose SplitPlan for next cycle)(section 4.3 of the Li Qu's manual)
std::size_t sim_mob::SplitPlan::findNextPlanIndex(std::vector<double> DS) {

	std::vector<double> maxproDS(NOF_Plans, 0); // max projected DS of each SplitPlan

	//step 1:Calculate the Max projected DS for each approach (and find the maximum projected DS)
	calMaxProDS(maxproDS, DS);
	//step2 2 & 3 in one function to save your brain.
	//Step 2: The split plan with the ' lowest "Maximum Projected DS" ' will get a vote
	//step 3: The split plan with the highest vote in the last certain number of cycles will win the vote
	nextSplitPlanID = Vote(maxproDS);
	return nextSplitPlanID;
}

void sim_mob::SplitPlan::updatecurrSplitPlan() {
	currSplitPlanID = nextSplitPlanID;
}

void sim_mob::SplitPlan::initialize() {
}

//find the minimum among the max projected DS
double sim_mob::SplitPlan::fmin_ID(std::vector<double> maxproDS) {
	int min = 0;
	for (int i = 1; i < maxproDS.size(); i++) {
		if (maxproDS[i] < maxproDS[min]) {
			min = i;
		}
	}
	return min;	//(Note: not minimum value ! but minimum value's "index")
}

///find the split plan Id which currently has the maximum vote
///remember : in votes, columns represent split plan vote
std::size_t sim_mob::SplitPlan::getMaxVote() {
	int PlanId_w_MaxVote = -1;
	int SplitPlanID;
	int max_vote_value = -1;
	
	for (SplitPlanID = 0; SplitPlanID < NOF_Plans; SplitPlanID++)//column iterator(plans)
	{
		//calculating sum of votes in each column
		int vote_sum = 0;
		for (int i = 0; i < votes.size(); i++)	//row iterator(cycles)
			vote_sum += votes[i][SplitPlanID];
		if (max_vote_value < vote_sum) {
			max_vote_value = vote_sum;
			PlanId_w_MaxVote = SplitPlanID;	// SplitPlanID with max vote so far
		}
	}
	return PlanId_w_MaxVote;
}

std::vector<double> sim_mob::SplitPlan::CurrSplitPlan() {
	if (choiceSet.size() == 0) {
		std::ostringstream out("");
		out << "Signal " << this->parentSignal->getId()
				<< " .  Choice Set is empty the program can crash without it";
		throw std::runtime_error(out.str());
	}
	return choiceSet[currSplitPlanID];
}

///	returns number of split plans available
std::size_t sim_mob::SplitPlan::nofPlans() {
	return NOF_Plans;
}

//find the max DS
double sim_mob::SplitPlan::fmax(std::vector<double> &DS) {
	double max = DS[0];
	for (int i = 0; i < DS.size(); i++) {
		if (DS[i] > max) {
			max = DS[i];
		}
	}
	return max;
}

void sim_mob::SplitPlan::Update(std::vector<double> &DS) {
	double DS_all = fmax(DS);
	cycle_.Update(DS_all);
	cycleLength = cycle_.getcurrCL();
	findNextPlanIndex(DS);
	updatecurrSplitPlan();
	initialize();
}

/*
 * find out which phase we are in the current plan
 * based on the currCycleTimer(= cycle-time lapse so far)
 * at the moment, this function just returns what phase it is going to be(does not set any thing)
 */
sim_mob::SplitPlan::SplitPlan(double cycleLength_, double offset_,/*int signalTimingMode_,*/
		unsigned int TMP_PlanID_) :
		cycleLength(cycleLength_), offset(offset_), TMP_PlanID(TMP_PlanID_) {
//	signalTimingMode = ConfigParams::GetInstance().signalTimingMode;
	nextSplitPlanID = 0;
	currSplitPlanID = 0;
	NOF_Plans = 0;
	cycle_.setCurrCL(cycleLength_);
}

//fill the choice set with default ones based on the number of intersection's approaches
void sim_mob::SplitPlan::fill(double defaultChoiceSet[5][10], int approaches) {
	for (int i = 0; i < NOF_Plans; i++)
		for (int j = 0; j < approaches; j++) {
			choiceSet[i][j] = defaultChoiceSet[i][j];
		}
}

void sim_mob::SplitPlan::setDefaultSplitPlan(int approaches) {
	//NOTE: This is barely used; may want to remove it entirely.
	const int signalTimingMode = 1;

	NOF_Plans = 5;
	if (signalTimingMode == 0) {	//fixed plan
		NOF_Plans = 1;
	}
	
	double defaultChoiceSet_1[5][10] = { { 100 }, { 100 }, { 100 }, { 100 }, {100 } };

	double defaultChoiceSet_2[5][10] = { { 50, 50 },
										 { 30, 70 },
										 { 75, 25 },
										 { 60, 40 },
										 { 40, 60 } };

	double defaultChoiceSet_3[5][10] = { { 33, 33, 34 },
										 { 40, 20, 40 },
										 { 25, 50, 25 },
										 { 25, 25, 50 },
										 { 50, 25, 25 } };

	double defaultChoiceSet_4[5][10] = { { 25, 25, 25, 25 },
										 { 20, 35, 20, 25 },
										 { 35, 35, 20, 10 },
										 { 35, 30, 10, 25 },
										 { 20, 35, 25, 20 } };

	double defaultChoiceSet_5[5][10] = { { 20, 20, 20, 20, 20 },
										 { 15, 15, 25, 25, 20 },
										 { 30, 30, 20, 10, 10 },
										 { 25, 25, 20, 15, 15 },
										 { 10, 15, 20, 25, 30 } };

	double defaultChoiceSet_6[5][10] = { { 16, 16, 17, 17, 17, 17 },
										 { 10, 15, 30, 20, 15, 10 },
										 { 30, 20, 15, 15, 10, 10 },
										 { 20, 30, 20, 10, 10, 10 },
										 { 15, 15, 20, 20, 15, 15 } };

	double defaultChoiceSet_7[5][10] = { { 14, 14, 14, 14, 14, 15, 15 },
										 { 30, 15, 15, 10, 10, 10, 10 },
										 { 15, 30, 10, 15, 10, 10, 10 },
										 { 15, 20, 20, 15, 10, 10, 10 },
										 { 10, 10, 10, 20, 20, 15, 15 } };


	choiceSet.resize(NOF_Plans, vector<double>(approaches));

	switch (approaches) {
	case 1:
		fill(defaultChoiceSet_1, 1);
		break;
	case 2:
		fill(defaultChoiceSet_2, 2);
		break;
	case 3:
		fill(defaultChoiceSet_3, 3);
		break;
	case 4:
		fill(defaultChoiceSet_4, 4);
		break;
	case 5:
		fill(defaultChoiceSet_5, 5);
		break;
	case 6:
		fill(defaultChoiceSet_6, 6);
		break;
	case 7:
		fill(defaultChoiceSet_7, 7);
		break;
	}

	currSplitPlanID = 0;
}