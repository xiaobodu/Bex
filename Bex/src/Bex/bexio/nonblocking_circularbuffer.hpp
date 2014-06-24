#ifndef __BEX_IO_NONBLOCKING_CIRCULARBUFFER_HPP__
#define __BEX_IO_NONBLOCKING_CIRCULARBUFFER_HPP__

//////////////////////////////////////////////////////////////////////////
// 环形缓冲区
/*
* 一个读一个写时, 无需锁即可线程安全的环形缓冲区.
*/

#include <cstdio>
#include <boost/noncopyable.hpp>
#include <algorithm>

namespace Bex { namespace bexio
{
    class nonblocking_circularbuffer
        : boost::noncopyable
    {
    public:
        typedef char value_type;
        typedef value_type* pointer;
        typedef value_type const* const_pointer;
        typedef std::size_t size_type;
        typedef std::size_t difference_type;

        nonblocking_circularbuffer(pointer buffer, size_type capacity)
        {
            reset(buffer, capacity);
        }

        // 缓冲区起始地址
        const_pointer address() const
        {
            return buffer_;
        }

        // 缓冲区总大小
        size_type capacity() const
        {
            return capacity_;
        }

        //////////////////////////////////////////////////////////////////////////
        // @{ only one thread methods
        // 重置读写指针
        void reset()
        {
            get_ = buffer_, put_ = buffer_ + 1;
        }

        // 重设缓冲区及读写指针
        void reset(pointer buffer, size_type capacity)
        {
            buffer_ = buffer, capacity_ = capacity;
            reset();
        }

        // 是否空
        bool empty() const
        {
            return (gbegin() == gend());
        }

        // 是否满
        bool full() const
        {
            return (pbegin() == pend());
        }
        // @}
        //////////////////////////////////////////////////////////////////////////

        //////////////////////////////////////////////////////////////////////////
        // @{ put thread methods
        // 写指针
        pointer pptr() const
        {
            return put_;
        }

        // 连续的可写长度
        size_type pcount() const
        {
            pointer pb = pbegin(), pe = pend();
            return (pb > pe) ? distance(pb, end()) : distance(pb, pe);
        }
        
        // 写指针向后移动offset个位置
        void pbump(difference_type offset)
        {
            put_ = advance(put_, offset);
        }

        // 总计的可写长度
        size_type psize() const
        {
            return distance(pbegin(), pend());
        }

        // 写入数据
        size_type sputn(const_pointer data, size_type size)
        {
            const_pointer pos = data;
            size_type put_size = size;
            while (put_size)
            {
                size_type once = (std::min)(pcount(), put_size);
                if (!once)
                    break;

                memcpy(pptr(), pos, once);
                pbump(once);
                pos += once, put_size -= once;
            }

            return (size - put_size);
        }

        // @}
        ////////////////////////////////////////////.ipp///////////////////////////////

        //////////////////////////////////////////////////////////////////////////
        // @{ get thread methods
        // 读指针
        const_pointer gptr() const
        {
            return increment(get_);
        }

        // 连续的可读长度
        size_type gcount() const
        {
            const_pointer gb = increment(gbegin()), ge = increment(gend());
            return (gb > ge) ? distance(gb, end()) : distance(gb, ge);
        }

        // 读指针向后移动offset个位置
        void gbump(difference_type offset)
        {
            get_ = advance(get_, offset);
        }

        // 总计的可读长度
        size_type gsize() const
        {
            return distance(gbegin(), gend());
        }

        // 读取数据
        size_type sgetn(pointer data, size_type size)
        {
            pointer pos = data;
            size_type get_size = size;
            while (get_size)
            {
                size_type once = (std::min)(gcount(), get_size);
                if (!once)
                    break;

                memcpy(pos, gptr(), once);
                gbump(once);
                pos += once, get_size -= once;
            }

            return (size - get_size);
        }
        // @}
        //////////////////////////////////////////////////////////////////////////

    private:
        pointer pbegin() const
        {
            return put_;
        }
        pointer pend() const
        {
            return get_;
        }

        const_pointer gbegin() const
        {
            return get_;
        }
        const_pointer gend() const
        {
            return decrement(put_);
        }

        const_pointer end() const
        {
            return buffer_ + capacity_;
        }

        difference_type distance(const_pointer const lhs, const_pointer const rhs) const
        {
            return (rhs >= lhs) ? (rhs - lhs) : (rhs + capacity_ - lhs);
        }

        template <typename Pointer>
        Pointer advance(Pointer const pos, difference_type offset) const
        {
            Pointer result = pos + offset;
            return (result >= end()) ? (result - capacity_) : result;
        }

        template <typename Pointer>
        Pointer retreat(Pointer const pos, difference_type offset) const
        {
            Pointer result = pos - offset;
            return (pos < buffer_ + offset) ? (result + capacity_) : result;
        }

        template <typename Pointer>
        Pointer increment(Pointer const pos) const
        {
            return advance(pos, 1);
        }

        template <typename Pointer>
        Pointer decrement(Pointer const pos) const
        {
            return retreat(pos, 1);
        }

    private:
        // 缓冲区首地址
        pointer buffer_;

        // 缓冲区容量
        size_type capacity_;

        // 写指针 [pbegin, pend)
        pointer volatile put_;

        // 读指针 [gbegin, gend)
        pointer volatile get_;
    };

} //namespace bexio
} //namespace Bex

#endif //__BEX_IO_NONBLOCKING_CIRCULARBUFFER_HPP__
