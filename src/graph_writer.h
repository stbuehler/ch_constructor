#ifndef _GRAPH_WRITER_H
#define _GRAPH_WRITER_H

#include "nodes_and_edges.h"
#include "function_traits.h"

#include <fstream>
#include <algorithm>


namespace chc {
	template<typename Writer, typename GraphData>
	void writeGraphWithWriter(std::ostream& out, GraphData&& data) {
		typedef arg_remove_reference<0, decltype(return_args(&Writer::writeNode))> node_type;
		typedef arg_remove_reference<0, decltype(return_args(&Writer::writeEdge))> edge_type;

		NodeID nr_of_nodes(data.nodes.size());
		EdgeID nr_of_edges(data.edges.size());

		Print("Exporting " << nr_of_nodes << " nodes and "
				<< nr_of_edges << " edges");

		Writer w(out);
		w.writeHeader(nr_of_nodes, nr_of_edges);

		NodeID node_id = 0;
		for (auto const& node: data.nodes) {
			w.writeNode(static_cast<node_type>(node), node_id++);
		}
		Print("Exported all nodes.");

		EdgeID edge_id = 0;
		for (auto const& edge: data.edges) {
			w.writeEdge(static_cast<edge_type>(edge), edge_id++);
		}
		Print("Exported all edges.");
	}

	template<typename Writer, typename GraphData>
	void writeGraphWithWriter(std::string const& filename, GraphData&& data) {
		std::ofstream f(filename.c_str());
		if (!f.is_open()) {
			std::cerr << "FATAL_ERROR: Couldn't open graph file \'" <<
				filename << "\'. Exiting." << std::endl;
			std::exit(1);
		}

		Print("Exporting to " << filename);
		writeGraphWithWriter<Writer, GraphData>(f, std::forward<GraphData>(data));
		f.close();
	}
}

#endif
