
#include "file_format_offlinetp.h"
#include "track_time.h"

#include <algorithm>
#include <cstdint>
#include <cmath>

namespace chc {

	namespace {
		int defaultSpeed(uint roadType)
		{
			switch (roadType) {
			case  1: return 130; // motorway
			case  2: return 100; // motorway link
			case  3: return  70; // primary
			case  4: return  70; // primary link
			case  5: return  65; // secondary
			case  6: return  65; // secondary link
			case  7: return  60; // tertiary
			case  8: return  60; // tertiary link
			case  9: return  80; // trunk
			case 10: return  80; // trunk link
			case 11: return  30; // unclassified
			case 12: return  50; // residential
			case 13: return  30; // living street
			case 14: return  30; // road
			case 15: return  30; // service
			case 16: return  30; // turning circle
			default: return  50;
			}
		}
	}

	namespace FormatOfflineTP {
		uint Edge::calcTime(uint dist, uint roadType, int speed)
		{
			if (speed <= 0) speed = defaultSpeed(roadType);
			long long unsigned int result = dist;
			result *= 1300;
			result /= speed;
			return std::min<decltype(result)>(result, std::numeric_limits<uint>::max());
		}

		Edge concat(Edge const& edge1, Edge const& edge2)
		{
			assert(edge1.tgt == edge2.src);
			return Edge(c::NO_EID, edge1.src, edge2.tgt, edge1.dist + edge2.dist, edge1.time + edge2.time);
		}

		namespace {
			template<typename T, std::size_t N>
			constexpr std::size_t array_size(T const (&)[N]) { return N; }

			/**
			 * the file stores only the first (highest resolution) grid, but we sort nodes into different grids depending on their CH level
			 * nodes below level grid_sizes[i][0] are sorted in a grid_sizes[i][1]xgrid_sizes[i][1] grid
			 * nodes >= grid_sizes[#last][0] are sorted in the core graph
			 */
			static uint const grid_sizes[][2] = { { 5, 256 }, { 10, 64 }, { 20, 32 }, { 40, 8 } };

			/** pick a block_size to use - must be <= 1024, and should be 2^n - 1 for some n */
			static int const BLOCK_SIZE = 255;

			struct OfflineTPWriter
			{
				std::ostream& os;
				std::vector<Writer::basic_node_type> const& nodes;
				std::vector<uint> const& node_levels;
				std::vector<Writer::edge_type> const& edges;

				OfflineTPWriter(std::ostream& os, std::vector<Writer::basic_node_type> const& nodes, std::vector<uint> const& node_levels, std::vector<Writer::edge_type> const& edges)
				: os(os), nodes(nodes), node_levels(node_levels), edges(edges)
				{
					TrackTime tt { VerboseTrackTime() };
					tt.track("calculate grid boundaries");
					DO_CalcBounds();
					tt.track("create base cell blocks");
					DO_PrepareCellBlocks();
					tt.track("fill nodes into blocks");
					DO_FillBlocks();
					tt.track("count and sort edges");
					DO_CountAndSortEdges();
					DO_Write(tt);
					tt.summary();
				}

				struct Block
				{
					int32_t basex = 0, basey = 0;
					uint32_t level = 0;
					uint32_t next = UINT32_MAX;
					uint32_t count = 0;
					uint32_t node_ids[BLOCK_SIZE];

					Block(int32_t basex, int32_t basey, uint32_t level)
					: basex(basex), basey(basey), level(level)
					{
						for (auto& ids: node_ids) ids = UINT32_MAX;
					}
				};
				std::vector<Block> blocks;

				uint32_t createBlock(int32_t basex, int32_t basey, uint32_t level)
				{
					uint32_t ndx = blocks.size();
					blocks.push_back(Block(basex, basey, level));
					return ndx;
				}

				uint32_t extendBlock(uint32_t block)
				{
					auto& old = blocks[block];
					assert(old.next == UINT32_MAX);
					uint32_t ndx = blocks.size();
					old.next = ndx;
					blocks.push_back(Block(old.basex, old.basey, old.level));
					return ndx;
				}

				uint32_t sameLevelLastBlock(uint32_t block)
				{
					if (UINT32_MAX == block) return block;
					Block const* blockb = &blocks[block];
					uint32_t t;
					while (UINT32_MAX != (t = blockb->next)) {
						Block const* tb = &blocks[t];
						if (blockb->basex != tb->basex || blockb->basey != tb->basey || blockb->level != tb->level) {
							std::cerr << "block chain is not in the same grid level\n";
							std::abort();
						}
						blockb = tb; block = t;
					}
					return block;
				}

