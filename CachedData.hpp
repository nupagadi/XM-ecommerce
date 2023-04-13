#pragma once

#include <cassert>
#include <unordered_map>
#include <future>
#include <mutex>

namespace ecommerce {

struct IDatabase;

template <typename TData, typename TId = int>
class CachedData
{
	struct ICacheItem;

public:

	CachedData(size_t a_maxSize)
		: m_maxSize(a_maxSize)
	{
		// + 1, since first the new item is added, then the oldest is removed.
		m_cacheItemById.reserve(a_maxSize + 1);
		m_cacheItemByCount.reserve(a_maxSize + 1);
	}

	// If a user of this class wants performance, they better use types that are good to std::move.
	// Or wrap it into std::unique_ptr. But that's not mandatory of course.
	// ProductDetails seems to be quite OK to use with std::move.
	TData getData(TId a_id, IDatabase* a_db)
	{
		auto promise = std::make_unique<PromisingCacheItem>(a_id);
		std::unique_ptr<ICacheItem> cacheItem = std::make_unique<EmptyCacheItem>(promise);

		{
			std::lock_guard<std::mutex> lock{m_mtx};

			// emplace() won't add a new element in case it is present.
			auto pair = m_cacheItemById.emplace(a_id, std::move(cacheItem));
			if (!pair.second)
			{
				auto& existingCacheItem = pair.first->second;
				m_cacheItemByCount[m_count] = existingCacheItem.get();
				m_cacheItemByCount.erase(existingCacheItem->getCount());
				existingCacheItem->setCount(m_count);
				m_count++;

				if (existingCacheItem->isReady())
					return existingCacheItem->getData();
				else if (!existingCacheItem->isWaiting())
				{
					// So not to get shared state every time.
					existingCacheItem = std::make_unique<FilledCacheItem>(existingCacheItem);
					m_cacheItemByCount[existingCacheItem->getCount()] = existingCacheItem.get();
					return existingCacheItem->getData();
				}
				else 
					cacheItem = std::make_unique<WaitingCacheItem>(existingCacheItem);
			}
			else
			{
				cacheItem = std::move(promise);

				auto& addedCacheItem = pair.first->second;
				m_cacheItemByCount[m_count] = addedCacheItem.get();
				addedCacheItem->setCount(m_count);

				if (m_cacheItemByCount.size() > m_maxSize)
				{
					// The loop is not more than a few elements.
					for (;; m_nextToDelete++)
					{
						auto toErase = m_cacheItemByCount.find(m_nextToDelete);
						if (toErase != m_cacheItemByCount.cend())
						{
							m_cacheItemById.erase(toErase->second->getId());
							m_cacheItemByCount.erase(toErase);
							m_nextToDelete++;
							break;
						}
					}
				}
				m_count++;
			}

			assert(m_cacheItemById.size() <= m_maxSize);
			assert(m_cacheItemByCount.size() <= m_maxSize);
		}

		// Avoiding additional fetching from the DB,
		// in case there is already a thread that is fetching the same element.
		if (cacheItem->isWaiting())
		{
			return cacheItem->getData();
		}
		
		// I'm not sure whether I can retrieve data ID's from the DB (with getProductIds()) and just check for
		// an element existence before actually trying to retrieve it, since usually DB's are meant to be dynamic,
		// but it wasn't specified in the task. Hence, I fetch without pre-emptive checking.

		try {
			auto fetched = a_db->fetchProductDetails(a_id);
			// Won't use move semantics here, since I need both to set a shared state and to return a result.
			cacheItem->setData(fetched);

			// RVO
			return fetched;
		}
		catch (std::runtime_error& ex)
		{
			dynamic_cast<PromisingCacheItem*>(cacheItem.get())
				->getPromise().set_exception(std::current_exception());
			throw;
		}
	}

private:

	struct ICacheItem
	{
		virtual ~ICacheItem() = default;

		virtual bool isReady() const = 0;

		virtual bool isWaiting() const = 0;

		virtual TData getData() const = 0;

		virtual std::shared_future<TData>& getFuture() = 0;

		virtual void setData(const TData&)
		{
		}

		TId getId() const { return Id; }
		void setId(TId a_id) { Id = a_id; }
		size_t getCount() const { return Count; }
		void setCount(size_t a_count) { Count = a_count; }

	private:

		TId Id = {};
		size_t Count = {};
	};

	struct PromisingCacheItem : ICacheItem
	{
		PromisingCacheItem(TId a_id)
		{
			this->setId(a_id);
			Future = Promise.get_future();
		}

		bool isReady() const override
		{
			return false;
		}

		bool isWaiting() const override
		{
			return false;
		}

		TData getData() const override
		{
			// Not expected to be called.
			assert(false);
		}

		std::shared_future<TData>& getFuture() override
		{
			return Future;
		}

		void setData(const TData& a_data) override
		{
			Promise.set_value(a_data);
		}

		std::promise<TData>& getPromise()
		{
			return Promise;
		}

	private:

		std::promise<TData> Promise;
		std::shared_future<TData> Future;
	};

	struct EmptyCacheItem : ICacheItem
	{
		EmptyCacheItem(std::unique_ptr<PromisingCacheItem>& a_promise)
			: Future(a_promise->getFuture())
		{
			this->setId(a_promise->getId());
		}

		bool isReady() const override
		{
			return false;
		}

		bool isWaiting() const override
		{
			return Future.wait_for(std::chrono::seconds{}) == std::future_status::timeout;
		}

		TData getData() const override
		{
			throw std::runtime_error("CachedData::EmptyCacheItem: no data");
		}

		std::shared_future<TData>& getFuture() override
		{
			return Future;
		}

	private:

		std::shared_future<TData> Future;
	};

	struct WaitingCacheItem : ICacheItem
	{
		WaitingCacheItem(std::unique_ptr<ICacheItem>& a_emptyItem)
			: Future(a_emptyItem->getFuture())
		{
			this->setId(a_emptyItem->getId());
			this->setCount(a_emptyItem->getCount());
		}

		bool isReady() const override
		{
			return true;
		}

		bool isWaiting() const override
		{
			return true;
		}

		TData getData() const override
		{
			// TODO: use a constant for the duration
			// TODO: perhaps, it is not this class responsibility
			if (Future.wait_for(std::chrono::seconds(3)) == std::future_status::ready)
				return Future.get();
			else 
				throw std::runtime_error("CachedData::WaitingCacheItem: too long");
		}

		std::shared_future<TData>& getFuture() override
		{
			return Future;
		}

	private:

		std::shared_future<TData> Future;
	};

	struct FilledCacheItem : ICacheItem
	{
		FilledCacheItem(std::unique_ptr<ICacheItem>& a_fulfilledCacheItem)
			: Data(a_fulfilledCacheItem->getFuture().get())
		{
			this->setId(a_fulfilledCacheItem->getId());
			this->setCount(a_fulfilledCacheItem->getCount());
		}

		bool isReady() const override
		{
			return true;
		}

		bool isWaiting() const override
		{
			return false;
		}

		TData getData() const override
		{
			return Data;
		}

		// Not expected to be called.
		std::shared_future<TData>& getFuture() override
		{
			assert(false);
		}

	private:

		TData Data;
	};

private:

	const size_t m_maxSize;
	size_t m_count = 1;
	size_t m_nextToDelete = m_count;

	std::mutex m_mtx;

	std::unordered_map<TId, std::unique_ptr<ICacheItem>> m_cacheItemById;
	std::unordered_map<size_t, ICacheItem*> m_cacheItemByCount;
};

}

