#pragma warning(disable:4996)
#define _WIN32_WINNT 0x501
#include <iostream>
#include <string>
#define Dump(x) do { std::cout << x << std::endl; } while(0)

#include <Bex/bexio/bexio.hpp>
#include <Bex/bexio/ssl_protocol.hpp>
#include <Bex/bexio/http/http_stream.hpp>
#include <Bex/utility/format.hpp>
#include <Bex/locale/charset_cvt.h>
#include <Bex/timer/timer.hpp>
using namespace Bex::bexio;
using namespace Bex::conv;

// debug tcmalloc
struct A
{
    A()
    {
        addr = (void*)&malloc;
        void * ptr = malloc(1);
        free(ptr);
    }

    void * addr;
};
A g_debug_malloc_A;

enum {
    t_simple = 0,       // 简单测试
    t_pingpong = 1,     // pingpong测试吞吐量
    t_multiconn = 2,    // 大量并发连接
    t_packet = 3,       // 解包测试
    t_tcp_shutdown = 4, // 测试优雅地关闭连接(发送后立即shutdown, 要保证对端可以接收完整, 不丢数据.)
    t_http_stream = 5,  // http_stream测试
};

std::string remote_ip = "127.0.0.1"; //"192.168.1.105";
std::atomic<long> s_count;
std::atomic<long> s_obj_count;
options opt = options::test();

template <typename Protocol>
class simple_session
    : public basic_session<Protocol>
{
    typedef basic_session<Protocol> base_type;
public:
    virtual void on_connect() BEX_OVERRIDE
    {
        Dump(boost::this_thread::get_id() << " on connect " << base_type::remote_endpoint());
        char buf[] = "Hello, I'm bexio!";
        base_type::send(buf, sizeof(buf));
    }

    virtual void on_disconnect(error_code const& ec) BEX_OVERRIDE
    {
        Dump(boost::this_thread::get_id() << " on disconnect, error:" << ec.message());
    }

    virtual void on_receive(char const* data, std::size_t size) BEX_OVERRIDE
    {
        Dump(boost::this_thread::get_id() << " size = " << size << " data:" << std::string(data, size));
    }
};

template <typename Protocol>
class pingpong_session
    : public simple_session<Protocol>
{
    typedef simple_session<Protocol> base_type;
public:
    static pingpong_session * p;
    char buf[64 * 1024];

    virtual void on_connect() BEX_OVERRIDE
    {
        Dump(boost::this_thread::get_id() << " on connect " << base_type::remote_endpoint());
        error_code ec;
        base_type::get_socket()->lowest_layer().set_option(socket_base::send_buffer_size(8 * 1024 * 1024), ec);
        base_type::get_socket()->lowest_layer().set_option(socket_base::receive_buffer_size(8 * 1024 * 1024), ec);
        if (ec)
            Dump("set socket options error:{" << ec.value() << ", " << ec.message() << "}");

        base_type::send(buf, sizeof(buf));
        p = this;
    }

    virtual void on_receive(char const* data, std::size_t size) BEX_OVERRIDE
    {
        //Dump(boost::this_thread::get_id() << " size = " << size << " data:" << std::string(data, size));
        //reply(data, size);
        base_type::send(buf, sizeof(buf));
    }

    virtual void on_disconnect(error_code const& ec) BEX_OVERRIDE
    {
        Dump(boost::this_thread::get_id() << " on disconnect, error:" << ec.message());
    }

    bool reply(char const* data, std::size_t size)
    {
        bool first = base_type::send(data, size);
        //bool second = send(data, size);
        return first;// || second;
    }
};
template <typename Protocol>
pingpong_session<Protocol> * pingpong_session<Protocol>::p = 0;

template <typename Protocol>
class multi_session
    : public basic_session<Protocol>
{
public:
    multi_session()
    {
        ++ s_obj_count;
    }

    ~multi_session()
    {
        -- s_obj_count;
    }

    virtual void on_connect() BEX_OVERRIDE
    {
        ++ s_count;
    }

    virtual void on_disconnect(error_code const& ec) BEX_OVERRIDE
    {
        -- s_count;
    }
};

