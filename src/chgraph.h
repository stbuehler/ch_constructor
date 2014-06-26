#ifndef _CHGRAPH_H
#define _CHGRAPH_H

#include "graph.h"
#include "nodes_and_edges.h"

#include <vector>
#include <algorithm>

namespace chc
{

template <typename NodeT, typename EdgeT>
class SCGraph : public Graph<NodeT, CHEdge<EdgeT> >
{
	private:
		typedef CHEdge<EdgeT> Shortcut;
		typedef Graph<NodeT, Shortcut> BaseGraph;
		using BaseGraph::_out_edges;
		using BaseGraph::_in_edges;
		using BaseGraph::_edges;
		using BaseGraph::_out_offsets;
		using BaseGraph::_in_offsets;
		using typename BaseGraph::OutEdgeSort;

		std::vector<uint> _node_levels;

		uint _next_lvl = 0;

	public:
		template <typename Data>
		void init(Data&& data)
		{
			_node_levels.resize(data.nodes.size(), c::NO_LVL);
			BaseGraph::init(std::forward<Data>(data));
		}


		void restructure(std::vector<NodeID> const& deleted,
				std::vector<bool> const& to_delete,
				std::vector<Shortcut>& new_shortcuts);

		void rebuildCompleteGraph();
		bool isUp(Shortcut const& edge, EdgeType direction) const;

		/* clears internal data structures */
		GraphCHOutData<NodeT, Shortcut> exportData();

		GraphCHOutData<NodeT, Shortcut> getData() const;
};

template <typename NodeT, typename EdgeT>
void SCGraph<NodeT, EdgeT>::restructure(
		std::vector<NodeID> const& deleted,
		std::vector<bool> const& to_delete,
		std::vector<Shortcut>& new_shortcuts)
{
	OutEdgeSort outEdgeSort;

	/*
	 * Process contracted nodes.
	 */
	for (auto delNode: deleted) {
		_node_levels[delNode] = _next_lvl;
		assert(to_delete[delNode]);
	}
	++_next_lvl;

	/* filter and sort new shortcuts: only use if center_node was actually contracted */
	erase_if(new_shortcuts, [&to_delete](Shortcut const& sc) {
		if (!to_delete[sc.center_node]) return true;
		/* can't contract src, tgt and center_node in same round */
		assert(!to_delete[sc.src] && !to_delete[sc.tgt]);
		return false;
	});

	std::sort(new_shortcuts.begin(), new_shortcuts.end(), [](Shortcut const& a, Shortcut const& b) {
		/* OutEdgeSort + order shorter edges first */
		return
			(a.src < b.src || (a.src == b.src &&
			(a.tgt < b.tgt || (a.tgt == b.tgt &&
			(a.distance() < b.distance()
		)))));
	});

	auto eq = [](Shortcut const& a, Shortcut const& b) { return equalEndpoints(a, b); };

	/* erase duplicate shortcuts: only keep shortest */
	new_shortcuts.erase(std::unique(new_shortcuts.begin(), new_shortcuts.end(), eq), new_shortcuts.end());

	/* replace existing shortcut, if new one is shorter; quick-contract may generate suboptimal shortcuts */
	erase_if(new_shortcuts, [this, &outEdgeSort](Shortcut const& sc) {
		auto src_out_edges = BaseGraph::nodeEdges(sc.src, OUT);
		auto range = std::equal_range(src_out_edges.first, src_out_edges.last, sc, outEdgeSort);
		for (auto i = range.first; i != range.second; ++i) {
			if (sc.distance() >= i->distance()) return true; /* remove - not short enough */
			if (c::NO_NID != i->center_node) {
				_edges[*i.pos] = sc;
				return true;
			}
		}

		return false;
	});

	/* drop edge (from index lists) if either source or target node was contracted
	 * the edge will still be in _edges
	 */
	auto drop_edge = [&to_delete](EdgeT const& edge) {
		return to_delete[edge.src] || to_delete[edge.tgt];
	};
	_out_edges.erase_if(drop_edge);
	_in_edges.erase_if(drop_edge);

	_edges.insert(_edges.end(), make_move_iterator(new_shortcuts.begin()), make_move_iterator(new_shortcuts.end()));

	BaseGraph::update();
}

template <typename NodeT, typename EdgeT>
void SCGraph<NodeT, EdgeT>::rebuildCompleteGraph()
{
	_out_edges.reset_sorted(typename BaseGraph::OutEdgeSort());
	_in_edges.reset_sorted(typename BaseGraph::InEdgeSort());
	BaseGraph::initOffsets();
}

template <typename NodeT, typename EdgeT>
bool SCGraph<NodeT, EdgeT>::isUp(Shortcut const& edge, EdgeType direction) const
{
	uint src_lvl = _node_levels[edge.src];
	uint tgt_lvl = _node_levels[edge.tgt];

	if (src_lvl > tgt_lvl) {
		return direction == IN ? true : false;
	}
	else if (src_lvl < tgt_lvl) {
		return direction == OUT ? true : false;
	}

	/* should never reach this: */
	assert(src_lvl != tgt_lvl);
	return false;
}

template <typename NodeT, typename EdgeT>
auto SCGraph<NodeT, EdgeT>::exportData() -> GraphCHOutData<NodeT, Shortcut>
{
	_out_edges.indices = decltype(_out_edges.indices)();
	_in_edges.indices = decltype(_in_edges.indices)();
	_out_offsets = decltype(_out_offsets)();
	_in_offsets = decltype(_in_offsets)();

	return getData();
}

template <typename NodeT, typename EdgeT>
auto SCGraph<NodeT, EdgeT>::getData() const -> GraphCHOutData<NodeT, Shortcut>
{
	return GraphCHOutData<NodeT, Shortcut>{BaseGraph::_nodes, _node_levels, _edges};
}

}

#endif
