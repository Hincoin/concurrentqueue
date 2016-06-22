#include <iostream>
#include <array>
#include <vector>
#include <queue>
#include <random>
#include <atomic>
#include <thread>
#include "wrappers.h"

template<typename T>
class lf_queue
{
private:

	template<typename Ptr, int N> struct aligned_atomic_ptr;
	static constexpr int alignment  = 64;
	static constexpr int num_queues = 32;
	using pointer  		 = std::queue<T>*;
	using atomic_pointer = aligned_atomic_ptr<pointer, alignment>;
	using container 	 = std::array<atomic_pointer, num_queues>;

	template<typename Ptr, int N> 
    struct alignas(alignment) aligned_atomic_ptr
	{
		std::atomic<Ptr> atomic_ptr;

		template<typename U>
		void store(U&& data, std::memory_order tag = std::memory_order_seq_cst)
		{
			atomic_ptr.store(std::forward<U>(data), tag);
		}
		Ptr load(std::memory_order tag = std::memory_order_seq_cst)
		{
			return atomic_ptr.load(tag);
		}

		bool compare_exchange_weak(Ptr& expec, Ptr desired, std::memory_order success, std::memory_order failure)
		{
			return atomic_ptr.compare_exchange_weak(expec, desired, success, failure);
		}
	};

	container data_;


	
public:

	using consumer_token_t = DummyToken;
	using producer_token_t = DummyToken;

	lf_queue() 
	{
		for(int i = 0 ; i < num_queues; ++i)
		 {
		 		data_[i].store( new std::queue<T>{}, std::memory_order_relaxed );
		 } 
	}

	struct queue_holder
	{
		container& data_;
		int index_;
		pointer ptr_;

		queue_holder(int idx, pointer p, container& init) : data_(init), index_(idx), ptr_(p) {}
		queue_holder(queue_holder&& other) : data_(other.data_), index_(other.index_), ptr_(other.ptr_)
		{
			other.index_ = -1;
		}
		queue_holder& operator=(queue_holder&& other)
		{
			data_ = other.data_;
			index_ = other.index_;
			ptr_ = other.ptr_;
			other.index_ = -1;
		}
		queue_holder& operator=(const queue_holder&) = delete;
		
		std::queue<T>& queue()
		{
			return *ptr_;
		}
		~queue_holder()
		{
			if(index_ >= 0)
				data_[index_].store(ptr_, std::memory_order_release);
		}
	};
	

	queue_holder acquire_queue()
	{
		static thread_local int starting_position = 0;



		
			
		for(int i = starting_position;;++i)
		{
			int index = i & (num_queues - 1);
			pointer ptr = data_[index].load(std::memory_order_relaxed);
			if(ptr == nullptr)
				continue; // owned by another thread
			if(data_[index].compare_exchange_weak(ptr, nullptr, std::memory_order_acquire, std::memory_order_relaxed)) 
			{
				// acquired data[i]
				starting_position = index; // will start from here next
				return {index, ptr, data_};
			}
		}
		
		

	}

	template<typename U>
	bool enqueue(U&& arg)
	{
		auto queue_guard =  acquire_queue(); // atmically acquire queue
		auto& queue = queue_guard.queue(); // get a reference to the queue

		queue.emplace(std::forward<U>(arg));
		return true;

	}

	template<typename It>
	bool enqueue_bulk(It iter, std::size_t count)
	{
		auto queue_guard =  acquire_queue(); // atmically acquire queue
		auto& queue = queue_guard.queue(); // get a reference to the queue

		for(std::size_t i = 0; i < count; ++i)
		{
			queue.push(*(iter++));
		}
		return true;
	}

	bool try_dequeue(T& item)
	{
		auto queue_guard{acquire_queue()}; // atmically acquire queue
		auto& queue = queue_guard.queue(); // get a reference to the queue
		if(queue.empty())
			return false;

		item = queue.front();
		queue.pop();
		return true;
	}

	template<typename It> 
	size_t try_dequeue_bulk(It output, size_t items)
	{
		auto queue_guard{acquire_queue()}; 
		auto& queue = queue_guard.queue();
		size_t iter = std::min(items, queue.size());

		for(std::size_t i = 0; i < iter; ++i)
		{
			*(output++) = queue.front();
			queue.pop();
		}

		return iter;

	}

	T& front()
	{
		auto queue_guard{acquire_queue()}; // atmically acquire queue
		auto& queue = queue_guard.queue(); // get a reference to the queue

		return queue.front();
	}

	bool enqueue(producer_token_t const&, T const&) { return false; }
	bool try_enqueue(producer_token_t, T const&) { return false; }
	bool try_dequeue(consumer_token_t, T& item) { return false; }
	template<typename It> bool enqueue_bulk(producer_token_t const&, It, size_t) { return false; }
	
	template<typename It> size_t try_dequeue_bulk(consumer_token_t, It, size_t) { return 0; }

	



	~lf_queue()
	{
		for(auto& st_queue : data_)
		{
			delete st_queue.load(std::memory_order_relaxed);
		}
	}


	


};