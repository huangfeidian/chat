#include "chat_manager.h"

namespace spiritsaway::system::chat
{
	chat_manager::chat_manager(chat_data_load_meta_func init_func, chat_data_load_normal_func load_func, chat_data_save_func save_func, chat_record_seq_t record_num_in_doc)
		: m_record_num_in_doc(record_num_in_doc)
		, m_load_meta_func(init_func)
		, m_save_func(save_func)
		, m_load_func(load_func)
	{

	}

	std::shared_ptr<chat_data_proxy> chat_manager::get_or_create_chat_data(const std::string& chat_key)
	{
		std::shared_ptr<chat_data_proxy> cur_chat_proxy = nullptr;
		auto cur_proxy_iter = m_chat_datas.find(chat_key);
		if (cur_proxy_iter == m_chat_datas.end())
		{
			cur_chat_proxy = std::make_shared<chat_data_proxy>(chat_key, m_load_meta_func, m_load_func, m_save_func, m_record_num_in_doc);
			m_chat_datas[chat_key] = cur_chat_proxy;
		}
		else
		{
			cur_chat_proxy = cur_proxy_iter->second;
		}
		return cur_chat_proxy;
	}
	void chat_manager::add_msg(const std::string& chat_key, const std::string& from, const json::object_t& msg, std::uint64_t chat_ts, std::function<void(chat_record_seq_t)> seq_cb)
	{
		auto cur_chat_proxy = get_or_create_chat_data(chat_key);
		if (!cur_chat_proxy->ready())
		{
			
			cur_chat_proxy->add_chat(from, msg, chat_ts, [=](chat_record_seq_t cur_seq)
				{
					add_msg_cb(cur_chat_proxy, cur_seq, seq_cb);
				});
		}
		else
		{
			auto cur_seq = cur_chat_proxy->add_chat(from, msg, chat_ts);
			add_msg_cb(cur_chat_proxy, cur_seq, seq_cb);
		}
	}

	void chat_manager::fetch_history_num(const std::string& chat_key, std::function<void(chat_record_seq_t)>  fetch_cb)
	{
		auto cur_chat_proxy = get_or_create_chat_data(chat_key);
		if (!cur_chat_proxy->ready())
		{
			cur_chat_proxy->add_ready_cb([=](chat_data_proxy& cur_chat_data)
				{
					fetch_history_num_cb(cur_chat_data, fetch_cb);
				});
		}
		else
		{
			fetch_history_num_cb(*cur_chat_proxy, fetch_cb);
		}
	}

	void chat_manager::fetch_history(const std::string& chat_key, chat_record_seq_t seq_begin, chat_record_seq_t seq_end, std::function<void(const std::vector<chat_record>&)> fetch_cb)
	{
		auto cur_chat_proxy = get_or_create_chat_data(chat_key);
		cur_chat_proxy->fetch_records(seq_begin, seq_end, fetch_cb);
	}

	void chat_manager::fetch_history_num_cb(chat_data_proxy& proxy_data, std::function<void(chat_record_seq_t)> num_cb)
	{
		num_cb(proxy_data.next_seq());
	}

	void chat_manager::add_msg_cb(std::shared_ptr<chat_data_proxy> cur_data, chat_record_seq_t msg_seq, std::function<void(chat_record_seq_t)> seq_cb)
	{
		if (cur_data->dirty_count() == 1)
		{
			m_dirty_chat_datas.push_back(cur_data);
		}
		seq_cb(msg_seq);
	}

	std::vector<std::string> chat_manager::tick_save(chat_record_seq_t max_num)
	{
		std::vector<std::string> result;
		std::reverse(m_dirty_chat_datas.begin(), m_dirty_chat_datas.end());
		chat_record_seq_t result_num = 0;
		for (int i = 0; i < max_num; i++)
		{
			if (m_dirty_chat_datas.empty())
			{
				break;
			}
			auto cur_back = m_dirty_chat_datas.back();
			m_dirty_chat_datas.pop_back();
			if (cur_back->save())
			{
				result.push_back(cur_back->m_chat_key);
				result_num++;
			}
			if (result_num >= max_num)
			{
				break;
			}
		}
		std::reverse(m_dirty_chat_datas.begin(), m_dirty_chat_datas.end());
		return result;
	}

	std::vector<std::string> chat_manager::tick_expire(chat_record_seq_t max_num)
	{
		std::vector<const chat_data_proxy*> result_expire_datas;
		for (const auto& one_pair : m_chat_datas)
		{
			one_pair.second->expire_loaded();
			if (one_pair.second->dirty_count() == 0 && one_pair.second->safe_to_remove())
			{
				result_expire_datas.push_back(one_pair.second.get());
			}
		}
		std::sort(result_expire_datas.begin(), result_expire_datas.end(), [](const chat_data_proxy* a, const chat_data_proxy* b)
			{
				return a->m_create_ts > b->m_create_ts;
			});
		std::vector<std::string> result;
		chat_record_seq_t result_num = 0;
		for (int i = 0; i < max_num; i++)
		{
			if (result_expire_datas.empty())
			{
				break;
			}
			auto cur_back = result_expire_datas.back();
			result_expire_datas.pop_back();
			result.push_back(cur_back->m_chat_key);
			m_chat_datas.erase(cur_back->m_chat_key);
			result_num++;
		}
		return result;
	}

	void chat_manager::on_meta_doc_loaded(const std::string& chat_key, const json::object_t& doc)
	{
		auto cur_iter = m_chat_datas.find(chat_key);
		if (cur_iter == m_chat_datas.end())
		{
			return;
		}
		cur_iter->second->on_meta_doc_loaded(doc);
	}
	void chat_manager::on_normal_doc_loaded(const std::string& chat_key, const json::object_t& doc)
	{
		auto cur_iter = m_chat_datas.find(chat_key);
		if (cur_iter == m_chat_datas.end())
		{
			return;
		}
		cur_iter->second->on_normal_doc_loaded(doc);
	}


}