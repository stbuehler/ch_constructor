#ifndef _CONTINUATION_READER_H
#define _CONTINUATION_READER_H

#include <iterator>

namespace chc {
	/* C++11 iterator for `bool read(T& out)` interfaces */
	template<typename T, typename Container, bool (Container::*read)(T& out)>
	struct Continuation {
	private:
		Container* container;
	public:
		struct iterator : public std::iterator<std::input_iterator_tag, T> {
		private:
			std::size_t ndx = 0;
			Container* container = nullptr;
			T current;

		public:
			inline explicit iterator() { }
			inline explicit iterator(Container* container)
			: container(container) {
				if (!(container->*read)(current)) container = nullptr;
			}

			inline bool operator==(iterator const& o) const { return ndx == o.ndx && container == o.container; };
			inline bool operator!=(iterator const& o) const { return !operator==(o); };
			inline iterator operator++(int) { iterator tmp(*this); operator++(); return tmp; }
			inline iterator& operator++() {
				if (ndx >= 0 && (container->*read)(current)) {
					++ndx;
				} else {
					container = nullptr;
					ndx = 0;
				}
				return *this;
			}

			inline T& operator*() { return current; }
		};

		inline explicit Continuation(Container* container) : container(container) { }

		inline iterator begin() { return iterator(container); }
		inline iterator end() { return iterator(); }
	};


	/* C++11 iterator for `T read()` interfaces, where the number of items is known in advance */
	template<typename T, typename Container, T (Container::*read)(std::size_t ndx)>
	struct LimitedIteration {
	private:
		Container* container = nullptr;
		std::size_t next_ndx = 0, limit = 0;
	public:
		struct iterator : public std::iterator<std::input_iterator_tag, T> {
		private:
			LimitedIteration& limited;
			std::size_t ndx;
			T current;

		public:
			inline explicit iterator(LimitedIteration& limited, std::size_t ndx) // "end()"
			: limited(limited), ndx(ndx) { }
			inline explicit iterator(LimitedIteration& limited)
			: limited(limited), ndx(limited.next_ndx) {
				if (ndx < limited.limit) {
					++limited.next_ndx;
					current = (limited.container->*read)(ndx);
				}
			}

			inline bool operator==(iterator const& o) const { return ndx == o.ndx; };
			inline bool operator!=(iterator const& o) const { return !operator==(o); };
			inline iterator operator++(int) { iterator tmp(*this); operator++(); return tmp; }
			inline iterator& operator++() {
				if (ndx >= limited.limit) return *this; /* already at end */
				++ndx;
				if (ndx >= limited.limit) return *this; /* now at end */
				if (ndx != limited.next_ndx) {
					std::cerr << "Can iterate with only one iterator\n";
					std::terminate();
				}
				++limited.next_ndx;
				current = (limited.container->*read)(ndx);
				return *this;
			}

			inline T& operator*() { return current; }
		};


		inline explicit LimitedIteration(Container* container, std::size_t limit = 0) : container(container), limit(limit) { }

		void setLimit(std::size_t limit) { this->limit = limit; }
		std::size_t nextIndex() { return next_ndx; }

		iterator begin() { return iterator(*this); }
		iterator end() { return iterator(*this, limit); }
	};
}

#endif
