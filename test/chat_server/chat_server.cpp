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
	chat_session::chat_session(tcp::socket&& socket,
		logger_t in_logger,
		std::uint32_t in_expire_time, chat_manager& in_chat_manager)
		: http_utils::server::session(std::move(socket), std::move(in_logger), in_expire_time)
		, m_chat_manager(in_chat_manager)
	{

	}

	std::string chat_session::check_request()
	{
		auto check_error = http_utils::server::session::check_request();
		if (!check_error.empty())
		{
			return check_error;
		}
		if (!req_.target().starts_with("/chat"))
		{
			return "request target must start with /chat";
		}
		if (!json::accept(req_.body()))
		{
			logger->info("request is {}", req_.body());
			return "body must be json";
		}
		auto json_body = json::parse(req_.body());
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
		auto self = std::dynamic_pointer_cast<chat_session>(shared_from_this());
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
		beast::get_lowest_layer(stream_).expires_after(
			std::chrono::seconds(expire_time));
		expire_timer = std::make_shared<boost::asio::steady_timer>(stream_.get_executor(), std::chrono::seconds(expire_time / 2));
		expire_timer->async_wait([self](const boost::system::error_code& e) {
			self->on_timeout(e);
			});
	}

	void chat_session::finish_task_1()
	{
		if (!expire_timer)
		{
			return;
		}
		finish_task_2();
	}

	void chat_session::finish_task_2()
	{
		expire_timer.reset();
		logger->debug("finish seq {}", req_.body());
		http::response<http::string_body> res{ http::status::ok, req_.version() };
		res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
		res.set(http::field::content_type, "text/html");
		res.keep_alive(req_.keep_alive());
		res.body() = json(reply).dump();
		res.prepare_payload();
		do_write(std::move(res));
	}

	void chat_session::on_timeout(const boost::system::error_code& e)
	{
		if (e == boost::asio::error::operation_aborted)
		{
			return;
		}
		expire_timer.reset();

		do_write(http_utils::common::create_response::bad_request("timeout", req_));
	}

	chat_listener::chat_listener(net::io_context& ioc,
		tcp::endpoint endpoint,
		logger_t in_logger,
		std::uint32_t expire_time,
		chat_manager& in_chat_manager,
		chat_sync_adpator& in_sync_adaptor)
		: http_utils::server::listener(ioc, std::move(endpoint), std::move(in_logger), expire_time)
		, m_chat_manager(in_chat_manager)
		, m_sync_adaptor(in_sync_adaptor)
	{
		tick_timer = std::make_shared<boost::asio::steady_timer>(ioc.get_executor(), std::chrono::seconds(1));
		tick_timer->async_wait([this](const boost::system::error_code& e) {
			this->tick();
			});
	}

	std::shared_ptr<http_utils::server::session> chat_listener::make_session(tcp::socket&& socket)
	{
		return std::make_shared<chat_session>(std::move(socket), logger, expire_time, m_chat_manager);
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
			logger->info("tick save {} ", json(m_chat_manager.tick_save(10)).dump());
			logger->info("tick expire {} ", json(m_chat_manager.tick_expire(10)).dump());
		}
		tick_timer = std::make_shared<boost::asio::steady_timer>(ioc_.get_executor(), std::chrono::seconds(1));
		tick_timer->async_wait([this](const boost::system::error_code& e) {
			this->tick();
			});
	}
}