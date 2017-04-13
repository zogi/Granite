#pragma once

#include <vector>
#include <list>
#include <type_traits>
#include <stdexcept>
#include <command_buffer.hpp>
#include "hashmap.hpp"
#include "enum_cast.hpp"

namespace Granite
{
class ShaderSuite;

struct RenderInfo
{
	// Plain function pointer so we can portably sort on it,
	// and the rendering function is kind of supposed to be a more
	// pure function anyways.
	// Adjacent render infos which share instance key will be batched together.
	void (*render)(Vulkan::CommandBuffer &cmd, const RenderInfo **infos, unsigned instance_count) = nullptr;

	// RenderInfos with same key can be instanced.
	Util::Hash instance_key = 0;

	// Sorting key.
	// Lower sorting keys will appear earlier.
	uint64_t sorting_key = 0;

	// RenderInfo objects cannot be deleted.
	// Classes which inherit from this class just be trivially destructible.
	// Classes which inherit data here are supposed to live temporarily and
	// should only hold POD data.
	// Dynamic allocation can be made from the RenderQueue.
	~RenderInfo() = default;
};

enum class Queue : unsigned
{
	Opaque = 0,
	Transparent,
	Count
};

class RenderQueue
{
public:
	enum { BlockSize = 256 * 1024 };

	template <typename T, typename... P>
	T &emplace(Queue queue, P&&... p)
	{
		static_assert(std::is_trivially_destructible<T>::value, "Dispatchable type is not trivially destructible!");
		void *buffer = allocate(sizeof(T), alignof(T));
		if (!buffer)
			throw std::bad_alloc();

		T *t = new(buffer) T(std::forward<P>(p)...);
		enqueue(queue, t);
		return *t;
	}

	template <typename T>
	T *allocate_multiple(Queue queue, size_t n)
	{
		static_assert(std::is_trivially_destructible<T>::value, "Dispatchable type is not trivially destructible!");
		void *buffer = allocate(sizeof(T) * n, alignof(T));
		if (!buffer)
			throw std::bad_alloc();

		T *t = new(buffer) T[n]();
		for (size_t i = 0; i < n; i++)
			enqueue(queue, &t[i]);

		return t;
	}

	void *allocate(size_t size, size_t alignment = 64);
	void enqueue(Queue queue, const RenderInfo *render_info);
	void combine_render_info(const RenderQueue &queue);
	void reset();
	void reset_and_reclaim();

	const RenderInfo **get_queue(Queue queue) const
	{
		return queues[ecast(queue)].queue;
	}

	size_t get_queue_count(Queue queue) const
	{
		return queues[ecast(queue)].count;
	}

	void sort();
	void dispatch(Queue queue, Vulkan::CommandBuffer &cmd);
	void dispatch(Queue queue, Vulkan::CommandBuffer &cmd, size_t begin, size_t end);

	void set_shader_suite(ShaderSuite *suite)
	{
		shader_suite = suite;
	}

	ShaderSuite *get_shader_suite() const
	{
		return shader_suite;
	}

private:
	struct Block
	{
		std::vector<uint8_t> buffer;
		uintptr_t ptr = 0;
		uintptr_t begin = 0;
		uintptr_t end = 0;

		Block(size_t size)
		{
			buffer.resize(size);
			begin = reinterpret_cast<uintptr_t>(buffer.data());
			end = reinterpret_cast<uintptr_t>(buffer.data()) + size;
			reset();
		}

		void operator=(const Block &) = delete;
		Block(const Block &) = delete;
		Block(Block &&) = default;
		Block &operator=(Block &&) = default;

		void reset()
		{
			ptr = begin;
		}
	};

	using Chain = std::list<Block>;
	Chain blocks;
	Chain::iterator current = std::end(blocks);

	struct QueueInfo
	{
		const RenderInfo **queue = nullptr;
		size_t count = 0;
		size_t capacity = 0;
	};
	QueueInfo queues[static_cast<unsigned>(Queue::Count)];

	void *allocate_from_block(Block &block, size_t size, size_t alignment);
	Chain::iterator insert_block();

	ShaderSuite *shader_suite = nullptr;
};
}