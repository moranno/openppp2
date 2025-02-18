#pragma once

#include <ppp/threading/Timer.h>
#include <ppp/net/IPEndPoint.h>
#include <ppp/net/packet/IPFrame.h>
#include <ppp/net/packet/IcmpFrame.h>
#include <ppp/threading/BufferswapAllocator.h>

namespace ppp {
    namespace net {
        namespace asio {
            class InternetControlMessageProtocol_EchoAsynchronousContext;

            // ICMP on Internet Control Message Protocol.
            class InternetControlMessageProtocol : public std::enable_shared_from_this<InternetControlMessageProtocol> {
            private:
                friend class InternetControlMessageProtocol_EchoAsynchronousContext;

            private:
                typedef ppp::threading::Timer                                   Timer;
                typedef Timer::TimeoutEventHandler                              TimeoutEventHandler;
                typedef std::weak_ptr<TimeoutEventHandler>                      TimeoutEventHandlerWeakPtr;
                typedef ppp::unordered_map<void*, TimeoutEventHandlerWeakPtr>   TimeoutEventHandlerTable;

            public:
                typedef ppp::net::packet::IPFrame                               IPFrame;
                typedef ppp::net::packet::IcmpFrame                             IcmpFrame;
                typedef ppp::net::IPEndPoint                                    IPEndPoint;

            public:
                static constexpr int MAX_ICMP_TIMEOUT                           = 3000;

            public:
                const std::shared_ptr<ppp::threading::BufferswapAllocator>      BufferAllocator;

            public:
                InternetControlMessageProtocol(const std::shared_ptr<ppp::threading::BufferswapAllocator>& allocator, const std::shared_ptr<boost::asio::io_context>& context) noexcept;
                virtual ~InternetControlMessageProtocol() noexcept;

            public:
                std::shared_ptr<boost::asio::io_context>                        GetContext() noexcept;
                std::shared_ptr<InternetControlMessageProtocol>                 GetReference() noexcept;

            public:
                virtual bool                                                    Echo(
                    const std::shared_ptr<IPFrame>&                             packet, 
                    const std::shared_ptr<IcmpFrame>&                           frame, 
                    const IPEndPoint&                                           destinationEP) noexcept;
                virtual void                                                    Dispose() noexcept;

            protected:
                virtual bool                                                    Replay(
                    const std::shared_ptr<IPFrame>                              ping, 
                    const std::shared_ptr<IcmpFrame>&                           request, 
                    const std::shared_ptr<IPFrame>&                             packet, 
                    const IPEndPoint&                                           destinationEP) noexcept;

            protected:
                virtual bool                                                    Output(
                    const IPFrame*                                              packet,
                    const IPEndPoint&                                           destinationEP) noexcept = 0;

            private:
                void                                                            Finalize() noexcept;

            private:
                bool                                                            disposed_ = false;
                boost::asio::ip::udp::endpoint                                  ep_;
                std::shared_ptr<Byte>                                           buffer_;
                std::shared_ptr<boost::asio::io_context>                        executor_;
                TimeoutEventHandlerTable                                        timeouts_;
            };
        }
    }
}