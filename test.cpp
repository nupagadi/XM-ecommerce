#include <iostream>
#include <string>
#include <thread>
#include <map>
#include <chrono>
#include <random>

#include "IDatabase.h"
#include "ProductDetails.hpp"
#include "CachingDatabase.hpp"

struct DatabaseStub : ecommerce::IDatabase
{
	DatabaseStub(size_t a_lagMs)
		: m_lagMs(a_lagMs)
	{
	}

	ecommerce::ProductDetails fetchProductDetails(int a_id) override
	{
		auto it = m_data.find(a_id);
		if (it == m_data.cend())
			throw std::runtime_error("no such element");

		// Emulate slow DB.
		std::this_thread::sleep_for(std::chrono::milliseconds(m_lagMs));

		return it->second;
	}

	std::vector<int> getProductIds() const override
	{
		decltype(getProductIds()) result;
		result.reserve(m_data.size());

		for (auto& el : m_data)
			result.push_back(el.first);

		return result;
	}

	template <typename TData>
	void add(int a_id, TData&& a_data)
	{
		m_data.emplace(a_id, std::forward<TData>(a_data));
	}

private:

	std::map<int, ecommerce::ProductDetails> m_data;
	size_t m_lagMs{};
};

struct Config
{
	int DbStartId = 100'000;
	size_t DbIdSize = 100'000;
	// Slow DB emulation
	size_t DbLagMs = 150;

	size_t ThreadsNumber = 10;
	size_t FetchesNumber = 100; // per worker
	size_t FetchIdsDeviation = 50; // see FetchTask comment

	size_t MaxCacheSize = 20;
};

DatabaseStub MakeDatabaseStub(const Config& a_config)
{
	std::string name = "Awesome product";
	std::string decription = "Better yet get it I tell ya!";
	std::vector<uint8_t> image;
	std::vector<std::string> comments;
	comments.push_back("Used it once and I'm rich and happy!");
	comments.push_back("My wife came back!");
	comments.push_back("I'm literally back to life omg");
	comments.push_back("Guys I can backflip now");

	DatabaseStub db(a_config.DbLagMs);
	for (int i = a_config.DbStartId; i < a_config.DbStartId + a_config.DbIdSize; i++)
	{
		ecommerce::ProductDetails item{ i, name, decription, image, comments };
		db.add(i, std::move(item));
	}

	return db;
}

// Emulating "hot" products with the Gaussian distribution.
// The ones closer to the mean are more likely to be "chosen".
// The mean is always the middle of the DB ID's list.
void FetchTask(ecommerce::IDatabase& a_db, const Config& a_config)
{
	auto ids = a_db.getProductIds();

	std::random_device rd;
	std::mt19937 generator(rd());
	std::normal_distribution<> distribution(a_config.DbStartId + ids.size()/2, a_config.FetchIdsDeviation);

	for (int i = 0; i < a_config.FetchesNumber; i++)
	{
		try {
			auto data = a_db.fetchProductDetails(std::round(distribution(generator)));
			std::cout << data.ProductId << " ";
		}
		catch (std::runtime_error& ex)
		{
			std::cerr << ex.what();
		}
	}
	std::cout << std::endl;
}

int main(int argc, char *argv[])
{
	// TODO: read config
	// TODO: deserialize config
	
	// See Config declaration for the parameters description.
	Config config;
	config.DbStartId = 1000;
	config.DbIdSize = 1000;
	config.DbLagMs = 150;
	config.ThreadsNumber = 4;
	config.FetchesNumber = 20;
	config.FetchIdsDeviation = 5;
	config.MaxCacheSize = 5;

	auto stub = MakeDatabaseStub(config);
	std::cout << "stub's ready" << std::endl;
	ecommerce::CachingDatabase cachingDatabase(&stub, config.MaxCacheSize);

	auto ids = cachingDatabase.getProductIds();
	//for (auto id : ids)
		//std::cout << id << " ";
	std::cout << std::endl;

	cachingDatabase.fetchProductDetails(1333);
	cachingDatabase.fetchProductDetails(1334);
	cachingDatabase.fetchProductDetails(1335);
	cachingDatabase.fetchProductDetails(1336);
	cachingDatabase.fetchProductDetails(1338);
	cachingDatabase.fetchProductDetails(1333);
	cachingDatabase.fetchProductDetails(1333);
	cachingDatabase.fetchProductDetails(1337);

	std::cout << "FetchTask:" << std::endl;
	std::vector<std::thread> threads;
	for (size_t i = 0; i < config.ThreadsNumber; i++)
		threads.emplace_back(FetchTask, std::ref(cachingDatabase), config);
	for (auto& el : threads)
		el.join();
}

