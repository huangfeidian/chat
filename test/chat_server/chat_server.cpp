#include "chat_server.h"

namespace spiritsaway::system::chat
{
	std::uint32_t chat_db::find_one_idx(const json::object_t& filter) const
	{
		for (std::uint32_t i = 0; i < m_content.size(); i++)
		{
			const auto& one_data = m_content[i];
			bool all_match = true;
			for (const auto& one_pair : filter)
			{
				auto cur_iter = one_data.find(one_pair.first);
				if (cur_iter == one_data.end())
				{
					all_match = false;
					break;
				}
				if (cur_iter->second != one_pair.second)
				{
					all_match = false;
					break;
				}
			}
			if (all_match)
			{
				return i;
			}
		}
		return std::numeric_limits<std::uint32_t>::max();
	}

	std::vector<std::uint32_t> chat_db::find_all_idx(const json::object_t& filter) const
	{
		std::vector<std::uint32_t> result;
		result.reserve(8);
		for (std::uint32_t i = 0; i < m_content.size(); i++)
		{
			const auto& one_data = m_content[i];
			bool all_match = true;
			for (const auto& one_pair : filter)
			{
				auto cur_iter = one_data.find(one_pair.first);
				if (cur_iter == one_data.end())
				{
					all_match = false;
					break;
				}
			}
			if (all_match)
			{
				result.push_back(i);
			}
		}
		return result;
	}

	std::vector<json::object_t> chat_db::find_all(const json::object_t& filter) const
	{
		auto all_indexes = find_all_idx(filter);
		std::vector<json::object_t> result;
		result.reserve(all_indexes.size());
		for (const auto& one_idx : all_indexes)
		{
			result.push_back(m_content[one_idx]);
		}
		return result;
	}
	json::object_t chat_db::find_one(const json::object_t& filter) const
	{
		auto cur_idx = find_one_idx(filter);
		if (cur_idx == std::numeric_limits<std::uint32_t>::max())
		{
			return {};
		}
		return m_content[cur_idx];
	}

	void chat_db::update_or_create(const json::object_t& filter, const json::object_t& doc)
	{
		auto one_idx = find_one_idx(filter);
		if (one_idx == std::numeric_limits<std::uint32_t>::max())
		{
			m_content.push_back(doc);
		}
		else
		{
			for (const auto& one_pair : doc)
			{
				m_content[one_idx][one_pair.first] = one_pair.second;
			}
		}
	}

	json::object_t chat_db::find_or_create(const json::object_t& filter, const json::object_t& doc)
	{
		auto one_idx = find_one_idx(filter);
		if (one_idx != std::numeric_limits<std::uint32_t>::max())
		{
			return m_content[one_idx];
			
		}
		else
		{
			m_content.push_back(doc);
			return doc;
		}
	}
	chat_session::chat_session(const http_utils::request& req, http_utils::reply_handler rep_cb,
		logger_t in_logger, chat_manager& in_chat_manager)
		: m_chat_manager(in_chat_manager)
		, m_logger(in_logger)
		, m_req(req)
		, m_rep_cb(rep_cb)

	{

	}

	std::string chat_session::check_request()
	{

		if (m_req.uri.rfind("/chat", 0) != 0)
		{
			return "request target must start with /chat";
		}
		if (!json::accept(m_req.body))
		{
			m_logger->info("request is {}", m_req.body);
			return "body must be json";
		}
		auto json_body = json::parse(m_req.body);
		if (!json_body.is_object())
		{
			return "body must be json object";

		}

		try
		{
			json_body.at("cmd").get_to(cur_cmd);
			json_body.at("chat_key").get_to(chat_key);
			json_body.at("from").get_to(from);
		}
		catch (std::exception& e)
		{
			return std::string("invalid request with exception ") + e.what();
		}
		if (cur_cmd == "add")
		{
			try
			{
				json_body.at("msg").get_to(msg);
			}
			catch (std::exception& e)
			{
				return "cant find msg in request";
			}
		}
		else if (cur_cmd == "fetch")
		{
			try
			{
				json_body.at("seq_begin").get_to(seq_begin);
				json_body.at("seq_end").get_to(seq_end);
			}
			catch (std::exception& e)
			{
				return "cant find seq begin seq end";
			}
		}
		else if (cur_cmd != "meta")
		{
			return "invalid method";
		}
		return "";
	}

