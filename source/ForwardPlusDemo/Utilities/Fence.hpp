#ifndef FORWARDPLUSDEMO_UTILITIES_FENCE_HPP
#define FORWARDPLUSDEMO_UTILITIES_FENCE_HPP
#include <atomic>
namespace ForwardPlusDemo
{
	class Fence
	{
	public:
		Fence(uint64_t value) : m_value(value) {}

		void signal(uint64_t value) 
		{ 
			if (value < m_value) 
				return; 
			
			m_value = value; 
			m_value.notify_all(); 
		}

		void wait_until(uint64_t value) const
		{
			uint64_t old_value = m_value.load();
			while (old_value < value)
			{
				m_value.wait(old_value);
				old_value = m_value.load();
			}
		}
	private:
		std::atomic<uint64_t> m_value;
	};
}
#endif