#include <ForwardPlusDemo/Utilities/EventQueue.hpp>

namespace ForwardPlusDemo
{
	char* EventQueue::allocate_raw_data(uint32_t id, size_t size)
	{
		{
			Header event_header;
			event_header.event_id = id;
			event_header.data_size = static_cast<uint32_t>(size);

			const char* header_begin = reinterpret_cast<const char*>(&event_header);
			const char* header_end = header_begin + sizeof(Header);
			m_data.insert(m_data.end(), header_begin, header_end);
		}

		const size_t data_offset = m_data.size();
		m_data.insert(m_data.end(), size, char(0));
		return m_data.data() + data_offset;
	}

	EventDoubleBuffer::EventDoubleBuffer()
	{
		m_read_queue = &m_queues[0];
		m_write_queue = &m_queues[1];
	}

	void EventDoubleBuffer::dispatch_write()
	{
		if (m_signal == true)
		{
			// Read thread still reading
			return;
		}

		// Swap buffers
		EventQueue* read_queue = m_read_queue;
		m_read_queue = m_write_queue;
		m_write_queue = read_queue;

		m_write_queue->clear();

		// Set the signal
		m_signal = true;
	}

	void EventDoubleBuffer::finish_read()
	{
		if (m_signal == true)
		{
			m_signal = false;
		}
	}
}