/* Copyright Singapore-MIT Alliance for Research and Technology */

#include "MapDecompositionWithInformationFlow.hpp"

using namespace std;
using namespace sim_mob_partitioning;

void MapDecompositionWithInformationFlow::do_map_partitioning(Configurations& config)
{
	generate_required_parameters_with_infor(config);
	do_map_decompose_with_infor(config);
	result_analysis_with_infor(config);
}

void MapDecompositionWithInformationFlow::generate_required_parameters_with_infor(Configurations& config)
{
	/**
	 * Generate the inputs for hMETIS Algorithm
	 */
	string command_hmetis_input = "java -jar resources/FlowAndInforToHMETIS.jar";
	command_hmetis_input += " " + config.node_file;
	command_hmetis_input += " " + config.flow_file;
	command_hmetis_input += " " + config.infor_file;
	command_hmetis_input += " " + config.output_folder + "//hmetis.input";

	/**
	 * Execute commands (Java Codes)
	 */
	if (system(command_hmetis_input.c_str()) != 0)
		throw "command FlowAndInforToHMETIS failed";

	/**
	 * Generate the inputs for METIS Algorithm
	 */
	string command_metis_input = "java -jar resources/FlowAndInforToMETIS.jar";
	command_metis_input += " " + config.node_file;
	command_metis_input += " " + config.flow_file;
	command_metis_input += " " + config.output_folder + "//metis.input";

	/**
	 * Execute commands (Java Codes)
	 */
	if (system(command_metis_input.c_str()) != 0)
		throw "command FlowAndInforToMETIS failed";
}

void MapDecompositionWithInformationFlow::do_map_decompose_with_infor(Configurations& config)
{
	/**
	 * Generate the command to execute hMETIS Algorithm
	 */
	string command_hmetis = "resources/hmetis2.0pre1";

	if (config.nparts == 2)
	{
		int imbalance = (int) ((config.load_imbalance - 1.0) * 100 / 2);
		stringstream ss_0;
		ss_0 << imbalance;

		command_hmetis += " -ufactor=" + ss_0.str();
		command_hmetis += " -otype=cut";
		command_hmetis += " -reconst";

		if (config.execute_speed != 0)
			command_hmetis += " -kwayrefine";
	}
	else
	{
		int imbalance = (int) ((config.load_imbalance - 1.0) * 100);
		imbalance = imbalance < 5 ? 5 : imbalance;

		stringstream ss_0;
		ss_0 << imbalance;

		command_hmetis += " -ufactor=" + ss_0.str();
		command_hmetis += " -otype=soed";
		command_hmetis += " -ptype=kway";

		//this refinement algorithm will make it slow
		if (config.execute_speed == 2)
			command_hmetis += " -rtype=kpfast";
	}

	if (config.execute_speed >= 1)
		command_hmetis += " -nruns=20";

	command_hmetis += " " + config.output_folder + "/hmetis.input";

	stringstream ss_0_1;
	ss_0_1 << config.nparts;
	command_hmetis += " " + ss_0_1.str();

	/**
	 * Execute hMETIS (C Code)
	 */
	if (system(command_hmetis.c_str()) != 0)
		throw "command hmetis2.0 (hMETIS) failed";

	/**
	 * Generate the command to execute METIS Algorithm
	 */
	string command_metis = "resources/gpmetis ";

	int metis_imbalance = (int) ((config.load_imbalance - 1.0) * 1000);
	stringstream ss_1;
	ss_1 << metis_imbalance;
	command_metis += " -ufactor=" + ss_1.str();
	command_metis += " " + config.output_folder + "/metis.input";
	command_metis += " " + ss_0_1.str();

	/**
	 * Execute METIS (C Code)
	 */
	if (system(command_metis.c_str()) != 0)
		throw "command gpmetis (METIS) failed";
}

void MapDecompositionWithInformationFlow::result_analysis_with_infor(Configurations& config)
{
	/**
	 * Generate the command to analysis hMETIS results
	 */
	stringstream ss_0_1;
	ss_0_1 << config.nparts;

	string command_analysis_hmetis = "java -jar resources/FlowAndInforPartitionResultAnalysis.jar";
	command_analysis_hmetis += " " + config.node_file;
	command_analysis_hmetis += " " + config.flow_file;
	command_analysis_hmetis += " " + config.infor_file;
	command_analysis_hmetis += " " + config.output_folder + "/hmetis.input.part." + ss_0_1.str();
	command_analysis_hmetis += " " + config.output_folder + "/hmetis.analysis";

	/**
	 * Execute commands (Java Codes)
	 */
	if (system(command_analysis_hmetis.c_str()) != 0)
		throw "command FlowAndInforPartitionResultAnalysis failed";

	/**
	 * Generate the command to analysis METIS results
	 * in most case, hMETIS is better than METIS,
	 * But, you can also read the analysis report to check which one to use
	 */

	string command_analysis_metis = "java -jar resources/FlowAndInforPartitionResultAnalysis.jar";
	command_analysis_metis += " " + config.node_file;
	command_analysis_metis += " " + config.flow_file;
	command_analysis_metis += " " + config.infor_file;
	command_analysis_metis += " " + config.output_folder + "/metis.input.part." + ss_0_1.str();
	command_analysis_metis += " " + config.output_folder + "/metis.analysis";

	/**
	 * Execute commands (Java Codes)
	 */
	if (system(command_analysis_metis.c_str()) != 0)
		throw "command FlowAndInforPartitionResultAnalysis failed";
}
