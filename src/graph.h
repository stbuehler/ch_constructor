#ifndef _GRAPH_H
#define _GRAPH_H

#include "defs.h"
#include "nodes_and_edges.h"
#include "indexed_container.h"

#include <vector>
#include <list>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <limits>

namespace chc
{

namespace unit_tests
{
	void testGraph();
}

template <typename NodeT, typename EdgeT>
class Graph
{
	protected:
		std::vector<NodeT> _nodes;
		std::vector<EdgeT> _edges;

		std::vector<uint> _out_offsets;
		std::vector<uint> _in_offsets;

		typedef index_vector<EdgeT, std::vector<EdgeT>, EdgeID> edge_index_vector;

		edge_index_vector _out_edges { _edges };
		edge_index_vector _in_edges { _edges };

		void sortInEdges();
		void sortOutEdges();
		void initOffsets();

		void update();

	public:
		typedef NodeT node_type;
		typedef EdgeT edge_type;

		typedef EdgeSortSrc<EdgeT> OutEdgeSort;
		typedef EdgeSortTgt<EdgeT> InEdgeSort;

		/* Init the graph from file 'filename' and sort
		 * the edges according to OutEdgeSort and InEdgeSort. */
		void init(GraphInData<NodeT,EdgeT>&& data);

		void printInfo() const;
		template<typename Range>
		void printInfo(Range&& nodes) const;

		uint getNrOfNodes() const { return _nodes.size(); }
		uint getNrOfEdges() const { return _edges.size(); }
		EdgeT const& getEdge(EdgeID edge_id) const { return _edges[edge_id]; }
		NodeT const& getNode(NodeID node_id) const { return _nodes[node_id]; }

		uint getNrOfEdges(NodeID node_id) const;
		uint getNrOfEdges(NodeID node_id, EdgeType type) const;

		typedef range<typename edge_index_vector::const_iterator> node_edges_range;
		node_edges_range nodeEdges(NodeID node_id, EdgeType type) const;

