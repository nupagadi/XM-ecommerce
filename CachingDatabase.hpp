#pragma once

#include "IDatabase.h"
#include "CachedData.hpp"

namespace ecommerce {

struct CachingDatabase : IDatabase
{
	CachingDatabase(IDatabase* a_productDetails, size_t a_maxCacheSize)
		: m_productDetails(a_productDetails)
		, m_cachedProductDetails(a_maxCacheSize)
	{
	}

	ProductDetails fetchProductDetails(int a_id) override
	{
		return m_cachedProductDetails.getData(a_id, m_productDetails);
	}

	std::vector<int> getProductIds() const override
	{
		return m_productDetails->getProductIds();
	}

private:

	// Database objects are possible to operate and be used outside of this class.
	// So the pointer is not owning.
	IDatabase* m_productDetails;
	CachedData<ProductDetails> m_cachedProductDetails;
	// Add other caches templated with the type you need. E.g.:
	CachedData<std::string, std::string> JustCheckItCompiles = {1};
};

}

