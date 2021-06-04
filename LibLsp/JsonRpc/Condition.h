#pragma once



//#include <condition_variable>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition_variable.hpp>

template <class T>
class Condition
{
public:

	boost::mutex m_mutex;
	boost::condition_variable   m_condition;
	~Condition() {
		m_condition.notify_all();
	}
	void notify(std::unique_ptr<T> data) noexcept
	{
		{
			boost::lock_guard<boost::mutex> eventLock(m_mutex);
			any.swap(data);
		}
		// wake up one waiter
		m_condition.notify_one();
	};

	
	std::unique_ptr<T> wait(unsigned timeout=0)
	{
		boost::unique_lock<boost::mutex> ul(m_mutex);
		if (!timeout) {
			m_condition.wait(ul,[&]() {
					if (!any)
						return false;
					return true;
			});
		}
		else{
			if(!any){
				boost::cv_status status = m_condition.wait_for(ul, boost::chrono::milliseconds(timeout));
				if (status == boost::cv_status::timeout)
				{
					return {};
				}
			}
		}
		return std::unique_ptr<T>(any.release());
		
	}
private:
	std::unique_ptr<T> any;
};
