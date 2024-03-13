#include <ppp/net/asio/websocket/websocket_async_sslv_websocket.h>
#include <ppp/net/asio/websocket/websocket_accept_sslv_websocket.h>

namespace ppp {
    namespace net {
        namespace asio {
            typedef sslwebsocket::SslvTcpSocket                             SslvTcpSocket;
            typedef sslwebsocket::SslvWebSocket                             SslvWebSocket;
            typedef std::shared_ptr<SslvWebSocket>                          SslvWebSocketPtr;
            
            bool AsyncSslvWebSocket::PerformSslHandshake(bool handshaked_client, YieldContext& y) noexcept {
                // Perform the SSL handshake.
                const std::shared_ptr<Reference> reference = GetReference();
                const SslvWebSocketPtr& ssl_websocket = GetSslSocket();
                if (NULL == ssl_websocket) {
                    return false;
                }

                bool ok = false;
                ssl_websocket->next_layer().async_handshake(handshaked_client ? boost::asio::ssl::stream_base::client : boost::asio::ssl::stream_base::server,
                    [reference, this, handshaked_client, &ok, &y](const boost::system::error_code& ec) noexcept {
                        auto& context = y.GetContext();
                        ok = ec == boost::system::errc::success;
                        context.dispatch(std::bind(&ppp::coroutines::YieldContext::Resume, y.GetPtr()));
                    });

                y.Suspend();
                if (!ok) {
                    return false;
                }

                return PerformWebSocketHandshake(handshaked_client, y);
            }
        }
    }
}