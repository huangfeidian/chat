#include "chat_data.h"
#include <chrono>
#include <unordered_set>

namespace spiritsaway::system::chat
{

	chat_data_proxy::chat_data_proxy(const std::string chat_key, chat_data_load_meta_func load_meta_func, chat_data_load_normal_func load_normal_func, chat_data_save_func save_func, chat_record_seq_t record_num_in_doc, chat_record_seq_t fetch_record_max_num)
		: m_chat_key(chat_key)
		, m_chat_key_hash(std::hash<std::string>{}(chat_key))
		, m_load_meta_func(load_meta_func), m_load_normal_func(load_normal_func), m_save_func(save_func)
		, m_record_num_in_doc(record_num_in_doc)
		, m_create_ts(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count())
		, m_fetch_record_max_num(fetch_record_max_num)
	{
		json::object_t temp_query;
		temp_query["chat_key"] = m_chat_key;
		temp_query["doc_seq"] = std::numeric_limits<chat_record_seq_t>::max();
		json::object_t temp_doc;
		temp_doc["chat_key"] = m_chat_key;
		temp_doc["doc_seq"] = std::numeric_limits<chat_record_seq_t>::max();
		temp_doc["next_seq"] = 0;
		m_load_meta_func(m_chat_key, temp_query, temp_doc);
	}

