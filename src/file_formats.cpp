#include "file_formats.h"

namespace chc {
	FileFormat toFileFormat(std::string const& format)
	{
		if (format == "STD") {
			return STD;
		}
		else if (format == "SIMPLE") {
			return SIMPLE;
		}
		else if (format == "FMI") {
			return FMI;
		}
		else if (format == "FMI_CH") {
			return FMI_CH;
		}
		else {
			std::cerr << "Unknown fileformat!" << "\n";
		}

		return FMI;
	}


	template<>
	OSMNode text_readNode<OSMNode>(std::istream& is)
	{
		OSMNode node;
		is >> node.id >> node.osm_id >> node.lat >> node.lon >> node.elev;
		return node;
	}

	template<>
	void text_writeNode<OSMNode>(std::ostream& os, OSMNode const& node)
	{
		os << node.id << " " << node.osm_id << " " << node.lat << " "
			<< node.lon << " " << node.elev << "\n";
	}

	template<>
	GeoNode text_readNode<GeoNode>(std::istream& is)
	{
		GeoNode node;
		node.id = c::NO_NID;
		is >> node.lat >> node.lon >> node.elev;
		return node;
	}

	template<>
	void text_writeNode<GeoNode>(std::ostream& os, GeoNode const& node)
	{
		os << node.lat << " " << node.lon << " " << node.elev << "\n";
	}

	template<>
	OSMEdge text_readEdge<OSMEdge>(std::istream& is)
	{
		OSMEdge edge;
		is >> edge.src >> edge.tgt >> edge.dist >> edge.type >> edge.speed;
		return edge;
	}

	template<>
	void text_writeEdge<OSMEdge>(std::ostream& os, OSMEdge const& edge)
	{
		os << edge.src << " " << edge.tgt << " " << edge.dist << " "
			<< edge.type << " " << edge.speed << "\n";
	}

	template<>
	Edge text_readEdge<Edge>(std::istream& is)
	{
		Edge edge;
		is >> edge.src >> edge.tgt >> edge.dist;
		return edge;
	}

	template<>
	void text_writeEdge<Edge>(std::ostream& os, Edge const& edge)
	{
		os << edge.src << " " << edge.tgt << " " << edge.dist << "\n";
	}


	namespace FormatSTD {
		void Reader_impl::readHeader(NodeID& estimated_nr_nodes, EdgeID& estimated_nr_edges)
		{
			is >> estimated_nr_nodes >> estimated_nr_edges;
		}

		auto Reader_impl::readNode(NodeID node_id) -> node_type
		{
			auto out = text_readNode<OSMNode>(is);
			if (out.id != node_id) {
				std::cerr << "FATAL_ERROR: Invalid node id " << out.id << " at index " << node_id << ". Exiting\n";
				std::terminate();
			}
			return out;
		}

		auto Reader_impl::readEdge(EdgeID) -> edge_type
		{
			return text_readEdge<OSMEdge>(is);
		}

		void Writer::writeHeader(NodeID nr_of_nodes, EdgeID nr_of_edges)
		{
			os << nr_of_nodes << "\n";
			os << nr_of_edges << "\n";
		}

		void Writer::writeNode(node_type const& out, NodeID node_id)
		{
			if (node_id != out.id) {
				std::cerr << "FATAL_ERROR: Invalid node id " << out.id << " at index " << node_id << ". Exiting\n";
				std::terminate();
			}
			text_writeNode<OSMNode>(os, out);
		}

		void Writer::writeEdge(edge_type const& out, EdgeID)
		{
			text_writeEdge<OSMEdge>(os, out);
		}
	}


	namespace FormatSimple {
		void Reader_impl::readHeader(NodeID& estimated_nr_nodes, EdgeID& estimated_nr_edges)
		{
			is >> estimated_nr_nodes >> estimated_nr_edges;
		}

		auto Reader_impl::readNode(NodeID) -> node_type
		{
			return text_readNode<GeoNode>(is);
		}

		auto Reader_impl::readEdge(EdgeID) -> edge_type
		{
			return text_readEdge<Edge>(is);
		}

		void Writer::writeHeader(NodeID nr_of_nodes, EdgeID nr_of_edges)
		{
			os << nr_of_nodes << "\n";
			os << nr_of_edges << "\n";
		}

		void Writer::writeNode(node_type const& out, NodeID)
		{
			text_writeNode<GeoNode>(os, out);
		}

		void Writer::writeEdge(edge_type const& out, EdgeID)
		{
			text_writeEdge<Edge>(os, out);
		}
	}



	void FormatFMI::Reader_impl::readHeader(NodeID& estimated_nr_nodes, EdgeID& estimated_nr_edges)
	{
		char c;
		is.get(c);
		// Note that this loop also reads the first character after
		// all the comments are over.
		while (c == '#') {
			is.ignore(1024, '\n');
			is.get(c);
		}

		is >> estimated_nr_nodes >> estimated_nr_edges;
	}

	/* Generate random id */
	static std::string random_id(unsigned int len)
	{
		static const char hex[17] = "0123456789abcdef";

		std::random_device rd;
		std::mt19937 gen(rd());
		std::uniform_int_distribution<> dist(0, 15);

		std::string s; s.reserve(len);
		for (unsigned int i = 0; i < len; ++i) {
			s[i] = hex[dist(gen)];
		}
		return s;
	}

	void FormatFMI_CH::Writer::writeHeader(NodeID nr_of_nodes, EdgeID nr_of_edges)
	{
		/* Write header */
		os << "# Id : " << random_id(32) << "\n";
		os << "# Timestamp : " << time(nullptr) << "\n";
		os << "# Type: maxspeed" << "\n";
		os << "# Revision: 1" << "\n";
		os << "\n";

		os << nr_of_nodes << "\n";
		os << nr_of_edges << "\n";
	}
}
