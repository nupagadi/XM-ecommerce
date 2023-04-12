#pragma once

#include "ProductDetails.hpp"

namespace ecommerce {

// I use int for ID here and in other similar places, since in the task int is used for ProductId.
struct IDatabase
{
	virtual ~IDatabase() = default;

	virtual ProductDetails fetchProductDetails(int a_id) = 0;

	virtual std::vector<int> getProductIds() const = 0;
};

}

