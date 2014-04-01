#ifndef __FIXED_PRIORITY_QUEUE_H__
#define __FIXED_PRIORITY_QUEUE_H__

#include <vector>
#include <algorithm>
#include <functional>
#include <cassert>

//------------------------------------------------------------------------------

template<class T, class ContainerT = std::vector<T>, class CompT = std::greater<T> >
class FixedPriorityQueue
{
	typedef typename ContainerT::value_type value_type;
	typedef typename ContainerT::size_type size_type;

	size_type m_capacity;
	ContainerT &m_cont;
	CompT m_comp;

public:
	explicit FixedPriorityQueue(size_type capacity, ContainerT &cont)
		: m_capacity(capacity), m_cont(cont), m_comp()
	{
		assert(m_capacity);
		m_cont.reserve(m_capacity);
	}

	bool empty() const
	{
		return (m_cont.empty());
	}

	size_type size() const
	{
		return (m_cont.size());
	}

	void push(const value_type& v)
	{
		if (m_cont.size() < m_capacity) {
			// ����δ�������뵽��󲢵����Ѽ���
			m_cont.push_back(v);
			std::push_heap(m_cont.begin(), m_cont.end(), m_comp);
		} else {
			// ��������
			// m_cont.front()�Ƕѵĸ��ڵ㣬Ҳ���ǵ�ǰ����Сֵ�����������������бȽϣ�����С��ֱ�Ӷ���
			if (!m_comp(m_cont.front(), v)) {
				// ������ǰ����Сֵ��ע���ʱ������size��ά�ֲ��䣬����������ֵ�ŵ������������λ����
				std::pop_heap(m_cont.begin(), m_cont.end(), m_comp);
				// ������ֵ��������
				m_cont.back() = v;
				std::push_heap(m_cont.begin(), m_cont.end(), m_comp);
			}
		}
	}

	void sort()
	{
		// ���ѻ�ԭΪ����õ���������
		if (m_cont.size()) {
			std::sort_heap(m_cont.begin(), m_cont.end(), m_comp);
		}
	}
};

//------------------------------------------------------------------------------

#endif // !__FIXED_PRIORITY_QUEUE_H__
