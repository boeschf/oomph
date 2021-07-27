#include "./context.hpp"
#include "./communicator.hpp"
#include "./message_buffer.hpp"
#include "./request.hpp"

namespace oomph
{
template<>
region
register_memory<context_impl>(context_impl& c, void* ptr, std::size_t size)
{
    return c.make_region(ptr, size);
}
#if HWMALLOC_ENABLE_DEVICE
template<>
region
register_device_memory<context_impl>(context_impl& c, void* ptr, std::size_t size)
{
    return c.make_region(ptr, size, true);
}
#endif

//template<>
//request::request<>()
//: m_impl()
//{
//}

request::impl::~impl()
{
    if (m_req) destroy();
}

communicator::impl*
context_impl::get_communicator()
{
    auto comm = new communicator::impl{this, m_worker.get(),
        std::make_unique<worker_type>(
            get(), m_db /*, m_mutex*/, UCS_THREAD_MODE_SERIALIZED /*, m_rank_topology*/), m_mutex};
    m_comms_set.insert(comm);
    return comm;
}

} // namespace oomph

#include "../src.cpp"

namespace oomph
{
void
communicator::impl::send(detail::message_buffer&& msg, std::size_t size, rank_type dst,
    tag_type tag, std::function<void(detail::message_buffer, rank_type, tag_type)>&& cb)
{
    auto req = send(msg.m_heap_ptr->m, size, dst, tag);
    m_callbacks.enqueue(std::move(msg), dst, tag, std::move(req), std::move(cb));
}
void
communicator::impl::recv(detail::message_buffer&& msg, std::size_t size, rank_type src,
    tag_type tag, std::function<void(detail::message_buffer, rank_type, tag_type)>&& cb)
{
    auto req = recv(msg.m_heap_ptr->m, size, src, tag);
    m_callbacks.enqueue(std::move(msg), src, tag, std::move(req), std::move(cb));
}
} // namespace oomph
