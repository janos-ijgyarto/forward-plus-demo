#ifndef FORWARDPLUSDEMO_UTILITIES_EVENTQUEUE_HPP
#define FORWARDPLUSDEMO_UTILITIES_EVENTQUEUE_HPP
#include <array>
#include <vector>
#include <atomic>
namespace ForwardPlusDemo
{
	class EventQueue
	{
	public:
		using Buffer = std::vector<char>;
		using BufferIterator = Buffer::const_iterator;

		struct Header
		{
			uint32_t event_id;
			uint32_t data_size;
			// TODO: anything else?
		};

		bool is_empty() const { return m_data.empty(); }
		size_t get_size() const { return m_data.size(); }

		void clear() { m_data.clear(); }

		template<typename T>
		T* allocate_event(uint32_t id)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			char* data_ptr = allocate_raw_data(id, sizeof(T));
			return new(data_ptr)T;
		}

		char* allocate_raw_data(uint32_t id, size_t size);

		template<typename T>
		void write_event(uint32_t id, const T& event)
		{
			T* event_ptr = allocate_event<T>(id);
			*event_ptr = event;
		}

		class Iterator
		{
		public:
			bool is_valid() const { return m_data_it != m_data_end; }

			void advance() { m_data_it += get_header().data_size + sizeof(Header); }

			const Header& get_header() const { return *reinterpret_cast<const Header*>(&(*m_data_it)); }
			const char* get_event_data() const
			{
				// TODO: assert if invalid!
				return &(*m_data_it) + sizeof(Header);
			}

			template<typename T>
			const T* get_event() const
			{
				// FIXME: could this be static_cast?
				return reinterpret_cast<const T*>(get_event_data());
			}
		private:
			Iterator(const EventQueue& queue, size_t start_offset) : m_data_it(queue.m_data.cbegin() + start_offset), m_data_end(queue.m_data.cend()) {}
			BufferIterator m_data_it;
			BufferIterator m_data_end;

			friend EventQueue;
		};

		Iterator get_iterator(size_t start_offset = 0) const { return Iterator(*this, start_offset); }
	private:
		Buffer m_data;
	};

	// Extremely naive "lock-free" double buffer for simplicity
	class EventDoubleBuffer
	{
	public:
		EventDoubleBuffer();

		EventQueue* get_read_queue() { return m_signal ? m_read_queue : nullptr; }
		EventQueue* get_write_queue() { return m_write_queue; }

		void dispatch_write();
		void finish_read();
	private:
		std::array<EventQueue, 2> m_queues;
		EventQueue* m_read_queue;
		EventQueue* m_write_queue;
		std::atomic<bool> m_signal;
	};
}
#endif