#pragma once
/*! \file block_header_hash_map.h

The block header-hash map is a merkle trie mapping block number to block hash.

Possible future optimization: Block numbers increment sequentially.
Once some subtrie fills up, it will never be modified again.  We don't need
to load that data into memory.  Would only be relevant if Speedex runs for 
millions of blocks.
*/

#include "config.h"

#include "lmdb/lmdb_wrapper.h"

#include "trie/merkle_trie.h"
#include "trie/merkle_trie_utils.h"


#include "xdr/types.h"

namespace edce {


/*! LMDB instance for persisting block header hashes to disk
*/
struct BlockHeaderHashMapLMDB : public LMDBInstance {
	constexpr static auto DB_NAME = "header_hash_lmdb";

	BlockHeaderHashMapLMDB() : LMDBInstance() {}

	void open_env() {
		LMDBInstance::open_env(
			std::string(ROOT_DB_DIRECTORY) + std::string(HEADER_HASH_DB));
	}

	void create_db() {
		LMDBInstance::create_db(DB_NAME);
	}

	void open_db() {
		LMDBInstance::open_db(DB_NAME);
	}
};

/*! Stores a merkle trie mapping block numbers to block root hashes.
*/
struct BlockHeaderHashMap {
	using HashWrapper = XdrTypeWrapper<Hash>;
	constexpr static unsigned int KEY_LEN = sizeof(uint64_t);

	using prefix_t = ByteArrayPrefix<KEY_LEN>;

	using ValueT = HashWrapper;
	using MetadataT = CombinedMetadata<SizeMixin>;

	using TrieT = MerkleTrie<prefix_t, ValueT, MetadataT>;

	TrieT block_map;

	BlockHeaderHashMapLMDB lmdb_instance;

	uint64_t last_committed_block_number;

public:

	//! Construct empty map.
	BlockHeaderHashMap() 
		: block_map(), lmdb_instance(), last_committed_block_number(0) {}

	/*! Insert hash of a newly produced block.
		In normal operation, map should include hashes for 
		[0, last_committed_block_number) and block_number input is 
		prev_block = last_committed_block_number
	*/
	void insert_for_production(uint64_t block_number, const Hash& block_hash);

	//! Insert hash of a block when validating a block.
	//! Difference with production is some minor accounting related to rolling
	//! back an insertion.
	//! Should be followed either by rollback_validation() or
	//! finalize_validation() 

	bool tentative_insert_for_validation(
		uint64_t block_number, const Hash& block_hash);
	//! Undo the last block hash insertion (i.e. if subsequent, unrelated
	//! validation checks failed)
	void rollback_validation();
	//! Finalize the insertion of a block hash (when validating a block).
	void finalize_validation(uint64_t finalized_block_number);

	//! Hash the merkle trie.
	void hash(Hash& hash) {
		block_map.hash(hash);
	}

	void open_lmdb_env() {
		lmdb_instance.open_env();
	}
	void create_lmdb() {
		lmdb_instance.create_db();
	}
	void open_lmdb() {
		lmdb_instance.open_db();
	}

	//! Persist block hashes to LMDB, up to current block number.
	void persist_lmdb(uint64_t current_block_number);

	//! Get the block number reflected in disk state.
	uint64_t get_persisted_round_number() {
		return lmdb_instance.get_persisted_round_number();
	}

	//! Read in trie contents from disk.
	void load_lmdb_contents_to_memory();
};

//! Mock around BlockHeaderHashMap that makes calls into no-ops when replaying
//! a block who's state changes are already reflected in lmdb.
class LoadLMDBHeaderMap : public LMDBLoadingWrapper<BlockHeaderHashMap&> {

	using LMDBLoadingWrapper<BlockHeaderHashMap&> :: generic_do;
public:
	LoadLMDBHeaderMap(
		uint64_t current_block_number,
		BlockHeaderHashMap& main_db) 
	: LMDBLoadingWrapper<BlockHeaderHashMap&>(current_block_number, main_db) {}

	//! Insert a block hash when replaying trusted blocks.
	void insert_for_loading(uint64_t block_number, const Hash& block_hash) {
		return generic_do<&BlockHeaderHashMap::insert_for_production>(
			block_number, block_hash);
	}
};

} /* speedex */