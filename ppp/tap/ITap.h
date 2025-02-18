#pragma once

#include <ppp/stdafx.h>
#include <ppp/net/native/ip.h>
#include <ppp/threading/BufferswapAllocator.h>

namespace ppp
{
    namespace tap
    {
        class ITap : public std::enable_shared_from_this<ITap>
        {
            friend class                                                    WritePacketToKernelNio;
            typedef struct
            {
                std::shared_ptr<Byte>                                       Packet;
                int                                                         PacketLength;
            }                                                               PacketContent;

        public:
            typedef struct
            {
                void*                                                       Packet;
                int                                                         PacketLength;
            }                                                               PacketInputEventArgs;
            typedef ppp::function<bool(ITap*, PacketInputEventArgs&)>       PacketInputEventHandler;

        public:
            const uint32_t                                                  IPAddress;
            const uint32_t                                                  GatewayServer;
            const uint32_t                                                  SubmaskAddress;

        public:
            PacketInputEventHandler                                         PacketInput;
            std::shared_ptr<ppp::threading::BufferswapAllocator>            BufferAllocator;

        public:
            static constexpr int                                            Mtu = ppp::net::native::ip_hdr::MTU;

        protected:
            ITap(const std::shared_ptr<boost::asio::io_context>& context, const ppp::string& id, void* tun, uint32_t ip, uint32_t gw, uint32_t mask, bool hosted_network);

        public:
            virtual ~ITap() noexcept;

        public:
            virtual bool                                                    IsReady() noexcept;
            virtual bool                                                    IsOpen() noexcept;

        public:
            virtual bool                                                    Open() noexcept;
            virtual void                                                    Dispose() noexcept;
            virtual bool                                                    Output(const std::shared_ptr<Byte>& packet, int packet_size) noexcept;
            virtual bool                                                    Output(const void* packet, int packet_size) noexcept;

        public:
            const ppp::string&                                              GetId() noexcept { return _id; }
            std::shared_ptr<boost::asio::io_context>                        GetContext() noexcept { return _context; }
            void*                                                           GetHandle() noexcept { return _handle; }
            int&                                                            GetInterfaceIndex() noexcept { return _interface_index; }
            bool                                                            IsHostedNetwork() noexcept { return _hosted_network; }

        public:
            static ppp::string                                              FindAnyDevice() noexcept;

        public:
#if defined(_WIN32)
            static std::shared_ptr<ITap>                                    Create(const std::shared_ptr<boost::asio::io_context>& context, const ppp::string& dev, uint32_t ip, uint32_t gw, uint32_t mask, uint32_t lease_time_in_seconds, bool hosted_network, const ppp::vector<uint32_t>& dns_addresses) noexcept;
            static std::shared_ptr<ITap>                                    Create(const std::shared_ptr<boost::asio::io_context>& context, const ppp::string& dev, const ppp::string& ip, const ppp::string& gw, const ppp::string& mask, uint32_t lease_time_in_seconds, bool hosted_network, const ppp::vector<ppp::string>& dns_addresses) noexcept;
#else
            static std::shared_ptr<ITap>                                    Create(const std::shared_ptr<boost::asio::io_context>& context, const ppp::string& dev, uint32_t ip, uint32_t gw, uint32_t mask, bool promisc, bool hosted_network, const ppp::vector<uint32_t>& dns_addresses) noexcept;
            static std::shared_ptr<ITap>                                    Create(const std::shared_ptr<boost::asio::io_context>& context, const ppp::string& dev, const ppp::string& ip, const ppp::string& gw, const ppp::string& mask, bool promisc, bool hosted_network, const ppp::vector<ppp::string>& dns_addresses) noexcept;
#endif

        protected:
            std::shared_ptr<boost::asio::posix::stream_descriptor>          GetStream() noexcept { return _stream; }
            Byte*                                                           GetPacketBuffers() noexcept { return _packet; }
            virtual void                                                    OnInput(PacketInputEventArgs& e) noexcept;

        private:
            void                                                            Finalize() noexcept;
            bool                                                            AsynchronousReadPacketLoops() noexcept;

        private:
            ppp::string                                                     _id;
            bool                                                            _opening         = false;
            bool                                                            _hosted_network  = false;
            void*                                                           _handle          = NULL;
            int                                                             _interface_index = -1;
            std::shared_ptr<boost::asio::posix::stream_descriptor>          _stream;
            std::shared_ptr<boost::asio::io_context>                        _context;
            Byte                                                            _packet[Mtu];
        };
    }
}