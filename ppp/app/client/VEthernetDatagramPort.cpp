#include <ppp/app/client/VEthernetDatagramPort.h>
#include <ppp/app/client/VEthernetExchanger.h>
#include <ppp/app/client/VEthernetNetworkSwitcher.h>
#include <ppp/net/Ipep.h>
#include <ppp/net/Socket.h>
#include <ppp/net/IPEndPoint.h>
#include <ppp/coroutines/asio/asio.h>
#include <ppp/coroutines/YieldContext.h>

typedef ppp::coroutines::YieldContext                   YieldContext;
typedef ppp::net::IPEndPoint                            IPEndPoint;
typedef ppp::net::Socket                                Socket;
typedef ppp::net::Ipep                                  Ipep;

namespace ppp {
    namespace app {
        namespace client {
            VEthernetDatagramPort::VEthernetDatagramPort(const VEthernetExchangerPtr& exchanger, const ITransmissionPtr& transmission, const boost::asio::ip::udp::endpoint& sourceEP) noexcept
                : disposed_(false)
                , onlydns_(true)
                , sendto_(false)
                , finalize_(false)
                , timeout_(0)
                , exchanger_(exchanger)
                , transmission_(transmission)
                , configuration_(exchanger->GetConfiguration())
                , sourceEP_(sourceEP) 
#if defined(_ANDROID)
                , opened_(0)
                , socket_(*transmission->GetContext())
#endif
            {
                switcher_ = exchanger->GetSwitcher();
                Update();

#if defined(_ANDROID)
                std::shared_ptr<boost::asio::io_context> context = transmission->GetContext();
                buffer_ = Executors::GetCachedBuffer(context.get());
                ProtectorNetwork = switcher_->GetProtectorNetwork();
#endif
            }

            VEthernetDatagramPort::~VEthernetDatagramPort() noexcept {
                Finalize();
            }

            void VEthernetDatagramPort::Finalize() noexcept {
                bool fin = false; {
                    SynchronizedObjectScope scope(syncobj_);
                    if (sendto_ && !finalize_) {
                        fin = true;
                    }

                    disposed_ = true;
                    sendto_ = false;
                    finalize_ = true;

#if defined(_ANDROID)
                    messages_.clear();
                    Socket::Closesocket(socket_);
#endif
                }

                exchanger_->ReleaseDatagramPort(sourceEP_);
                if (fin) {
                    YieldContext& y_null = nullof<YieldContext>();
                    if (!exchanger_->DoSendTo(transmission_, sourceEP_, sourceEP_, NULL, 0, y_null)) {
                        transmission_->Dispose();
                    }
                }
            }

            void VEthernetDatagramPort::Dispose() noexcept {
                auto self = shared_from_this();
                std::shared_ptr<boost::asio::io_context> context = GetContext();
                context->post(std::bind(&VEthernetDatagramPort::Finalize, self));
            }

            bool VEthernetDatagramPort::SendTo(const void* packet, int packet_length, const boost::asio::ip::udp::endpoint& destinationEP) noexcept {
                if (NULL == packet || packet_length < 1) {
                    return false;
                }

                if (disposed_) {
                    return false;
                }

                int destinationPort = destinationEP.port();
                if (destinationPort <= IPEndPoint::MinPort || destinationPort > IPEndPoint::MaxPort) {
                    return false;
                }

                boost::asio::ip::address address = destinationEP.address();
                if (address.is_unspecified()) {
                    return false;
                }

                bool ok = false;
#if defined(_ANDROID)
                bool fin = false;
#endif
                do {
#if defined(_ANDROID)
                    // It is sent out through the local physical NIC.
                    if (address.is_v4() && switcher_->IsBypassIpAddress(address)) {
                        // If the socket is currently open, send data directly.
                        SynchronizedObjectScope scope(syncobj_);
                        if (opened_ > 1) {
                            boost::system::error_code ec;
                            socket_.send_to(boost::asio::buffer(packet, packet_length), 
                                destinationEP, boost::asio::socket_base::message_end_of_record, ec);

                            if (ec == boost::system::errc::success) {
                                ok = true;
                            }
                        }
                        else {
                            // If you are not currently opening a physical network socket, try to open the socket.
                            if (opened_ < 1) {
                                bool opening = false;
                                if (std::shared_ptr<boost::asio::io_context> context = GetContext(); NULL != context) {
                                    auto self = shared_from_this();
                                    opening = YieldContext::Spawn(transmission_->BufferAllocator.get(), *context,
                                        [self, this](YieldContext& y) noexcept {
                                            bool opened = Open(y);
                                            if (!opened) {
                                                Dispose();
                                            }
                                        });
                                }

                                if (opening) {
                                    opened_ = 1;
                                }
                                else {
                                    fin = true;
                                    break;
                                }
                            }

                            // If you are currently trying to open the socket, cache the data and do not send it until it is opened.
                            std::shared_ptr<Byte> packet_managed = ppp::net::asio::IAsynchronousWriteIoQueue::Copy(
                                transmission_->BufferAllocator, packet, packet_length);
                            if (NULL == packet_managed) {
                                break;
                            }

                            Message message;
                            message.packet        = packet_managed;
                            message.packet_length = packet_length;
                            message.destinationEP = destinationEP;

                            ok = true;
                            messages_.emplace_back(message);
                        }
                        break;
                    }
#endif
                    // Send it to the VPN server for outgoing.
                    ok = exchanger_->DoSendTo(transmission_, sourceEP_, destinationEP, (Byte*)packet, packet_length, nullof<YieldContext>());
                    if (!ok) {
                        transmission_->Dispose();
                    }
                } while (false);

                // Successfully sent a UDP data packet, so need to update the last activity time.
                if (ok) {
                    sendto_ = true;
                    if (destinationPort != PPP_DNS_SYS_PORT) {
                        onlydns_ = false;
                    }

                    Update();
                }

#if defined(_ANDROID)
                // UDP port mapping has failed and needs to be shut down.
                if (fin) {
                    Dispose();
                }
#endif
                return ok;
            }

