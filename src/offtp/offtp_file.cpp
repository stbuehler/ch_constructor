#include "offtp_file.h"

#include <cmath>
#include <set>
#include <limits>

#include <arpa/inet.h>

namespace offtp {
	using namespace offtp::impl;

	namespace {
		uint64_t align4k(uint64_t offset) {
			return (offset + 4095u) & ~4095u;
		}

		native_coords native_coords_for(coords c)
		{
			return native_coords{
				static_cast<uint32_t>(std::round(c.lon * 1e7)),
				static_cast<uint32_t>(std::round(c.lat * 1e7))
			};
		}

		uint64_t square_distance(native_coords a, native_coords b) {
			int64_t dlon = int64_t{a.lon} - int64_t{b.lon};
			int64_t dlat = int64_t{a.lat} - int64_t{b.lat};
			return static_cast<uint64_t>(dlon*dlon + dlat*dlat);
		}

		struct node_geo {
			uint32_t id = 0;
			native_coords coords;
		};
	}

	class GraphFile::node_geo_iterator {
	public:
		node_geo_iterator(GraphFile &gf)
		: m_gf(gf) {
		}

		void load_block(uint32_t block_nr) {
			m_block_remaining = 0;
			m_next_block = block_nr;
		}

		bool next() {
			if (!load_block()) return false;

			uint32_t node_geo[2];
			if (!read(node_geo, 2)) return false;
			m_node.id = m_next_node_id++;
			m_node.coords.lon = node_geo[0];
			m_node.coords.lat = node_geo[1];
			--m_block_remaining;

			return true;
		}

		node_geo& operator*() { return m_node; }
		node_geo* operator->() { return &m_node; }

	private:
		bool read(uint32_t* target, uint32_t count) {
			bool result = m_gf.read_uint32_array(m_current_offset, target, count);
			m_current_offset += count * sizeof(uint32_t);
			return result;
		}

		bool load_block() {
			while (0 == m_block_remaining)
			{
				if (m_next_block >= m_gf.m_header.block_count) return false;
				if (m_visited_blocks.insert(m_next_block).second) return false; // already know this block
				m_current_offset = m_gf.m_meta.offset_node_geo + m_next_block * m_gf.m_meta.stride;

				m_next_node_id = m_next_block * (m_gf.m_header.block_size + 1);

				uint32_t block_header[2];
				if (!read(block_header, 2)) return false;
				m_next_block = block_header[0];
				m_block_remaining = block_header[1];
			}
			return true;
		}

		GraphFile& m_gf;

		node_geo m_node;

		uint32_t m_next_node_id;
		uint64_t m_current_offset = 0;
		uint32_t m_block_remaining = 0;
		uint32_t m_next_block = 0;
		std::set<uint32_t> m_visited_blocks;
	};

	GraphFile::GraphFile(std::istream& is)
	: m_is(is) {
	}

	bool GraphFile::load_header() {
		uint32_t h[13];

		if (!read_uint32_array(0, h, 13)) return false;

		// magic ("CHGOffTP") + version (1)
		if (h[0] != 0x4348474Fu || h[1] != 0x66665450u || h[2] != 1u) return false;

		m_header.base_cell_x      = h[ 3];
		m_header.base_cell_y      = h[ 4];
		m_header.base_cell_width  = h[ 5];
		m_header.base_cell_height = h[ 6];
		m_header.base_grid_width  = h[ 7];
		m_header.base_grid_height = h[ 8];
		m_header.block_size       = h[ 9];
		m_header.block_count      = h[10];
		m_header.core_block_start = h[11];
		m_header.edge_count       = h[12];

		m_meta.stride = (m_header.block_size + 1) * 2 * sizeof(uint32_t);
		m_meta.offset_node_edges = align4k(m_meta.offset_node_geo + m_header.block_count * m_meta.stride);
		m_meta.offset_edges = align4k(m_meta.offset_node_edges + m_header.block_count * m_meta.stride);
		m_meta.offset_edges_details = align4k(m_meta.offset_edges + m_header.edge_count * 8);

		return true;
	}

	bool GraphFile::read_uint32_array(uint64_t offset, uint32_t* target, uint32_t count)
	{
		if (0 == count) return true;

		m_is.seekg(static_cast<std::istream::off_type>(offset), std::ios_base::beg);
		if (!m_is) return false;
		auto want = static_cast<std::streamsize>(count * sizeof(uint32_t));
		m_is.read(reinterpret_cast<char*>(target), want);
		if (!m_is) return false;

		// read from big endian (network byte order)
		for (uint32_t i = 0; i < count; ++i)
		{
			target[i] = ntohl(target[i]);
		}
		return true;
	}

	uint32_t GraphFile::find_node(double lon, double lat) {
		native_coords search = native_coords_for(coords{lon, lat});

		bool found_any = false;
		uint64_t min_dist = std::numeric_limits<uint64_t>::max();
		node_geo found;

		node_geo_iterator it(*this);

		for (;;) {
			uint32_t last_node_id = found.id;
			grid_coords start = grid_coords_for(found_any ? found.coords : search);
			it.load_block(start.y * m_header.base_grid_width + start.x);

			while (it.next()) {
				uint64_t d = square_distance(search, it->coords);
				if (d < min_dist) {
					min_dist = d;
					found_any = true;
					found = *it;
				}
			}

			if (found.id != last_node_id) continue; // restart loop at new coords

			if (!found_any) {
				/* empty grid cell - start at some random base point: first node in the core */
				node_geo_iterator core_it(*this);
				core_it.load_block(m_header.core_block_start);
				if (!core_it.next()) return std::numeric_limits<uint32_t>::max(); // empty core.. don't search furhter
				found_any = true;
				found = *core_it;
				min_dist = square_distance(search, found.coords);
				continue;
			}

			// search cells in direction from found node to searched position (in the other direction all points are
			uint32_t nx = (search.lon < found.coords.lon && start.x > 0) ? start.x - 1
				: (search.lon > found.coords.lon && start.x + 1 < m_header.base_grid_width) ? start.x + 1 : start.x;
			uint32_t ny = (search.lat < found.coords.lat && start.y > 0) ? start.y - 1
				: (search.lat > found.coords.lat && start.y + 1 < m_header.base_grid_height) ? start.y + 1 : start.y;
			for (grid_coords neigh : { grid_coords(start.x, ny), grid_coords(nx, start.y), grid_coords(nx, ny) })
			{
				it.load_block(neigh.y * m_header.base_grid_width + neigh.x);

				while (it.next()) {
					uint64_t d = square_distance(search, it->coords);
					if (d < min_dist) {
						// found_any is already true
						min_dist = d;
						found = *it;
					}
				}
			}

			if (found.id == last_node_id) return found.id;
		}
	}

	grid_coords GraphFile::grid_coords_for(native_coords native) {
		return grid_coords{
			std::min(m_header.base_cell_width - 1,
				native.lon >= m_header.base_cell_x ? (native.lon - m_header.base_cell_x)/m_header.base_cell_width : 0 ),
			std::min(m_header.base_cell_height - 1,
				native.lat >= m_header.base_cell_y ? (native.lat - m_header.base_cell_y)/m_header.base_cell_height : 0 )
		};
	}
}