template <typename Protocol>
class packet_session
    : public basic_session<Protocol>
{
    typedef basic_session<Protocol> base_type;
public:
    virtual void on_connect() BEX_OVERRIDE
    {
        Dump(boost::this_thread::get_id() << " on connect " << base_type::remote_endpoint());
        ++ s_count;

        char p1[4] = {};
        char p2[8] = {};
        char p3[138] = {};
        char p4[4099] = {};
        *(boost::uint32_t*)p2 = 4;
        *(boost::uint32_t*)p3 = 134;
        *(boost::uint32_t*)p4 = 4095;

        base_type::send(p1, sizeof(p1));
        base_type::send(p2, sizeof(p2));
        base_type::send(p3, sizeof(p3));
        base_type::send(p4, sizeof(p4));
    }

    virtual void on_disconnect(error_code const& ec) BEX_OVERRIDE
    {
        Dump(boost::this_thread::get_id() << " on disconnect");
        -- s_count;
    }

    virtual void on_receive(error_code const& ec, boost::uint32_t * ph
        , char const* data, std::size_t size) BEX_OVERRIDE
    {
        //Dump(boost::this_thread::get_id() << " size = " << size << " data:" << std::string(data, size));
        if (ec)
        {
            Dump("error: " << ec.message());
            base_type::terminate();
            return ;
        }

        base_type::send((char const*)ph, size + sizeof(boost::uint32_t));
    }
};

template <typename Protocol>
class shutdown_session
    : public basic_session<Protocol>
{
    typedef basic_session<Protocol> base_type;
public:
    shutdown_session()
    {
        ++ s_obj_count;
    }

    ~shutdown_session()
    {
        -- s_obj_count;
    }

    virtual void on_connect() BEX_OVERRIDE
    {
        ++ s_count;
        Dump(boost::this_thread::get_id() << " on connect " << base_type::remote_endpoint());
        char buf[] = "I will shutdown session!";
        bool ok = base_type::send(buf, sizeof(buf));
        base_type::shutdown();
        if (!ok)
            Dump("Send failed!");
    }

    virtual void on_receive(char const* data, std::size_t size) BEX_OVERRIDE
    {
        Dump("recv " << size << " bytes: " << std::string(data, size));
    }

    virtual void on_disconnect(error_code const& ec) BEX_OVERRIDE
    {
        -- s_count;
        Dump(boost::this_thread::get_id() << " on disconnect! error:" << ec.message());
    }
};

template <typename EndPoint>
void on_handshake_error(error_code const& ec, EndPoint const& addr)
{
    Dump("handshake error: {" << ec.value() << ", " << ec.message() << "} addr:{" << addr << "}");
}

template <class Session>
void start_server()
{
    typedef basic_server<Session> server;
    opt.ssl_opts.reset(new ssl_options(ssl_options::server()));

    server s(opt);
    s.set_handshake_error_callbcak(boost::BOOST_BIND(&on_handshake_error<typename server::endpoint>, ::_1, ::_2));
    bool ok = s.startup(typename server::endpoint(ip::address::from_string("0.0.0.0"), 28087), 10);
    if (!ok)
        Dump("server startup error: " << s.get_error_code().message());

    io_service::work worker(core<typename server::allocator>::getInstance().backfront());
    core<typename server::allocator>::getInstance().run();
}

template <class Session>
void start_client()
{
    typedef basic_client<Session> client;
    opt.ssl_opts.reset(new ssl_options(ssl_options::client()));

    client c(opt);
    c.set_handshake_error_callbcak(boost::BOOST_BIND(&on_handshake_error<typename client::endpoint>, ::_1, ::_2));
    bool ok = c.connect(typename client::endpoint(ip::address::from_string(remote_ip), 28087));
    if (!ok)
        Dump("connect error: " << c.get_error_code().message());

    io_service::work worker(core<typename client::allocator>::getInstance().backfront());
    core<typename client::allocator>::getInstance().run();
}