            std::shared_ptr<VEthernetDatagramPort> VEthernetDatagramPort::GetReference() noexcept {
                return shared_from_this();
            }

            VEthernetDatagramPort::VEthernetExchangerPtr VEthernetDatagramPort::GetExchanger() noexcept {
                return exchanger_;
            }

            VEthernetDatagramPort::ContextPtr VEthernetDatagramPort::GetContext() noexcept {
                return transmission_->GetContext();
            }

            VEthernetDatagramPort::AppConfigurationPtr VEthernetDatagramPort::GetConfiguration() noexcept {
                return configuration_;
            }

            boost::asio::ip::udp::endpoint& VEthernetDatagramPort::GetLocalEndPoint() noexcept {
                return sourceEP_;
            }

            void VEthernetDatagramPort::OnMessage(void* packet, int packet_length, const boost::asio::ip::udp::endpoint& destinationEP) noexcept {
                std::shared_ptr<VEthernetExchanger> exchanger = exchanger_;
                if (exchanger) {
                    switcher_->DatagramOutput(sourceEP_, destinationEP, packet, packet_length);
                }
            }

#if defined(_ANDROID)
            bool VEthernetDatagramPort::Open(ppp::coroutines::YieldContext& y) noexcept {
                if (disposed_) {
                    return false;
                }

                bool opened = false;
                boost::asio::io_context& context = y.GetContext();

                context.post(
                    [this, &y, &opened]() noexcept {
                        // Open the udp port and listen on any address 0.0.0.0.
                        boost::asio::io_context& context = y.GetContext(); {
                            SynchronizedObjectScope scope(syncobj_);
                            opened = Socket::OpenSocket(socket_, 
                                boost::asio::ip::address_v4::any(), IPEndPoint::MinPort);
                        }

                        // Wake up the coroutine currently waiting to open the udp socket.
                        context.dispatch(std::bind(&ppp::coroutines::YieldContext::Resume, y.GetPtr()));
                    });

                // Suspend and wait for the udp socket to open.
                y.Suspend();
                if (!opened) {
                    return false;
                }

                // Protect udp sockets to prevent udp data from being sent to the VPN loop.
                if (auto protector_network = ProtectorNetwork; NULL != protector_network) {
                    if (!protector_network->Protect(socket_.native_handle(), y)) {
                        return false;
                    }
                }

                // Send all unsent message data to the public network.
                Messages messages; {
                    SynchronizedObjectScope scope(syncobj_);
                    opened_ = 2;
                    
                    messages = std::move(messages_);
                    messages_.clear();
                }

                for (Message& message : messages) {
                    SendTo(message.packet.get(), message.packet_length, message.destinationEP);
                }

                return Loopback();
            }

            bool VEthernetDatagramPort::Loopback() noexcept {
                SynchronizedObjectScope scope(syncobj_);
                if (disposed_) {
                    return false;
                }

                if (!socket_.is_open()) {
                    return false;
                }

                auto self = shared_from_this();
                socket_.async_receive_from(boost::asio::buffer(buffer_.get(), PPP_BUFFER_SIZE), remoteEP_,
                    [self, this](const boost::system::error_code& ec, std::size_t sz) noexcept {
                        bool disposing = false;
                        if (ec == boost::system::errc::success) {
                            if (sz > 0) {
                                OnMessage(buffer_.get(), sz, remoteEP_);
                            }
                        }
                        elif(ec == boost::system::errc::operation_canceled) {
                            disposing = true;
                        }

                        if (disposing) {
                            Dispose();
                        }
                        else {
                            Loopback();
                        }
                    });
                return true;
            }
#endif
        }
    }
}