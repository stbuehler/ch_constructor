#pragma once

#include <istream>
#include <cstdint>

namespace offtp
{
	namespace impl {
		struct grid_coords {
			uint32_t x = 0;
			uint32_t y = 0;
			grid_coords() = default;
			grid_coords(uint32_t X, uint32_t Y) : x(X), y(Y) { }
		};
		struct native_coords {
			uint32_t lon = 0;
			uint32_t lat = 0;
			native_coords() = default;
			native_coords(uint32_t Lon, uint32_t Lat) : lon(Lon), lat(Lat) { }
		};
		struct coords {
			double lon = 0;
			double lat = 0;
			coords() = default;
			coords(double Lon, double Lat) : lon(Lon), lat(Lat) { }
		};
	}

	class GraphFile {
	public:
		GraphFile(std::istream& is);

		bool load_header();
		uint32_t find_node(double lon, double lat);

	private:
		struct header {
			uint32_t base_cell_x = 0;
			uint32_t base_cell_y = 0;
			uint32_t base_cell_width = 0;
			uint32_t base_cell_height = 0;
			uint32_t base_grid_width = 0;
			uint32_t base_grid_height = 0;
			uint32_t block_size = 0;
			uint32_t block_count = 0;
			uint32_t core_block_start = 0;
			uint32_t edge_count = 0;
		};

		struct meta {
			uint64_t stride = 0;
			static const uint64_t offset_node_geo = 4096;
			uint64_t offset_node_edges = 0;
			uint64_t offset_edges = 0;
			uint64_t offset_edges_details = 0;
		};

		class node_geo_iterator;
		friend class node_geo_iterator;

		std::istream& m_is;
		header m_header;
		meta m_meta;

		bool read_uint32_array(uint64_t offset, uint32_t *target, uint32_t count);
		impl::grid_coords grid_coords_for(impl::native_coords native);
	};

}
