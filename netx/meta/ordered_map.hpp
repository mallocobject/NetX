#ifndef NETX_META_ORDERED_MAP_HPP
#define NETX_META_ORDERED_MAP_HPP

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>
namespace netx
{
namespace meta
{
template <typename Key, typename Value> struct OrderedMap
{
	template <typename... Args> void try_emplace(const Key& k, Args&&... args)
	{
		if (!data_.contains(k))
		{
			order_.push_back(k);
			data_.try_emplace(k, std::forward<Args>(args)...);
		}
	}

	template <typename... Args> auto try_emplace(Key&& k, Args&&... args)
	{
		if (!data_.contains(k))
		{
			order_.push_back(k);
			data_.try_emplace(std::move(k), std::forward<Args>(args)...);
		}
	}

	template <typename... Args>
	void insert_or_assign(const Key& k, Args&&... args)
	{
		if (!data_.contains(k))
		{
			order_.push_back(k);
		}
		data_.insert_or_assign(k, std::forward<Args>(args)...);
	}

	template <typename... Args> auto insert_or_assign(Key&& k, Args&&... args)
	{
		if (!data_.contains(k))
		{
			order_.push_back(k);
		}
		data_.insert_or_assign(std::move(k), std::forward<Args>(args)...);
	}

	Value& operator[](const Key& k)
	{
		if (data_.find(k) == data_.end())
		{
			order_.push_back(k);
		}
		return data_[k];
	}

	Value& operator[](Key&& k)
	{
		if (data_.find(k) == data_.end())
		{
			order_.push_back(k);
		}
		return data_[std::move(k)];
	}

	const std::vector<Key> keys() const
	{
		return order_;
	}

	bool empty() const noexcept
	{
		return data_.empty();
	}

	struct Iterator
	{

		const Key& first() const
		{
			return *iter;
		}

		Value& second()
		{
			return operator*();
		}

		Value& operator*()
		{
			return (*data)[*iter];
		}

		Iterator& operator++() noexcept
		{
			++iter;
			return *this;
		}

		bool operator!=(const Iterator& other) const noexcept
		{
			return iter != other.iter;
		}

		std::vector<Key>::iterator iter;
		std::unordered_map<Key, Value>* data;
	};

	struct ConstIterator
	{

		const Key& first() const
		{
			return *iter;
		}

		const Value& second() const
		{
			return operator*();
		}

		const Value& operator*() const
		{
			return data->at(*iter);
		}

		ConstIterator& operator++() noexcept
		{
			++iter;
			return *this;
		}

		bool operator!=(const ConstIterator& other) const noexcept
		{
			return iter != other.iter;
		}

		std::vector<Key>::const_iterator iter;
		const std::unordered_map<Key, Value>* data;
	};

	Iterator begin()
	{
		return {order_.begin(), &data_};
	}

	ConstIterator begin() const
	{
		return {order_.begin(), &data_};
	}

	Iterator end()
	{
		return {order_.end(), &data_};
	}

	ConstIterator end() const
	{
		return {order_.end(), &data_};
	}

	friend struct Iterator;
	friend struct ConstIterator;

  private:
	std::vector<Key> order_;
	std::unordered_map<Key, Value> data_;
};
} // namespace meta
} // namespace netx

#endif