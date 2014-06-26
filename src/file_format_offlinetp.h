#ifndef _FILE_FORMAT_OFFLINETP_H
#define _FILE_FORMAT_OFFLINETP_H

#include "file_formats_helper.h"

#include <cmath>

namespace chc {

	namespace FormatOfflineTP {
		struct Node
		{
			int32_t lat = 0;
			int32_t lon = 0;

			Node() { }

			Node(int32_t lat, int32_t lon)
			: lat(lat), lon(lon) { }

			Node(OSMNode const& node)
			: Node((int32_t) std::round(node.lat * 1e7), (int32_t) std::round(node.lon * 1e7)) { }

			Node(GeoNode const& node)
			: Node((int32_t) std::round(node.lat * 1e7), (int32_t) std::round(node.lon * 1e7)) { }
		};

		struct Edge
		{
			EdgeID id = c::NO_EID;
			NodeID src = c::NO_NID;
			NodeID tgt = c::NO_NID;
			uint32_t dist = std::numeric_limits<uint32_t>::max();
			uint32_t time = std::numeric_limits<uint32_t>::max(); // unit: [ 9/325 s ] = [ 1/130000 h ]

			Edge() { }
			Edge(OSMEdge const& edge)
			: Edge(edge.id, edge.src, edge.tgt, edge.dist, calcTime(edge.dist, edge.type, edge.speed)) { }
			Edge(EdgeID id, NodeID src, NodeID tgt, uint dist, uint time)
			: id(id), src(src), tgt(tgt), dist(dist), time(time) { }

			static uint calcTime(uint dist, uint roadType, int speed);

			uint distance() const { return time; }
		};
		Edge concat(Edge const& edge1, Edge const& edge2);

		struct Writer
		{
			typedef Node basic_node_type;
			typedef CHNode<basic_node_type> node_type;
			typedef CHEdge<Edge> edge_type;

			static void writeCHGraph(std::ostream& os, GraphCHOutData<basic_node_type, edge_type> const& data);

			template<typename NodeT, typename EdgeT>
			static void writeCHGraph(std::ostream&, GraphCHOutData<NodeT, EdgeT> const&)
			{
				Print("Can't export nodes / edges in this format");
				std::abort();
			}
		};
	}

}

#endif