template <class Container>
void on_multiconnect_callback(error_code const& ec
    , typename Container::iterator it
    , Container & origin, Container & conn
    , Bex::inter_lock & lock)
{
    if (ec)
    {
        Dump("on connect error:{" << ec.value() << ", " << ec.message() << "}");
        return ;
    }

    Bex::inter_lock::scoped_lock lc(lock);
    conn.push_back(*it);
    origin.erase(it);
}

template <class Session>
void start_multi_client()
{
    int count = 1;
    std::cout << u82a("请输入客户端数量:") << std::endl;
    std::cin >> count;

    opt.ssl_opts.reset(new ssl_options(ssl_options::client()));

    typedef basic_client<Session> client;
    typedef std::list<boost::shared_ptr<client> > client_list;

    client_list origin;
    client_list conn;
    Bex::inter_lock lock;

    for (int i = 0; i < count; ++i)
    {
        shared_ptr<client> c(new client(opt));
        origin.push_back(c);
    }

    for (typename client_list::iterator it = origin.begin(); it != origin.end(); ++it)
    {
        shared_ptr<client> & c = *it;
        c->set_async_connect_callback(boost::BOOST_BIND(&on_multiconnect_callback<client_list>
            , ::_1, it, boost::ref(origin), boost::ref(conn), boost::ref(lock)));
        c->set_handshake_error_callbcak(boost::BOOST_BIND(&on_handshake_error<typename client::endpoint>, ::_1, ::_2));
        //bool ok = c->async_connect(client::endpoint(ip::address::from_string(remote_ip), 28087));
        bool ok = c->async_connect_timed(typename client::endpoint(ip::address::from_string(remote_ip), 28087)
            , boost::posix_time::milliseconds(10000));
        if (!ok)
            Dump("connect error: " << c->get_error_code().message());
    }

    io_service::work worker(core<typename client::allocator>::getInstance().backfront());
    core<typename client::allocator>::getInstance().run();
}

void start_http_stream()
{
    std::string _url;
    Dump(u82a("请输入url:"));
    std::cin >> _url;

    http::http_stream<ip::tcp::socket> s(core<>::getInstance().backfront());
    s.open(_url);
    http::response_header r = s.response();
    Dump("-- Response Header:");
    Dump(r.to_string());

    {
        Bex::timer bt;
        boost::asio::basic_streambuf<> buf;
        error_code ec;
        s.lowest_layer().non_blocking(true);
        std::size_t length = boost::asio::read(s, buf, [&](error_code const& ec, std::size_t){
            return (std::size_t)((!!ec || bt.elapsed() > 5) ? 0 : boost::asio::detail::default_max_transfer_size);
        }, ec);
        Dump("-- Body: times(" << bt.elapsed() << "s), " << "error(" << ec.message() << ")");
        Dump(std::string((char const*)boost::asio::detail::buffer_cast_helper(buf.data()), length));
    }
    std::cout << std::endl;
}

void handle_ctrl_c(error_code, int, signal_set * ss)
{
    ss->async_wait(boost::bind(&handle_ctrl_c, ::_1, ::_2, ss));
    Dump("ctrl-c");
    char buf[10];
    if (pingpong_session<tcp_protocol<> >::p)
    {
        bool ok = pingpong_session<tcp_protocol<> >::p->send(buf, sizeof(buf));
        Dump("Send " << ok);
    }
}