	bool chat_data_proxy::on_meta_doc_loaded(const json::object_t &meta_doc)
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
			on_ready();
			return true;
		}
		json::object_t temp_query;
		temp_query["chat_key"] = m_chat_key;
		temp_query["doc_seq"] = m_next_seq / m_record_num_in_doc;
		m_load_normal_func(m_chat_key, temp_query);
		return true;
	}

	bool chat_data_proxy::on_normal_doc_loaded(const json::object_t&cur_doc)
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
		temp_doc.ttl = m_default_loaded_doc_ttl;
		m_loaded_docs[cur_temp_doc_seq] = std::move(temp_doc);
		if (cur_temp_doc_seq == m_next_seq / m_record_num_in_doc)
		{
			m_is_ready = true;
			on_ready();
			return true;
		}

		check_fetch_complete(cur_temp_doc_seq);
		if (!m_pending_load_docs.empty())
		{
			json::object_t temp_query;
			temp_query["chat_key"] = m_chat_key;
			temp_query["doc_seq"] = *m_pending_load_docs.rbegin();
			m_load_normal_func(m_chat_key, temp_query);
		}
		return true;
	}

	void chat_data_proxy::check_fetch_complete(chat_record_seq_t cur_doc_seq)
	{
		chat_record_seq_t cur_doc_record_seq_begin = cur_doc_seq * m_record_num_in_doc;
		chat_record_seq_t cur_doc_record_seq_end = cur_doc_record_seq_begin + m_record_num_in_doc;
		chat_record_seq_t has_cb_invoked = 0;
		std::vector<chat_record> cur_fetch_result;
		std::uint64_t remain_refered_task_count = 0;
		for (std::uint32_t i = 0; i < m_fetch_tasks.size(); i++)
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
			cur_cb.chat_seq_begin = std::numeric_limits<chat_record_seq_t>::max(); // 标记已经执行的任务
			has_cb_invoked++;
		}
		// 删除已经执行了的任务
		for (std::uint32_t i = 0; i < m_fetch_tasks.size(); i++)
		{
			if (m_fetch_tasks[i].chat_seq_begin != std::numeric_limits<chat_record_seq_t>::max())
			{
				continue;
			}
			while (!m_fetch_tasks.empty() && m_fetch_tasks.back().chat_seq_begin == std::numeric_limits<chat_record_seq_t>::max())
			{
				m_fetch_tasks.pop_back();
			}
			if (i >= m_fetch_tasks.size())
			{
				break;
			}
			if (i + 1 != m_fetch_tasks.size())
			{
				std::swap(m_fetch_tasks[i], m_fetch_tasks.back());
			}
			m_fetch_tasks.pop_back();
		}
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

	void chat_data_proxy::fetch_records(chat_record_seq_t seq_begin, chat_record_seq_t seq_end, std::function<void(const std::vector<chat_record> &)> fetch_cb)
	{
		std::vector<chat_record> temp_result;
		if (seq_end < seq_begin)
		{
			return fetch_cb(temp_result);
		}
		if (ready()) // 如果已经ready了
		{
			if (seq_end >= m_next_seq) // 如果最大序列号大于最新序列号 说明请求非法
			{
				return fetch_cb(temp_result);
			}
			if (fetch_record_impl(seq_begin, seq_end, temp_result)) // 如果现有数据满足要求 立即执行
			{
				fetch_cb(temp_result);
				return;
			}
		}
		// 添加聊天记录获取任务
		chat_fetch_task cur_fetch_task;
		cur_fetch_task.chat_seq_begin = seq_begin;
		cur_fetch_task.chat_seq_end = seq_end;
		cur_fetch_task.fetch_cb = fetch_cb;
		m_fetch_tasks.push_back(std::move(cur_fetch_task));
		if (!m_is_ready) // 没有初始化的情况下 先暂存请求
		{
			return;
		}

		// 计算好要加载哪些doc_seq 
		auto cur_fetch_doc_begin = seq_begin / m_record_num_in_doc;
		auto cur_fetch_doc_end = seq_end / m_record_num_in_doc + 1;
		for (auto i = cur_fetch_doc_begin; i < cur_fetch_doc_end; i++)
		{
			auto cur_iter = m_loaded_docs.find(i);
			if (cur_iter == m_loaded_docs.end())
			{
				m_pending_load_docs.insert(i);
			}
		}
		
		if (m_fetch_tasks.size() != 1)
		{
			return;// 已经在执行加载任务了 等待之前的加载任务执行完
		}
		json::object_t temp_query;
		temp_query["chat_key"] = m_chat_key;
		temp_query["doc_seq"] = *m_pending_load_docs.rbegin();
		m_load_normal_func(m_chat_key, temp_query);
		return;
	}

	void chat_data_proxy::on_ready()
	{
		for (auto& one_cb : m_on_ready_cbs)
		{
			one_cb(*this);
		}
		m_on_ready_cbs.clear();
		for (auto &one_add_task : m_add_tasks)
		{
			auto cur_add_seq = m_next_seq;
			add_chat_impl(one_add_task.from, one_add_task.detail, one_add_task.chat_ts);
			one_add_task.add_cb(cur_add_seq);
		}
		m_add_tasks.clear();
		if (m_fetch_tasks.empty())
		{
			return;
		}
		// 因为没有初始化之前 只是暂存了聊天记录获取请求 初始化之后要将这些请求重新执行一遍
		std::vector<chat_fetch_task> pre_fetch_tasks;
		std::swap(m_fetch_tasks, pre_fetch_tasks);
		for (const auto& one_fetch_task : pre_fetch_tasks)
		{
			fetch_records(one_fetch_task.chat_seq_begin, one_fetch_task.chat_seq_end, one_fetch_task.fetch_cb);
		}
	}
	bool chat_data_proxy::add_ready_cb(std::function<void(chat_data_proxy&)> cur_ready_cb)
	{
		if (m_is_ready)
		{
			return false;
		}
		m_on_ready_cbs.push_back(cur_ready_cb);
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

	std::uint64_t chat_data_proxy::expire_loaded()
	{
		std::unordered_set<chat_record_seq_t> doc_seq_needed;
		for (const auto& one_fetch_task : m_fetch_tasks)
		{
			auto cur_fetch_doc_begin = one_fetch_task.chat_seq_begin / m_record_num_in_doc;
			auto cur_fetch_doc_end = one_fetch_task.chat_seq_end / m_record_num_in_doc + 1;
			for (auto i = cur_fetch_doc_begin; i < cur_fetch_doc_end; i++)
			{
				doc_seq_needed.insert(i);
			}
		}
		auto current_doc_seq = m_next_seq / m_record_num_in_doc;
		
		const std::uint32_t always_in_loaded_doc_num = 3; // 最新的若干页面永驻
		for (std::uint32_t i = 0; i < always_in_loaded_doc_num; i++)
		{
			doc_seq_needed.insert(current_doc_seq);
			if (current_doc_seq == 0)
			{
				break;
			}
			current_doc_seq--;
		}
		std::vector<std::uint64_t> doc_seqs_to_delete;
		for (auto& one_pair : m_loaded_docs)
		{
			if (doc_seq_needed.find(one_pair.first) != doc_seq_needed.end())
			{
				one_pair.second.ttl = m_default_loaded_doc_ttl;
				continue;
			}
			one_pair.second.ttl--;
			if (one_pair.second.ttl == 0)
			{
				doc_seqs_to_delete.push_back(one_pair.first);
			}
		}
		for (auto one_doc_seq : doc_seqs_to_delete)
		{
			m_loaded_docs.erase(one_doc_seq);
		}
		return doc_seqs_to_delete.size();
	}

}
