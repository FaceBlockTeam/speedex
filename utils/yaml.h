/**
 * SPEEDEX: A Scalable, Parallelizable, and Economically Efficient Decentralized Exchange
 * Copyright (C) 2023 Geoffrey Ramseyer

 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <utils/cleanup.h>

#include <libfyaml.h>

namespace speedex
{

struct yaml : utils::unique_destructor_t<fy_document_destroy>
{
	yaml(std::string const& filename)
	{
		reset(fy_document_build_from_file(NULL, filename.c_str()));
	}

	yaml(const char* filename)
	{
		reset(fy_document_build_from_file(NULL, filename));
	}
};
	
}