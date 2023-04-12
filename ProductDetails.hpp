#pragma once

#include <string>
#include <vector>

namespace ecommerce {

struct ProductDetails
{
	int ProductId;
	std::string ProductName;
	std::string Description;
	std::vector<uint8_t> Image;
	std::vector<std::string> Comments;
};

}
