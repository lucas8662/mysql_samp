#pragma once
#ifndef INC_CQUERYQUEUE_H
#define INC_CQUERYQUEUE_H


#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>

#include "CMySQLQuery.h"


// Unbounded FIFO of threaded queries, consumed by one or more connection
// threads. mysql_tquery uses a queue with a single consumer, so its queries run
// strictly in order; mysql_pquery shares one queue between every pool
// connection, so a free connection takes the next query instead of waiting
// behind a slow one.
//
// The queue's condition variable is also the only thing a consumer sleeps on:
// work that belongs to one connection (reconnect, charset, shutdown) wakes it
// through Notify().
class CQueryQueue
{
public:
	void Push(std::unique_ptr<CMySQLQuery> query)
	{
		{
			std::lock_guard<std::mutex> lock(m_Mtx);
			m_Queue.push_back(std::move(query));
		}
		// notify_one could wake a consumer that is waiting for a reconnect
		// and cannot take the query; it would go back to sleep and the
		// notification would be lost for the connected ones.
		m_Cond.notify_all();
	}

	std::unique_ptr<CMySQLQuery> TryPop()
	{
		std::lock_guard<std::mutex> lock(m_Mtx);
		if (m_Queue.empty())
			return nullptr;
		std::unique_ptr<CMySQLQuery> query = std::move(m_Queue.front());
		m_Queue.pop_front();
		return query;
	}

	// Wakes every consumer so it re-checks its own pending work.
	void Notify()
	{
		// Taking the mutex orders this notification after a consumer's
		// predicate check, so it cannot be lost between the check and the wait.
		{
			std::lock_guard<std::mutex> lock(m_Mtx);
		}
		m_Cond.notify_all();
	}

	// Sleeps until pred(has_queries) holds or the timeout expires. pred runs
	// with the queue locked and must not call back into this queue.
	template<typename Pred>
	void WaitFor(std::chrono::milliseconds timeout, Pred pred)
	{
		std::unique_lock<std::mutex> lock(m_Mtx);
		m_Cond.wait_for(lock, timeout, [&] { return pred(!m_Queue.empty()); });
	}

	// Discards queries nobody will run; only call once every consumer stopped.
	void Clear()
	{
		std::lock_guard<std::mutex> lock(m_Mtx);
		m_Queue.clear();
	}

private:
	std::mutex m_Mtx;
	std::condition_variable m_Cond;
	std::deque<std::unique_ptr<CMySQLQuery> > m_Queue;
};


#endif // INC_CQUERYQUEUE_H
