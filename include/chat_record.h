#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace spiritsaway::system::chat
{
	struct chat_record
	{
		std::string from;
		json::object_t detail;
		std::uint32_t seq;
		NLOHMANN_DEFINE_TYPE_INTRUSIVE(chat_record, from, detail, seq)
	};
}