		friend void unit_tests::testGraph();
};

/*
 * Graph member functions.
 */

template <typename NodeT, typename EdgeT>
void Graph<NodeT, EdgeT>::init(GraphInData<NodeT, EdgeT>&& data)
{
	_nodes.swap(data.nodes);
	_edges.swap(data.edges);
	_out_edges.sync_sorted(OutEdgeSort());
	_in_edges.sync_sorted(InEdgeSort());

	initOffsets();

	Print("Graph info:");
	Print("===========");
	printInfo();
}

template <typename NodeT, typename EdgeT>
void Graph<NodeT, EdgeT>::printInfo() const
{
	printInfo(counting_iteration(range<NodeID>(0, _nodes.size())));
}

template <typename NodeT, typename EdgeT>
template <typename Range>
void Graph<NodeT, EdgeT>::printInfo(Range&& nodes) const
{
#ifdef NVERBOSE
	(void) nodes;
#else
	uint active_nodes(0);

	double avg_out_deg(0);
	double avg_in_deg(0);
	double avg_deg(0);

	std::vector<uint> out_deg;
	std::vector<uint> in_deg;
	std::vector<uint> deg;

	for (auto it(nodes.begin()), end(nodes.end()); it != end; it++) {
		uint out(getNrOfEdges(*it, OUT));
		uint in(getNrOfEdges(*it, IN));

		if (out != 0 || in != 0) {
			++active_nodes;

			out_deg.push_back(out);
			in_deg.push_back(in);
			deg.push_back(out + in);

			avg_out_deg += out;
			avg_in_deg += in;
			avg_deg += out+in;
		}
	}

	Print("#nodes: " << nodes.size() << ", #active nodes: " << active_nodes << ", #edges: " << _edges.size());

	if (active_nodes != 0) {
		auto mm_out_deg = std::minmax_element(out_deg.begin(), out_deg.end());
		auto mm_in_deg = std::minmax_element(in_deg.begin(), in_deg.end());
		auto mm_deg = std::minmax_element(deg.begin(), deg.end());

		avg_out_deg /= active_nodes;
		avg_in_deg /= active_nodes;
		avg_deg /= active_nodes;

		Print("min/max/avg degree:"
			<< " out "   << *mm_out_deg.first << " / " << *mm_out_deg.second << " / " << avg_out_deg
			<< ", in "   << *mm_in_deg.first  << " / " << *mm_in_deg.second  << " / " << avg_in_deg
			<< ", both " << *mm_deg.first     << " / " << *mm_deg.second     << " / " << avg_deg);
	}
	else {
		Debug("(no degree info is provided as there are no active nodes)");
	}
#endif
}

template <typename NodeT, typename EdgeT>
void Graph<NodeT, EdgeT>::sortInEdges()
{
	Debug("Sort the incomming edges.");

	_in_edges.sync_sorted(InEdgeSort());
	debug_assert(std::is_sorted(_in_edges.begin(), _in_edges.end(), InEdgeSort()));
}

template <typename NodeT, typename EdgeT>
void Graph<NodeT, EdgeT>::sortOutEdges()
{
	Debug("Sort the outgoing edges.");

	_out_edges.sync_sorted(OutEdgeSort());
	debug_assert(std::is_sorted(_out_edges.begin(), _out_edges.end(), OutEdgeSort()));
}

template <typename NodeT, typename EdgeT>
void Graph<NodeT, EdgeT>::initOffsets()
{
	Debug("Init the offsets.");
	debug_assert(std::is_sorted(_out_edges.begin(), _out_edges.end(), OutEdgeSort()));
	debug_assert(std::is_sorted(_in_edges.begin(), _in_edges.end(), InEdgeSort()));

	uint nr_of_nodes(_nodes.size());

	_out_offsets.assign(nr_of_nodes + 1, 0);
	_in_offsets.assign(nr_of_nodes + 1, 0);

	/* assume "valid" edges are in _out_edges and _in_edges */
	for (auto const& edge: _out_edges) {
		_out_offsets[edge.src]++;
		_in_offsets[edge.tgt]++;
	}

	uint out_sum(0);
	uint in_sum(0);
	for (NodeID i(0); i<nr_of_nodes; i++) {
		auto old_out_sum = out_sum, old_in_sum = in_sum;
		out_sum += _out_offsets[i];
		in_sum += _in_offsets[i];
		_out_offsets[i] = old_out_sum;
		_in_offsets[i] = old_in_sum;
	}
	assert(out_sum == _out_edges.indices.size());
	assert(in_sum == _in_edges.indices.size());
	_out_offsets[nr_of_nodes] = out_sum;
	_in_offsets[nr_of_nodes] = in_sum;
}

template <typename NodeT, typename EdgeT>
void Graph<NodeT, EdgeT>::update()
{
	sortOutEdges();
	sortInEdges();
	initOffsets();
}

template <typename NodeT, typename EdgeT>
uint Graph<NodeT, EdgeT>::getNrOfEdges(NodeID node_id) const
{
	return getNrOfEdges(node_id, OUT) + getNrOfEdges(node_id, IN);
}

template <typename NodeT, typename EdgeT>
uint Graph<NodeT, EdgeT>::getNrOfEdges(NodeID node_id, EdgeType type) const
{
	if (type == IN) {
		return _in_offsets[node_id+1] - _in_offsets[node_id];
	}
	else {
		return _out_offsets[node_id+1] - _out_offsets[node_id];
	}
}

template <typename NodeT, typename EdgeT>
auto Graph<NodeT, EdgeT>::nodeEdges(NodeID node_id, EdgeType type) const -> node_edges_range {
	if (OUT == type) {
		return node_edges_range(_out_edges.begin() + _out_offsets[node_id], _out_edges.begin() + _out_offsets[node_id+1]);
	} else {
		return node_edges_range(_in_edges.begin() + _in_offsets[node_id], _in_edges.begin() + _in_offsets[node_id+1]);
	}
}

}

#endif
