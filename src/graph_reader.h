#ifndef _GRAPH_READER_H
#define _GRAPH_READER_H

#include "nodes_and_edges.h"
#include "continuation_iterator.h"
#include "function_traits.h"

#include <vector>
#include <iostream>
#include <algorithm>

namespace chc
{
	template<typename Node, typename Edge>
	struct GraphData
	{
		std::vector<Node> nodes;
		std::vector<Edge> edges;
	};

	/* required Reader members:
	 * - estimated_nr_nodes, estimeated_nr_edges: for reserving space in vector
	 * - nodes: iteratable (once) with nodes
	 * - edges: iteratable (once) with nodes
	 * iterates nodes first, then edges
	 * Result Node and Edge need a conversion (that works with static_cast) to
	 * be defined from whatever the reader nodes/edges iterators return.
	 */
	template<typename Node, typename Edge, typename Reader, typename... ReaderArgs>
	GraphData<Node, Edge> readGraphWithReader(ReaderArgs&&... readerArgs)
	{
		Reader r(std::forward<ReaderArgs>(readerArgs)...);
		GraphData<Node, Edge> result;

		result.nodes.reserve(r.estimated_nr_nodes);
		for (auto& node: r.nodes) {
			result.nodes.push_back(static_cast<Node>(node));
		}
		Print("Read all the nodes.");

		result.edges.reserve(r.estimated_nr_edges);
		for (auto& edge: r.edges) {
			result.edges.push_back(static_cast<Edge>(edge));
		}
		Print("Read all the edges.");

		return result;
	}


	/* generic helper for readers that:
	 * - read data using a std::istream
	 * - read number of nodes and edges from a file header
	 * - read node then edges using readNode and readEdge calls
	 */
	template<typename Implementation>
	struct BasicReader {
	protected:
		std::ifstream in;
	public:
		/* estimate is exact in this case */
		NodeID estimated_nr_nodes = 0;
		EdgeID estimated_nr_edges = 0;
	protected:
		Implementation impl;

	public:
		typedef typename std::remove_reference<decltype(result_of(&Implementation::readNode))>::type node_type;
		typedef typename std::remove_reference<decltype(result_of(&Implementation::readEdge))>::type edge_type;

		inline node_type readNode(std::size_t ndx) {
			node_type out = impl.readNode(ndx);
			out.id = ndx;
			return out;
		}

		inline edge_type readEdge(std::size_t ndx) {
			if (nodes.nextIndex() < estimated_nr_nodes) {
				std::cerr << "FATAL_ERROR: can't read edge while nodes are not done. Exiting\n";
				std::terminate();
			}
			edge_type out = impl.readEdge(ndx);
			out.id = ndx;
			return out;
		}

		/* begin() on these triggers the first read - can read only once. */
		LimitedIteration<node_type, BasicReader, &BasicReader::readNode> nodes;
		LimitedIteration<edge_type, BasicReader, &BasicReader::readEdge> edges;

		/* implementation constructor needs to set the number of nodes and edges,
		 * i.e. access them through references to unsigned int
		 */
		BasicReader(std::string const& filename)
		: in(filename), impl(in), nodes(this), edges(this)
		{
			if (!in.is_open()) {
				std::cerr << "FATAL_ERROR: Couldn't open graph file \'" <<
					filename << "\'. Exiting." << std::endl;
				std::terminate();
			}

			impl.readHeader(estimated_nr_nodes, estimated_nr_edges);
			nodes.setLimit(estimated_nr_nodes);
			edges.setLimit(estimated_nr_edges);

			Print("Number of nodes: " << estimated_nr_nodes);
			Print("Number of edges: " << estimated_nr_edges);
		}
	};
}

#endif
