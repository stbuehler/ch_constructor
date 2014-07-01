#ifndef _CHGRAPH_H
#define _CHGRAPH_H

#include "graph.h"
#include "nodes_and_edges.h"

#include <vector>
#include <algorithm>
#include <deque>

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
		using BaseGraph::_id_to_index;
		using BaseGraph::_next_id;
		using typename BaseGraph::OutEdgeSort;

		std::vector<uint> _node_levels;

		std::vector<Shortcut> _edges_dump;

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

		/* destroys internal data structures */
		GraphCHOutData<NodeT, Shortcut> exportData();
};

template <typename Iterator, typename StrictWeakOrdering>
void bubbleSort(Iterator first, Iterator last, StrictWeakOrdering compare)
{
	if (first == last) return;
	bool changed = true;
	auto const last_m1 = last - 1;
	for (auto i = first; changed && i != last_m1; ++i) {
		changed = false;
		for (auto j = i; j != last_m1; ++j) {
			if (compare(*(j+1), *j)) {
				std::swap(*j, *(j+1));
				changed = true;
			}
		}
	}
}

template <typename NodeT, typename EdgeT>
void SCGraph<NodeT, EdgeT>::restructure(
		std::vector<NodeID> const& deleted,
		std::vector<bool> const& to_delete,
		std::vector<Shortcut>& new_shortcuts)
{
	/*
	 * Process contracted nodes.
	 */
	for (uint i(0); i<deleted.size(); i++) {
		_node_levels[deleted[i]] = _next_lvl;
		assert(to_delete[deleted[i]]);
	}
	_next_lvl++;

	/* sort with OutEdgeSort as usual, but for same endpoints sort
	 * "non shortcut" edges before "shortcut" edges;
	 * "non shortcut" edges are sorted by id (they are never equal),
	 * "shorcut" edges are all equal (they get merged to the one with the smallest
	 * distance).
	 */
	auto edgeSort = [](Shortcut const& a, Shortcut const& b) {
		return
			(a.src < b.src || (a.src == b.src &&
			(a.tgt < b.tgt || (a.tgt == b.tgt &&
			((a.center_node == c::NO_NID && b.center_node != c::NO_NID)
				|| (a.center_node == c::NO_NID && b.center_node == c::NO_NID && a.id < b.id)
		)))));
	};

	auto edgeDistSort = [](Shortcut const& a, Shortcut const& b) {
		return
			(a.src < b.src || (a.src == b.src &&
			(a.tgt < b.tgt || (a.tgt == b.tgt &&
			(a.distance() < b.distance())
		))));
	};

	/* as new_shortcuts only contains "real" shortcuts, edgeDistSort is also ordered by edgeSort and OutEdgeSort */
	std::sort(new_shortcuts.begin(), new_shortcuts.end(), edgeDistSort);

	/* FIFO: push_back, pop_front */
	std::deque<Shortcut> edges_fifo;

	uint index_write(0), index_read(0);
	uint const old_edge_count(_out_edges.size());
	_out_edges.reserve(old_edge_count + new_shortcuts.size());

	/* only edges with same endpoints need to get sorted, use early aborting bubble sort */
	bubbleSort(_out_edges.begin(), _out_edges.end(), edgeSort);

	debug_assert(std::is_sorted(_out_edges.begin(), _out_edges.end(), edgeSort));
	debug_assert(std::is_sorted(_out_edges.begin(), _out_edges.end(), OutEdgeSort()));
	debug_assert(std::is_sorted(new_shortcuts.begin(), new_shortcuts.end(), OutEdgeSort()));

	auto forward_index_read = [&]() {
		// search for next edge that is not to be deleted
		for (; index_read < old_edge_count; ++index_read) {
			Shortcut& sc = _out_edges[index_read];
			if (!to_delete[sc.src] && !to_delete[sc.tgt]) break;
			_edges_dump.push_back(std::move(sc));
		}
	};
	// mark current "index_read" edge as done, search for next
	auto inc_index_read = [&]() {
		++index_read;
		forward_index_read();
	};
	/* search for first edge to use */
	forward_index_read();

	auto insert_new_sc = [&](Shortcut&& sc) -> Shortcut& {
		/* if still needed element is in "index_write" (i.e. "index_read"),
		 * move it to fifo
		 */
		if (index_write == index_read && index_read < old_edge_count) {
			edges_fifo.push_back(std::move(_out_edges[index_read]));
			inc_index_read();
		}

		/* either there is still enough space or we need a new element */
		if (index_write < old_edge_count) {
			return _out_edges[index_write++] = std::move(sc);
		} else {
			assert(_out_edges.size() == index_write);
			_out_edges.push_back(std::move(sc));
			return _out_edges[index_write++];
		}
	};
	auto use_old_edge = [&]() -> Shortcut& {
		if (edges_fifo.empty()) {
			if (index_write == index_read) {
				/* - basically there is nothing to do. */
				inc_index_read();
				return _out_edges[index_write++];
			} else {
				// move withing _out_edges: we now there is enough space (could use insert_new_sc)
				auto& e = _out_edges[index_write++] = std::move(_out_edges[index_read]);
				inc_index_read();
				return e;
			}
		} else {
			auto& e = insert_new_sc(std::move(edges_fifo.front()));
			edges_fifo.pop_front();
			return e;
		}
	};


	Shortcut const* prev_new_sc = nullptr;
	for (Shortcut& new_sc: new_shortcuts) {
		/* invariants:
		 * - edges_fifo is sorted with "edgeSort" (and contains no "equal" elements)
		 * - edges_fifo doesn't contain "new" shortcuts
		 * - \forall i: edges_fifo[i] < _out_edges[index_read]
		 * - index_write <= index_read
		 * note: deque push_back doesn't invalidate iterators
		 */

		if (!to_delete[new_sc.center_node]) continue;
		assert(!to_delete[new_sc.src] && !to_delete[new_sc.tgt]);

		if (prev_new_sc && equalEndpoints(*prev_new_sc, new_sc)) {
			// the previous edge had shorter distance, ignore this one:
			continue;
		}

		/* multiple iterations if we have to move forward in _out_edges */
		for (;;) {
			bool have_index = index_read < _out_edges.size();
			bool from_fifo = !edges_fifo.empty();
			if (!from_fifo && !have_index) {
				/* fifo empty and at end of _out_edges: just insert new_sc */
				new_sc.id = _next_id++;
				insert_new_sc(std::move(new_sc));
				break; // done with new_sc
			}

			Shortcut& sc_index_read();
			Shortcut& current(from_fifo ? edges_fifo.front() : _out_edges[index_read]);

			if (edgeSort(new_sc, current)) {
				/* as new_sc is a "real" shortcut, it can't be smaller than an
				 * edge with the same endpoints
				 */
				assert(!equalEndpoints(new_sc, current));
				new_sc.id = _next_id++;
				insert_new_sc(std::move(new_sc));
				break; // done with new_sc
			}

			/* use old edge and possibly merge */
			auto& written = use_old_edge();

			if (edgeSort(written, new_sc)) {
				/* try again at next index */
				continue; // not done yet with new_sc
			}

			assert(equalEndpoints(written, new_sc) && written.center_node != c::NO_NID);

			// merge
			if (new_sc.distance() < written.distance()) {
				/* replace content (but not id) of index */
				new_sc.id = written.id;
				written = new_sc;
			}
			/* otherwise skip new_sc */
			break; // done with new_sc
		}
	}

	while (index_write < index_read && !edges_fifo.empty()) {
		_out_edges[index_write++] = edges_fifo.front();
		edges_fifo.pop_front();
	}
	while (index_read < old_edge_count) {
		if (edges_fifo.empty()) {
			if (index_write == index_read) {
				/* edge already in the correct place, go forward */
				index_write++;
			} else {
				_out_edges[index_write++] = std::move(_out_edges[index_read]);
			}
		} else {
			edges_fifo.push_back(std::move(_out_edges[index_read]));
		}
		inc_index_read();

		while (index_write < index_read && !edges_fifo.empty()) {
			_out_edges[index_write++] = edges_fifo.front();
			edges_fifo.pop_front();
		}
	}

	_out_edges.resize(index_write);

	_out_edges.insert(_out_edges.end(), edges_fifo.begin(), edges_fifo.end());

	/*
	 * Build new graph structures.
	 */
	debug_assert(std::is_sorted(_out_edges.begin(), _out_edges.end(), OutEdgeSort()));

	_in_edges = _out_edges;
	BaseGraph::sortInEdges();
	BaseGraph::initOffsets();
	// BaseGraph::initIdToIndex(); /* not used during ch construction */
}

template <typename NodeT, typename EdgeT>
void SCGraph<NodeT, EdgeT>::rebuildCompleteGraph()
{
	assert(_out_edges.empty() && _in_edges.empty());

	_out_edges.swap(_edges_dump);
	_in_edges = _out_edges;
	_edges_dump.clear();

	BaseGraph::update();
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
	std::vector<Shortcut>* edges_source;
	std::vector<Shortcut> edges;

	_id_to_index = decltype(_id_to_index)();

	if (_out_edges.empty() && _in_edges.empty()) {
		edges_source = &_edges_dump;
	}
	else {
		assert(_edges_dump.empty());
		edges_source = &_out_edges;
		_in_edges = decltype(_in_edges)();
	}

	edges.resize(edges_source->size());
	for (auto const& edge: *edges_source) {
		edges[edge.id] = edge;
	}

	_out_edges = std::move(edges);

	_edges_dump = decltype(_edges_dump)();

	return GraphCHOutData<NodeT, Shortcut>{BaseGraph::_nodes, _node_levels, _out_edges};
}

}

#endif