				uint32_t blockAddNode(uint32_t node, uint32_t block)
				{
					block = sameLevelLastBlock(block);
					if (blocks[block].count >= BLOCK_SIZE) block = extendBlock(block);
					auto& b = blocks[block];
					uint32_t bndx = b.count++;
					b.node_ids[bndx] = node;
					return (block << 10) + bndx;
				}

				/** follow chain for a base cell, so we can link a new block to it */
				uint32_t findBaseCellLastBlock(int32_t x, int32_t y) {
					uint32_t block = cell_blocks[getBaseGridOffset(x, y)], t;
					assert(UINT32_MAX != block);
					while (UINT32_MAX != (t = blocks[block].next)) {
						block = t;
					}
					return block;
				}

				uint32_t curLevel = 0; /** current grid level. used to print stats and verify ascending CH Level */
				uint32_t curLevelNodes = 0; /** nodes on current level for stats */
				uint32_t core_block_start = UINT32_MAX;

				/** sort a node into the grid. must be called in ascending rank (CH Level) order
				 * @param node index in original graph
				 */
				int blocksAddNode(uint32_t node) {
					int32_t x = nodes[node].lon, y = nodes[node].lat;
					uint32_t rank = node_levels[node];

					uint32_t level = UINT32_MAX;
					for (uint i = 0; i < array_size(grid_sizes); ++i) {
						if (grid_sizes[i][0] > rank) {
							level = i;
							break;
						}
					}

					if (curLevel < level) {
						// print stats if we hit this level the first time
						std::cout << "After Level " << curLevel << ": Blocks in use: " << blocks.size() << " (min: +" << ((BLOCK_SIZE+1+curLevelNodes)/BLOCK_SIZE) << ")\n";
						curLevel = level;
						curLevelNodes = 0;
					}
					else if (curLevel != level) {
						std::cerr << "Nodes not in CH level ascending order\n";
						std::abort();
					}
					++curLevelNodes;

					uint32_t block;

					if (UINT32_MAX != curLevel) {
						uint32_t cellNdx = getGridOffset(curLevel, x, y);
						block = cell_blocks[cellNdx];
						if (UINT32_MAX == block) {
							assert(curLevel > 0);
							block = createBlock(getGridBaseX(curLevel, getGridX(curLevel, x)), getGridBaseY(curLevel, getGridY(curLevel, y)), curLevel);
							cell_blocks[cellNdx] = block;
						}
					} else {
						// got a core node!
						if (UINT32_MAX == core_block_start) core_block_start = createBlock(base_cell_x, base_cell_y, UINT32_MAX);
						block = core_block_start;
					}

					if (curLevel > 0) {
						// above base grid, make sure we have a link up
						uint32_t old = findBaseCellLastBlock(x, y);
						assert(UINT32_MAX != old);
						if (old < block) {
							// no link yet
							assert(UINT32_MAX == blocks[old].next);
							blocks[old].next = block;
						} else {
							// current chain should end in the link
							assert(old == sameLevelLastBlock(block));
						}
					}
					return blockAddNode(node, block);
				}

				/** length of a block chain starting at block */
				uint blockChainLength(uint32_t block) {
					uint len = 0;
					while (UINT32_MAX != block) {
						block = blocks[block].next;
						++len;
					}
					return len;
				}

				/** length of a block chain at the same level starting at block */
				uint blockLevelChainLength(uint32_t block) {
					uint len = 0;
					if (UINT32_MAX == block) return 0;
					auto lvl = blocks[block].level;

					do {
						++len;
						block = blocks[block].next;
					} while (UINT32_MAX != block && lvl == blocks[block].level);
					return len;
				}


				int32_t minLon, minLat, maxLon, maxLat;
				void DO_CalcBounds()
				{
					minLon = minLat = std::numeric_limits<int32_t>::max();
					maxLon = maxLat = std::numeric_limits<int32_t>::min();
					for (auto const& node: nodes) {
						minLon = std::min(minLon, node.lon); maxLon = std::max(maxLon, node.lon);
						minLat = std::min(minLat, node.lat); maxLat = std::max(maxLat, node.lat);
					}
					std::cerr << "Size: " << (maxLon - minLon) << " x " << (maxLat - minLat) << "\n";
				}

				/**
				 * for all grid cells in all grids store the current block to insert nodes into (or -1)
				 * starting with the first cell in the first row of the first grid
				 */
				std::vector<uint32_t> cell_blocks;
				int32_t base_cell_x, base_cell_y;
				int32_t base_cell_width, base_cell_height;

