#include "chat_data.h"
#include <chrono>
namespace spiritsaway::system::chat
{

	chat_data_proxy::chat_data_proxy(const std::string chat_key, chat_data_init_func init_func, chat_data_load_func load_func, chat_data_save_func save_func, chat_record_seq_t record_num_in_doc)
		: m_chat_key(chat_key), m_init_func(init_func), m_load_func(load_func), m_save_func(save_func)
		, m_record_num_in_doc(record_num_in_doc)
		, m_create_ts(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count())
	{
		json::object_t temp_query;
		temp_query["chat_key"] = m_chat_key;
		temp_query["doc_seq"] = std::numeric_limits<chat_record_seq_t>::max();
		json::object_t temp_doc;
		temp_doc["chat_key"] = m_chat_key;
		temp_doc["doc_seq"] = std::numeric_limits<chat_record_seq_t>::max();
		temp_doc["next_seq"] = 0;
		m_init_func(m_chat_key, temp_query, temp_doc);
	}

	bool chat_data_proxy::on_init(const json::object_t &meta_doc)
	{
		try
		{
			meta_doc.at("next_seq").get_to(m_next_seq);
		}
		catch (const std::exception &e)
		{
			return false;
		}
		if (m_next_seq % m_record_num_in_doc == 0)
		{
			m_is_ready = true;
			chat_doc temp_doc;
			temp_doc.doc_seq = 0;
			temp_doc.chat_key = m_chat_key;
			m_loaded_docs[0] = std::move(temp_doc);
			on_init();
			return true;
		}
		json::object_t temp_query;
		temp_query["chat_key"] = m_chat_key;
		temp_query["doc_seq"] = m_next_seq / m_record_num_in_doc;
		m_load_func(m_chat_key, temp_query);
		return true;
	}

	bool chat_data_proxy::on_doc_fetch(const json::object_t&cur_doc)
	{
		chat_doc temp_doc;
		try
		{
			json(cur_doc).get_to(temp_doc);
		}
		catch (const std::exception &e)
		{
			return false;
		}
		auto cur_temp_doc_seq = temp_doc.doc_seq;
		m_pending_load_docs.erase(cur_temp_doc_seq);
		m_loaded_docs[cur_temp_doc_seq] = std::move(temp_doc);
		if (cur_temp_doc_seq == m_next_seq / m_record_num_in_doc)
		{
			m_is_ready = true;
			on_init();
			return true;
		}

		check_fetch_complete(cur_temp_doc_seq);
		if (!m_pending_load_docs.empty())
		{
			json::object_t temp_query;
			temp_query["chat_key"] = m_chat_key;
			temp_query["doc_seq"] = *m_pending_load_docs.rbegin();
			m_load_func(m_chat_key, temp_query);
		}
		return true;
	}

	void chat_data_proxy::check_fetch_complete(chat_record_seq_t cur_doc_seq)
	{
		chat_record_seq_t cur_doc_record_seq_begin = cur_doc_seq * m_record_num_in_doc;
		chat_record_seq_t cur_doc_record_seq_end = cur_doc_record_seq_begin + m_record_num_in_doc;
		chat_record_seq_t has_cb_invoked = 0;
		std::vector<chat_record> cur_fetch_result;
		for (int i = 0; i < m_fetch_tasks.size(); i++)
		{
			auto &cur_cb = m_fetch_tasks[i];
			if (cur_cb.chat_seq_begin >= cur_doc_record_seq_end || cur_cb.chat_seq_end < cur_doc_record_seq_begin)
			{
				continue;
			}
			cur_fetch_result.clear();
			if (!fetch_record_impl(cur_cb.chat_seq_begin, cur_cb.chat_seq_end, cur_fetch_result))
			{
				continue;
			}
			cur_cb.fetch_cb(cur_fetch_result);
			cur_cb.chat_seq_begin = std::numeric_limits<chat_record_seq_t>::max();
			has_cb_invoked++;
		}
		std::vector<chat_fetch_task> remain_fetch_cbs;
		remain_fetch_cbs.reserve(m_fetch_tasks.size() - has_cb_invoked);
		for (auto &one_cb : m_fetch_tasks)
		{
			if (one_cb.chat_seq_begin == std::numeric_limits<chat_record_seq_t>::max())
			{
				continue;
			}
			remain_fetch_cbs.push_back(std::move(one_cb));
		}
		std::swap(m_fetch_tasks, remain_fetch_cbs);
	}

