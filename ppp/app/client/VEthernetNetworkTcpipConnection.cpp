#include <ppp/app/client/VEthernetNetworkTcpipConnection.h>
#include <ppp/app/client/VEthernetExchanger.h>
#include <ppp/app/client/VEthernetNetworkSwitcher.h>
#include <ppp/app/protocol/VirtualEthernetLinklayer.h>
#include <ppp/app/protocol/VirtualEthernetTcpipConnection.h>

#include <ppp/IDisposable.h>
#include <ppp/net/Socket.h>
#include <ppp/net/Ipep.h>
#include <ppp/net/IPEndPoint.h>
#include <ppp/net/rinetd/RinetdConnection.h>

#include <ppp/coroutines/asio/asio.h>
#include <ppp/coroutines/YieldContext.h>
#include <ppp/transmissions/ITransmission.h>

namespace ppp {
    namespace app {
        namespace client {
            VEthernetNetworkTcpipConnection::VEthernetNetworkTcpipConnection(const std::shared_ptr<VEthernetExchanger>& exchanger, const std::shared_ptr<boost::asio::io_context>& context) noexcept
                : TapTcpClient(context)
                , exchanger_(exchanger) {
                Update();
            }

            VEthernetNetworkTcpipConnection::~VEthernetNetworkTcpipConnection() noexcept {
                Finalize();
            }

            void VEthernetNetworkTcpipConnection::Finalize() noexcept {
                if (std::shared_ptr<VirtualEthernetTcpipConnection> connection = std::move(connection_); NULL != connection) {
                    connection_.reset();
                    connection->Dispose();
                }

                if (std::shared_ptr<RinetdConnection> connection_rinetd = std::move(connection_rinetd_); NULL != connection_rinetd) {
                    connection_rinetd_.reset();
                    connection_rinetd->Dispose();
                }
            }

            void VEthernetNetworkTcpipConnection::Dispose() noexcept {
                auto self = shared_from_this();
                std::shared_ptr<boost::asio::io_context> context = GetContext();
                context->post(
                    [self, this]() noexcept {
                        Finalize();
                    });
                TapTcpClient::Dispose();
            }

            std::shared_ptr<VEthernetExchanger> VEthernetNetworkTcpipConnection::GetExchanger() noexcept {
                return exchanger_;
            }

            bool VEthernetNetworkTcpipConnection::ConnectToPeer(ppp::coroutines::YieldContext& y) noexcept {
                using VEthernetTcpipConnection = ppp::app::protocol::templates::VEthernetTcpipConnection<TapTcpClient>;

                // Create a link and correctly establish a link between remote peers, 
                // Indicating whether to use VPN link or Rinetd local loopback forwarding.
                do {
                    if (IsDisposed()) {
                        return false;
                    }

                    std::shared_ptr<boost::asio::io_context> context = GetContext();
                    if (NULL == context) {
                        return false;
                    }

                    std::shared_ptr<AppConfiguration> configuration = exchanger_->GetConfiguration();
                    if (NULL == configuration) {
                        return false;
                    }

                    std::shared_ptr<boost::asio::ip::tcp::socket> socket = GetSocket();
                    if (NULL == socket) {
                        return false;
                    }

                    auto self = shared_from_this();
                    boost::asio::ip::tcp::endpoint remoteEP = GetRemoteEndPoint();

                    int rinetd_status = Rinetd(self, exchanger_, context, configuration, socket, remoteEP, connection_rinetd_, y);
                    if (rinetd_status < 1) {
                        bool rinetd_ok = rinetd_status == 0;
                        if (rinetd_ok) {
                            std::shared_ptr<RinetdConnection> connection_rinetd = connection_rinetd_;
                            if (NULL == connection_rinetd) {
                                rinetd_ok = false;
                            }
                            else {
                                rinetd_ok = connection_rinetd->Run();
                            }
                        }

                        return rinetd_ok;
                    }

                    std::shared_ptr<ppp::transmissions::ITransmission> transmission = exchanger_->ConnectTransmission(context, y);
                    if (NULL == transmission) {
                        return false;
                    }

                    std::shared_ptr<VEthernetTcpipConnection> connection =
                        make_shared_object<VEthernetTcpipConnection>(self, configuration, context, exchanger_->GetId(), socket);
                    if (NULL == connection) {
                        IDisposable::DisposeReferences(transmission);
                        return false;
                    }

#if defined(_LINUX)
                    if (auto switcher = exchanger_->GetSwitcher(); NULL != switcher) {
                        connection->ProtectorNetwork = switcher->GetProtectorNetwork();
                    }
#endif

                    bool ok = connection->Connect(y, transmission, ppp::net::Ipep::ToAddressString<ppp::string>(remoteEP), remoteEP.port());
                    if (!ok) {
                        IDisposable::DisposeReferences(connection, transmission);
                        return false;
                    }

                    connection_ = std::move(connection);
                } while (false);

                // If the connection is interrupted while the coroutine is working, 
                // Or closed during other asynchronous processes or coroutines, do not perform meaningless processing.
                if (IsDisposed()) {
                    return false;
                }

                // If the link is relayed through the VPN remote switcher, then run the VPN link relay subroutine.
                if (std::shared_ptr<VirtualEthernetTcpipConnection> connection = connection_; NULL != connection) {
                    bool ok = connection->Run(y);
                    IDisposable::DisposeReferences(connection);
                    return ok;
                }
                
                // If rinetd local loopback link forwarding is not used, failure will be returned, 
                // Otherwise the link to the peer will be processed successfully.
                return NULL != connection_rinetd_;
            }

            bool VEthernetNetworkTcpipConnection::Establish() noexcept {
                auto self = shared_from_this();
                std::shared_ptr<boost::asio::io_context> context = GetContext();

                return ppp::coroutines::YieldContext::Spawn(*context,
                    [self, this](ppp::coroutines::YieldContext& y) noexcept {
                        ConnectToPeer(y);
                    });
            }

            bool VEthernetNetworkTcpipConnection::EndAccept(const std::shared_ptr<boost::asio::ip::tcp::socket>& socket, const boost::asio::ip::tcp::endpoint& natEP) noexcept {
                if (NULL == socket) {
                    return false;
                }

                std::shared_ptr<ppp::configurations::AppConfiguration> configuration = exchanger_->GetConfiguration();
                if (NULL == configuration) {
                    return false;
                }
                else {
                    boost::system::error_code ec;
                    boost::asio::ip::tcp::endpoint localEP = socket->local_endpoint(ec);
                    boost::asio::ip::address localIP = localEP.address();

                    ppp::net::Socket::AdjustSocketOptional(*socket, !localIP.is_v6(), configuration->tcp.fast_open, configuration->tcp.turbo);
                }

                return TapTcpClient::EndAccept(socket, natEP);
            }
        }
    }
}