				uint32_t getGridX(uint32_t level, int32_t x)
				{
					int64_t basex = (x - base_cell_x) / base_cell_width;
					return (uint32_t) ((basex * grid_sizes[level][1]) / grid_sizes[0][1]);
				}
				uint32_t getGridY(uint32_t level, int32_t y)
				{
					int64_t basey = (y - base_cell_y) / base_cell_height;
					return (uint32_t) ((basey * grid_sizes[level][1]) / grid_sizes[0][1]);
				}
				// offset from first cell in a level
				uint32_t _getLocalGridOffset(uint32_t level, int32_t x, int32_t y)
				{
					return getGridY(level, y) * grid_sizes[level][1] + getGridX(level, x);
				}
				uint32_t getBaseGridOffset(int32_t x, int32_t y)
				{
					return _getLocalGridOffset(0, x, y);
				}
				uint32_t getGridOffset(uint32_t level, int32_t x, int32_t y)
				{
					int32_t baseNdx = 0;
					for (uint32_t i = 0; i < level; ++i) baseNdx += grid_sizes[i][1]*grid_sizes[i][1];
					return baseNdx + _getLocalGridOffset(level, x, y);
				}
				int32_t getGridBaseX(uint32_t level, uint32_t cellX)
				{
					int32_t baseCellX = cellX * (grid_sizes[0][1] / grid_sizes[level][1]);
					return base_cell_x + base_cell_width * baseCellX;
				}
				int32_t getGridBaseY(uint32_t level, uint32_t cellY)
				{
					int32_t baseCellY = cellY * (grid_sizes[0][1] / grid_sizes[level][1]);
					return base_cell_y + base_cell_height * baseCellY;
				}


				void DO_PrepareCellBlocks()
				{
					base_cell_x = minLon - 1;
					base_cell_y = minLat - 1;

					uint32_t cell_count = 0;
					for (auto const& grid_size: grid_sizes) cell_count += grid_size[1] * grid_size[1];

					uint32_t n = grid_sizes[0][1];
					base_cell_width = (maxLon - minLon) / n + 1;
					base_cell_height = (maxLat - minLat) / n + 1;
					std::cout << "Base cell size: " << base_cell_width << " x " << base_cell_height << "\n";

					cell_blocks.resize(cell_count, UINT32_MAX);

					// base grid must always be allocated in a fixed order
					for (uint32_t i = 0, x = 0; x < n; ++x) {
						for (uint32_t y = 0; y < n; ++y, ++i) {
							uint32_t ndx = createBlock(base_cell_x + x * base_cell_width, base_cell_y + y * base_cell_height, 0);
							cell_blocks[i] = ndx;
							assert(i == ndx);
						}
					}
				}

				/** for each index in the original graph store the assigned nodeID */
				std::vector<uint32_t> nodeBlockIDs;

				void checkNodeID(uint32_t nodeid) {
					if ( (nodeid >> 10) >= blocks.size() ) std::abort();
					if ( (nodeid & 1023) >= BLOCK_SIZE ) std::abort();
				}

				/** sort all nodes into grid and their blocks */
				void DO_FillBlocks() {
					std::vector<uint32_t> node_indices(nodes.size());
					std::iota(node_indices.begin(), node_indices.end(), 0);
					std::sort(node_indices.begin(), node_indices.end(), [this](uint32_t a, uint32_t b) {
						return node_levels[a] < node_levels[b];
					});

					nodeBlockIDs.resize(nodes.size());
					for (auto ndx: node_indices) {
						nodeBlockIDs[ndx] = blocksAddNode(ndx);
						checkNodeID(nodeBlockIDs[ndx]);
					}

					// stats
					uint blocks_min = (BLOCK_SIZE+1+nodes.size())/BLOCK_SIZE;
					std::size_t wasted = (blocks.size() - blocks_min) * 16 + (blocks.size() * (std::size_t)BLOCK_SIZE - nodes.size()) * 16;
					std::cout << "Blocks in use: " << blocks.size() << " (min: " << blocks_min << ")\n";
					std::cout << "Wasted " << wasted << " bytes\n";

					{
						uint cell_ndx = 0;
						for (uint level = 0; level < array_size(grid_sizes); ++level) {
							uint maxChain = 1, blocks = 0;
							for (uint k = 0, l = grid_sizes[level][1]*grid_sizes[level][1]; k < l; ++k, ++cell_ndx) {
								maxChain = std::max<uint>(maxChain, blockChainLength(cell_blocks[cell_ndx]));
								blocks += blockLevelChainLength(cell_blocks[cell_ndx]);
							}
							std::cout << "Max chain length for level " << level << ": " << maxChain << ", total blocks: " << blocks << "\n";
						}
					}
					std::cout << "Core blocks: " << blockChainLength(core_block_start) << "\n";
				}