	void chat_session::route_request()
	{
		auto self = shared_from_this();
		if (cur_cmd == "add")
		{
			m_chat_manager.add_msg(chat_key, from, msg, [self, this](std::uint32_t cur_seq)
				{
					reply["seq"] = cur_seq;
					finish_task_1();
				});
		}
		else if (cur_cmd == "fetch")
		{
			m_chat_manager.fetch_history(chat_key, seq_begin, seq_end, [self, this](const std::vector<chat_record>& history)
				{
					reply["history"] = history;
					finish_task_1();
				});
		}
		else if (cur_cmd == "meta")
		{
			m_chat_manager.fetch_history_num(chat_key, [self, this](std::uint32_t num)
				{
					reply["num"] = num;
					finish_task_1();
				});
		}
		if (!reply.empty())
		{
			finish_task_2();
			return;
		}


	}

	void chat_session::finish_task_1()
	{

		finish_task_2();
	}

	void chat_session::finish_task_2()
	{
		m_logger->debug("finish seq {}", m_req.body);
		http_utils::reply res;
		res.status_code = int(http_utils::reply::status_type::ok);
		
		res.add_header("Content-Type", "text/html");
		res.content = json(reply).dump();
		m_rep_cb(res);
	}

	void chat_session::run()
	{
		auto cur_err = check_request();
		if (!cur_err.empty())
		{
			http_utils::reply res;
			res.status_code = int(http_utils::reply::status_type::bad_request);

			res.add_header("Content-Type", "text/html");
			res.content = json(reply).dump();
			m_rep_cb(res);
		}
		else
		{
			route_request();
		}
	}


	chat_listener::chat_listener(asio::io_context& io_context, std::shared_ptr<spdlog::logger> in_logger, const std::string& address, const std::string& port,
		chat_manager& in_chat_manager,
		chat_sync_adpator& in_sync_adaptor)
		: http_utils::http_server(io_context,  std::move(in_logger), address, port)
		, m_chat_manager(in_chat_manager)
		, m_sync_adaptor(in_sync_adaptor)
		, m_ioc2(io_context)
	{
		tick_timer = std::make_shared<asio::steady_timer>(m_ioc2.get_executor(), std::chrono::seconds(1));
		tick_timer->async_wait([this](const boost::system::error_code& e) {
			this->tick();
			});
	}



	void chat_listener::tick()
	{
		m_tick_counter++;
		auto init_result = m_sync_adaptor.init_results;
		m_sync_adaptor.init_results.clear();
		for (const auto& one_pair : init_result)
		{
			m_chat_manager.on_init(one_pair.first, one_pair.second);
		}
		auto find_result = m_sync_adaptor.find_one_results;
		m_sync_adaptor.find_one_results.clear();
		for (const auto& one_pair : find_result)
		{
			m_chat_manager.on_load(one_pair.first, one_pair.second);
		}
		m_sync_adaptor.save_results.clear();
		if (m_tick_counter % 10 == 0)
		{
			m_logger->info("tick save {} ", json(m_chat_manager.tick_save(10)).dump());
			m_logger->info("tick expire {} ", json(m_chat_manager.tick_expire(10)).dump());
		}
		tick_timer = std::make_shared<asio::steady_timer>(m_ioc2.get_executor(), std::chrono::seconds(1));
		tick_timer->async_wait([this](const boost::system::error_code& e) {
			this->tick();
			});
	}

	void chat_listener::handle_request(const http_utils::request& req, http_utils::reply_handler rep_cb)
	{
		auto cur_chat_session = std::make_shared<chat_session>(req, rep_cb, m_logger, m_chat_manager);
		cur_chat_session->run();
	}
}