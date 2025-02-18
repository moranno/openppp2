#include <ppp/app/client/http/VEthernetHttpProxySwitcher.h>
#include <ppp/app/client/http/VEthernetHttpProxyConnection.h>
#include <ppp/app/client/VEthernetExchanger.h>
#include <ppp/net/Ipep.h>
#include <ppp/net/Socket.h>
#include <ppp/threading/Timer.h>
#include <ppp/threading/Executors.h>
#include <ppp/collections/Dictionary.h>
#include <ppp/coroutines/YieldContext.h>

namespace ppp {
    namespace app {
        namespace client {
            namespace http {
                VEthernetHttpProxySwitcher::VEthernetHttpProxySwitcher(const std::shared_ptr<VEthernetExchanger>& exchanger) noexcept
                    : disposed_(false)
                    , exchanger_(exchanger)
                    , context_(ppp::threading::Executors::GetDefault())
                    , configuration_(exchanger->GetConfiguration()) {

                }

                VEthernetHttpProxySwitcher::~VEthernetHttpProxySwitcher() noexcept {
                    Finalize();
                }

                void VEthernetHttpProxySwitcher::Finalize() noexcept {
                    VEthernetHttpProxyConnectionTable connections;
                    exchangeof(disposed_, true); {
                        SynchronizedObjectScope scope(syncobj_);
                        connections = std::move(connections_);
                        connections_.clear();
                    }

                    if (std::shared_ptr<ppp::threading::Timer> timeout = std::move(timeout_); NULL != timeout) {
                        timeout_.reset();
                        timeout->Dispose();
                    }

                    if (std::shared_ptr<ppp::net::SocketAcceptor> acceptor = std::move(acceptor_); NULL != acceptor) {
                        acceptor_.reset();
                        acceptor->Dispose();
                    }

                    ppp::collections::Dictionary::ReleaseAllObjects(connections);
                }

                void VEthernetHttpProxySwitcher::Update(UInt64 now) noexcept {
                    SynchronizedObjectScope scope(syncobj_);
                    ppp::collections::Dictionary::UpdateAllObjects(connections_, now);
                }

                void VEthernetHttpProxySwitcher::Dispose() noexcept {
                    auto self = shared_from_this();
                    std::shared_ptr<boost::asio::io_context> context = GetContext();
                    context->post(
                        [self, this]() noexcept {
                            Finalize();
                        });
                }

                std::shared_ptr<boost::asio::io_context> VEthernetHttpProxySwitcher::GetContext() noexcept {
                    return context_;
                }

                bool VEthernetHttpProxySwitcher::Open() noexcept {
                    if (NULL != acceptor_) {
                        return false;
                    }

                    std::shared_ptr<ppp::net::SocketAcceptor> acceptor;
                    if (disposed_) {
                        return false;
                    }
                    else {
                        boost::asio::ip::address bind_ips[] = {
                                ppp::net::Ipep::ToAddress(configuration_->client.http_proxy.bind, true),
                                boost::asio::ip::address_v6::any(),
                                boost::asio::ip::address_v4::any()
                            };
                        for (boost::asio::ip::address& interfaceIP : bind_ips) {
                            if (interfaceIP.is_multicast()) {
                                continue;
                            }

                            if (!(interfaceIP.is_v4() || interfaceIP.is_v6())) {
                                continue;
                            }

                            if (!interfaceIP.is_unspecified() && ppp::net::IPEndPoint::IsInvalid(interfaceIP)) {
                                continue;
                            }

                            std::shared_ptr<ppp::net::SocketAcceptor> t = ppp::net::SocketAcceptor::New();
                            if (NULL == t) {
                                return false;
                            }

                            ppp::string address_string = ppp::net::Ipep::ToAddressString<ppp::string>(interfaceIP);
                            if (!t->Open(address_string.data(), configuration_->client.http_proxy.port, configuration_->tcp.backlog)) {
                                continue;
                            }

                            acceptor = std::move(t);
                            break;
                        }

                        if (NULL == acceptor) {
                            return false;
                        }
                    }

                    int sockfd = acceptor->GetHandle();
                    ppp::net::Socket::AdjustDefaultSocketOptional(sockfd, false);
                    ppp::net::Socket::SetTypeOfService(sockfd);
                    ppp::net::Socket::SetSignalPipeline(sockfd, false);
                    
                    auto self = shared_from_this();
                    acceptor->AcceptSocket = 
                        [self, this](ppp::net::SocketAcceptor*, ppp::net::SocketAcceptor::AcceptSocketEventArgs& e) noexcept {
                            int sockfd = e.Socket;
                            for (;;) {
                                if (disposed_) {
                                    break;
                                }

                                auto context = ppp::threading::Executors::GetExecutor();
                                if (NULL == context) {
                                    break;
                                }

                                context->dispatch(std::bind(&VEthernetHttpProxySwitcher::ProcessAcceptSocket, self, context, sockfd));
                                return true;
                            }

                            ppp::net::Socket::Closesocket(sockfd);
                            return false;
                        };

                    bool bok = CreateAlwaysTimeout();
                    if (!bok) {
                        acceptor->Dispose();
                        return false;
                    }

                    acceptor_ = std::move(acceptor);
                    return bok;
                }

                void VEthernetHttpProxySwitcher::ReleaseConnection(VEthernetHttpProxyConnection* connection) noexcept {
                    if (NULL != connection) {
                        auto self = shared_from_this();
                        context_->post(std::bind(&VEthernetHttpProxySwitcher::RemoveConnection, self, connection));
                    }
                }