				std::vector<uint32_t> nodeFirstOutEdgeID, nodeFirstInEdgeID, nodeEndEdgeID; /** index from original graph */
				/** indices mapping in both directions */
				std::vector<uint> useEdges; /** (out file) edgeID -> original graph edge id */
				std::vector<uint32_t> edgesReverse; /** original graph edge id -> (out file) edgeID */

				void DO_CountAndSortEdges() {
					uint use_edges = 0; /** number of edges we want to store; we drop shortcuts in the core graph */

					auto const coreRank = grid_sizes[array_size(grid_sizes)-1][0];
					nodeFirstOutEdgeID.resize(nodes.size(), 0);
					nodeFirstInEdgeID.resize(nodes.size(), 0);
					nodeEndEdgeID.resize(nodes.size());

					// count edges for each node
					for (auto const& edge: edges) {
						auto srank = node_levels[edge.src], trank = node_levels[edge.tgt];
						assert(srank != trank);
						if (srank >= coreRank && trank >= coreRank) {
							// edge in core graph
							if (c::NO_NID == edge.center_node || node_levels[edge.center_node] < coreRank) {
								// don't store core shortcuts
								// store core edges always in the source
								++nodeFirstOutEdgeID[edge.src];
								++use_edges;
							}
						} else if (srank < trank) {
							++nodeFirstOutEdgeID[edge.src];
							++use_edges;
						} else {
							++nodeFirstInEdgeID[edge.tgt];
							++use_edges;
						}
					}

					// store start offset for edge mapping below, start as simple
					// copies of nodeFirstOutEdgeID and nodeFirstInEdgeID
					std::vector<uint32_t> nextOutEdge(nodes.size());
					std::vector<uint32_t> nextInEdge(nodes.size());

					// calculate outgoing and incoming first edgeID for each node 
					uint32_t nextEdgeID = 0;
					for (auto const& block: blocks) {
						assert(block.count <= BLOCK_SIZE);

						for (uint j = 0; j < BLOCK_SIZE; ++j) {
							uint32_t n = block.node_ids[j];
							assert((UINT32_MAX != n) == (j < block.count));

							if (UINT32_MAX != n) {
								uint32_t currentEdgeID;

								currentEdgeID = nextEdgeID;
								nextEdgeID += nodeFirstOutEdgeID[n];
								nextOutEdge[n] = nodeFirstOutEdgeID[n] = currentEdgeID;

								currentEdgeID = nextEdgeID;
								nextEdgeID += nodeFirstInEdgeID[n];
								nextInEdge[n] = nodeFirstInEdgeID[n] = currentEdgeID;

								nodeEndEdgeID[n] = nextEdgeID;
							}
						}
					}

					useEdges.resize(use_edges);
					edgesReverse.resize(edges.size());

					uint edge_ndx = 0;
					for (auto const& edge: edges) {
						uint32_t k = UINT32_MAX;

						auto srank = node_levels[edge.src], trank = node_levels[edge.tgt];
						assert(srank != trank);

						if (srank >= coreRank && trank >= coreRank) {
							// edge in core graph
							if (c::NO_NID == edge.center_node || node_levels[edge.center_node] < coreRank) {
								// don't store core shortcuts
								// store core edges always in the source
								k = nextOutEdge[edge.src]++;
							}
						} else if (srank < trank) {
							k = nextOutEdge[edge.src]++;
						} else {
							k = nextInEdge[edge.tgt]++;
						}

						if (UINT32_MAX != k) useEdges[k] = edge_ndx;
						edgesReverse[edge_ndx++] = k;
					}
				}

				uint written;
				void writeInt(uint32_t val) {
					unsigned char buf[4];
					buf[0] = val >> 24;
					buf[1] = val >> 16;
					buf[2] = val >>  8;
					buf[3] = val;
					os.write((char const*) (&buf[0]), sizeof(buf));
					written += 4;
				}

				void writeInt(int32_t val) {
					writeInt((uint32_t) val);
				}

				void writeAlign() {
					static const uint PAGESIZE = 4096;
					static const char padding[PAGESIZE] = { };
					uint padlen = PAGESIZE - (written % PAGESIZE);
					if (PAGESIZE == padlen) return;
					os.write(padding, padlen);
					written += padlen;
				}



