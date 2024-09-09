#pragma once

#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>
#include <boost/range/begin.hpp> // needed by spawn
#include <boost/range/end.hpp> // needed by spawn
#include <boost/asio/spawn.hpp>
#include <boost/intrusive/list.hpp>
#include <upnp/detail/cancel.h>
#include <upnp/third_party/net.h>

#include <optional>

namespace upnp {

class ConditionVariable {
    using IntrusiveHook = boost::intrusive::list_base_hook
        <boost::intrusive::link_mode
            <boost::intrusive::auto_unlink>>;

    using HandlerType = boost::asio::async_result<boost::asio::yield_context,
                                               void(boost::system::error_code)>::handler_type;

    struct WaitEntry : IntrusiveHook {
        std::optional<HandlerType> handler;
    };

    using IntrusiveList = boost::intrusive::list
        <WaitEntry, boost::intrusive::constant_time_size<false>>;

public:
    ConditionVariable(const net::any_io_executor&);

    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;

    ~ConditionVariable();

    net::any_io_executor get_executor() { return _exec; }

    void notify(const boost::system::error_code& ec
                    = boost::system::error_code());

    void wait(cancel_t&, net::yield_context yield);
    void wait(net::yield_context yield);

private:
    net::any_io_executor _exec;
    IntrusiveList _on_notify;
};

inline
ConditionVariable::ConditionVariable(const net::any_io_executor& exec)
    : _exec(exec)
{
}

inline
ConditionVariable::~ConditionVariable()
{
    notify(net::error::operation_aborted);
}

inline
void ConditionVariable::notify(const boost::system::error_code& ec)
{
    while (!_on_notify.empty()) {
        auto& e = _on_notify.front();
        net::post(_exec, [h = std::move(e.handler), ec] () mutable
        {
            h.value()(ec);
        });
        _on_notify.pop_front();
    }
}
   
inline
void ConditionVariable::wait(cancel_t& cancel, boost::asio::yield_context yield)
{
    WaitEntry entry;
    auto work = net::make_work_guard(_exec);
    net::async_initiate<boost::asio::yield_context, void(boost::system::error_code)>(
        [this, &work, &cancel, &entry](HandlerType&& handler) {
            entry.handler.emplace(std::forward<decltype(handler)>(handler));
            _on_notify.push_back(std::move(entry));
            auto slot = cancel.connect([&] {
                assert(entry.is_linked());
                if (entry.is_linked()) entry.unlink();

                net::post(_exec, [h = std::move(entry.handler)] () mutable {
                    h.value()(boost::asio::error::operation_aborted);
                });
            });
        }, yield);
}

inline
void ConditionVariable::wait(boost::asio::yield_context yield)
{
    cancel_t dummy_cancel;
    wait(dummy_cancel, yield);
}

} // ouinet namespace
