#ifndef _FILE_FORMATS_H
#define _FILE_FORMATS_H

#include "graph_reader.h"
#include "graph_writer.h"

namespace chc {
	// "default" text serialization of some nodes and edge types,
	// used in the the formats below
	template<typename NodeT>
	NodeT text_readNode(std::istream& is);
	template<typename NodeT>
	void text_writeNode(std::ostream& os, NodeT const& node);
	template<typename EdgeT>
	EdgeT text_readEdge(std::istream& is);
	template<typename EdgeT>
	void text_writeEdge(std::ostream& os, EdgeT const& edge);

	template<>
	OSMNode text_readNode<OSMNode>(std::istream& is);
	template<>
	void text_writeNode<OSMNode>(std::ostream& os, OSMNode const& node);

	template<>
	GeoNode text_readNode<GeoNode>(std::istream& is);
	template<>
	void text_writeNode<GeoNode>(std::ostream& os, GeoNode const& node);

	template<>
	OSMEdge text_readEdge<OSMEdge>(std::istream& is);
	template<>
	void text_writeEdge<OSMEdge>(std::ostream& os, OSMEdge const& edge);

	template<>
	Edge text_readEdge<Edge>(std::istream& is);
	template<>
	void text_writeEdge<Edge>(std::ostream& os, Edge const& edge);


	namespace FormatSTD
	{
		typedef OSMNode node_type;
		typedef OSMEdge edge_type;

		struct Reader_impl
		{
			Reader_impl(std::istream& is) : is(is) { }
			void readHeader(NodeID& estimated_nr_nodes, EdgeID& estimated_nr_edges);
			node_type readNode(NodeID id);
			edge_type readEdge(EdgeID id);
		protected:
			std::istream& is;
		};
		typedef BasicReader<Reader_impl> Reader;

		struct Writer
		{
			Writer(std::ostream& os) : os(os) { }
			void writeHeader(NodeID nr_of_nodes, EdgeID nr_of_edges);
			void writeNode(node_type const& out, NodeID node_id);
			void writeEdge(edge_type const& out, EdgeID edge_id);
		protected:
			std::ostream& os;
		};
	}


	namespace FormatSimple
	{
		typedef GeoNode node_type;
		typedef Edge edge_type;

		struct Reader_impl
		{
			Reader_impl(std::istream& is) : is(is) { }
			void readHeader(NodeID& estimated_nr_nodes, EdgeID& estimated_nr_edges);
			node_type readNode(NodeID id);
			edge_type readEdge(EdgeID id);
		protected:
			std::istream& is;
		};
		typedef BasicReader<Reader_impl> Reader;

		struct Writer
		{
		public:
			Writer(std::ostream& os) : os(os) { }
			void writeHeader(NodeID nr_of_nodes, EdgeID nr_of_edges);
			void writeNode(node_type const& out, NodeID node_id);
			void writeEdge(edge_type const& out, EdgeID edge_id);
		protected:
			std::ostream& os;
		};
	}

	/* only header is different from FormatSTD */
	namespace FormatFMI
	{
		typedef OSMNode node_type;
		typedef OSMEdge edge_type;

		struct Reader_impl : public FormatSTD::Reader_impl
		{
			Reader_impl(std::istream& is) : FormatSTD::Reader_impl(is) { }
			void readHeader(NodeID& estimated_nr_nodes, EdgeID& estimated_nr_edges);
		};
		typedef BasicReader<Reader_impl> Reader;
	}
	namespace FormatFMI_CH
	{
		typedef OSMNode node_type;
		typedef OSMEdge edge_type;

		struct Writer : public FormatSTD::Writer
		{
		public:
			Writer(std::ostream& os) : FormatSTD::Writer(os) { }
			void writeHeader(NodeID nr_of_nodes, EdgeID nr_of_edges);
		};
	}



	enum FileFormat { STD, SIMPLE, FMI, FMI_CH };
	FileFormat toFileFormat(std::string const& format);

	template<typename Target, typename GraphData>
	void writeGraph(FileFormat format, Target&& target, GraphData&& data)
	{
		switch (format) {
		case STD:
			writeGraphWithWriter<FormatSTD::Writer>(std::forward<Target>(target), std::forward<GraphData>(data));
			return;
		case SIMPLE:
			writeGraphWithWriter<FormatSimple::Writer>(std::forward<Target>(target), std::forward<GraphData>(data));
			return;
		case FMI:
			break;
		case FMI_CH:
			writeGraphWithWriter<FormatFMI_CH::Writer>(std::forward<Target>(target), std::forward<GraphData>(data));
			return;
		}
		std::cerr << "Unknown fileformat: " << format << std::endl;
		std::exit(1);
	}

	template<typename Node, typename Edge>
	GraphData<Node, Edge> readGraph(FileFormat format, std::string const& filename)
	{
		switch (format) {
		case STD:
			return readGraphWithReader<Node, Edge, FormatSTD::Reader>(filename);
		case SIMPLE:
			return readGraphWithReader<Node, Edge, FormatSimple::Reader>(filename);
		case FMI:
			return readGraphWithReader<Node, Edge, FormatFMI::Reader>(filename);
		case FMI_CH:
			break;
		}
		std::cerr << "Unknown fileformat: " << format << std::endl;
		std::exit(1);
	}
}


#endif
