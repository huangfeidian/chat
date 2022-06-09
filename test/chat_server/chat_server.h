#pragma once

#include "chat_manager.h"

#include <http_utils/http_server.h>

#include <nlohmann/json.hpp>

namespace spiritsaway::system::chat
{
	using namespace spiritsaway;
	using json = nlohmann::json;
	namespace beast = boost::beast;         // from <boost/beast.hpp>
	namespace http = beast::http;           // from <boost/beast/http.hpp>
	namespace net = boost::asio;            // from <boost/asio.hpp>
	using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
	using logger_t = std::shared_ptr<spdlog::logger>;

	class chat_db
	{
		std::vector<json::object_t> m_content;
		std::uint32_t find_one_idx(const json::object_t& filter) const;
		std::vector<std::uint32_t> find_all_idx(const json::object_t& filter) const;
	public:
		std::vector<json::object_t> find_all(const json::object_t& filter) const;
		json::object_t find_one(const json::object_t& filter) const;
		void update_or_create(const json::object_t& filter, const json::object_t& doc);
		json::object_t find_or_create(const json::object_t& filter, const json::object_t& doc);
	};

	struct chat_sync_adpator
	{
		std::vector<std::pair<std::string, json::object_t>> init_results;
		std::vector<std::string> save_results;
		std::vector<std::pair<std::string, json::object_t>> find_one_results;
	};

	class chat_session : public http_utils::server::session
	{
		chat_manager& m_chat_manager;
		std::string from;
		std::string chat_key;
		std::string cur_cmd;
		std::uint32_t seq_begin;
		std::uint32_t seq_end;
		json::object_t msg;
		json::object_t reply;
		std::string check_request() override;
		void route_request() override;

		std::shared_ptr<boost::asio::steady_timer> expire_timer;
		void on_timeout(const boost::system::error_code& e);

		void finish_task_1();
		void finish_task_2();

	public:
		chat_session(tcp::socket&& socket,
			logger_t in_logger,
			std::uint32_t in_expire_time, chat_manager& in_chat_manager);

	};

	class chat_listener : public http_utils::server::listener
	{
	protected:
		chat_manager& m_chat_manager;
		chat_sync_adpator& m_sync_adaptor;
		std::shared_ptr<boost::asio::steady_timer> tick_timer;
		std::uint64_t m_tick_counter = 0;
		void tick();
	public:
		chat_listener(net::io_context& ioc,
			tcp::endpoint endpoint,
			logger_t in_logger,
			std::uint32_t expire_time,
			chat_manager& in_chat_manager,
			chat_sync_adpator& in_sync_adaptor);

		std::shared_ptr<http_utils::server::session> make_session(tcp::socket&& socket) override;
	};
}