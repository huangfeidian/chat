#include "chat_data.h"
#include <unordered_map>

namespace spiritsaway::system::chat
{
	class chat_manager
	{
		std::unordered_map<std::string, std::shared_ptr<chat_data_proxy>> m_chat_datas;
		std::vector<std::shared_ptr< chat_data_proxy>> m_dirty_chat_datas;
		chat_data_init_func m_init_func;
		chat_data_load_func m_load_func;
		chat_data_save_func m_save_func;
		const chat_record_seq_t m_record_num_in_doc;
	private:
		void fetch_history_num_cb(chat_data_proxy& proxy_data, std::function<void(chat_record_seq_t)> num_cb);
		void add_msg_cb(std::shared_ptr<chat_data_proxy> cur_data, chat_record_seq_t msg_seq, std::function<void(chat_record_seq_t)> seq_cb);
		std::shared_ptr<chat_data_proxy> get_or_create_chat_data(const std::string& chat_key);
	public:
		chat_manager(chat_data_init_func init_func, chat_data_load_func load_func, chat_data_save_func save_func, chat_record_seq_t record_num_in_doc = 50);
		void add_msg(const std::string& chat_key, const std::string& from, const json::object_t& msg, std::uint64_t chat_ts, std::function<void(chat_record_seq_t)> seq_cb);

		void fetch_history(const std::string& chat_key, chat_record_seq_t seq_begin, chat_record_seq_t seq_end, std::function<void(const std::vector<chat_record>&)> fetch_cb);
		void fetch_history_num(const std::string& chat_key, std::function<void(chat_record_seq_t)>  fetch_cb);

		std::uint64_t dirty_chat_num() const
		{
			return m_dirty_chat_datas.size();
		}

		std::uint64_t chat_data_num() const
		{
			return m_chat_datas.size();
		}
		std::vector<std::string> tick_save(chat_record_seq_t max_num);

		std::vector<std::string> tick_expire(chat_record_seq_t max_num);
		void on_init(const std::string& chat_key, const json::object_t& doc);
		void on_load(const std::string& chat_key, const json::object_t& doc);


	};
}