	void chat_data_proxy::add_chat_impl(const std::string &from_player_id, const json::object_t &chat_info, std::uint64_t chat_ts)
	{
		chat_record cur_chat_record;
		cur_chat_record.detail = chat_info;
		cur_chat_record.from = from_player_id;
		cur_chat_record.seq = m_next_seq;
		cur_chat_record.ts = chat_ts;
		m_loaded_docs.rbegin()->second.records.push_back(std::move(cur_chat_record));
		m_next_seq++;
		m_dirty_count++;
		if (m_loaded_docs.rbegin()->second.records.size() == m_record_num_in_doc)
		{
			save();
			chat_doc new_chat_doc;
			new_chat_doc.chat_key = m_chat_key;
			new_chat_doc.doc_seq = m_loaded_docs.rbegin()->second.doc_seq + 1;
			m_loaded_docs[new_chat_doc.doc_seq] = std::move(new_chat_doc);
		}
	}

	bool chat_data_proxy::save()
	{
		if (!m_dirty_count)
		{
			return false;
		}
		json::object_t temp_query;
		temp_query["chat_key"] = m_chat_key;
		temp_query["doc_seq"] = m_loaded_docs.rbegin()->second.doc_seq;
		json cur_doc_json = m_loaded_docs.rbegin()->second;
		m_save_func(m_chat_key, temp_query, cur_doc_json);
		temp_query["doc_seq"] = std::numeric_limits<chat_record_seq_t>::max();
		json cur_meta_doc = temp_query;
		cur_meta_doc["next_seq"] = m_next_seq;
		m_save_func(m_chat_key, temp_query, cur_meta_doc);
		m_dirty_count = 0;
		return true;
	}

	chat_record_seq_t chat_data_proxy::add_chat(const std::string &from_player_id, const json::object_t &chat_info, std::uint64_t chat_ts)
	{
		if (!ready())
		{
			return std::numeric_limits<chat_record_seq_t>::max();
		}
		if (m_next_seq == std::numeric_limits<chat_record_seq_t>::max())
		{
			return std::numeric_limits<chat_record_seq_t>::max();
		}
		auto cur_record_seq = m_next_seq;
		add_chat_impl(from_player_id, chat_info, chat_ts);
		return cur_record_seq;
	}

	void chat_data_proxy::add_chat(const std::string &from_player_id, const json::object_t &chat_info, std::uint64_t chat_ts, std::function<void(chat_record_seq_t)> add_cb)
	{
		if (ready())
		{
			if (m_next_seq != std::numeric_limits<chat_record_seq_t>::max())
			{
				auto cur_record_seq = m_next_seq;
				add_chat_impl(from_player_id, chat_info, chat_ts);
				add_cb(cur_record_seq);
				return;
			}
			else
			{
				add_cb(std::numeric_limits<chat_record_seq_t>::max());
			}
		}
		else
		{
			chat_add_task cur_add_task;
			cur_add_task.add_cb = add_cb;
			cur_add_task.detail = chat_info;
			cur_add_task.from = from_player_id;
			cur_add_task.chat_ts = chat_ts;
			m_add_tasks.push_back(std::move(cur_add_task));
		}
	}