				/* 1. section */
				void writeHeader() {
					// magic ("CHGOffTP") + version (1)
					writeInt((uint32_t) 0x4348474Fu);
					writeInt((uint32_t) 0x66665450u);
					writeInt((uint32_t) 1u);

					writeInt(base_cell_x);
					writeInt(base_cell_y);
					writeInt(base_cell_width);
					writeInt(base_cell_height);
					writeInt((uint32_t) grid_sizes[0][1]);
					writeInt((uint32_t) grid_sizes[0][1]);
					writeInt((uint32_t) BLOCK_SIZE);
					writeInt((uint32_t) blocks.size());
					writeInt(core_block_start);
					writeInt((uint32_t) useEdges.size());
				}

				/* 2. section */
				void writeNodeGeoBlocks() {
					for (auto const& block: blocks) {
						writeInt(block.next);
						assert(block.count <= BLOCK_SIZE);
						writeInt(block.count);

						for (uint j = 0; j < BLOCK_SIZE; ++j) {
							auto n = block.node_ids[j];
							assert((UINT32_MAX != n) == (j < block.count));
							if (UINT32_MAX != n) {
								writeInt(nodes[n].lon);
								writeInt(nodes[n].lat);
							} else {
								writeInt(0); writeInt(0);
							}
						}
					}
				}

				/* 3. section */
				void writeNodeEgesBlocks() {
					uint32_t currentEndEdgeID = 0;
					for (auto const& block: blocks) {
						writeInt(0);

						for (uint j = 0; j < BLOCK_SIZE; ++j) {
							auto n = block.node_ids[j];
							assert((UINT32_MAX != n) == (j < block.count));
							if (UINT32_MAX != n) {
								writeInt(nodeFirstOutEdgeID[n]);
								writeInt(nodeFirstInEdgeID[n]);
								currentEndEdgeID = nodeEndEdgeID[n];
							} else {
								writeInt(currentEndEdgeID);
								writeInt(currentEndEdgeID);
							}
						}

						writeInt(currentEndEdgeID);
					}
				}

				/* 4. section */
				void writeEdgesBlock() {
					auto const coreRank = grid_sizes[array_size(grid_sizes)-1][0];

					for (auto edge_ndx: useEdges) {
						auto const& edge = edges[edge_ndx];
						auto srank = node_levels[edge.src], trank = node_levels[edge.tgt];
						assert(srank != trank);
						/* either store target in "CH up" direction or target in the core */
						if (srank < trank || trank >= coreRank) {
							writeInt(nodeBlockIDs[edge.tgt]);
						} else {
							writeInt(nodeBlockIDs[edge.src]);
						}
						writeInt((uint32_t) edge.time);
					}
				}

				/* 5. section */
				void writeEdgesDetailsBlock() {
					for (auto edge_ndx: useEdges) {
						auto const& edge = edges[edge_ndx];
						writeInt((uint32_t) edge.dist);

						assert((c::NO_EID == edge.child_edge1) == (c::NO_EID == edge.child_edge2));
						if (c::NO_EID == edge.child_edge1) {
							writeInt((int32_t) -1); writeInt((int32_t) -1); writeInt((int32_t) -1);
						} else {
							assert(UINT32_MAX != edgesReverse[edge.child_edge1]);
							assert(UINT32_MAX != edgesReverse[edge.child_edge2]);
							assert(c::NO_NID != edge.center_node);
							writeInt(edgesReverse[edge.child_edge1]);
							writeInt(edgesReverse[edge.child_edge2]);
							writeInt(nodeBlockIDs[edge.center_node]);
						}
					}
				}

				void DO_Write(TrackTime &tt) {
					tt.track("write header");
					writeHeader();
					writeAlign();

					tt.track("write nodes geo data");
					writeNodeGeoBlocks();
					writeAlign();

					tt.track("write nodes edge ids");
					writeNodeEgesBlocks();
					writeAlign();

					tt.track("write edge basic data");
					writeEdgesBlock();
					writeAlign();

					tt.track("write edge detail data");
					writeEdgesDetailsBlock();
				}
			};
		}

		std::vector<std::size_t> simple_histogram(std::vector<uint> const& data) {
			std::vector<std::size_t> hist;
			for (auto value: data) {
				if (value >= hist.size()) hist.resize(value + 1, 0);
				++hist[value];
			}
			return hist;
		}

		void Writer::writeCHGraph(std::ostream& os, GraphCHOutData<basic_node_type, edge_type> const& data)
		{
#ifndef NDEBUG
			auto hist = simple_histogram(data.node_levels);
			Debug("Histogram:");
			{ uint32_t level=0; for (auto count: hist) { Debug(level << ": " << count); ++level; } }
#endif

			std::cout << "Writing Offline ToureNPlaner Graph: Nodes: " << data.nodes.size() << ", Edges: " << data.edges.size() << "\n";
			OfflineTPWriter w(os, data.nodes, data.node_levels, data.edges);
		}
	}
}
