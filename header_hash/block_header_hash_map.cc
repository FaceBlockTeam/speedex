#include "block_header_hash_map.h"

#include "utils/big_endian.h"

namespace speedex {

void BlockHeaderHashMap::insert_for_production(uint64_t block_number, const Hash& block_hash) {

	if (block_number == 0) {
		if (last_committed_block_number != 0 || block_map.size() != 0) {
			throw std::runtime_error(
				"can't insert prev block 0 if we already have elements in block hash map");
		}
		Hash zero_hash;
		zero_hash.fill(0);
		if (memcmp(block_hash.data(), zero_hash.data(), zero_hash.size()) != 0) {
			throw std::runtime_error("can't have genesis block with nonzero hash");
		}
		//prev block is genesis block, do nothing
		return;
	}

	if (block_number != last_committed_block_number + 1) {
		throw std::runtime_error("inserting wrong block number");
	}

	prefix_t key_buf;

	write_unsigned_big_endian(key_buf, block_number);

	block_map.insert(key_buf, HashWrapper(block_hash));
	
	// Difference between production and validation is here.
	last_committed_block_number = block_number;
}

void BlockHeaderHashMap::persist_lmdb(uint64_t current_block_number) {
	BLOCK_INFO("persisting header hash map at round %lu", current_block_number);

	if (!lmdb_instance) {
		return;
	}
	uint64_t persisted_round_number 
		= lmdb_instance.get_persisted_round_number();

	auto wtx = lmdb_instance.wbegin();

	//We don't commit current block's hash because we don't have it yet.
	for (uint64_t i = persisted_round_number; i < current_block_number; i++) { 
		if (i == 0) continue;
		TrieT::prefix_t round_buf;


		write_unsigned_big_endian(round_buf, i);
		auto round_bytes = round_buf.get_bytes_array();
		dbval key{round_bytes};

		// querying for round i
		auto hash_opt = block_map.get_value(round_buf);
		if (!hash_opt) {
			throw std::runtime_error("did not find hash in hash_map!");
		}

		dbval hash_val{*hash_opt};
		wtx.put(lmdb_instance.get_data_dbi(), key, hash_val);
	}

	lmdb_instance.commit_wtxn(wtx, current_block_number);
}

// LMDB committed to round X contains entries 1 through X-1.
// To sync back with LMDB, we need to remove all entries X and higher.

void 
BlockHeaderHashMap::rollback_to_committed_round(uint64_t committed_block_number)
{
	if (committed_block_number < lmdb_instance.get_persisted_round_number()) {
		throw std::runtime_error("can't rollback beyond lmdb persist");
	}
	for (uint64_t i = committed_block_number; i <= last_committed_block_number; i++) {
		if (i == 0) continue;
		TrieT::prefix_t round_buf;
		write_unsigned_big_endian(round_buf, i);

		if (!block_map.perform_deletion(round_buf)) {
			throw std::runtime_error("error when deleting from header hash map");
		}
	}
	last_committed_block_number = (committed_block_number == 0) ? 0 : committed_block_number - 1;
}



bool 
BlockHeaderHashMap::tentative_insert_for_validation(
	uint64_t block_number, const Hash& block_hash) 
{
	if (block_number == 0) {
		if (last_committed_block_number != 0 || block_map.size() != 0) {
			throw std::runtime_error(
				"can't insert prev block 0 if we already have elements in block hash map");
		}
		Hash zero_hash;
		zero_hash.fill(0);
		if (memcmp(
				block_hash.data(), 
				zero_hash.data(), 
				zero_hash.size()) != 0) {
			throw std::runtime_error(
				"can't have genesis block with nonzero hash");
		}
		//validation prev block is genesis block, do nothing
		return true;
	}



	//input block number corresponds to previous block.
	if (block_number != last_committed_block_number) {
		return false;
	}

	prefix_t key_buf;

	write_unsigned_big_endian(key_buf, block_number);

	block_map.insert(key_buf, HashWrapper(block_hash));

	return true;
}
void BlockHeaderHashMap::rollback_validation() {
	
	prefix_t key_buf;
	write_unsigned_big_endian(key_buf, last_committed_block_number + 1);

	block_map.perform_deletion(key_buf);
}
void BlockHeaderHashMap::finalize_validation(uint64_t finalized_block_number) {
	if (finalized_block_number < last_committed_block_number) {
		throw std::runtime_error("can't finalize prior block");
	}
	last_committed_block_number = finalized_block_number;
}

void BlockHeaderHashMap::load_lmdb_contents_to_memory() {
	auto rtx = lmdb_instance.rbegin();

	auto cursor = rtx.cursor_open(lmdb_instance.get_data_dbi());

	for (auto kv : cursor) {
		auto bytes = kv.first.bytes();
		uint64_t round_number;

		read_unsigned_big_endian(bytes.data(), round_number);


		if (round_number > lmdb_instance.get_persisted_round_number()) {

			std::printf(
				"round number: %lu persisted_round_number: %lu\n", 
				round_number, 
				lmdb_instance.get_persisted_round_number());
			std::fflush(stdout);
			throw std::runtime_error(
				"lmdb contains round idx beyond committed max");
		}

		prefix_t round_buf;
		write_unsigned_big_endian(round_buf, round_number);

		auto value = HashWrapper();
		if (kv.second.mv_size != 32) {
			throw std::runtime_error("invalid value size");
		}
		memcpy(value.data(), kv.second.mv_data, 32);
		block_map.insert(round_buf, value);
	}
	last_committed_block_number = lmdb_instance.get_persisted_round_number();
	rtx.commit();
}



} /* speedex */
