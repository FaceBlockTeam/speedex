#include "hotstuff/block_storage/block_fetch_manager.h"

#include "hotstuff/block_storage/block_store.h"

#include "hotstuff/network_event_queue.h"

#include "utils/debug_macros.h"

namespace hotstuff {


RequestContext::RequestContext(speedex::Hash const& request)
	: request(request)
	, block_is_received(false)
	, dependent_network_events()
	, requested_from(0)
	{}

bool 
RequestContext::is_received() const {
	return block_is_received.load(std::memory_order_relaxed);
}

void 
RequestContext::mark_received() {
	return block_is_received.store(true, std::memory_order_relaxed);
}

void
RequestContext::add_network_events(std::vector<NetEvent> events)
{
	dependent_network_events.insert(
		dependent_network_events.end(),
		events.begin(),
		events.end());
}

void
ReplicaFetchQueue::do_gc() {

	for (auto it = outstanding_reqs.before_begin(); it != outstanding_reqs.end();)
	{
		if ((*it) -> is_received())
		{
			it = outstanding_reqs.erase_after(it);
			num_reqs--;
		} 
		else
		{
			it++;
		}
	}
}

void 
ReplicaFetchQueue::add_request(request_ctx_ptr req) {
	std::lock_guard lock(mtx);
	outstanding_reqs.insert_after(outstanding_reqs.before_begin(), req);
	worker.add_request(req -> get_requested_hash());
	num_reqs++;
	if (num_reqs > GC_FREQ) {
		do_gc();
	}
}


void
BlockFetchManager::add_replica(ReplicaInfo const& info, NetworkEventQueue& net_queue) {
	queues.emplace(info.id, std::make_unique<ReplicaFetchQueue>(info, net_queue));
}


void
BlockFetchManager::add_fetch_request(speedex::Hash const& requested_block, ReplicaID request_target, std::vector<NetEvent> const& dependent_events)
{
	if (config.is_valid_replica(request_target)) {
		return;
	}

	auto it = outstanding_reqs.find(requested_block);
	
	request_ctx_ptr ctx;

	if (it == outstanding_reqs.end()) {
		ctx = std::make_shared<RequestContext>(requested_block);
		outstanding_reqs.emplace(requested_block, ctx);
	} else {
		ctx = it -> second;
	}

	if (!ctx -> was_requested_from(request_target)) {
		queues.at(request_target)->add_request(ctx);
	}

	ctx -> add_network_events(dependent_events);
}

std::vector<NetEvent>
BlockFetchManager::deliver_block(block_ptr_t blk) {
	auto it = outstanding_reqs.find(blk -> get_hash());

	if (it == outstanding_reqs.end()) {
		HOTSTUFF_INFO("received block with no pending request");
		return {};
	}

	auto req_ctx = it -> second;

	req_ctx -> mark_received();

	outstanding_reqs.erase(it);

	return req_ctx -> get_network_events();
}


} /* hotstuff */