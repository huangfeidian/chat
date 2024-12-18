#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace spiritsaway::system::chat
{
	using chat_record_seq_t = std::uint64_t;
	struct chat_record
	{
		std::string from;
		json::object_t detail;
		chat_record_seq_t seq;
		std::uint64_t ts;
		NLOHMANN_DEFINE_TYPE_INTRUSIVE(chat_record, from, detail, seq, ts)
	};
}