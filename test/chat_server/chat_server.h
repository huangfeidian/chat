#pragma once

#include "chat_manager.h"

#include <http_utils/http_server.h>

#include <nlohmann/json.hpp>

namespace spiritsaway::system::chat
{
	using namespace spiritsaway;
	using json = nlohmann::json;
	namespace asio = boost::asio;
	namespace net = boost::asio;            // from <boost/asio.hpp>
	using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
	using logger_t = std::shared_ptr<spdlog::logger>;

	class chat_db
	{
		std::vector<json::object_t> m_content;
		chat_record_seq_t find_one_idx(const json::object_t& filter) const;
		std::vector<chat_record_seq_t> find_all_idx(const json::object_t& filter) const;
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

	class chat_session :public std::enable_shared_from_this<chat_session>
	{
		chat_manager& m_chat_manager;
		std::string from;
		std::string chat_key;
		std::string cur_cmd;
		chat_record_seq_t seq_begin = 0;
		chat_record_seq_t seq_end = 0;
		json::object_t msg;
		json::object_t reply;
		logger_t m_logger;
		http_utils::request m_req;
		http_utils::reply_handler m_rep_cb;
		std::string check_request();
		void route_request();



		void finish_task_1();
		void finish_task_2();

	public:
		chat_session(const http_utils::request& req, http_utils::reply_handler rep_cb,
			logger_t in_logger,
			 chat_manager& in_chat_manager);
		void run();

	};

	class chat_listener : public http_utils::http_server
	{
	protected:
		chat_manager& m_chat_manager;
		chat_sync_adpator& m_sync_adaptor;
		std::shared_ptr<asio::steady_timer> tick_timer;
		std::uint64_t m_tick_counter = 0;
		void tick();
		net::io_context& m_ioc2;
	public:
		chat_listener(asio::io_context& io_context, std::shared_ptr<spdlog::logger> in_logger, const std::string& address, const std::string& port,
			chat_manager& in_chat_manager,
			chat_sync_adpator& in_sync_adaptor);

		void handle_request(const http_utils::request& req, http_utils::reply_handler rep_cb) override;
	};
}