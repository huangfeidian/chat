#pragma once

#include <cstdint>
#include <vector>
#include <algorithm>
#include <cassert>

namespace spiritsaway::system::chat
{
	struct chat_readed_region
	{
		std::uint64_t begin;
		std::uint64_t end;
	};

	class chat_unread_mgr
	{
		std::vector<chat_readed_region> readed_regions; // 所有已经设置为已读的不相交上升区间 
		std::uint64_t max_msg_seq; // 最新消息的编号
		std::uint64_t unread_msg_num; // 剩下未读消息的数量
		void add_new_msg(std::uint64_t new_msg_seq)
		{
			if (new_msg_seq > max_msg_seq)
			{
				unread_msg_num += new_msg_seq - max_msg_seq;
				max_msg_seq = new_msg_seq;
			}
		}
		void mark_all_readed()
		{
			readed_regions.clear();
			readed_regions.push_back(chat_readed_region{ 0, max_msg_seq + 1 });
		}

		// 传入的必须是与之前区域不相交的
		void mark_readed(std::uint64_t begin, std::uint64_t end)
		{
			if (end <= begin || end > max_msg_seq + 1 || begin > max_msg_seq)
			{
				return;
			}
			std::uint64_t i = 0;
			for (; i < readed_regions.size(); i++)
			{
				if (readed_regions[i].begin > begin)
				{
					break;
				}
			}
			bool merge_left = false;
			bool merge_right = false;
			if (i != 0)
			{
				assert(readed_regions[i - 1].end <= begin);
				if (readed_regions[i - 1].end == begin)
				{
					readed_regions[i - 1].end = end;
					merge_left = true;
				}
			}
			if (i != readed_regions.size())
			{
				assert(readed_regions[i].begin >= end);
				if (readed_regions[i].begin == end)
				{
					readed_regions[i].begin = begin;
					merge_right = true;
				}
			}
			if (merge_left && merge_right)
			{
				readed_regions[i - 1].end = readed_regions[i].end;
				readed_regions.erase(readed_regions.begin() + i);
			}
			if (!merge_left && !merge_right)
			{
				readed_regions.insert(readed_regions.begin() + i, chat_readed_region{ begin, end });
			}
			unread_msg_num -= end - begin;
		}
	};
}