	bool chat_data_proxy::fetch_record_impl(chat_record_seq_t chat_seq_begin, chat_record_seq_t chat_seq_end, std::vector<chat_record> &cur_fetch_result) const
	{
		auto cur_fetch_doc_begin = chat_seq_begin / m_record_num_in_doc;
		auto cur_fetch_doc_end = chat_seq_end / m_record_num_in_doc + 1;
		auto doc_begin_iter = m_loaded_docs.find(cur_fetch_doc_begin);
		if (doc_begin_iter == m_loaded_docs.end())
		{
			return false;
		}
		auto next_doc_iter = doc_begin_iter;
		auto next_doc_seq = cur_fetch_doc_begin;
		while (next_doc_iter != m_loaded_docs.end() && next_doc_seq < cur_fetch_doc_end && next_doc_iter->second.doc_seq == next_doc_seq)
		{
			next_doc_seq++;
			next_doc_iter++;
		}
		if (next_doc_seq != cur_fetch_doc_end)
		{
			return false;
		}
		cur_fetch_result.reserve(chat_seq_end - chat_seq_begin + 1);
		while (doc_begin_iter != m_loaded_docs.end())
		{
			for (const auto &one_record : doc_begin_iter->second.records)
			{
				if (one_record.seq >= chat_seq_begin && one_record.seq <= chat_seq_end)
				{
					cur_fetch_result.push_back(one_record);
				}
			}
			if (doc_begin_iter->second.doc_seq + 1 == cur_fetch_doc_end)
			{
				break;
			}
			doc_begin_iter++;
		}
		return true;
	}
	bool chat_data_proxy::fetch_records(chat_record_seq_t seq_begin, chat_record_seq_t seq_end, std::vector<chat_record> &result) const
	{
		if (seq_end < seq_begin)
		{
			return false;
		}
		if (seq_end >= m_next_seq)
		{
			return false;
		}

		if (!ready())
		{
			return false;
		}

		return fetch_record_impl(seq_begin, seq_end, result);
	}

	void chat_data_proxy::fetch_records(chat_record_seq_t seq_begin, chat_record_seq_t seq_end, std::function<void(const std::vector<chat_record> &)> fetch_cb)
	{
		std::vector<chat_record> temp_result;
		if (seq_end < seq_begin)
		{
			return fetch_cb(temp_result);
		}
		if (seq_end >= m_next_seq)
		{
			return fetch_cb(temp_result);
		}
		if (fetch_record_impl(seq_begin, seq_end, temp_result))
		{
			fetch_cb(temp_result);
			return;
		}
		auto cur_fetch_doc_begin = seq_begin / m_record_num_in_doc;
		auto cur_fetch_doc_end = seq_end / m_record_num_in_doc + 1;
		auto pre_pending_sz = m_pending_load_docs.size();
		for (auto i = cur_fetch_doc_begin; i < cur_fetch_doc_end; i++)
		{
			auto cur_iter = m_loaded_docs.find(i);
			if (cur_iter == m_loaded_docs.end())
			{
				m_pending_load_docs.insert(i);
			}
		}
		chat_fetch_task cur_fetch_task;
		cur_fetch_task.chat_seq_begin = seq_begin;
		cur_fetch_task.chat_seq_end = seq_end;
		cur_fetch_task.fetch_cb = fetch_cb;
		m_fetch_tasks.push_back(std::move(cur_fetch_task));
		if (!m_is_ready)
		{
			return;
		}
		if (pre_pending_sz)
		{
			return;
		}
		json::object_t temp_query;
		temp_query["chat_key"] = m_chat_key;
		temp_query["doc_seq"] = *m_pending_load_docs.rbegin();
		m_load_func(m_chat_key, temp_query);
		return;
	}

	void chat_data_proxy::on_init()
	{
		for (auto& one_cb : m_on_init_cbs)
		{
			one_cb(*this);
		}
		m_on_init_cbs.clear();
		for (auto &one_add_task : m_add_tasks)
		{
			auto cur_add_seq = m_next_seq;
			add_chat_impl(one_add_task.from, one_add_task.detail, one_add_task.chat_ts);
			one_add_task.add_cb(cur_add_seq);
		}
		m_add_tasks.clear();
		if (m_pending_load_docs.empty())
		{
			return;
		}
		json::object_t temp_query;
		temp_query["chat_key"] = m_chat_key;
		temp_query["doc_seq"] = *m_pending_load_docs.rbegin();
		m_load_func(m_chat_key, temp_query);
	}
	bool chat_data_proxy::add_init_cb(std::function<void(chat_data_proxy&)> cur_init_cb)
	{
		if (m_is_ready)
		{
			return false;
		}
		m_on_init_cbs.push_back(cur_init_cb);
		return true;
	}

	bool chat_data_proxy::safe_to_remove() const
	{
		if (!m_is_ready)
		{
			return false;
		}
		if (!m_add_tasks.empty())
		{
			return false;
		}
		if (!m_pending_load_docs.empty())
		{
			return false;
		}
		return true;
	}

}
