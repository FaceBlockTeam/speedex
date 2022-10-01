#include "automation/experiment_control.h"
#include "automation/get_experiment_vars.h"
#include "automation/get_replica_id.h"

#include "config/replica_config.h"

#include "hotstuff/hotstuff_app.h"
#include "hotstuff/liveness.h"

#include "overlay/overlay_client_manager.h"
#include "overlay/overlay_flooder.h"

#include "speedex/speedex_options.h"
#include "speedex/vm/speedex_vm.h"

#include "synthetic_data_generator/synthetic_data_stream.h"

#include "utils/save_load_xdr.h"
#include "utils/manage_data_dirs.h"

#include "xdr/experiments.h"

#include <optional>

#include <getopt.h>
#include <libfyaml.h>

#include <tbb/global_control.h>


using namespace hotstuff;
using namespace speedex;

using namespace std::chrono_literals;

[[noreturn]]
static void usage() {
	std::printf(R"(
usage: speedex_vm_hotstuff --speedex_options=<options_yaml, required> 
                          --exp_data_folder=<experiment_data path, required>
                          --replica_id=<id, required> 
                          --config_file=<filename, required>
                          --results_folder=<filename, required> (really a prefix to output filenames)
                          --load_from_lmdb <flag, optional>
)");
	exit(1);
}

enum opttag {
	OPT_REPLICA_ID = 0x100,
	OPT_CONFIG_FILE,
	SPEEDEX_OPTIONS,
	EXPERIMENT_DATA_FOLDER,
	RESULTS_FOLDER,
	LOAD_FROM_LMDB
};

static const struct option opts[] = {
	{"replica_id", required_argument, nullptr, OPT_REPLICA_ID},
	{"config_file", required_argument, nullptr, OPT_CONFIG_FILE},
	{"speedex_options", required_argument, nullptr, SPEEDEX_OPTIONS},
	{"exp_data_folder", required_argument, nullptr, EXPERIMENT_DATA_FOLDER},
	{"results_folder", required_argument, nullptr, RESULTS_FOLDER},
	{"load_lmdb", no_argument, nullptr, LOAD_FROM_LMDB},
	{nullptr, 0, nullptr, 0}
};

ExperimentParameters load_params(std::string filename) {
	ExperimentParameters out;
	if (load_xdr_from_file(out, filename.c_str())) {
		throw std::runtime_error(std::string("failed to load params from file ") + filename);
	}
	return out;
}


int main(int argc, char* const* argv)
{
	std::optional<ReplicaID> self_id;
	std::optional<std::string> config_file;

	std::string speedex_options_file;
	std::string experiment_data_folder;
	std::string experiment_results_folder;

	bool load_from_lmdb = false;
	
	int opt;

	while ((opt = getopt_long_only(argc, argv, "",
				 opts, nullptr)) != -1)
	{
		switch(opt) {
			case OPT_REPLICA_ID:
				self_id = std::stol(optarg);
				break;
			case OPT_CONFIG_FILE:
				config_file = optarg;
				break;
			case SPEEDEX_OPTIONS:
				speedex_options_file = optarg;
				break;
			case EXPERIMENT_DATA_FOLDER:
				experiment_data_folder = optarg;
				break;
			case RESULTS_FOLDER:
				experiment_results_folder = optarg;
				break;
			case LOAD_FROM_LMDB:
				load_from_lmdb = true;
				break;
			default:
				usage();
		}
	}

	if (!self_id) {
		self_id = get_replica_id();
	}
	if (!config_file) {
		config_file = get_config_file();
	}

	struct fy_document* fyd = fy_document_build_from_file(NULL, config_file->c_str());
	if (fyd == NULL) {
		std::printf("Failed to build doc from file \"%s\"\n", config_file->c_str());
		usage();
	}

	auto [config, sk] = parse_replica_config(fyd, *self_id);

	fy_document_destroy(fyd);

	if (speedex_options_file.size() == 0) {
		speedex_options_file = get_speedex_options();
	}

	if (experiment_data_folder.size() == 0) {
		experiment_data_folder = get_experiment_data_folder();
	}

	if (experiment_results_folder.size() == 0) {
		experiment_results_folder = get_experiment_results_folder();
	}

	size_t num_threads = get_num_threads();

	std::string experiment_params_file = experiment_data_folder + "params";

	ExperimentParameters params = load_params(experiment_params_file);

	SpeedexOptions speedex_options;
	speedex_options.parse_options(speedex_options_file.c_str());

	if (speedex_options.num_assets != params.num_assets) {
		throw std::runtime_error("mismatch in num assets between speedex_options and experiment_options");
	}

	if (config.nreplicas != params.n_replicas) {
		std::printf("WARNING: mismatch between experiment data sharding and num replicas\n");
	}

	make_all_data_dirs(config.get_info(*self_id));

	auto vm = std::make_shared<SpeedexVM>(params, speedex_options, experiment_results_folder);

	auto app = hotstuff::make_speculative_hotstuff_instance(config, *self_id, sk, vm);

//	HotstuffApp app(config, *self_id, sk, vm);

	if (load_from_lmdb) {
		app->init_from_disk();
	} else {
		app->init_clean();
	}

	SyntheticDataStream data_stream(experiment_data_folder);

	auto& mp = vm -> get_mempool();

	OverlayServer server(mp, config);

	OverlayClientManager client_manager(config, *self_id, mp, server.get_handler());

	tbb::global_control control(
		tbb::global_control::max_allowed_parallelism, num_threads);

	ExperimentController control_server(vm);
	control_server.wait_for_breakpoint_signal();

	OverlayFlooder flooder(data_stream, client_manager, server, 2'000'000);

	PaceMakerWaitQC pmaker(app);

	if (*self_id == 0) {
		pmaker.set_self_as_proposer();
	}

	std::this_thread::sleep_for(2000ms);

	bool self_signalled_end = false;

	while (true) {
		if (pmaker.should_propose()) {
			app->put_vm_in_proposer_mode();
			pmaker.do_propose();
			pmaker.wait_for_qc();
		} else {
			std::this_thread::sleep_for(1000ms);
		}

		/* Experiment control conditions */

		// conditions only activate for current producer
		if (vm -> experiment_is_done()) {
			app->stop_proposals();
			self_signalled_end = true;
		}
		if (app->proposal_buffer_is_empty()) {
			std::printf("done with experiment\n");

			//flush proposal buffers
			pmaker.do_empty_propose();
			pmaker.wait_for_qc();
			pmaker.do_empty_propose();
			pmaker.wait_for_qc();
			pmaker.do_empty_propose();
			pmaker.wait_for_qc();

			control_server.wait_for_breakpoint_signal();
			vm -> write_measurements();
			exit(0);
		}

		// conditions for validator nodes
		if (control_server.producer_is_done_signal_was_received() && (!self_signalled_end)) {
			std::printf("leader terminated experiment, waiting for signal\n");
			control_server.wait_for_breakpoint_signal();
			vm -> write_measurements();
			exit(0);
		}
	}
}