                bool VEthernetHttpProxySwitcher::RemoveConnection(VEthernetHttpProxyConnection* connection) noexcept {
                    VEthernetHttpProxyConnectionPtr r; 
                    if (NULL != connection) {
                        SynchronizedObjectScope scope(syncobj_);
                        r = ppp::collections::Dictionary::ReleaseObjectByKey(connections_, connection); 
                    }
                    return NULL != r;
                }

                std::shared_ptr<ppp::configurations::AppConfiguration> VEthernetHttpProxySwitcher::GetConfiguration() noexcept {
                    return configuration_;
                }

                std::shared_ptr<VEthernetExchanger> VEthernetHttpProxySwitcher::GetExchanger() noexcept {
                    return exchanger_;
                }

                std::shared_ptr<boost::asio::ip::tcp::socket> VEthernetHttpProxySwitcher::NewSocket(const std::shared_ptr<boost::asio::io_context>& context, int sockfd) noexcept {
                    if (NULL == context) {
                        return NULL;
                    }

                    boost::asio::ip::tcp::endpoint remoteEP = ppp::net::Socket::GetRemoteEndPoint(sockfd);
                    boost::system::error_code ec = boost::asio::error::operation_aborted;

                    std::shared_ptr<boost::asio::ip::tcp::socket> socket = make_shared_object<boost::asio::ip::tcp::socket>(*context);
                    try {
                        if (NULL == socket) {
                            return NULL;
                        }
                        else {
                            socket->assign(remoteEP.protocol(), sockfd, ec);
                        }
                    }
                    catch (const std::exception&) {}

                    if (ec) {
                        ppp::net::Socket::Closesocket(sockfd);
                        return NULL;
                    }
                    else {
                        boost::asio::ip::tcp::endpoint localEP = socket->local_endpoint(ec);
                        boost::asio::ip::address localIP = localEP.address();
                        ppp::net::Socket::AdjustSocketOptional(*socket, !localIP.is_v6(), configuration_->tcp.fast_open, configuration_->tcp.turbo);
                    }

                    return socket;
                }

                bool VEthernetHttpProxySwitcher::AddConnection(const std::shared_ptr<VEthernetHttpProxyConnection>& connection) noexcept {
                    if (NULL == connection) {
                        return false;
                    }
                    
                    SynchronizedObjectScope scope(syncobj_);
                    return ppp::collections::Dictionary::TryAdd(connections_, connection.get(), connection);
                }

                bool VEthernetHttpProxySwitcher::ProcessAcceptSocket(const std::shared_ptr<boost::asio::io_context>& context, int sockfd) noexcept {
                    if (NULL == context) {
                        ppp::net::Socket::Closesocket(sockfd);
                        return false;
                    }

                    std::shared_ptr<boost::asio::ip::tcp::socket> socket = NewSocket(context, sockfd);
                    if (NULL == socket) {
                        return false;
                    }

                    std::shared_ptr<VEthernetHttpProxyConnection> connection = NewConnection(context, socket);
                    if (NULL == connection) {
                        connection->Dispose();
                        return false;
                    }

                    bool bok = AddConnection(connection);
                    if (!bok) {
                        connection->Dispose();
                        return false;
                    }
                    
                    auto allocator = GetBufferAllocator();
                    auto self = shared_from_this();
                    bok = ppp::coroutines::YieldContext::Spawn(allocator.get(), *context,
                        [self, this, connection](ppp::coroutines::YieldContext& y) noexcept {
                            bool bok = connection->Run(y);
                            if (!bok) {
                                connection->Dispose();
                            }
                        });

                    if (!bok && RemoveConnection(connection.get())) {
                        connection->Dispose(); 
                    }
                    return bok;
                }

                std::shared_ptr<ppp::threading::BufferswapAllocator> VEthernetHttpProxySwitcher::GetBufferAllocator() noexcept {
                    std::shared_ptr<ppp::configurations::AppConfiguration> configuration = configuration_;
                    return NULL != configuration ? configuration->GetBufferAllocator() : NULL;
                }

                bool VEthernetHttpProxySwitcher::CreateAlwaysTimeout() noexcept {
                    if (disposed_) {
                        return false;
                    }

                    std::shared_ptr<boost::asio::io_context> context = GetContext();
                    if (!context) {
                        return false;
                    }

                    auto self = shared_from_this();
                    auto timeout = make_shared_object<ppp::threading::Timer>(context);
                    if (!timeout) {
                        return false;
                    }

                    timeout_ = timeout;
                    timeout->TickEvent = 
                        [self, this](ppp::threading::Timer* sender, ppp::threading::Timer::TickEventArgs& e) noexcept {
                            UInt64 now = ppp::threading::Executors::GetTickCount();
                            Update(now);
                        };
                    return timeout->SetInterval(1000) && timeout->Start();
                }

                std::shared_ptr<VEthernetHttpProxyConnection> VEthernetHttpProxySwitcher::NewConnection(const std::shared_ptr<boost::asio::io_context>& context, const std::shared_ptr<boost::asio::ip::tcp::socket>& socket) noexcept {
                    auto self = shared_from_this();
                    return make_shared_object<VEthernetHttpProxyConnection>(self, exchanger_, context, socket);
                }

                boost::asio::ip::tcp::endpoint VEthernetHttpProxySwitcher::GetLocalEndPoint() noexcept {
                    std::shared_ptr<ppp::net::SocketAcceptor> acceptor = acceptor_;
                    if (NULL != acceptor) {
                        return ppp::net::Socket::GetLocalEndPoint(acceptor->GetHandle());
                    }

                    return boost::asio::ip::tcp::endpoint(boost::asio::ip::address_v4::any(), ppp::net::IPEndPoint::MinPort);
                }
            }
        }
    }
}