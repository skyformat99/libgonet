#pragma once

#include <string>
#include <stdint.h>
#include <atomic>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/smart_ptr/enable_shared_from_this.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/variant.hpp>
#include <boost/function.hpp>
#include <libgo/coroutine.h>
#include "error.h"
#include "abstract.h"
#include "option.h"

namespace network {
namespace tcp_detail {

using namespace boost::asio;
using namespace boost::asio::ip;
using boost_ec = boost::system::error_code;
using boost::shared_ptr;

class TcpSession;
typedef shared_ptr<TcpSession> TcpSessionEntry;
class LifeHolder {};

io_service& GetTcpIoService();

class TcpServerImpl;
class TcpSession
    : public Options<TcpSession>, public boost::enable_shared_from_this<TcpSession>, public SessionBase
{
public:
    struct Msg
    {
        struct shutdown_msg_t {};

        std::atomic<bool> timeout{false};
        bool send_half = false;
        bool shutdown = false;
        std::size_t pos = 0;
        uint64_t id;
        SndCb cb;
        co_timer_id tid;
        Buffer buf;

        Msg(uint64_t uid, SndCb ocb) : id(uid), cb(ocb) {}
        explicit Msg(shutdown_msg_t) : shutdown(true) {}
    };
    typedef co_chan<boost::shared_ptr<Msg>> MsgChan;
    typedef std::list<boost::shared_ptr<Msg>> MsgList;

    explicit TcpSession(shared_ptr<tcp::socket> s, shared_ptr<LifeHolder> holder, uint32_t max_pack_size);
    ~TcpSession();
    void goStart();
    TcpSessionEntry GetSession();

    virtual void Send(Buffer && buf, SndCb const& cb = NULL) override;
    virtual void Send(const void* data, size_t bytes, SndCb const& cb = NULL) override;
    virtual void Shutdown(bool immediately = false) override;
    virtual bool IsEstab() override;
    virtual endpoint LocalAddr() override;
    virtual endpoint RemoteAddr() override;

private:
    void goReceive();
    void goSend();
    void SetCloseEc(boost_ec const& ec);
    void OnClose();
    void ShutdownSend();
    void ShutdownRecv();

private:
    shared_ptr<tcp::socket> socket_;
    shared_ptr<LifeHolder> holder_;
    Buffer recv_buf_;
    uint64_t msg_id_;
    MsgChan msg_chan_;
    MsgList msg_send_list_;
    co_mutex close_ec_mutex_;
    boost_ec close_ec_;

    std::atomic<bool> send_shutdown_{false};
    std::atomic<bool> recv_shutdown_{false};
    co_mutex closed_;

    tcp::endpoint local_addr_;
    tcp::endpoint remote_addr_;
};

class TcpServerImpl
    : public Options<TcpServerImpl>, public LifeHolder, public boost::enable_shared_from_this<TcpServerImpl>
{
public:
    typedef std::map<::network::SessionEntry, shared_ptr<TcpSession>> Sessions;

    boost_ec goStart(endpoint addr);
    void ShutdownAll();
    void Shutdown();
    tcp::endpoint LocalAddr();
    std::size_t SessionCount();

private:
    void Accept();
    void OnSessionClose(::network::SessionEntry id, boost_ec const& ec);

private:
    shared_ptr<tcp::acceptor> acceptor_;
    tcp::endpoint local_addr_;
    shared_ptr<tcp::socket> socket_;
    co_mutex sessions_mutex_;
    Sessions sessions_;
    std::atomic<bool> shutdown_{false};
    friend TcpSession;
};

class TcpServer
    : public Options<TcpServer>, public ServerBase
{
public:
    TcpServer() : impl_(new TcpServerImpl())
    {
        Link(*impl_);
    }

    ~TcpServer()
    {
        Shutdown();
    }

    boost_ec goStart(endpoint addr)
    {
        return impl_->goStart(addr);
    }

    void ShutdownAll()
    {
        impl_->ShutdownAll();
    }

    void Shutdown()
    {
        impl_->Shutdown();
    }

    tcp::endpoint LocalAddr()
    {
        return impl_->LocalAddr();
    }

    OptionsBase* GetOptions()
    {
        return this;
    }

private:
    shared_ptr<TcpServerImpl> impl_;
};

class TcpClientImpl
    : public Options<TcpClientImpl>, public LifeHolder, public boost::enable_shared_from_this<TcpClientImpl>
{
public:
    boost_ec Connect(endpoint addr);
    TcpSessionEntry GetSession();

private:
    void OnSessionClose(::network::SessionEntry id, boost_ec const& ec);

private:
    shared_ptr<TcpSession> sess_;
    co_mutex connect_mtx_;
    friend TcpSession;
    friend class TcpClient;
};

class TcpClient
    : public Options<TcpClient>, public ClientBase
{
public:
    TcpClient() : impl_(new TcpClientImpl())
    {
        Link(*impl_);
    }
    ~TcpClient();

    boost_ec Connect(endpoint addr)
    {
        auto impl = impl_;
        return impl->Connect(addr);
    }
    SessionEntry GetSession()
    {
        return impl_->GetSession();
    }
    OptionsBase* GetOptions()
    {
        return this;
    }
    void Shutdown(bool immediately = false);

private:
    shared_ptr<TcpClientImpl> impl_;
};

} //namespace tcp_detail
} //namespace network

