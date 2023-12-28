#pragma once

#include <bit>
#include <vector>
#include <concepts>

#include "pyoptinterface/core.hpp"

// index as the key, variable as the value
// index is monotone increasing
// if we want to delete	a index, we can set the value to -1
template <std::signed_integral T>
class MonotoneVector
{
  private:
	std::vector<T> m_data;
	T m_start = 0;
	T m_update_start = 0;
	std::size_t m_last_correct_index = 0;
	std::size_t m_cardinality = 0;

  public:
	MonotoneVector() = default;
	MonotoneVector(T start) : m_start(start), m_update_start(start)
	{
	}

	IndexT add_index()
	{
		IndexT index = m_data.size();
		m_data.push_back(m_start);
		m_cardinality += 1;
		return index;
	}
	void delete_index(const IndexT &index)
	{
		if (m_data[index] < 0)
		{
			return;
		}
		if (index < m_last_correct_index)
		{
			m_update_start = m_data[index];
			m_last_correct_index = index;
		}
		m_data[index] = -1;
		m_cardinality -= 1;
	}
	bool has_index(const IndexT &index)
	{
		return get_index(index) >= 0;
	}
	T get_index(const IndexT &index)
	{
		if (index >= m_data.size())
		{
			throw std::runtime_error("Index out of range");
		}
		if (m_data[index] >= 0 && index > m_last_correct_index)
		{
			update_to(index);
		}
		return m_data[index];
	}
	std::size_t num_active_indices() const
	{
		return m_cardinality;
	}
	std::vector<IndexT> get_active_indices() const
	{
		std::vector<IndexT> indices;
		indices.reserve(m_cardinality);
		for (IndexT i = 0; i < m_data.size(); i++)
		{
			if (m_data[i] >= 0)
			{
				indices.push_back(i);
			}
		}
		return indices;
	}
	void update_to(IndexT index)
	{
		// we ensure that m_data[index] >= 0
		T counter = m_update_start;
		constexpr int STEP_STATE = 1;
		constexpr int JUMP_STATE = 2;
		int state;
		std::size_t jump_start_index;
		if (m_data[m_last_correct_index] < 0)
		{
			state = JUMP_STATE;
			jump_start_index = m_last_correct_index;
		}
		else
		{
			state = STEP_STATE;
			counter++;
		}
		for (IndexT i = m_last_correct_index + 1; i <= index;)
		{
			switch (state)
			{
			case JUMP_STATE:
				if (m_data[i] >= 0)
				{
					m_data[jump_start_index] = -i;
					state = STEP_STATE;
					goto CONTINUE_STEP;
				}
				else
				{
				CONTINUE_JUMP:
					T new_i = -m_data[i];
					if (new_i > index)
					{
						i = new_i;
					}
					else
					{
						i++;
					}
				}
				break;
			case STEP_STATE:
				if (m_data[i] < 0)
				{
					state = JUMP_STATE;
					jump_start_index = i;
					goto CONTINUE_JUMP;
				}
				else
				{
				CONTINUE_STEP:
					m_data[i] = counter;
					counter++;
					i++;
				}
				break;
			default:
				throw std::runtime_error("Unknown state");
			}
		}
		m_last_correct_index = index;
		m_update_start = m_data[index];
	}
	void clear()
	{
		m_data.clear();
		m_update_start = m_start;
		m_last_correct_index = 0;
		m_cardinality = 0;
	}
};

template <std::unsigned_integral ChunkT, std::signed_integral ResultT>
class ChunkedBitVector
{
  public:
	ChunkedBitVector(ResultT start = 0) : m_start(start)
	{
		CHUNK_WIDTH = sizeof(ChunkT) * 8;
		LOG2_CHUNK_WIDTH = std::bit_width(CHUNK_WIDTH) - 1;
		clear();
	}

