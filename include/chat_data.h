
#include <nlohmann/json.hpp>
#include <set>
#include "chat_record.h"

using json = nlohmann::json;
namespace spiritsaway::system::chat
{
	struct chat_doc
	{
		std::string chat_key;
		chat_record_seq_t doc_seq;
		std::vector<chat_record> records;
		std::uint32_t ttl; // 在缓存中的剩余有效时间
		NLOHMANN_DEFINE_TYPE_INTRUSIVE(chat_doc, chat_key, doc_seq, records)
	};
	struct chat_data
	{
		std::string chat_key;
		chat_record_seq_t next_seq;
		chat_record_seq_t doc_seq;
		NLOHMANN_DEFINE_TYPE_INTRUSIVE(chat_data, chat_key, next_seq, doc_seq)
	};
	
	using chat_data_load_meta_func = std::function<void(const std::string&, const json::object_t&, const json::object_t&)>;
	using chat_data_load_normal_func = std::function<void(const std::string&, const json::object_t&)>;
	using chat_data_save_func = std::function<void(const std::string&, const json::object_t&, const json&)>;
	
	class chat_data_proxy
	{
	public:
		const std::string m_chat_key;
		const std::uint64_t m_chat_key_hash;
		const chat_record_seq_t m_record_num_in_doc;
		const std::uint64_t m_create_ts;
		const chat_record_seq_t m_fetch_record_max_num; // 记录单次历史记录拉取的最大record数量
		const std::uint32_t m_default_loaded_doc_ttl = 60; // 默认的loaded_doc保留时间
		chat_data_proxy(const std::string chat_key, chat_data_load_meta_func load_meta_func, chat_data_load_normal_func load_normal_func, chat_data_save_func save_func, chat_record_seq_t record_num_in_doc = 50, chat_record_seq_t fetch_record_max_num = 200);
	private:
		struct chat_fetch_task
		{
			// 这里两个是闭区间
			chat_record_seq_t chat_seq_begin;
			chat_record_seq_t chat_seq_end;

			std::function<void(const std::vector<chat_record>&)> fetch_cb;
		};

		struct chat_add_task
		{
			std::string from;
			json::object_t detail;
			std::uint64_t chat_ts;
			std::function<void(chat_record_seq_t)> add_cb;
		};
		chat_data_load_meta_func m_load_meta_func;
		chat_data_load_normal_func m_load_normal_func;
		chat_data_save_func m_save_func;
		bool m_is_ready = false;
		std::map<chat_record_seq_t, chat_doc> m_loaded_docs;
		chat_record_seq_t m_dirty_count = 0;
		chat_record_seq_t m_next_seq = 0;
		std::vector<chat_fetch_task> m_fetch_tasks;
		std::vector<chat_add_task> m_add_tasks;
		std::set<chat_record_seq_t> m_pending_load_docs;
		std::vector<std::function<void(chat_data_proxy&)>> m_on_ready_cbs;

	private:
		void on_ready();
		void check_fetch_complete(chat_record_seq_t cur_doc_seq); // 返回这个doc在执行之后还被几个任务需要
		void add_chat_impl(const std::string& from_player_id, const json::object_t& chat_info, std::uint64_t chat_ts);
		bool fetch_record_impl(chat_record_seq_t seq_min, chat_record_seq_t seq_end, std::vector<chat_record>& result) const;
	public:
		chat_record_seq_t dirty_count() const
		{
			return m_dirty_count;
		}
		bool ready() const
		{
			return m_is_ready;
		}
		bool on_meta_doc_loaded(const json::object_t& meta_doc);
		bool add_ready_cb(std::function<void(chat_data_proxy&)> cur_ready_cb);
		bool on_normal_doc_loaded(const json::object_t& cur_doc);
		void fetch_records(chat_record_seq_t seq_begin, chat_record_seq_t seq_end, std::function<void(const std::vector<chat_record>&)> fetch_cb);
		chat_record_seq_t add_chat(const std::string& from_player_id, const json::object_t& chat_info, std::uint64_t chat_ts);
		void add_chat(const std::string& from_player_id, const json::object_t& chat_info, std::uint64_t chat_ts, std::function<void(chat_record_seq_t)> add_cb);
		bool save();

		bool has_add_task() const
		{
			return !m_add_tasks.empty();
		}
		bool safe_to_remove() const;
		chat_record_seq_t next_seq() const
		{
			return m_next_seq;
		}
		std::uint64_t expire_loaded(); // 从loaded中删除一些不再需要的doc
	};
}