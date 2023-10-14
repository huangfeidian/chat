#include "chat_server.h"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/logger.h>
using namespace spiritsaway::system::chat;

using namespace spiritsaway::http_utils;
std::shared_ptr<spdlog::logger> create_logger(const std::string& name)
{
	auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
	console_sink->set_level(spdlog::level::debug);
	std::string pattern = "[" + name + "] [%^%l%$] %v";
	console_sink->set_pattern(pattern);

	auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(name + ".log", true);
	file_sink->set_level(spdlog::level::trace);
	auto logger = std::make_shared<spdlog::logger>(name, spdlog::sinks_init_list{ console_sink, file_sink });
	logger->set_level(spdlog::level::trace);
	return logger;
}

int main(int argc, char* argv[])
{
	// Check command line arguments.
	//if (argc != 5)
	//{
	//	std::cerr <<
	//		"Usage: http-server-async <address> <port> <doc_root> <threads>\n" <<
	//		"Example:\n" <<
	//		"    http-server-async 0.0.0.0 8080 . 1\n";
	//	return EXIT_FAILURE;
	//}
	std::string address_str = "127.0.0.1";
	std::uint16_t port = 8080;
	std::uint8_t const threads = 2;
	std::uint32_t expire_time = 10;
	// The io_context is required for all I/O
	net::io_context ioc{ threads };
	auto cur_logger = create_logger("server");
	chat_db cur_chat_data;
	chat_sync_adpator cur_sync_apdator;
	chat_data_init_func init_func = [&](const std::string& chat_key, const json::object_t& filter, const json::object_t& doc)
	{
		cur_sync_apdator.init_results.emplace_back(chat_key, cur_chat_data.find_or_create(filter, doc));
		cur_logger->info("init key {} filter {} with result {}", chat_key, json(filter).dump(), json(cur_sync_apdator.init_results.back().second).dump());
	};
	chat_data_load_func load_func = [&](const std::string& chat_key, const json::object_t& filter)
	{
		cur_logger->info("load key {} filter {}", chat_key, json(filter).dump());
		cur_sync_apdator.find_one_results.emplace_back(chat_key, cur_chat_data.find_one(filter));
	};

	chat_data_save_func save_func = [&](const std::string& chat_key, const json::object_t& filter, const json::object_t& doc)
	{
		cur_chat_data.update_or_create(filter, doc);
		cur_sync_apdator.save_results.push_back(chat_key);
		cur_logger->info("save chat_key {} one doc {}", chat_key, json(doc).dump());
	};
	chat_manager cur_chat_manager(init_func, load_func, save_func, 5);

	// Create and launch a listening port
	auto cur_listener = std::make_shared<chat_listener>(
		ioc,
		cur_logger,
		address_str, std::to_string(port) ,
		cur_chat_manager, cur_sync_apdator);

	cur_listener->run();
	
	// Run the I/O service on the requested number of threads
	std::vector<std::thread> v;
	v.reserve(threads - 1);
	for (auto i = threads - 1; i > 0; --i)
		v.emplace_back(
			[&ioc]
			{
				ioc.run();
			});
	ioc.run();

	return EXIT_SUCCESS;
}