	IndexT add_index()
	{
		if (m_last_bit == CHUNK_WIDTH)
		{
			IndexT result = m_data.size() << LOG2_CHUNK_WIDTH;

			m_data.push_back(1);
			m_cumulated_ranks.push_back(m_cumulated_ranks.back());
			m_chunk_ranks.push_back(-1);
			m_last_bit = 1;

			return result;
		}
		else
		{
			auto last_chunk_end = (m_data.size() - 1) << LOG2_CHUNK_WIDTH;
			IndexT result = last_chunk_end + m_last_bit;
			ChunkT &last_chunk = m_data.back();
			// set m_last_bit to 1
			last_chunk |= (1 << m_last_bit);
			m_last_bit++;

			return result;
		}
	}

	void delete_index(const IndexT &index)
	{
		if (index >= m_data.size() * CHUNK_WIDTH)
		{
			return;
		}
		std::size_t chunk_index;
		std::uint8_t bit_index;
		locate_index(index, chunk_index, bit_index);
		ChunkT &chunk = m_data[chunk_index];
		if (chunk & (1 << bit_index))
		{
			// set bit_index to 0
			chunk &= ~(1 << bit_index);
			if (m_last_correct_chunk > chunk_index)
			{
				m_last_correct_chunk = chunk_index;
			}
			// rank of this chunk should be recalculated
			m_chunk_ranks[chunk_index] = -1;
		}
	}

	bool has_index(const IndexT &index) const
	{
		std::size_t chunk_index;
		std::uint8_t bit_index;
		locate_index(index, chunk_index, bit_index);
		const ChunkT &chunk = m_data[chunk_index];
		return chunk & (1 << bit_index);
	}

	ResultT get_index(const IndexT &index)
	{
		if (index >= m_data.size() * CHUNK_WIDTH)
		{
			throw std::runtime_error("Index out of range");
		}
		std::size_t chunk_index;
		std::uint8_t bit_index;
		locate_index(index, chunk_index, bit_index);
		ChunkT &chunk = m_data[chunk_index];
		bool bit = chunk & (1 << bit_index);

		if (!bit)
		{
			return -1;
		}
		if (chunk_index > m_last_correct_chunk)
		{
			update_to(chunk_index);
		}
		// count the 1 on the right of bit_index
		ChunkT mask = (static_cast<ChunkT>(1) << bit_index) - 1;
		std::uint8_t current_chunk_index = std::popcount(chunk & mask);
		return m_cumulated_ranks[chunk_index] + current_chunk_index;
	}

	void update_to(std::size_t chunk_index)
	{
		// m_cumulated_ranks[0, m_last_correct_chunk] and m_chunk_ranks[0, m_last_correct_chunk) are
		// all correct
		// we need to update m_cumulated_ranks[m_last_correct_chunk + 1, chunk_index]
		// and m_chunk_ranks[m_last_correct_chunk, chunk_index)
		for (int ichunk = m_last_correct_chunk; ichunk < chunk_index; ichunk++)
		{
			if (m_chunk_ranks[ichunk] < 0)
			{
				m_chunk_ranks[ichunk] = std::popcount(m_data[ichunk]);
			}
			m_cumulated_ranks[ichunk + 1] = m_cumulated_ranks[ichunk] + m_chunk_ranks[ichunk];
		}
		m_last_correct_chunk = chunk_index;
	}

	void locate_index(IndexT index, std::size_t &chunk_index, std::uint8_t &bit_index) const
	{
		chunk_index = index >> LOG2_CHUNK_WIDTH;
		bit_index = index & (CHUNK_WIDTH - 1);
	}

	void clear()
	{
		m_data.resize(1, 0);
		m_cumulated_ranks.resize(1, m_start);
		m_chunk_ranks.resize(1, -1);
		m_last_correct_chunk = 0;
		m_last_bit = 0;
	}

  private:
	std::uint8_t CHUNK_WIDTH;
	std::uint8_t LOG2_CHUNK_WIDTH;

	ResultT m_start;

	std::vector<ChunkT> m_data;
	std::vector<ResultT> m_cumulated_ranks;
	std::vector<std::int8_t> m_chunk_ranks; // -1 represents tainted
	std::size_t m_last_correct_chunk;
	std::uint8_t m_last_bit;
};

template <typename ResultT>
using MonotoneIndexer = ChunkedBitVector<std::uint64_t, ResultT>;
