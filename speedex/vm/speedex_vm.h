#pragma once

#include "block_processing/block_producer.h"
#include "block_processing/block_validator.h"

#include "mempool/mempool_structures.h"

#include "modlog/log_merge_worker.h"

#include "speedex/speedex_management_structures.h"
#include "speedex/speedex_options.h"
#include "speedex/speedex_persistence.h"
#include "speedex/vm/speedex_vm_block_id.h"

#include "xdr/block.h"

#include <mutex>


namespace hotstuff {
class HotstuffLMDB;
} /* hotstuff */

namespace speedex {

class ExperimentParameters;

class SpeedexVM {
	const size_t PERSIST_BATCH;
	
	SpeedexManagementStructures management_structures;

	std::mutex operation_mtx;
	std::mutex confirmation_mtx;

	HashedBlock proposal_base_block;
	HashedBlock last_committed_block;

	AsyncPersister async_persister;
	
	SpeedexMeasurements measurements_log;

	std::string measurement_output_folder;

	const SpeedexOptions& options;
	const ExperimentParameters& params;

	//constexpr static bool small = false;
	const size_t TARGET_BLOCK_SIZE;
	//constexpr static size_t TARGET_BLOCK_SIZE = small ? 50'000 : 500'000;
	constexpr static size_t MEMPOOL_CHUNK_SIZE = 100;//1'000;
	const size_t MEMPOOL_TARGET_SIZE;

	TatonnementManagementStructures tatonnement_structs;
	std::vector<Price> prices;

	MempoolStructures mempool_structs;

	LogMergeWorker log_merge_worker;

	BlockProducer block_producer;
	BlockValidator block_validator;

	std::atomic<bool> experiment_done = false;

	void rewind_structs_to_committed_height();
	size_t assemble_block(TaggedSingleBlockResults& measurements_base, BlockStateUpdateStatsWrapper& state_update_stats);

	ExperimentResultsUnion 
	get_measurements_nolock();

public:
	using block_type = HashedBlockTransactionListPair;
	using block_id = SpeedexVMBlockID;

	static block_id nonempty_block_id(const block_type& blk) {
		return SpeedexVMBlockID(blk.hashedBlock);
	}
	static block_id empty_block_id() {
		return SpeedexVMBlockID();
	}

	SpeedexVM(
		const ExperimentParameters& params,
		const SpeedexOptions& options,
		std::string measurement_output_folder);

	std::unique_ptr<block_type> propose();

	void exec_block(const block_type& blk);

	void log_commitment(const block_id& id);

	void write_measurements();
	ExperimentResultsUnion get_measurements();

	std::string 
	overall_measurement_filename() const {
		return measurement_output_folder + "results";
	}

	~SpeedexVM() {
		write_measurements();
	}

	void rewind_to_last_commit();

	void init_clean();
	void init_from_disk(hotstuff::HotstuffLMDB const& decided_block_cache);


	// expose state to non-hotstuff components
	bool experiment_is_done() const {
		return experiment_done;
	}

	uint64_t get_lead_block_height();

	Mempool& get_mempool() {
		return mempool_structs.mempool;
	}
};

} /* speedex */