void test()
{
    // 配置
    //signal_set signal_proc(core<>::getInstance().backfront(), SIGINT);
    //signal_proc.async_wait(boost::bind(&handle_ctrl_c, ::_1, ::_2, &signal_proc));

    int input = 0;
    do 
    {
        std::cout << u82a("请输入端类型"
            "\n\t(0:tcp, 1:ssl_tcp, 2:http)"
            "\n\t(0:simple, 1:pingpong, 2:multiconn, 3:packet, 4:tcp_shutdown, 5:http_stream)"
            "\n\t(0:server, 1:client):") << std::endl;
        std::cin >> input;

        int proto = input / 100;
        int type = (input % 100) / 10;
        int point = input % 10;

#if defined(BEX_SUPPORT_CXX11)
        boost::thread th([point] {
            for (;;)
            {
                long c = s_count;
                long oc = s_obj_count;
#if defined(_WIN32) || defined(_WIN64)
                ::SetConsoleTitleA(Bex::format("%s (%d)(%d)", ((point == 0) ? "server" : "client"), oc, c).c_str());
#else //defined(_WIN32) || defined(_WIN64)
                static long s_c = c;
                static long s_oc = oc;
                if (s_c != c || s_oc != oc)
                {
                    Dump(Bex::format("%s (%d)(%d)", ((point == 0) ? "server" : "client"), oc, c));
                    s_c = c, s_oc = oc;
                }
#endif //defined(_WIN32) || defined(_WIN64)
                boost::this_thread::sleep(boost::posix_time::millisec(100));
            }
        });
#endif //defined(BEX_SUPPORT_CXX11)

        typedef tcp_protocol<> tcp_proto;
        typedef tcp_packet_protocol<> tcp_packet_proto;
        typedef ssl_protocol<tcp_protocol<> > ssl_proto;
        typedef ssl_protocol<tcp_packet_protocol<> > ssl_packet_proto;

        switch (type)
        {
        case t_simple:
            if (point == 0)
                if (proto == 0)
                    start_server<simple_session<tcp_proto> >();
                else
                    start_server<simple_session<ssl_proto> >();
            else
                if (proto == 0)
                    start_client<simple_session<tcp_proto> >();
                else
                    start_client<simple_session<ssl_proto> >();
            break;

        case t_pingpong:
            if (point == 0)
                if (proto == 0)
                    start_server<pingpong_session<tcp_proto> >();
                else
                    start_server<pingpong_session<ssl_proto> >();
            else
                if (proto == 0)
                    start_client<pingpong_session<tcp_proto> >();
                else
                    start_client<pingpong_session<ssl_proto> >();
            break;

        case t_multiconn:
            {
                opt.send_buffer_size = opt.receive_buffer_size = 64;

                if (point == 0)
                    if (proto == 0)
                        start_server<multi_session<tcp_proto> >();
                    else
                        start_server<multi_session<ssl_proto> >();
                else
                {
                    if (proto == 0)
                        start_multi_client<multi_session<tcp_proto> >();
                    else
                        start_multi_client<multi_session<ssl_proto> >();
                }
            }
            break;

        case t_packet:
            {
                if (point == 0)
                    if (proto == 0)
                        start_server<packet_session<tcp_packet_proto> >();
                    else
                        start_server<packet_session<ssl_packet_proto> >();
                else
                {
                    if (proto == 0)
                        start_multi_client<packet_session<tcp_packet_proto> >();
                    else
                        start_multi_client<packet_session<ssl_packet_proto> >();
                }
            }
            break;

        case t_tcp_shutdown:
            if (point == 0)
                if (proto == 0)
                    start_server<shutdown_session<tcp_proto> >();
                else
                    start_server<shutdown_session<ssl_proto> >();
            else
                if (proto == 0)
                    start_client<shutdown_session<tcp_proto> >();
                else
                    start_client<shutdown_session<ssl_proto> >();
            break;

        case t_http_stream:
            if (point == 1)
                start_http_stream();
            break;

        default:
            break;
        }
    } while (true);
}

int main()
{
    try
    {
        test();
    }
    catch (std::exception& e)
    {
        Dump("exception -> " << e.what());
    }

#if defined(BOOST_WINDOWS_API)
    system("pause");
#endif //defined(BOOST_WINDOWS_API)
    return 0;
}
