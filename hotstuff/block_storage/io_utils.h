#pragma once

#include "xdr/hotstuff.h"

namespace hotstuff {

std::string
block_filename(const HotstuffBlockWire& block);

std::string
block_filename(const speedex::Hash& header_hash);

void save_block(const HotstuffBlockWire& block);

HotstuffBlockWire load_block(const speedex::Hash& req_header_hash);

} /* hotstuff */
